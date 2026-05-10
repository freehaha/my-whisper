#include "llm_refiner.h"
#include "config.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QDebug>

Q_LOGGING_CATEGORY(lcLlmRefiner, "mywhisper.llm_refiner")

namespace {
constexpr int kLlamaServerPort = 18765;
constexpr int kLlamaServerIdleMs = 120000;
constexpr int kLlamaServerProbeIntervalMs = 500;
constexpr int kLlamaServerProbeMaxAttempts = 40;

QString effectivePrompt(const AppConfig &config) {
    return config.refinementPrompt.isEmpty()
        ? QStringLiteral("Fix spelling and grammar. Return only the fixed text.")
        : config.refinementPrompt;
}

QString resolveLlamaExecutable(const AppConfig &config) {
    QString executable = config.llamaCppBinaryPath.trimmed();
    if (executable.isEmpty()) {
        executable = QStandardPaths::findExecutable(QStringLiteral("llama-server"));
    }
    return executable;
}
}

LLMRefiner::LLMRefiner(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_llamaServerIdleTimer(new QTimer(this))
    , m_llamaServerProbeTimer(new QTimer(this)) {
    m_llamaServerIdleTimer->setSingleShot(true);
    m_llamaServerIdleTimer->setInterval(kLlamaServerIdleMs);
    connect(m_llamaServerIdleTimer, &QTimer::timeout, this, &LLMRefiner::stopLlamaServer);

    m_llamaServerProbeTimer->setSingleShot(false);
    m_llamaServerProbeTimer->setInterval(kLlamaServerProbeIntervalMs);
    connect(m_llamaServerProbeTimer, &QTimer::timeout, this, &LLMRefiner::probeLlamaServerReadiness);
}

void LLMRefiner::refine(const QString &text, const AppConfig &config) {
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "Refine requested. provider=" << config.refinementProvider
                          << "enabled=" << config.enableRefinement
                          << "textLength=" << text.size();
#endif

    if (!Config::hasUsableRefiner(config)) {
        qCWarning(lcLlmRefiner) << "Refiner unavailable. provider=" << config.refinementProvider
                                << "enableRefinement=" << config.enableRefinement
                                << "openAiKeyPresent=" << !config.openaiApiKey.trimmed().isEmpty()
                                << "llamaModelPresent=" << !config.llamaCppModelPath.trimmed().isEmpty();
        QMetaObject::invokeMethod(this, [this, text]() {
            emit refined(text);
        }, Qt::QueuedConnection);
        return;
    }

    const QString provider = config.refinementProvider.trimmed().toLower();
    if (provider == QStringLiteral("llama_cpp")) {
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "Dispatching to llama.cpp refiner";
#endif
        refineWithLlamaCpp(text, config);
        return;
    }

#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "Dispatching to OpenAI refiner";
#endif
    refineWithOpenAi(text, config);
}

void LLMRefiner::refineWithOpenAi(const QString &text, const AppConfig &config) {
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "OpenAI refinement start. textLength=" << text.size();
#endif

    QNetworkRequest request(QUrl(QStringLiteral("https://api.openai.com/v1/chat/completions")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + config.openaiApiKey.toUtf8());

    QJsonObject body;
    body["model"] = QStringLiteral("gpt-5.4-nano");
    body["temperature"] = 0.7;

    QJsonObject responseFormat;
    responseFormat["type"] = QStringLiteral("json_schema");
    QJsonObject jsonSchema;
    jsonSchema["name"] = QStringLiteral("refined_result");
    QJsonObject schema;
    schema["type"] = QStringLiteral("object");
    QJsonObject properties;
    QJsonObject refinedTextProp;
    refinedTextProp["type"] = QStringLiteral("string");
    properties["refined_text"] = refinedTextProp;
    schema["properties"] = properties;
    schema["required"] = QJsonArray{QStringLiteral("refined_text")};
    schema["additionalProperties"] = false;
    jsonSchema["strict"] = true;
    jsonSchema["schema"] = schema;
    responseFormat["json_schema"] = jsonSchema;
    body["response_format"] = responseFormat;

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = QStringLiteral("system");
    systemMessage["content"] = effectivePrompt(config);
    messages.append(systemMessage);

    QJsonObject userMessage;
    userMessage["role"] = QStringLiteral("user");
    userMessage["content"] = text;
    messages.append(userMessage);
    body["messages"] = messages;

    const QByteArray requestBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner).noquote() << "LLM Refiner OpenAI request:" << requestBody;
#endif
    QNetworkReply *reply = m_networkManager->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply, text]() {
        const QByteArray responseBody = reply->readAll();
#ifdef QT_DEBUG
        qCDebug(lcLlmRefiner).noquote() << "LLM Refiner OpenAI response:" << responseBody;
#endif
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(responseBody).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            qCWarning(lcLlmRefiner) << "OpenAI refinement failed. status=" << statusCode.toInt()
                                    << "networkError=" << reply->error()
                                    << "message=" << message;
            emit errorOccurred(tr("OpenAI error: %1").arg(message));
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(responseBody);
        const auto choices = document.object().value("choices").toArray();
        QString refinedText = text;
        if (!choices.isEmpty()) {
            const QString content = choices.first().toObject().value("message").toObject().value("content").toString();
            const QJsonDocument parsed = QJsonDocument::fromJson(content.toUtf8());
            if (parsed.isObject()) {
                refinedText = parsed.object().value("refined_text").toString(text);
            } else {
                refinedText = content.trimmed();
            }
        }

#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "OpenAI refinement success. outputLength=" << refinedText.trimmed().size();
#endif
        emit refined(refinedText.trimmed());
        reply->deleteLater();
    });
}

