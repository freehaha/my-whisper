#include "transcriber.h"
#include "assembly_ai_websocket_client.h"
#include "config.h"
#include "deepgram_transcriber.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <algorithm>

Q_LOGGING_CATEGORY(lcTranscriber, "mywhisper.transcriber")

namespace {
QString jsonString(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString().trimmed();
}

}

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_deepgramTranscriber(new DeepgramTranscriber(this)) {
    connect(m_deepgramTranscriber, &DeepgramTranscriber::transcriptionReady, this, &Transcriber::transcriptionReady);
    connect(m_deepgramTranscriber, &DeepgramTranscriber::errorOccurred, this, &Transcriber::errorOccurred);
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
    cancelStreaming();

    if (!Config::hasValidAssemblyAiKey(config)) {
        emit errorOccurred(tr("Invalid AssemblyAI API key. Set it in %1").arg(Config::configPath()));
        return;
    }

    if (format.channelCount() != 1 || format.sampleFormat() != QAudioFormat::Int16 || format.sampleRate() <= 0) {
        emit errorOccurred(tr("AssemblyAI streaming requires mono 16-bit PCM microphone audio. Current format: %1 Hz, %2 channel(s), sample format %3.")
            .arg(format.sampleRate())
            .arg(format.channelCount())
            .arg(static_cast<int>(format.sampleFormat())));
        return;
    }

    const int frameBytes = std::max(1, format.bytesPerSample() * format.channelCount());
    const int framesPer50ms = std::max(1, (format.sampleRate() * 50 + 999) / 1000);
    m_streamingChunkBytes = framesPer50ms * frameBytes;

    qCDebug(lcTranscriber) << "Starting AssemblyAI streaming transcription. sampleRate=" << format.sampleRate()
                           << "speechModel=" << config.assemblyAiSpeechModel
                           << "keyterms=" << config.deepgramKeywords
                           << "chunkBytes=" << m_streamingChunkBytes;

    m_streamingAudioBuffer.clear();
    m_finalStreamingTurns.clear();
    m_latestStreamingPartial.clear();
    m_streamingActive = true;
    m_streamingFinishRequested = false;
    m_streamingTerminateSent = false;
    m_streamingCancelRequested = false;
    m_streamingResultEmitted = false;

    m_streamingSocket = new AssemblyAiWebSocketClient(this);
    m_streamingSocket->onOpen = [this]() {
        qCDebug(lcTranscriber) << "AssemblyAI WebSocket opened.";
        flushPendingStreamingAudio(m_streamingFinishRequested);
        if (m_streamingFinishRequested) {
            sendStreamingTerminate();
        }
    };
    m_streamingSocket->onTextMessage = [this](const QString &message) {
        handleAssemblyAiMessage(message);
    };
    m_streamingSocket->onError = [this](const QString &message) {
        qCWarning(lcTranscriber).noquote() << "AssemblyAI streaming socket error:" << message;
        const bool shouldReport = m_streamingActive && !m_streamingCancelRequested && !m_streamingResultEmitted;
        resetStreamingState();
        if (shouldReport) {
            emit errorOccurred(tr("AssemblyAI streaming error: %1").arg(message));
        }
    };
    m_streamingSocket->onClosed = [this]() {
        if (m_streamingCancelRequested || m_streamingResultEmitted) {
            resetStreamingState();
            return;
        }

        if (m_streamingFinishRequested) {
            emitStreamingResult();
            return;
        }

        qCWarning(lcTranscriber) << "AssemblyAI streaming connection closed unexpectedly.";
        resetStreamingState();
        emit errorOccurred(tr("AssemblyAI streaming connection closed unexpectedly."));
    };
    m_streamingSocket->connectToAssemblyAI(config.assemblyAiApiKey, config.normalizedAssemblyAiSpeechModel(), format.sampleRate(), config.deepgramKeywords);
}

void Transcriber::streamAudio(const QByteArray &data) {
    if (!m_streamingActive || m_streamingFinishRequested || m_streamingCancelRequested || data.isEmpty()) {
        return;
    }

    m_streamingAudioBuffer.append(data);
    flushPendingStreamingAudio();
}

void Transcriber::finishStreaming() {
    if (!m_streamingActive || !m_streamingSocket) {
        emit errorOccurred(tr("AssemblyAI streaming session is not active."));
        return;
    }

    m_streamingFinishRequested = true;
    flushPendingStreamingAudio(true);
    sendStreamingTerminate();

    QTimer::singleShot(15000, this, [this]() {
        if (m_streamingActive && m_streamingFinishRequested && !m_streamingResultEmitted && !m_streamingCancelRequested) {
            qCWarning(lcTranscriber) << "AssemblyAI termination timed out; using latest transcript.";
            emitStreamingResult();
        }
    });
}

void Transcriber::cancelStreaming() {
    if (!m_streamingActive && !m_streamingSocket) {
        return;
    }

    m_streamingCancelRequested = true;
    if (m_streamingSocket) {
        m_streamingSocket->onOpen = nullptr;
        m_streamingSocket->onTextMessage = nullptr;
        m_streamingSocket->onError = nullptr;
        m_streamingSocket->onClosed = nullptr;
        if (m_streamingSocket->isOpen()) {
            m_streamingSocket->sendTextMessage(QByteArrayLiteral("{\"type\":\"Terminate\"}"));
            m_streamingSocket->closeGracefully();
        } else {
            m_streamingSocket->abort();
        }
        m_streamingSocket->deleteLater();
        m_streamingSocket = nullptr;
    }
    resetStreamingState();
}

