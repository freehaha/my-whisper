#include "llm_refiner.h"
#include "config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QLoggingCategory>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

Q_LOGGING_CATEGORY(lcLlmRefiner, "mywhisper.llm_refiner")

LLMRefiner::LLMRefiner(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void LLMRefiner::refine(const QString &text, const AppConfig &config) {
    if (!Config::hasUsableRefiner(config)) {
        QMetaObject::invokeMethod(this, [this, text]() {
            emit refined(text);
        }, Qt::QueuedConnection);
        return;
    }

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
    systemMessage["content"] = config.refinementPrompt.isEmpty()
        ? QStringLiteral("Fix spelling and grammar. Return the corrected text in the refined_text field.")
        : config.refinementPrompt;
    messages.append(systemMessage);

    QJsonObject userMessage;
    userMessage["role"] = QStringLiteral("user");
    userMessage["content"] = text;
    messages.append(userMessage);
    body["messages"] = messages;

    const QByteArray requestBody = QJsonDocument(body).toJson(QJsonDocument::Compact);
#ifndef QT_NO_DEBUG_OUTPUT
    qCDebug(lcLlmRefiner).noquote() << "LLM Refiner request:" << requestBody;
#endif
    QNetworkReply *reply = m_networkManager->post(request, requestBody);
    connect(reply, &QNetworkReply::finished, this, [this, reply, text]() {
        const QByteArray responseBody = reply->readAll();
#ifdef QT_DEBUG
        qCDebug(lcLlmRefiner).noquote() << "LLM Refiner response:" << responseBody;
#endif
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(responseBody).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            qWarning() << "LLM Refiner error:" << statusCode.toInt() << message;
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

        emit refined(refinedText.trimmed());
        reply->deleteLater();
    });
}
