#pragma once

#include "types.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QStringList>

class DeepgramWebSocketClient;

class DeepgramStreamingTranscriber : public QObject {
    Q_OBJECT

public:
    explicit DeepgramStreamingTranscriber(QObject *parent = nullptr);
    ~DeepgramStreamingTranscriber() override;

    void start(const AppConfig &config, const QAudioFormat &format);
    void streamAudio(const QByteArray &data);
    void finish();
    void cancel();
    bool isStreaming() const;

signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);

private:
    void resetStreamingState();
    void flushPendingStreamingAudio(bool forceFinalChunk = false);
    void sendStreamingCloseStream();
    void handleDeepgramMessage(const QString &message);
    void emitStreamingResult();
    QString assembledStreamingTranscript() const;

    DeepgramWebSocketClient *m_streamingSocket = nullptr;
    QByteArray m_streamingAudioBuffer;
    QStringList m_finalStreamingTurns;
    QString m_latestStreamingPartial;
    bool m_streamingActive = false;
    bool m_streamingFinishRequested = false;
    bool m_streamingCloseSent = false;
    bool m_streamingCancelRequested = false;
    bool m_streamingResultEmitted = false;
    int m_streamingChunkBytes = 0;
};