void LLMRefiner::refineWithLlamaCpp(const QString &text, const AppConfig &config) {
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "llama.cpp refinement start. textLength=" << text.size()
                          << "modelPath=" << config.llamaCppModelPath
                          << "binaryPath=" << config.llamaCppBinaryPath
                          << "hasExtraArgs=" << !config.llamaCppAdditionalArgs.trimmed().isEmpty();
#endif

    const QString modelPath = config.llamaCppModelPath.trimmed();
    if (modelPath.isEmpty()) {
        qCWarning(lcLlmRefiner) << "llama.cpp refinement failed: model path empty";
        emit errorOccurred(tr("llama.cpp model path is empty."));
        return;
    }

    if (!QFileInfo::exists(modelPath)) {
        qCWarning(lcLlmRefiner) << "llama.cpp refinement failed: model missing" << modelPath;
        emit errorOccurred(tr("llama.cpp model not found: %1").arg(modelPath));
        return;
    }

    const QString executable = resolveLlamaExecutable(config);
    if (executable.isEmpty()) {
        qCWarning(lcLlmRefiner) << "llama.cpp refinement failed: llama-server not found";
        emit errorOccurred(tr("llama-server not found in PATH."));
        return;
    }
    if (!QFileInfo::exists(executable)) {
        qCWarning(lcLlmRefiner) << "llama.cpp refinement failed: binary missing" << executable;
        emit errorOccurred(tr("llama.cpp binary not found: %1").arg(executable));
        return;
    }

    m_pendingLlamaRequests.enqueue({text, config});
    m_llamaServerIdleTimer->stop();
    ensureLlamaServer(config);
}

void LLMRefiner::ensureLlamaServer(const AppConfig &config) {
    const QString requestedExecutable = resolveLlamaExecutable(config);
    const bool configChanged = m_llamaServerConfig.llamaCppModelPath != config.llamaCppModelPath
        || m_llamaServerConfig.llamaCppBinaryPath != config.llamaCppBinaryPath
        || m_llamaServerConfig.llamaCppAdditionalArgs != config.llamaCppAdditionalArgs;

    if (m_llamaServerProcess && (configChanged || m_llamaServerProcess->program() != requestedExecutable)) {
        qCWarning(lcLlmRefiner) << "llama-server config changed; restarting server";
        stopLlamaServer();
    }

    if (m_llamaServerReady && m_llamaServerProcess && m_llamaServerProcess->state() == QProcess::Running) {
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "llama-server already ready; flushing queue";
#endif
        flushPendingLlamaRequests();
        return;
    }

    if (m_llamaServerStarting) {
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "llama-server already starting; request queued";
#endif
        return;
    }

    startLlamaServer(config);
}

