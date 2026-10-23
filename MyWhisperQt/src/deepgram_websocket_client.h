#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>

class QSslSocket;

class DeepgramWebSocketClient : public QObject {
public:
    explicit DeepgramWebSocketClient(QObject *parent = nullptr);
    ~DeepgramWebSocketClient() override;

    std::function<void()> onOpen;
    std::function<void(const QString &)> onTextMessage;
    std::function<void(const QString &)> onError;
    std::function<void()> onClosed;

    bool isOpen() const;
    qint64 pendingBytes() const;
    void connectToDeepgram(const QString &apiKey, int sampleRate, const QStringList &keyterms);
    // Shared RFC 6455 transport. The caller supplies the provider URL and authorization.
    void connectToUrl(const QUrl &url, const QByteArray &authorization);
    void sendBinaryMessage(const QByteArray &payload);
    void sendTextMessage(const QByteArray &payload);
    void closeGracefully();
    void abort();

private:
    enum class State {
        Closed,
        Connecting,
        Handshaking,
        Open,
        Closing,
    };

    void sendHandshake();
    void handleReadyRead();
    bool processHandshake();
    void processFrames();
    void handleFrame(quint8 opcode, bool fin, const QByteArray &payload);
    void handleDataFrame(quint8 opcode, bool fin, const QByteArray &payload);
    void handleContinuationFrame(bool fin, const QByteArray &payload);
    void deliverMessage(quint8 opcode, const QByteArray &payload);
    void sendFrame(quint8 opcode, const QByteArray &payload);
    void fail(const QString &message);
    void emitClosed();

    QSslSocket *m_socket = nullptr;
    State m_state = State::Closed;
    QByteArray m_buffer;
    QByteArray m_expectedAccept;
    QByteArray m_fragmentBuffer;
    quint8 m_fragmentOpcode = 0;
    QByteArray m_authorization;
    QString m_host;
    quint16 m_port = 443;
    bool m_secure = true;
    QString m_requestTarget;
    bool m_failed = false;
    bool m_closedCallbackEmitted = false;
};
