#include "transcriber.h"
#include "config.h"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSslError>
#include <QSslSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QtEndian>
#include <algorithm>
#include <functional>
#include <limits>

Q_LOGGING_CATEGORY(lcTranscriber, "mywhisper.transcriber")

namespace {
constexpr auto AssemblyAiHost = "streaming.assemblyai.com";
constexpr quint16 AssemblyAiPort = 443;
constexpr auto AssemblyAiPath = "/v3/ws";
constexpr auto WebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QUrl deepgramUrlForConfig(const AppConfig &config) {
    QUrl url(QStringLiteral("https://api.deepgram.com/v1/listen"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("model"), QStringLiteral("nova-3"));
    query.addQueryItem(QStringLiteral("smart_format"), QStringLiteral("true"));
    for (const QString &keyword : config.deepgramKeywords) {
        const QString trimmed = keyword.trimmed();
        if (!trimmed.isEmpty()) {
            query.addQueryItem(QStringLiteral("keyterm"), trimmed);
        }
    }
    url.setQuery(query);
    return url;
}

QByteArray randomBytes(int count) {
    QByteArray bytes(count, Qt::Uninitialized);
    for (int i = 0; i < count; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->generate() & 0xff);
    }
    return bytes;
}

QString firstLine(const QByteArray &bytes) {
    const int newline = bytes.indexOf('\n');
    return QString::fromLatin1(newline >= 0 ? bytes.left(newline).trimmed() : bytes.trimmed());
}

QString jsonString(const QJsonObject &object, const char *key) {
    return object.value(QLatin1String(key)).toString().trimmed();
}

bool assemblyAiModelSupportsKeyterms(const QString &speechModel) {
    return !speechModel.trimmed().contains(QStringLiteral("whisper"), Qt::CaseInsensitive);
}

QString assemblyAiKeytermsPrompt(const QStringList &keyterms) {
    QJsonArray array;
    for (const QString &keyterm : keyterms) {
        const QString trimmed = keyterm.trimmed();
        if (!trimmed.isEmpty()) {
            array.append(trimmed);
        }
    }
    return array.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact));
}
}

class AssemblyAIWebSocket : public QObject {
public:
    explicit AssemblyAIWebSocket(QObject *parent = nullptr)
        : QObject(parent) {
    }

    ~AssemblyAIWebSocket() override {
        if (m_socket) {
            m_socket->abort();
        }
    }

    std::function<void()> onOpen;
    std::function<void(const QString &)> onTextMessage;
    std::function<void(const QString &)> onError;
    std::function<void()> onClosed;

    bool isOpen() const {
        return m_state == State::Open && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
    }

    void connectToAssemblyAI(const QString &apiKey, const QString &speechModel, int sampleRate, const QStringList &keytermsPrompt) {
        abort();

        m_state = State::Connecting;
        m_failed = false;
        m_closedCallbackEmitted = false;
        m_buffer.clear();
        m_fragmentBuffer.clear();
        m_fragmentOpcode = 0;
        m_apiKey = apiKey.trimmed();

        const QString normalizedSpeechModel = speechModel.trimmed().isEmpty() ? QStringLiteral("u3-rt-pro") : speechModel.trimmed();
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("speech_model"), normalizedSpeechModel);
        query.addQueryItem(QStringLiteral("sample_rate"), QString::number(sampleRate));
        if (assemblyAiModelSupportsKeyterms(normalizedSpeechModel)) {
            const QString keytermsJson = assemblyAiKeytermsPrompt(keytermsPrompt);
            if (!keytermsJson.isEmpty()) {
                query.addQueryItem(QStringLiteral("keyterms_prompt"), keytermsJson);
            }
        }
        m_requestTarget = QStringLiteral("%1?%2").arg(QString::fromLatin1(AssemblyAiPath), query.toString(QUrl::FullyEncoded));

