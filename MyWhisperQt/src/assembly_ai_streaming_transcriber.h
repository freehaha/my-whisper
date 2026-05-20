#pragma once

#include "types.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QStringList>

class AssemblyAiWebSocketClient;

class AssemblyAiStreamingTranscriber : public QObject {
    Q_OBJECT

public:
    explicit AssemblyAiStreamingTranscriber(QObject *parent = nullptr);
    ~AssemblyAiStreamingTranscriber() override;

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
    void sendStreamingTerminate();
    void handleAssemblyAiMessage(const QString &message);
    void emitStreamingResult();
    QString assembledStreamingTranscript() const;

    AssemblyAiWebSocketClient *m_streamingSocket = nullptr;
    QByteArray m_streamingAudioBuffer;
    QStringList m_finalStreamingTurns;
    QString m_latestStreamingPartial;
    bool m_streamingActive = false;
    bool m_streamingFinishRequested = false;
    bool m_streamingTerminateSent = false;
    bool m_streamingCancelRequested = false;
    bool m_streamingResultEmitted = false;
    int m_streamingChunkBytes = 0;
};
