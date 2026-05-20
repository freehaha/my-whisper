#include "transcriber.h"

#include "assembly_ai_streaming_transcriber.h"
#include "config.h"
#include "deepgram_transcriber.h"

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_deepgramTranscriber(new DeepgramTranscriber(this))
    , m_assemblyAiTranscriber(new AssemblyAiStreamingTranscriber(this)) {
    connect(m_deepgramTranscriber, &DeepgramTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_deepgramTranscriber, &DeepgramTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
    connect(m_assemblyAiTranscriber, &AssemblyAiStreamingTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_assemblyAiTranscriber, &AssemblyAiStreamingTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
}

Transcriber::~Transcriber() {
    cancelStreaming();
}

void Transcriber::transcribe(const QString &audioFilePath, const AppConfig &config) {
    if (Config::usesAssemblyAi(config)) {
        emit errorOccurred(tr("AssemblyAI transcription uses streaming and must start when recording starts."));
        return;
    }

    m_deepgramTranscriber->transcribe(audioFilePath, config);
}

void Transcriber::startStreaming(const AppConfig &config, const QAudioFormat &format) {
    m_assemblyAiTranscriber->start(config, format);
}

void Transcriber::streamAudio(const QByteArray &data) {
    m_assemblyAiTranscriber->streamAudio(data);
}

void Transcriber::finishStreaming() {
    m_assemblyAiTranscriber->finish();
}

void Transcriber::cancelStreaming() {
    m_assemblyAiTranscriber->cancel();
}

bool Transcriber::isStreaming() const {
    return m_assemblyAiTranscriber->isStreaming();
}
