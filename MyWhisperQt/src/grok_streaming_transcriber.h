#pragma once
#include "types.h"
#include <QAudioFormat>
#include <QObject>
#include <QMap>
#include <QTimer>
#include <QUrl>

class DeepgramWebSocketClient;

class GrokStreamingTranscriber : public QObject {
    Q_OBJECT
public:
    explicit GrokStreamingTranscriber(QObject *parent = nullptr,
        const QUrl &endpoint = QUrl(QStringLiteral("wss://api.x.ai/v1/stt")));
    ~GrokStreamingTranscriber() override;
    void start(const AppConfig &config, const QAudioFormat &format);
    void streamAudio(const QByteArray &data);
    void finish();
    void cancel();
    bool isStreaming() const { return m_active; }
signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);
private:
    void handleMessage(const QString &message);
    void sendNextChunk();
    void fail(const QString &message);
    DeepgramWebSocketClient *m_socket = nullptr;
    QUrl m_endpoint;
    QTimer m_pacer;
    QTimer m_deadline;
    QByteArray m_pending;
    struct Utterance {
        QMap<double, QString> chunks;
        QString text;
        bool complete = false;
    };
    QMap<double, Utterance> m_utterances;
    int m_chunkBytes = 0;
    bool m_active = false;
    bool m_ready = false;
    bool m_finishing = false;
    bool m_doneSent = false;
};
