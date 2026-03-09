#include "llm_refiner.h"
#include "config.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

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
    body["model"] = QStringLiteral("gpt-4o-mini");
    body["temperature"] = 0.3;

    QJsonArray messages;
    QJsonObject systemMessage;
    systemMessage["role"] = QStringLiteral("system");
    systemMessage["content"] = config.refinementPrompt.isEmpty()
        ? QStringLiteral("Fix spelling and grammar. Return only the fixed text.")
        : config.refinementPrompt;
    messages.append(systemMessage);

    QJsonObject userMessage;
    userMessage["role"] = QStringLiteral("user");
    userMessage["content"] = text;
    messages.append(userMessage);
    body["messages"] = messages;

    QNetworkReply *reply = m_networkManager->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, text]() {
        const QByteArray responseBody = reply->readAll();
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(responseBody).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            emit errorOccurred(tr("OpenAI error: %1").arg(message));
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(responseBody);
        const auto choices = document.object().value("choices").toArray();
        QString refinedText = text;
        if (!choices.isEmpty()) {
            refinedText = choices.first().toObject().value("message").toObject().value("content").toString(text);
        }

        emit refined(refinedText.trimmed());
        reply->deleteLater();
    });
}