        m_socket = new QSslSocket(this);
        connect(m_socket, &QSslSocket::encrypted, this, [this]() { sendHandshake(); });
        connect(m_socket, &QSslSocket::readyRead, this, [this]() { handleReadyRead(); });
        connect(m_socket, &QSslSocket::disconnected, this, [this]() {
            if (!m_failed) {
                m_state = State::Closed;
                emitClosed();
            }
        });
        connect(m_socket, &QSslSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
            fail(m_socket ? m_socket->errorString() : QStringLiteral("socket error"));
        });
        connect(m_socket, &QSslSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
            QStringList messages;
            for (const QSslError &error : errors) {
                messages.append(error.errorString());
            }
            fail(messages.isEmpty() ? QStringLiteral("TLS error") : messages.join(QStringLiteral("; ")));
        });

        m_socket->connectToHostEncrypted(QString::fromLatin1(AssemblyAiHost), AssemblyAiPort);
    }

    void sendBinaryMessage(const QByteArray &payload) {
        sendFrame(0x2, payload);
    }

    void sendTextMessage(const QByteArray &payload) {
        sendFrame(0x1, payload);
    }

    void closeGracefully() {
        if (!m_socket) {
            return;
        }

        if (isOpen()) {
            sendFrame(0x8, QByteArray());
        }
        m_state = State::Closing;
        m_socket->disconnectFromHost();
    }

    void abort() {
        if (!m_socket) {
            return;
        }

        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
        m_state = State::Closed;
    }

