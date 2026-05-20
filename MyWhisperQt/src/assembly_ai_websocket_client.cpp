#include "assembly_ai_websocket_client.h"

#include <QAbstractSocket>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QSslError>
#include <QSslSocket>
#include <QUrlQuery>
#include <QtEndian>
#include <limits>

namespace {
constexpr auto AssemblyAiHost = "streaming.assemblyai.com";
constexpr quint16 AssemblyAiPort = 443;
constexpr auto AssemblyAiPath = "/v3/ws";
constexpr auto WebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

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

AssemblyAiWebSocketClient::AssemblyAiWebSocketClient(QObject *parent)
    : QObject(parent) {
}

AssemblyAiWebSocketClient::~AssemblyAiWebSocketClient() {
    if (m_socket) {
        m_socket->abort();
    }
}

bool AssemblyAiWebSocketClient::isOpen() const {
    return m_state == State::Open && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

void AssemblyAiWebSocketClient::connectToAssemblyAI(const QString &apiKey, const QString &speechModel, int sampleRate, const QStringList &keytermsPrompt) {
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

void AssemblyAiWebSocketClient::sendBinaryMessage(const QByteArray &payload) {
    sendFrame(0x2, payload);
}

void AssemblyAiWebSocketClient::sendTextMessage(const QByteArray &payload) {
    sendFrame(0x1, payload);
}

void AssemblyAiWebSocketClient::closeGracefully() {
    if (!m_socket) {
        return;
    }

    if (isOpen()) {
        sendFrame(0x8, QByteArray());
    }
    m_state = State::Closing;
    m_socket->disconnectFromHost();
}

void AssemblyAiWebSocketClient::abort() {
    if (!m_socket) {
        return;
    }

    m_socket->abort();
    m_socket->deleteLater();
    m_socket = nullptr;
    m_state = State::Closed;
}

void AssemblyAiWebSocketClient::sendHandshake() {
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

void AssemblyAiWebSocketClient::handleReadyRead() {
    if (!m_socket) {
        return;
    }

    m_buffer += m_socket->readAll();
    if (m_state == State::Handshaking && !processHandshake()) {
        return;
    }
    if (m_state == State::Open) {
        processFrames();
    }
}

bool AssemblyAiWebSocketClient::processHandshake() {
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

void AssemblyAiWebSocketClient::processFrames() {
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

void AssemblyAiWebSocketClient::handleFrame(quint8 opcode, bool fin, const QByteArray &payload) {
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

void AssemblyAiWebSocketClient::handleDataFrame(quint8 opcode, bool fin, const QByteArray &payload) {
    if (fin) {
        deliverMessage(opcode, payload);
        return;
    }

    m_fragmentOpcode = opcode;
    m_fragmentBuffer = payload;
}

void AssemblyAiWebSocketClient::handleContinuationFrame(bool fin, const QByteArray &payload) {
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

void AssemblyAiWebSocketClient::deliverMessage(quint8 opcode, const QByteArray &payload) {
    if (opcode == 0x1 && onTextMessage) {
        onTextMessage(QString::fromUtf8(payload));
    }
}

void AssemblyAiWebSocketClient::sendFrame(quint8 opcode, const QByteArray &payload) {
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

void AssemblyAiWebSocketClient::fail(const QString &message) {
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

void AssemblyAiWebSocketClient::emitClosed() {
    if (m_closedCallbackEmitted) {
        return;
    }
    m_closedCallbackEmitted = true;
    if (onClosed) {
        onClosed();
    }
}
