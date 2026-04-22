#pragma once

#include "types.h"

#include <QObject>
#include <QQueue>

class QNetworkAccessManager;
class QProcess;
class QTimer;

class LLMRefiner : public QObject {
    Q_OBJECT

public:
    explicit LLMRefiner(QObject *parent = nullptr);
    void refine(const QString &text, const AppConfig &config);

private:
    struct PendingLlamaRequest {
        QString text;
        AppConfig config;
    };

    void refineWithOpenAi(const QString &text, const AppConfig &config);
    void refineWithLlamaCpp(const QString &text, const AppConfig &config);
    void ensureLlamaServer(const AppConfig &config);
    void startLlamaServer(const AppConfig &config);
    void probeLlamaServerReadiness();
    void flushPendingLlamaRequests();
    void failPendingLlamaRequests(const QString &message);
    void scheduleLlamaServerIdleShutdown();
    void stopLlamaServer();
    QString llamaServerUrl() const;

signals:
    void refined(const QString &text);
    void errorOccurred(const QString &message);

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QProcess *m_llamaServerProcess = nullptr;
    QTimer *m_llamaServerIdleTimer = nullptr;
    QTimer *m_llamaServerProbeTimer = nullptr;
    QQueue<PendingLlamaRequest> m_pendingLlamaRequests;
    AppConfig m_llamaServerConfig;
    bool m_llamaServerReady = false;
    bool m_llamaServerStarting = false;
    int m_llamaServerProbeAttempts = 0;
};
