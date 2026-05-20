#pragma once

#include "types.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>

class AssemblyAiStreamingTranscriber;
class DeepgramTranscriber;

class Transcriber : public QObject {
    Q_OBJECT

public:
    explicit Transcriber(QObject *parent = nullptr);
    ~Transcriber() override;

    void transcribe(const QString &audioFilePath, const AppConfig &config);

    void startStreaming(const AppConfig &config, const QAudioFormat &format);
    void streamAudio(const QByteArray &data);
    void finishStreaming();
    void cancelStreaming();
    bool isStreaming() const;

signals:
    void transcriptionReady(const QString &text);
    void errorOccurred(const QString &message);

private:
    DeepgramTranscriber *m_deepgramTranscriber = nullptr;
    AssemblyAiStreamingTranscriber *m_assemblyAiTranscriber = nullptr;
};
