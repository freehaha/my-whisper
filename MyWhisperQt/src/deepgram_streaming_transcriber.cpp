#include "deepgram_streaming_transcriber.h"

#include "config.h"
#include "deepgram_websocket_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QTimer>
#include <algorithm>

Q_LOGGING_CATEGORY(lcDeepgramStreamingTranscriber, "mywhisper.deepgram_streaming_transcriber")

namespace {
QString jsonString(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString().trimmed();
}
}

DeepgramStreamingTranscriber::DeepgramStreamingTranscriber(QObject *parent)
    : QObject(parent) {
}

DeepgramStreamingTranscriber::~DeepgramStreamingTranscriber() {
    cancel();
}

void DeepgramStreamingTranscriber::start(const AppConfig &config, const QAudioFormat &format) {
    cancel();

    if (!Config::hasValidDeepgramKey(config)) {
        emit errorOccurred(tr("Invalid Deepgram API key. Set it in %1").arg(Config::configPath()));
        return;
    }

    if (format.channelCount() != 1 || format.sampleFormat() != QAudioFormat::Int16 || format.sampleRate() <= 0) {
        emit errorOccurred(tr("Deepgram streaming requires mono 16-bit PCM microphone audio. Current format: %1 Hz, %2 channel(s), sample format %3.")
            .arg(format.sampleRate())
            .arg(format.channelCount())
            .arg(static_cast<int>(format.sampleFormat())));
        return;
    }

    const int frameBytes = std::max(1, format.bytesPerSample() * format.channelCount());
    const int framesPer50ms = std::max(1, (format.sampleRate() * 50 + 999) / 1000);
    m_streamingChunkBytes = framesPer50ms * frameBytes;

    qCDebug(lcDeepgramStreamingTranscriber) << "Starting Deepgram streaming transcription. sampleRate=" << format.sampleRate()
                       << "keywords=" << config.deepgramKeywords
                       << "chunkBytes=" << m_streamingChunkBytes;

    m_streamingAudioBuffer.clear();
    m_finalStreamingTurns.clear();
    m_latestStreamingPartial.clear();
    m_streamingActive = true;
    m_streamingFinishRequested = false;
    m_streamingCloseSent = false;
    m_streamingCancelRequested = false;
    m_streamingResultEmitted = false;

    m_streamingSocket = new DeepgramWebSocketClient(this);
    m_streamingSocket->onOpen = [this]() {
        qCDebug(lcDeepgramStreamingTranscriber) << "Deepgram WebSocket opened.";
        flushPendingStreamingAudio(m_streamingFinishRequested);
        if (m_streamingFinishRequested) {
            sendStreamingCloseStream();
        }
    };
    m_streamingSocket->onTextMessage = [this](const QString &message) {
        handleDeepgramMessage(message);
    };
    m_streamingSocket->onError = [this](const QString &message) {
        qCWarning(lcDeepgramStreamingTranscriber).noquote() << "Deepgram streaming socket error:" << message;
        const bool shouldReport = m_streamingActive && !m_streamingCancelRequested && !m_streamingResultEmitted;
        resetStreamingState();
        if (shouldReport) {
            emit errorOccurred(tr("Deepgram streaming error: %1").arg(message));
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

        qCWarning(lcDeepgramStreamingTranscriber) << "Deepgram streaming connection closed unexpectedly.";
        resetStreamingState();
        emit errorOccurred(tr("Deepgram streaming connection closed unexpectedly."));
    };
    m_streamingSocket->connectToDeepgram(config.deepgramApiKey, format.sampleRate(), config.deepgramKeywords);
}

void DeepgramStreamingTranscriber::streamAudio(const QByteArray &data) {
    if (!m_streamingActive || m_streamingFinishRequested || m_streamingCancelRequested || data.isEmpty()) {
        return;
    }

    m_streamingAudioBuffer.append(data);
    flushPendingStreamingAudio();
}

void DeepgramStreamingTranscriber::finish() {
    if (!m_streamingActive || !m_streamingSocket) {
        emit errorOccurred(tr("Deepgram streaming session is not active."));
        return;
    }

    m_streamingFinishRequested = true;
    flushPendingStreamingAudio(true);
    sendStreamingCloseStream();

    QTimer::singleShot(15000, this, [this]() {
        if (m_streamingActive && m_streamingFinishRequested && !m_streamingResultEmitted && !m_streamingCancelRequested) {
            qCWarning(lcDeepgramStreamingTranscriber) << "Deepgram close timed out; using latest transcript.";
            emitStreamingResult();
        }
    });
}

void DeepgramStreamingTranscriber::cancel() {
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
            m_streamingSocket->sendTextMessage(QByteArrayLiteral("{\"type\":\"CloseStream\"}"));
            m_streamingSocket->closeGracefully();
        } else {
            m_streamingSocket->abort();
        }
        m_streamingSocket->deleteLater();
        m_streamingSocket = nullptr;
    }
    resetStreamingState();
}

