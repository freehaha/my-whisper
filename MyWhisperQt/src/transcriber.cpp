#include "transcriber.h"

#include "assembly_ai_streaming_transcriber.h"
#include "config.h"
#include "grok_transcriber.h"
#include "grok_streaming_transcriber.h"
#include "deepgram_streaming_transcriber.h"
#include "deepgram_transcriber.h"

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_deepgramTranscriber(new DeepgramTranscriber(this))
    , m_deepgramStreamingTranscriber(new DeepgramStreamingTranscriber(this))
    , m_assemblyAiTranscriber(new AssemblyAiStreamingTranscriber(this)) {
    m_grokStreamingTranscriber = new GrokStreamingTranscriber(this);
    connect(m_grokStreamingTranscriber, &GrokStreamingTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_grokStreamingTranscriber, &GrokStreamingTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
    m_grokTranscriber = new GrokTranscriber(this);
    connect(m_grokTranscriber, &GrokTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_grokTranscriber, &GrokTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
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

    if (Config::usesGrok(config)) {
        m_grokTranscriber->transcribe(audioFilePath, config);
        return;
    }
    m_deepgramTranscriber->transcribe(audioFilePath, config);
}

void Transcriber::startStreaming(const AppConfig &config, const QAudioFormat &format) {
    if (Config::usesGrok(config)) {
        if (config.transcriptionBackend.trimmed().toLower() == QStringLiteral("grok_streaming")) {
            m_grokStreamingTranscriber->start(config, format);
        }
        return;
    }
    if (Config::usesAssemblyAi(config)) {
        m_assemblyAiTranscriber->start(config, format);
        return;
    }

    m_deepgramStreamingTranscriber->start(config, format);
}

void Transcriber::streamAudio(const QByteArray &data) {
    if (m_grokStreamingTranscriber->isStreaming()) {
        m_grokStreamingTranscriber->streamAudio(data);
        return;
    }
    if (m_assemblyAiTranscriber->isStreaming()) {
        m_assemblyAiTranscriber->streamAudio(data);
        return;
    }

    if (m_deepgramStreamingTranscriber->isStreaming()) {
        m_deepgramStreamingTranscriber->streamAudio(data);
    }
}

void Transcriber::finishStreaming() {
    if (m_grokStreamingTranscriber->isStreaming()) {
        m_grokStreamingTranscriber->finish();
        return;
    }
    if (m_assemblyAiTranscriber->isStreaming()) {
        m_assemblyAiTranscriber->finish();
        return;
    }

    if (m_deepgramStreamingTranscriber->isStreaming()) {
        m_deepgramStreamingTranscriber->finish();
    }
}

void Transcriber::cancelStreaming() {
    m_grokStreamingTranscriber->cancel();
    m_assemblyAiTranscriber->cancel();
    m_deepgramStreamingTranscriber->cancel();
}

bool Transcriber::isStreaming() const {
    return m_grokStreamingTranscriber->isStreaming() || m_assemblyAiTranscriber->isStreaming() || m_deepgramStreamingTranscriber->isStreaming();
}