bool Transcriber::isStreaming() const {
    return m_streamingActive;
}

void Transcriber::resetStreamingState() {
    if (m_streamingSocket) {
        m_streamingSocket->onOpen = nullptr;
        m_streamingSocket->onTextMessage = nullptr;
        m_streamingSocket->onError = nullptr;
        m_streamingSocket->onClosed = nullptr;
        m_streamingSocket->closeGracefully();
        m_streamingSocket->deleteLater();
        m_streamingSocket = nullptr;
    }

    m_streamingAudioBuffer.clear();
    m_finalStreamingTurns.clear();
    m_latestStreamingPartial.clear();
    m_streamingActive = false;
    m_streamingFinishRequested = false;
    m_streamingTerminateSent = false;
    m_streamingCancelRequested = false;
    m_streamingResultEmitted = false;
    m_streamingChunkBytes = 0;
}

void Transcriber::flushPendingStreamingAudio(bool forceFinalChunk) {
    if (!m_streamingSocket || !m_streamingSocket->isOpen() || m_streamingChunkBytes <= 0) {
        return;
    }

    while (m_streamingAudioBuffer.size() >= m_streamingChunkBytes) {
        const QByteArray chunk = m_streamingAudioBuffer.left(m_streamingChunkBytes);
        m_streamingAudioBuffer.remove(0, m_streamingChunkBytes);
        m_streamingSocket->sendBinaryMessage(chunk);
    }

    if (forceFinalChunk && !m_streamingAudioBuffer.isEmpty()) {
        QByteArray chunk = m_streamingAudioBuffer;
        chunk.append(QByteArray(m_streamingChunkBytes - chunk.size(), '\0'));
        m_streamingAudioBuffer.clear();
        m_streamingSocket->sendBinaryMessage(chunk);
    }
}

void Transcriber::sendStreamingTerminate() {
    if (!m_streamingSocket || !m_streamingSocket->isOpen() || m_streamingTerminateSent) {
        return;
    }

    m_streamingTerminateSent = true;
    m_streamingSocket->sendTextMessage(QByteArrayLiteral("{\"type\":\"Terminate\"}"));
}

void Transcriber::handleAssemblyAiMessage(const QString &message) {
    qCDebug(lcTranscriber).noquote() << "AssemblyAI message:" << message;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcTranscriber) << "Unable to parse AssemblyAI message:" << parseError.errorString();
        return;
    }

    const QJsonObject object = document.object();
    const QString type = jsonString(object, "type");

    if (type == QStringLiteral("Begin")) {
        qCDebug(lcTranscriber) << "AssemblyAI session began:" << jsonString(object, "id");
        return;
    }

    if (type == QStringLiteral("Turn")) {
        const QString transcript = jsonString(object, "transcript");
        if (object.value(QStringLiteral("end_of_turn")).toBool(false)) {
            if (!transcript.isEmpty()) {
                m_finalStreamingTurns.append(transcript);
            }
            m_latestStreamingPartial.clear();
        } else {
            m_latestStreamingPartial = transcript;
        }
        return;
    }

    if (type == QStringLiteral("Termination")) {
        qCDebug(lcTranscriber) << "AssemblyAI session terminated. audioDuration="
                               << object.value(QStringLiteral("audio_duration_seconds")).toDouble()
                               << "sessionDuration=" << object.value(QStringLiteral("session_duration_seconds")).toDouble();
        emitStreamingResult();
        return;
    }

    if (type == QStringLiteral("Error")) {
        const QString errorMessage = jsonString(object, "message");
        const QString displayedMessage = errorMessage.isEmpty() ? tr("AssemblyAI streaming returned an error: %1").arg(message) : errorMessage;
        qCWarning(lcTranscriber).noquote() << "AssemblyAI Error event:" << displayedMessage;
        resetStreamingState();
        emit errorOccurred(displayedMessage);
    }
}

void Transcriber::emitStreamingResult() {
    if (m_streamingResultEmitted) {
        return;
    }

    const QString transcript = assembledStreamingTranscript();
    m_streamingResultEmitted = true;

    AssemblyAiWebSocketClient *socket = m_streamingSocket;
    m_streamingSocket = nullptr;
    if (socket) {
        socket->onOpen = nullptr;
        socket->onTextMessage = nullptr;
        socket->onError = nullptr;
        socket->onClosed = nullptr;
        socket->closeGracefully();
        socket->deleteLater();
    }

    m_streamingAudioBuffer.clear();
    m_finalStreamingTurns.clear();
    m_latestStreamingPartial.clear();
    m_streamingActive = false;
    m_streamingFinishRequested = false;
    m_streamingTerminateSent = false;
    m_streamingCancelRequested = false;

    qCDebug(lcTranscriber) << "AssemblyAI transcript length:" << transcript.size();
    qCDebug(lcTranscriber).noquote() << "AssemblyAI transcript:" << transcript;
    emit transcriptionReady(transcript);
    m_streamingResultEmitted = false;
}

QString Transcriber::assembledStreamingTranscript() const {
    QString transcript = m_finalStreamingTurns.join(QStringLiteral(" ")).trimmed();
    if (transcript.isEmpty()) {
        transcript = m_latestStreamingPartial.trimmed();
    } else if (!m_latestStreamingPartial.trimmed().isEmpty()) {
        transcript = QStringLiteral("%1 %2").arg(transcript, m_latestStreamingPartial.trimmed()).trimmed();
    }
    return transcript;
}
