#pragma once

#include "types.h"
#include <QObject>
#include <QUrl>

class QNetworkAccessManager;

class GrokTranscriber : public QObject {
    Q_OBJECT
public:
    explicit GrokTranscriber(QObject *parent = nullptr,
                             const QUrl &endpoint = QUrl(QStringLiteral("https://api.x.ai/v1/stt")));
    void transcribe(const QString &audioFilePath, const AppConfig &config);
signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);
private:
    QNetworkAccessManager *m_networkManager;
    QUrl m_endpoint;
};