bool DeepgramStreamingTranscriber::isStreaming() const {
    return m_streamingActive;
}

void DeepgramStreamingTranscriber::resetStreamingState() {
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
    m_streamingCloseSent = false;
    m_streamingCancelRequested = false;
    m_streamingResultEmitted = false;
    m_streamingChunkBytes = 0;
}

void DeepgramStreamingTranscriber::flushPendingStreamingAudio(bool forceFinalChunk) {
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

void DeepgramStreamingTranscriber::sendStreamingCloseStream() {
    if (!m_streamingSocket || !m_streamingSocket->isOpen() || m_streamingCloseSent) {
        return;
    }

    m_streamingCloseSent = true;
    m_streamingSocket->sendTextMessage(QByteArrayLiteral("{\"type\":\"CloseStream\"}"));
}

void DeepgramStreamingTranscriber::handleDeepgramMessage(const QString &message) {
    qCDebug(lcDeepgramStreamingTranscriber).noquote() << "Deepgram message:" << message;

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(message.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcDeepgramStreamingTranscriber) << "Unable to parse Deepgram message:" << parseError.errorString();
        return;
    }

    const QJsonObject object = document.object();
    const QString type = jsonString(object, "type");

    if (type == QStringLiteral("Metadata")) {
        qCDebug(lcDeepgramStreamingTranscriber) << "Deepgram session metadata:" << jsonString(object, "request_id");
        return;
    }

    if (type == QStringLiteral("SpeechStarted")) {
        return;
    }

    if (type == QStringLiteral("UtteranceEnd")) {
        return;
    }

    if (type == QStringLiteral("Results")) {
        const QJsonObject channel = object.value(QStringLiteral("channel")).toObject();
        const QJsonArray alternatives = channel.value(QStringLiteral("alternatives")).toArray();
        const QString transcript = alternatives.isEmpty() ? QString() : alternatives.first().toObject().value(QStringLiteral("transcript")).toString().trimmed();

        const bool isFinal = object.value(QStringLiteral("is_final")).toBool(false);
        if (isFinal) {
            if (!transcript.isEmpty()) {
                m_finalStreamingTurns.append(transcript);
            }
            m_latestStreamingPartial.clear();
        } else {
            m_latestStreamingPartial = transcript;
        }
        return;
    }

    if (type == QStringLiteral("Error")) {
        const QString errorMessage = jsonString(object, "message");
        const QString displayedMessage = errorMessage.isEmpty() ? tr("Deepgram streaming returned an error: %1").arg(message) : errorMessage;
        qCWarning(lcDeepgramStreamingTranscriber).noquote() << "Deepgram Error event:" << displayedMessage;
        resetStreamingState();
        emit errorOccurred(displayedMessage);
    }
}

void DeepgramStreamingTranscriber::emitStreamingResult() {
    if (m_streamingResultEmitted) {
        return;
    }

    const QString transcript = assembledStreamingTranscript();
    m_streamingResultEmitted = true;

    DeepgramWebSocketClient *socket = m_streamingSocket;
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
    m_streamingCloseSent = false;
    m_streamingCancelRequested = false;

    qCDebug(lcDeepgramStreamingTranscriber) << "Deepgram transcript length:" << transcript.size();
    qCDebug(lcDeepgramStreamingTranscriber).noquote() << "Deepgram transcript:" << transcript;
    emit transcriptionReady(transcript);
    m_streamingResultEmitted = false;
}

QString DeepgramStreamingTranscriber::assembledStreamingTranscript() const {
    QString transcript = m_finalStreamingTurns.join(QStringLiteral(" ")).trimmed();
    if (transcript.isEmpty()) {
        transcript = m_latestStreamingPartial.trimmed();
    } else if (!m_latestStreamingPartial.trimmed().isEmpty()) {
        transcript = QStringLiteral("%1 %2").arg(transcript, m_latestStreamingPartial.trimmed()).trimmed();
    }
    return transcript;
}