void LLMRefiner::startLlamaServer(const AppConfig &config) {
    const QString executable = resolveLlamaExecutable(config);
    QStringList arguments = {
        QStringLiteral("-m"), config.llamaCppModelPath.trimmed(),
        QStringLiteral("--host"), QStringLiteral("127.0.0.1"),
        QStringLiteral("--port"), QStringLiteral("18765")
    };

    const QString extraArgs = config.llamaCppAdditionalArgs.trimmed();
    if (!extraArgs.isEmpty()) {
        const QStringList parsedExtraArgs = QProcess::splitCommand(extraArgs);
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "llama-server parsed extra args:" << parsedExtraArgs;
#endif
        arguments.append(parsedExtraArgs);
    }

    m_llamaServerConfig = config;
    m_llamaServerReady = false;
    m_llamaServerStarting = true;
    m_llamaServerProbeAttempts = 0;

    if (m_llamaServerProcess) {
        m_llamaServerProcess->deleteLater();
        m_llamaServerProcess = nullptr;
    }

    m_llamaServerProcess = new QProcess(this);
    m_llamaServerProcess->setProgram(executable);
    m_llamaServerProcess->setArguments(arguments);

#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "Starting llama-server:" << executable << arguments;
#endif

    connect(m_llamaServerProcess, &QProcess::started, this, [this]() {
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "llama-server started. pid=" << m_llamaServerProcess->processId();
#endif
        m_llamaServerProbeTimer->start();
    });

    connect(m_llamaServerProcess, &QProcess::readyReadStandardError, this, [this]() {
#ifdef QT_DEBUG
        const QString chunk = QString::fromUtf8(m_llamaServerProcess->readAllStandardError());
        if (!chunk.trimmed().isEmpty()) {
            qCDebug(lcLlmRefiner).noquote() << "llama-server stderr chunk:" << chunk;
        }
#endif
    });

    connect(m_llamaServerProcess, &QProcess::readyReadStandardOutput, this, [this]() {
#ifdef QT_DEBUG
        const QString chunk = QString::fromUtf8(m_llamaServerProcess->readAllStandardOutput());
        if (!chunk.trimmed().isEmpty()) {
            qCDebug(lcLlmRefiner).noquote() << "llama-server stdout chunk:" << chunk;
        }
#endif
    });

    connect(m_llamaServerProcess, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        const QString stdoutText = m_llamaServerProcess ? QString::fromUtf8(m_llamaServerProcess->readAllStandardOutput()).trimmed() : QString();
        const QString stderrText = m_llamaServerProcess ? QString::fromUtf8(m_llamaServerProcess->readAllStandardError()).trimmed() : QString();
        qCWarning(lcLlmRefiner) << "llama-server exited. exitCode=" << exitCode
                                << "exitStatus=" << exitStatus
                                << "stderr=" << stderrText
                                << "stdout=" << stdoutText;
        const bool hadPending = !m_pendingLlamaRequests.isEmpty();
        m_llamaServerProbeTimer->stop();
        m_llamaServerIdleTimer->stop();
        m_llamaServerStarting = false;
        m_llamaServerReady = false;
        if (hadPending) {
            failPendingLlamaRequests(tr("llama-server exited unexpectedly."));
        }
    });

    connect(m_llamaServerProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        qCWarning(lcLlmRefiner) << "llama-server process error. error=" << error
                                << "message=" << (m_llamaServerProcess ? m_llamaServerProcess->errorString() : QString());
        m_llamaServerProbeTimer->stop();
        m_llamaServerIdleTimer->stop();
        m_llamaServerStarting = false;
        m_llamaServerReady = false;
        failPendingLlamaRequests(tr("Failed to start llama-server: %1").arg(m_llamaServerProcess ? m_llamaServerProcess->errorString() : QString()));
    });

    m_llamaServerProcess->start();
}

void LLMRefiner::probeLlamaServerReadiness() {
    ++m_llamaServerProbeAttempts;
    if (m_llamaServerProbeAttempts > kLlamaServerProbeMaxAttempts) {
        qCWarning(lcLlmRefiner) << "llama-server readiness probe timed out";
        m_llamaServerProbeTimer->stop();
        m_llamaServerStarting = false;
        failPendingLlamaRequests(tr("Timed out waiting for llama-server to become ready."));
        stopLlamaServer();
        return;
    }

    QNetworkRequest request(QUrl(llamaServerUrl() + QStringLiteral("/health")));
    QNetworkReply *reply = m_networkManager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && statusCode >= 200 && statusCode < 300;
#ifdef QT_DEBUG
        qCDebug(lcLlmRefiner) << "llama-server probe status=" << statusCode << "ok=" << ok << "body=" << body;
#endif
        reply->deleteLater();
        if (!ok || !m_llamaServerProcess || m_llamaServerProcess->state() != QProcess::Running) {
            return;
        }

        m_llamaServerProbeTimer->stop();
        m_llamaServerStarting = false;
        m_llamaServerReady = true;
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner) << "llama-server ready";
#endif
        flushPendingLlamaRequests();
    });
}