private:
    enum class State {
        Closed,
        Connecting,
        Handshaking,
        Open,
        Closing,
    };

    void sendHandshake() {
        if (!m_socket || m_state != State::Connecting) {
            return;
        }

        m_state = State::Handshaking;
        const QByteArray key = randomBytes(16).toBase64();
        m_expectedAccept = QCryptographicHash::hash(key + WebSocketGuid, QCryptographicHash::Sha1).toBase64();

        QByteArray request;
        request += "GET " + m_requestTarget.toUtf8() + " HTTP/1.1\r\n";
        request += "Host: " + QByteArray(AssemblyAiHost) + "\r\n";
        request += "Upgrade: websocket\r\n";
        request += "Connection: Upgrade\r\n";
        request += "Sec-WebSocket-Key: " + key + "\r\n";
        request += "Sec-WebSocket-Version: 13\r\n";
        request += "Authorization: " + m_apiKey.toUtf8() + "\r\n";
        request += "User-Agent: MyWhisperQt\r\n";
        request += "\r\n";
        m_socket->write(request);
    }

    void handleReadyRead() {
        if (!m_socket) {
            return;
        }

        m_buffer += m_socket->readAll();
        if (m_state == State::Handshaking) {
            if (!processHandshake()) {
                return;
            }
        }
        if (m_state == State::Open) {
            processFrames();
        }
    }

    bool processHandshake() {
        const int headerEnd = m_buffer.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            return false;
        }

        const QByteArray header = m_buffer.left(headerEnd + 4);
        m_buffer.remove(0, headerEnd + 4);

        const QList<QByteArray> lines = header.split('\n');
        const QByteArray statusLine = lines.isEmpty() ? QByteArray() : lines.first().trimmed();
        if (!statusLine.startsWith("HTTP/") || !statusLine.contains(" 101")) {
            fail(QStringLiteral("WebSocket handshake failed: %1").arg(firstLine(header)));
            return false;
        }

        QByteArray accept;
        for (const QByteArray &rawLine : lines) {
            const QByteArray line = rawLine.trimmed();
            const int separator = line.indexOf(':');
            if (separator <= 0) {
                continue;
            }
            const QByteArray name = line.left(separator).trimmed().toLower();
            const QByteArray value = line.mid(separator + 1).trimmed();
            if (name == "sec-websocket-accept") {
                accept = value;
                break;
            }
        }

        if (accept != m_expectedAccept) {
            fail(QStringLiteral("WebSocket handshake failed: invalid Sec-WebSocket-Accept header."));
            return false;
        }

        m_state = State::Open;
        if (onOpen) {
            onOpen();
        }
        return true;
    }

    void processFrames() {
        while (true) {
            if (m_buffer.size() < 2) {
                return;
            }

            const auto *bytes = reinterpret_cast<const uchar *>(m_buffer.constData());
            const quint8 first = bytes[0];
            const quint8 second = bytes[1];
            const bool fin = (first & 0x80) != 0;
            const quint8 opcode = first & 0x0f;
            const bool masked = (second & 0x80) != 0;
            quint64 length = second & 0x7f;
            int offset = 2;

            if (length == 126) {
                if (m_buffer.size() < offset + 2) {
                    return;
                }
                length = qFromBigEndian<quint16>(bytes + offset);
                offset += 2;
            } else if (length == 127) {
                if (m_buffer.size() < offset + 8) {
                    return;
                }
                length = qFromBigEndian<quint64>(bytes + offset);
                offset += 8;
            }

            QByteArray mask;
            if (masked) {
                if (m_buffer.size() < offset + 4) {
                    return;
                }
                mask = m_buffer.mid(offset, 4);
                offset += 4;
            }

            if (length > static_cast<quint64>(std::numeric_limits<int>::max())) {
                fail(QStringLiteral("WebSocket frame is too large."));
                return;
            }

            const quint64 frameSize = static_cast<quint64>(offset) + length;
            if (static_cast<quint64>(m_buffer.size()) < frameSize) {
                return;
            }

            QByteArray payload = m_buffer.mid(offset, static_cast<int>(length));
            m_buffer.remove(0, static_cast<int>(frameSize));

            if (masked) {
                for (int i = 0; i < payload.size(); ++i) {
                    payload[i] = static_cast<char>(payload.at(i) ^ mask.at(i % 4));
                }
            }

            handleFrame(opcode, fin, payload);
            if (m_state != State::Open) {
                return;
            }
        }
    }

    void handleFrame(quint8 opcode, bool fin, const QByteArray &payload) {
        switch (opcode) {
        case 0x0:
            handleContinuationFrame(fin, payload);
            return;
        case 0x1:
        case 0x2:
            handleDataFrame(opcode, fin, payload);
            return;
        case 0x8:
            m_state = State::Closing;
            if (m_socket) {
                m_socket->disconnectFromHost();
            }
            emitClosed();
            return;
        case 0x9:
            sendFrame(0xA, payload);
            return;
        case 0xA:
            return;
        default:
            fail(QStringLiteral("Unsupported WebSocket opcode %1.").arg(opcode));
            return;
        }
    }

    void handleDataFrame(quint8 opcode, bool fin, const QByteArray &payload) {
        if (fin) {
            deliverMessage(opcode, payload);
            return;
        }

        m_fragmentOpcode = opcode;
        m_fragmentBuffer = payload;
    }

    void handleContinuationFrame(bool fin, const QByteArray &payload) {
        if (m_fragmentOpcode == 0) {
            fail(QStringLiteral("Unexpected WebSocket continuation frame."));
            return;
        }

        m_fragmentBuffer += payload;
        if (!fin) {
            return;
        }

        const quint8 opcode = m_fragmentOpcode;
        const QByteArray completePayload = m_fragmentBuffer;
        m_fragmentOpcode = 0;
        m_fragmentBuffer.clear();
        deliverMessage(opcode, completePayload);
    }

    void deliverMessage(quint8 opcode, const QByteArray &payload) {
        if (opcode == 0x1 && onTextMessage) {
            onTextMessage(QString::fromUtf8(payload));
        }
    }

    void sendFrame(quint8 opcode, const QByteArray &payload) {
        if (!isOpen() || !m_socket) {
            return;
        }

        QByteArray frame;
        frame.append(static_cast<char>(0x80 | (opcode & 0x0f)));

        const quint64 length = static_cast<quint64>(payload.size());
        if (length <= 125) {
            frame.append(static_cast<char>(0x80 | static_cast<quint8>(length)));
        } else if (length <= 0xffff) {
            frame.append(static_cast<char>(0x80 | 126));
            uchar encoded[2];
            qToBigEndian(static_cast<quint16>(length), encoded);
            frame.append(reinterpret_cast<const char *>(encoded), 2);
        } else {
            frame.append(static_cast<char>(0x80 | 127));
            uchar encoded[8];
            qToBigEndian(length, encoded);
            frame.append(reinterpret_cast<const char *>(encoded), 8);
        }

        const QByteArray mask = randomBytes(4);
        frame.append(mask);

        QByteArray maskedPayload = payload;
        for (int i = 0; i < maskedPayload.size(); ++i) {
            maskedPayload[i] = static_cast<char>(maskedPayload.at(i) ^ mask.at(i % 4));
        }
        frame.append(maskedPayload);
        m_socket->write(frame);
    }

    void fail(const QString &message) {
        if (m_failed) {
            return;
        }

        m_failed = true;
        m_state = State::Closed;
        if (m_socket) {
            m_socket->abort();
        }
        if (onError) {
            onError(message);
        }
    }

    void emitClosed() {
        if (m_closedCallbackEmitted) {
            return;
        }
        m_closedCallbackEmitted = true;
        if (onClosed) {
            onClosed();
        }
    }

    QSslSocket *m_socket = nullptr;
    State m_state = State::Closed;
    QByteArray m_buffer;
    QByteArray m_expectedAccept;
    QByteArray m_fragmentBuffer;
    quint8 m_fragmentOpcode = 0;
    QString m_apiKey;
    QString m_requestTarget;
    bool m_failed = false;
    bool m_closedCallbackEmitted = false;
};

