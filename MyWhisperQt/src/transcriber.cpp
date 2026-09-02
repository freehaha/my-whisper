#include "transcriber.h"

#include "assembly_ai_streaming_transcriber.h"
#include "config.h"
#include "deepgram_streaming_transcriber.h"
#include "deepgram_transcriber.h"

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_deepgramTranscriber(new DeepgramTranscriber(this))
    , m_deepgramStreamingTranscriber(new DeepgramStreamingTranscriber(this))
    , m_assemblyAiTranscriber(new AssemblyAiStreamingTranscriber(this)) {
    connect(m_deepgramTranscriber, &DeepgramTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_deepgramTranscriber, &DeepgramTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
    connect(m_deepgramStreamingTranscriber, &DeepgramStreamingTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_deepgramStreamingTranscriber, &DeepgramStreamingTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
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
    if (Config::usesAssemblyAi(config)) {
        m_assemblyAiTranscriber->start(config, format);
        return;
    }

    m_deepgramStreamingTranscriber->start(config, format);
}

void Transcriber::streamAudio(const QByteArray &data) {
    if (m_assemblyAiTranscriber->isStreaming()) {
        m_assemblyAiTranscriber->streamAudio(data);
        return;
    }

    if (m_deepgramStreamingTranscriber->isStreaming()) {
        m_deepgramStreamingTranscriber->streamAudio(data);
    }
}

void Transcriber::finishStreaming() {
    if (m_assemblyAiTranscriber->isStreaming()) {
        m_assemblyAiTranscriber->finish();
        return;
    }

    if (m_deepgramStreamingTranscriber->isStreaming()) {
        m_deepgramStreamingTranscriber->finish();
    }
}

void Transcriber::cancelStreaming() {
    m_assemblyAiTranscriber->cancel();
    m_deepgramStreamingTranscriber->cancel();
}

bool Transcriber::isStreaming() const {
    return m_assemblyAiTranscriber->isStreaming() || m_deepgramStreamingTranscriber->isStreaming();
}
