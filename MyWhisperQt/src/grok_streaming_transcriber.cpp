#include "grok_streaming_transcriber.h"
#include "config.h"
#include "deepgram_websocket_client.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

GrokStreamingTranscriber::GrokStreamingTranscriber(QObject *parent, const QUrl &endpoint)
    : QObject(parent), m_endpoint(endpoint) {
    // Poll every 50 ms, sending at most one captured 100 ms chunk per tick.
    // This drains startup backlog at up to 2x real time without unbounded bursts.
    // Once caught up, microphone availability limits sending to real time.
    m_pacer.setInterval(50);
    m_pacer.setTimerType(Qt::PreciseTimer);
    m_deadline.setSingleShot(true);
    connect(&m_pacer, &QTimer::timeout, this, &GrokStreamingTranscriber::sendNextChunk);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(tr("Grok streaming timed out while waiting for the server."));
    });
}

GrokStreamingTranscriber::~GrokStreamingTranscriber() { cancel(); }

void GrokStreamingTranscriber::start(const AppConfig &config, const QAudioFormat &format) {
    cancel();
    if (!Config::hasValidGrokKey(config)) {
        emit errorOccurred(tr("Invalid xAI API key. Set it in Settings or XAI_API_KEY."));
        return;
    }
    const QList<int> rates{8000, 16000, 22050, 24000, 44100, 48000};
    if (format.channelCount() != 1 || format.sampleFormat() != QAudioFormat::Int16
        || !rates.contains(format.sampleRate())) {
        emit errorOccurred(tr("Grok streaming requires mono 16-bit PCM at a supported sample rate. Use Grok upload for this microphone."));
        return;
    }
    m_chunkBytes = format.sampleRate() / 10 * 2;
    m_active = true;
    QUrl url(m_endpoint);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("model"), QStringLiteral("grok-voice-transcribe-2.0"));
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("pcm"));
    query.addQueryItem(QStringLiteral("sample_rate"), QString::number(format.sampleRate()));
    query.addQueryItem(QStringLiteral("channels"), QStringLiteral("1"));
    url.setQuery(query);
    m_socket = new DeepgramWebSocketClient(this);
    m_socket->onTextMessage = [this](const QString &message) { handleMessage(message); };
    m_socket->onError = [this](const QString &message) { fail(tr("Grok streaming error: %1").arg(message)); };
    m_socket->onClosed = [this] { fail(tr("Grok streaming closed before the final transcript arrived.")); };
    m_deadline.start(15000);
    m_socket->connectToUrl(url, "Bearer " + Config::grokApiKey(config).toUtf8());
}

void GrokStreamingTranscriber::streamAudio(const QByteArray &data) {
    if (!m_active || m_finishing) return;
    // Bound the startup/backpressure queue to 30 seconds of audio.
    if (m_pending.size() + data.size() > m_chunkBytes * 300) {
        fail(tr("Grok streaming could not keep up with the microphone."));
        return;
    }
    m_pending.append(data);
}

void GrokStreamingTranscriber::finish() {
    if (!m_active || m_finishing) return;
    m_finishing = true;
    // Include time to drain buffered audio and receive the final result.
    m_deadline.start(45000);
    // Do not add a timer tick when all audio has already reached the socket.
    if (m_pending.isEmpty()) sendNextChunk();
}

void GrokStreamingTranscriber::sendNextChunk() {
    if (!m_active || !m_ready || !m_socket || m_doneSent) return;
    if (m_socket->pendingBytes() > m_chunkBytes * 300) {
        fail(tr("Grok streaming network queue exceeded 30 seconds of audio."));
        return;
    }
    if (m_pending.size() >= m_chunkBytes || (m_finishing && !m_pending.isEmpty())) {
        const int size = qMin(m_chunkBytes, static_cast<int>(m_pending.size()));
        if (size % 2 != 0) {
            fail(tr("Grok streaming received an incomplete PCM sample."));
            return;
        }
        const QByteArray chunk = m_pending.left(size);
        m_pending.remove(0, size);
        m_socket->sendBinaryMessage(chunk);
    }
    if (m_finishing && m_pending.isEmpty()) {
        m_doneSent = true;
        m_pacer.stop();
        m_socket->sendTextMessage(QByteArrayLiteral("{\"type\":\"audio.done\"}"));
    }
}

void GrokStreamingTranscriber::handleMessage(const QString &message) {
    if (!m_active) return;
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(message.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        fail(tr("Grok streaming returned invalid JSON."));
        return;
    }
    const auto object = document.object();
    const auto type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("transcript.created")) {
        if (m_ready) return;
        m_ready = true;
        if (!m_finishing) m_deadline.stop();
        m_pacer.start();
    } else if (type == QStringLiteral("transcript.partial")) {
        // Chunk finals contain new text. Speech finals repeat the stitched utterance.
        // Group by the utterance start, not by text, so genuine repetitions survive.
        if (!object.value(QStringLiteral("is_final")).toBool()) return;
        const auto text = object.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) return;
        const auto start = object.value(QStringLiteral("start"));
        const auto duration = object.value(QStringLiteral("duration"));
        if (!start.isDouble() || !duration.isDouble()) {
            fail(tr("Grok streaming returned a finalized transcript without timing."));
            return;
        }
        auto &utterance = m_utterances[start.toDouble()];
        if (object.value(QStringLiteral("speech_final")).toBool()) {
            utterance.text = text;
            utterance.complete = true;
            utterance.chunks.clear();
        } else if (!utterance.complete) {
            utterance.chunks[duration.toDouble()] = text;
        }
    } else if (type == QStringLiteral("transcript.done")) {
        if (!m_doneSent || !object.value(QStringLiteral("text")).isString()) {
            fail(tr("Grok streaming returned an invalid final transcript."));
            return;
        }
        auto text = object.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) {
            // The live API can finish with an empty done event after emitting finals.
            QStringList parts;
            for (const auto &utterance : m_utterances) {
                if (utterance.complete) parts.append(utterance.text);
                else for (const auto &chunk : utterance.chunks) parts.append(chunk);
            }
            text = parts.join(QLatin1Char(' ')).trimmed();
        }
        if (text.isEmpty()) {
            fail(tr("Grok returned an empty transcript."));
            return;
        }
        cancel();
        emit transcriptionReady(text);
    } else if (type == QStringLiteral("error")) {
        fail(tr("Grok streaming server reported an error. Check the key and account limits."));
    }
}

void GrokStreamingTranscriber::fail(const QString &message) {
    if (!m_active) return;
    cancel();
    emit errorOccurred(message);
}

void GrokStreamingTranscriber::cancel() {
    m_active = false;
    m_pacer.stop();
    m_deadline.stop();
    if (m_socket) {
        m_socket->onOpen = nullptr;
        m_socket->onTextMessage = nullptr;
        m_socket->onError = nullptr;
        m_socket->onClosed = nullptr;
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_pending.clear();
    m_utterances.clear();
    m_ready = false;
    m_finishing = false;
    m_doneSent = false;
}
