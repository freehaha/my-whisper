#include "grok_transcriber.h"
#include "config.h"

#include <QFile>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

GrokTranscriber::GrokTranscriber(QObject *parent, const QUrl &endpoint)
    : QObject(parent), m_networkManager(new QNetworkAccessManager(this)), m_endpoint(endpoint) {}

void GrokTranscriber::transcribe(const QString &audioFilePath, const AppConfig &config) {
    if (!Config::hasValidGrokKey(config)) {
        emit errorOccurred(tr("Invalid xAI API key. Set it in Settings or XAI_API_KEY."));
        return;
    }

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto *file = new QFile(audioFilePath, multipart);
    if (!file->open(QIODevice::ReadOnly) || file->size() == 0 || file->size() > 500000000) {
        emit errorOccurred(tr("Grok requires a readable, nonempty audio file of at most 500 MB."));
        delete multipart;
        return;
    }
    auto appendField = [multipart](const QByteArray &name, const QByteArray &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QString::fromLatin1("form-data; name=\"" + name + "\""));
        part.setBody(value);
        multipart->append(part);
    };
    appendField("model", "grok-voice-transcribe-2.0");
    // Let Grok detect the language. Formatting requires an explicit language.
    QHttpPart audio;
    audio.setHeader(QNetworkRequest::ContentDispositionHeader,
                    QStringLiteral("form-data; name=\"file\"; filename=\"recording.wav\""));
    audio.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    audio.setBodyDevice(file);
    multipart->append(audio); // The API requires the file to be the last field.

    QNetworkRequest request(m_endpoint);
    request.setRawHeader("Authorization", "Bearer " + Config::grokApiKey(config).toUtf8());
    request.setTransferTimeout(120000);
    auto *reply = m_networkManager->post(request, multipart);
    multipart->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            // Do not log audio, transcripts, credentials, or arbitrary server bodies.
            emit errorOccurred(tr("Grok transcription failed (HTTP %1): %2")
                               .arg(status).arg(reply->errorString()));
            return;
        }
        QJsonParseError parseError;
        const auto document = QJsonDocument::fromJson(body, &parseError);
        const auto text = document.object().value(QStringLiteral("text"));
        if (parseError.error != QJsonParseError::NoError || !document.isObject() || !text.isString()) {
            emit errorOccurred(tr("Grok returned an invalid transcription response."));
            return;
        }
        if (text.toString().trimmed().isEmpty()) {
            emit errorOccurred(tr("Grok returned an empty transcript."));
            return;
        }
        emit transcriptionReady(text.toString().trimmed());
    });
}
