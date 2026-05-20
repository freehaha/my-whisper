#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

class QSslSocket;

class AssemblyAiWebSocketClient : public QObject {
public:
    explicit AssemblyAiWebSocketClient(QObject *parent = nullptr);
    ~AssemblyAiWebSocketClient() override;

    std::function<void()> onOpen;
    std::function<void(const QString &)> onTextMessage;
    std::function<void(const QString &)> onError;
    std::function<void()> onClosed;

    bool isOpen() const;
    void connectToAssemblyAI(const QString &apiKey, const QString &speechModel, int sampleRate, const QStringList &keytermsPrompt);
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
    QString m_apiKey;
    QString m_requestTarget;
    bool m_failed = false;
    bool m_closedCallbackEmitted = false;
};
