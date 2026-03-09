#include "transcriber.h"
#include "config.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void Transcriber::transcribe(const QString &audioFilePath, const AppConfig &config) {
    if (!Config::hasValidDeepgramKey(config)) {
        emit errorOccurred(tr("Invalid Deepgram API key. Set it in %1").arg(Config::configPath()));
        return;
    }

    QFile file(audioFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Unable to read audio file: %1").arg(file.errorString()));
        return;
    }

    QNetworkRequest request(QUrl(QStringLiteral("https://api.deepgram.com/v1/listen?model=nova-3&smart_format=true")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    request.setRawHeader("Authorization", QByteArray("Token ") + config.deepgramApiKey.toUtf8());

    QNetworkReply *reply = m_networkManager->post(request, file.readAll());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(body).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            emit errorOccurred(tr("Deepgram API error: %1").arg(message));
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject object = document.object();
        const auto results = object.value("results").toObject();
        const auto channels = results.value("channels").toArray();
        const auto alternatives = channels.isEmpty() ? QJsonArray() : channels.first().toObject().value("alternatives").toArray();
        const QString transcript = alternatives.isEmpty() ? QString() : alternatives.first().toObject().value("transcript").toString();

        emit transcriptionReady(transcript);
        reply->deleteLater();
    });
}