Transcriber::Transcriber(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this)) {
}

Transcriber::~Transcriber() {
    cancelStreaming();
}

void Transcriber::transcribe(const QString &audioFilePath, const AppConfig &config) {
    if (Config::usesAssemblyAi(config)) {
        emit errorOccurred(tr("AssemblyAI transcription uses streaming and must start when recording starts."));
        return;
    }

    qCDebug(lcTranscriber) << "Starting Deepgram transcription for" << audioFilePath;
    qCDebug(lcTranscriber) << "Deepgram keyword hints:" << config.deepgramKeywords;

    if (!Config::hasValidDeepgramKey(config)) {
        emit errorOccurred(tr("Invalid Deepgram API key. Set it in %1").arg(Config::configPath()));
        return;
    }

    QFile file(audioFilePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred(tr("Unable to read audio file: %1").arg(file.errorString()));
        return;
    }

    const QByteArray audioData = file.readAll();
    const QUrl requestUrl = deepgramUrlForConfig(config);

    qCDebug(lcTranscriber) << "Deepgram request URL:" << requestUrl.toString(QUrl::FullyEncoded);
    qCDebug(lcTranscriber) << "Audio file size bytes:" << audioData.size();
    qCDebug(lcTranscriber) << "Audio file name:" << QFileInfo(file).fileName();

    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("audio/wav"));
    request.setRawHeader("Authorization", QByteArray("Token ") + config.deepgramApiKey.toUtf8());

    QNetworkReply *reply = m_networkManager->post(request, audioData);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        const QByteArray body = reply->readAll();
        const QVariant statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        const QVariant reasonPhrase = reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute);

        qCDebug(lcTranscriber) << "Deepgram HTTP status:" << statusCode << reasonPhrase.toString();
        qCDebug(lcTranscriber).noquote() << "Deepgram raw response:" << QString::fromUtf8(body);

        if (reply->error() != QNetworkReply::NoError || statusCode.toInt() != 200) {
            QString message = QString::fromUtf8(body).trimmed();
            if (message.isEmpty()) {
                message = reply->errorString();
            }
            qCWarning(lcTranscriber) << "Deepgram request failed. Network error:" << reply->error()
                                      << "errorString:" << reply->errorString();
            qCWarning(lcTranscriber).noquote() << "Deepgram error body:" << message;
            emit errorOccurred(tr("Deepgram API error (%1 %2): %3")
                .arg(statusCode.toInt())
                .arg(reasonPhrase.toString(), message));
            reply->deleteLater();
            return;
        }

        const QJsonDocument document = QJsonDocument::fromJson(body);
        const QJsonObject object = document.object();
        const auto results = object.value("results").toObject();
        const auto channels = results.value("channels").toArray();
        const auto alternatives = channels.isEmpty() ? QJsonArray() : channels.first().toObject().value("alternatives").toArray();
        const QString transcript = alternatives.isEmpty() ? QString() : alternatives.first().toObject().value("transcript").toString();

        qCDebug(lcTranscriber) << "Deepgram transcript length:" << transcript.size();
        qCDebug(lcTranscriber).noquote() << "Deepgram transcript:" << transcript;

        emit transcriptionReady(transcript);
        reply->deleteLater();
    });
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

    m_streamingSocket = new AssemblyAIWebSocket(this);
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

    AssemblyAIWebSocket *socket = m_streamingSocket;
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