void LLMRefiner::flushPendingLlamaRequests() {
    while (!m_pendingLlamaRequests.isEmpty()) {
        const PendingLlamaRequest requestData = m_pendingLlamaRequests.dequeue();

        QNetworkRequest request(QUrl(llamaServerUrl() + QStringLiteral("/v1/chat/completions")));
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

        QJsonObject body;
        body["temperature"] = 1.0f;
        body["max_tokens"] = 2048;
        body["top_p"] = 0.95;
        body["top_k"] = 64;
        body["max_tokens"] = 2048;
        body["stream"] = false;

        QJsonArray messages;
        QJsonObject systemMessage;
        systemMessage["role"] = QStringLiteral("system");
        systemMessage["content"] = effectivePrompt(requestData.config);
        messages.append(systemMessage);

        QJsonObject userMessage;
        userMessage["role"] = QStringLiteral("user");
        userMessage["content"] = QStringLiteral("<text>") + requestData.text + QStringLiteral("</text>");
        messages.append(userMessage);
        body["messages"] = messages;

        const QByteArray requestBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
#ifndef QT_NO_DEBUG_OUTPUT
        qCDebug(lcLlmRefiner).noquote() << "llama-server HTTP request:" << requestBody;
#endif

        QNetworkReply *reply = m_networkManager->post(request, requestBody);
        connect(reply, &QNetworkReply::finished, this, [this, reply, originalText = requestData.text]() {
            const QByteArray responseBody = reply->readAll();
            const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
#ifdef QT_DEBUG
            qCDebug(lcLlmRefiner).noquote() << "llama-server HTTP response:" << responseBody;
#endif

            if (reply->error() != QNetworkReply::NoError || statusCode != 200) {
                QString message = QString::fromUtf8(responseBody).trimmed();
                if (message.isEmpty()) {
                    message = reply->errorString();
                }
                qCWarning(lcLlmRefiner) << "llama-server HTTP failure. status=" << statusCode
                                        << "networkError=" << reply->error()
                                        << "message=" << message;
                emit errorOccurred(tr("llama-server error: %1").arg(message));
                scheduleLlamaServerIdleShutdown();
                reply->deleteLater();
                return;
            }

            const QJsonDocument document = QJsonDocument::fromJson(responseBody);
            const QJsonArray choices = document.object().value("choices").toArray();
            QString refinedText = originalText;
            if (!choices.isEmpty()) {
                refinedText = choices.first().toObject().value("message").toObject().value("content").toString(originalText).trimmed();
            }

#ifndef QT_NO_DEBUG_OUTPUT
            qCDebug(lcLlmRefiner) << "llama-server refinement success. outputLength=" << refinedText.size();
#endif
            emit refined(refinedText.isEmpty() ? originalText : refinedText);
            scheduleLlamaServerIdleShutdown();
            reply->deleteLater();
        });
    }
}

void LLMRefiner::failPendingLlamaRequests(const QString &message) {
    if (m_pendingLlamaRequests.isEmpty()) {
        return;
    }

    qCWarning(lcLlmRefiner) << "Failing pending llama requests:" << message << "count=" << m_pendingLlamaRequests.size();
    m_pendingLlamaRequests.clear();
    emit errorOccurred(message);
}

void LLMRefiner::scheduleLlamaServerIdleShutdown() {
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "Scheduling llama-server idle shutdown in" << kLlamaServerIdleMs << "ms";
#endif
    m_llamaServerIdleTimer->start();
}

void LLMRefiner::stopLlamaServer() {
    if (!m_llamaServerProcess) {
        return;
    }

#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner) << "Stopping llama-server";
#endif
    m_llamaServerProbeTimer->stop();
    m_llamaServerIdleTimer->stop();
    m_llamaServerStarting = false;
    m_llamaServerReady = false;

    if (m_llamaServerProcess->state() != QProcess::NotRunning) {
        m_llamaServerProcess->terminate();
        if (!m_llamaServerProcess->waitForFinished(2000)) {
            m_llamaServerProcess->kill();
            m_llamaServerProcess->waitForFinished(1000);
        }
    }

    m_llamaServerProcess->deleteLater();
    m_llamaServerProcess = nullptr;
}

QString LLMRefiner::llamaServerUrl() const {
    return QStringLiteral("http://127.0.0.1:%1").arg(kLlamaServerPort);
}
