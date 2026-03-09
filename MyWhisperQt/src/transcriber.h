#pragma once

#include "types.h"

#include <QObject>

class QNetworkAccessManager;

class Transcriber : public QObject {
    Q_OBJECT

public:
    explicit Transcriber(QObject *parent = nullptr);
    void transcribe(const QString &audioFilePath, const AppConfig &config);

signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
};
