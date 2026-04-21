#include "transcriber.h"
#include "config.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

Q_LOGGING_CATEGORY(lcDeepgram, "mywhisper.transcriber")

namespace {
QUrl deepgramUrlForConfig(const AppConfig &config) {
    QUrl url(QStringLiteral("https://api.deepgram.com/v1/listen"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("model"), QStringLiteral("nova-3"));
    query.addQueryItem(QStringLiteral("smart_format"), QStringLiteral("true"));
    for (const QString &keyword : config.deepgramKeywords) {
        const QString trimmed = keyword.trimmed();
        if (!trimmed.isEmpty()) {
            query.addQueryItem(QStringLiteral("keyterm"), trimmed);
        }
    }
    url.setQuery(query);
    return url;
}
}

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

void Transcriber::transcribe(const QString &audioFilePath, const AppConfig &config) {
    qCDebug(lcDeepgram) << "Starting Deepgram transcription for" << audioFilePath;
    qCDebug(lcDeepgram) << "Deepgram keyword hints:" << config.deepgramKeywords;

    if (!Config::hasValidDeepgramKey(config)) {
        emit errorOccurred(tr("Invalid Deepgram API key. Set it in %1").arg(Config::configPath()));
        return;
    }

    QFile file(audioFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Unable to read audio file: %1").arg(file.errorString()));
        return;
    }

    const QByteArray audioData = file.readAll();
    const QUrl requestUrl = deepgramUrlForConfig(config);

    qCDebug(lcDeepgram) << "Deepgram request URL:" << requestUrl.toString(QUrl::FullyEncoded);
    qCDebug(lcDeepgram) << "Audio file size bytes:" << audioData.size();
    qCDebug(lcDeepgram) << "Audio file name:" << QFileInfo(file).fileName();

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    request.setRawHeader("Authorization", QByteArray("Token ") + config.deepgramApiKey.toUtf8());

    QNetworkReply *reply = m_networkManager->post(request, audioData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QVariant reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        qCDebug(lcDeepgram) << "Deepgram HTTP status:" << statusCode << reasonPhrase.toString();
        qCDebug(lcDeepgram).noquote() << "Deepgram raw response:" << QString::fromUtf8(body);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(body).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            qCWarning(lcDeepgram) << "Deepgram request failed. Network error:" << reply->error()
                                  << "errorString:" << reply->errorString();
            qCWarning(lcDeepgram).noquote() << "Deepgram error body:" << message;
            emit errorOccurred(tr("Deepgram API error (%1 %2): %3")
                .arg(statusCode.toInt())
                .arg(reasonPhrase.toString(), message));
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject object = document.object();
        const auto results = object.value("results").toObject();
        const auto channels = results.value("channels").toArray();
        const auto alternatives = channels.isEmpty() ? QJsonArray() : channels.first().toObject().value("alternatives").toArray();
        const QString transcript = alternatives.isEmpty() ? QString() : alternatives.first().toObject().value("transcript").toString();

        qCDebug(lcDeepgram) << "Deepgram transcript length:" << transcript.size();
        qCDebug(lcDeepgram).noquote() << "Deepgram transcript:" << transcript;

        emit transcriptionReady(transcript);
        reply->deleteLater();
    });
}
