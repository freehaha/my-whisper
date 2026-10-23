#include "grok_streaming_transcriber.h"
#include "config.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>
#include <iostream>
#include <cstdlib>

static void check(bool value, const char *message) {
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static QByteArray frame(const QByteArray &text) {
    QByteArray out(1, char(0x81));
    if (text.size() < 126) out += char(text.size());
    else {
        out += char(126);
        char size[2]; qToBigEndian<quint16>(text.size(), reinterpret_cast<uchar *>(size));
        out.append(size, 2);
    }
    return out + text;
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    AppConfig config;
    config.grokApiKey = "stream-test-key";
    config.transcriptionBackend = "grok_streaming";
    check(Config::usesGrok(config), "streaming key validation uses Grok");
    QAudioFormat format;
    format.setSampleRate(16000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    for (const QString mode : {"success", "early-stop", "pacing", "finalized-empty", "chunk-only", "repeated-words", "interim-only", "error", "disconnect", "invalid", "empty", "cancel"}) {
        QTcpServer server;
        check(server.listen(QHostAddress::LocalHost), "mock listen");
        GrokStreamingTranscriber transcriber(nullptr, QUrl(QString("ws://127.0.0.1:%1/v1/stt").arg(server.serverPort())));
        QEventLoop loop;
        QByteArray incoming, request, audio;
        bool upgraded = false, ready = false;
        int doneCount = 0, successes = 0, errors = 0;
        QString transcript;
        QTcpSocket *socket = nullptr;
        QTimer feeder;
        feeder.setInterval(100);
        feeder.setTimerType(Qt::PreciseTimer);
        QElapsedTimer clock;
        clock.start();
        qint64 readyAt = 0, stoppedAt = -1;
        int capturedChunks = 0;
        QObject::connect(&feeder, &QTimer::timeout, &transcriber, [&] {
            ++capturedChunks;
            transcriber.streamAudio(QByteArray(3200, 'a'));
            if (capturedChunks == 20) {
                feeder.stop();
                stoppedAt = clock.elapsed();
                transcriber.finish();
            }
        });
        QTimer deadline;
        deadline.setSingleShot(true);
        QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&transcriber, &GrokStreamingTranscriber::transcriptionReady, &loop, [&](const QString &text) {
            ++successes; transcript = text; loop.quit();
        });
        QObject::connect(&transcriber, &GrokStreamingTranscriber::errorOccurred, &loop, [&](const QString &) {
            ++errors; loop.quit();
        });
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, &server, [&] {
                incoming += socket->readAll();
                if (!upgraded) {
                    const auto end = incoming.indexOf("\r\n\r\n");
                    if (end < 0) return;
                    request = incoming.left(end + 4);
                    incoming.remove(0, end + 4);
                    check(request.startsWith("GET /v1/stt?"), "stream endpoint");
                    check(request.contains("model=grok-voice-transcribe-2.0"), "stream model");
                    check(request.contains("encoding=pcm") && request.contains("sample_rate=16000"), "PCM configuration");
                    check(request.contains("Authorization: Bearer stream-test-key"), "stream Bearer key");
                    QByteArray key;
                    for (const auto &line : request.split('\n')) {
                        if (line.toLower().startsWith("sec-websocket-key:")) key = line.mid(18).trimmed();
                    }
                    const auto accept = QCryptographicHash::hash(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", QCryptographicHash::Sha1).toBase64();
                    socket->write("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept + "\r\n\r\n");
                    upgraded = true;
                    // Delay readiness to test queued audio and release before readiness.
                    QTimer::singleShot(mode == "pacing" ? 450 : 150, &server, [&] {
                        if (mode == "cancel") {
                            transcriber.cancel();
                            QTimer::singleShot(100, &loop, &QEventLoop::quit);
                            return;
                        }
                        ready = true;
                        readyAt = clock.elapsed();
                        socket->write(frame(R"({"type":"transcript.created"})"));
                        if (mode == "error") socket->write(frame(R"({"type":"error","message":"test"})"));
                        if (mode == "invalid") socket->write(frame("not json"));
                        if (mode == "disconnect") socket->disconnectFromHost();
                    });
                }
                while (incoming.size() >= 2) {
                    const auto *bytes = reinterpret_cast<const uchar *>(incoming.constData());
                    const auto opcode = bytes[0] & 15;
                    check(bytes[1] & 128, "client frames must be masked");
                    quint64 length = bytes[1] & 127;
                    int offset = 2;
                    if (length == 126) {
                        if (incoming.size() < 4) return;
                        length = qFromBigEndian<quint16>(bytes + 2); offset = 4;
                    }
                    check(length < 65536, "bounded test frame");
                    if (incoming.size() < offset + 4 + static_cast<qint64>(length)) return;
                    const auto mask = incoming.mid(offset, 4); offset += 4;
                    auto payload = incoming.mid(offset, length);
                    incoming.remove(0, offset + length);
                    for (qsizetype i = 0; i < payload.size(); ++i) payload[i] = payload[i] ^ mask[i % 4];
                    if (opcode == 2) {
                        check(ready, "no audio before transcript.created");
                        check(payload.size() <= 3200, "100 ms chunks");
                        audio += payload;
                        if (mode == "pacing") {
                            check(audio.size() <= capturedChunks * 3200, "never send uncaptured audio");
                            // Allow scheduling/receive jitter, but reject an unbounded startup burst.
                            const qint64 ticks = (clock.elapsed() - readyAt) / 45 + 1;
                            check(audio.size() <= ticks * 3200, "catch-up rate stays bounded near 2x");
                        }
                    } else if (opcode == 1) {
                        check(QJsonDocument::fromJson(payload).object().value("type") == "audio.done", "audio.done control message");
                        ++doneCount;
                        check(audio == QByteArray(mode == "pacing" ? 64000 : 6800, 'a'), "all PCM bytes precede audio.done");
                        if (mode == "pacing") {
                            check(stoppedAt >= 0, "completion follows capture stop");
                            check(clock.elapsed() - stoppedAt < 175, "startup backlog drains during capture, not after stop");
                        }
                        if (mode == "finalized-empty") {
                            // Replay the actual service pattern: chunks, stitched finals, empty done.
                            for (const auto &event : {
                                R"({"type":"transcript.partial","text":"And this is a recording for testing.","is_final":true,"start":1.54,"duration":3.9})",
                                R"({"type":"transcript.partial","text":"And this is a recording for testing.","is_final":true,"speech_final":true,"start":1.54,"duration":3.9})",
                                R"({"type":"transcript.partial","text":"I'm wondering whether","is_final":true,"start":5.44,"duration":2.88})",
                                R"({"type":"transcript.partial","text":"the transcription service can do its job.","is_final":true,"start":5.44,"duration":5.76})",
                                R"({"type":"transcript.partial","text":"I'm wondering whether the transcription service can do its job.","is_final":true,"speech_final":true,"start":5.44,"duration":6.36})",
                                R"({"type":"transcript.partial","text":"I'm wondering whether the transcription service can do its job.","is_final":true,"speech_final":true,"start":5.44,"duration":6.36})"}) socket->write(frame(event));
                        } else if (mode == "chunk-only") {
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello","is_final":true,"start":0,"duration":1})"));
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello","is_final":true,"start":0,"duration":1})"));
                            socket->write(frame(R"({"type":"transcript.partial","text":"world.","is_final":true,"start":0,"duration":2})"));
                        } else if (mode == "repeated-words") {
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello.","is_final":true,"speech_final":true,"start":0,"duration":1})"));
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello.","is_final":true,"speech_final":true,"start":2,"duration":1})"));
                        } else if (mode == "interim-only") {
                            socket->write(frame(R"({"type":"transcript.partial","text":"unconfirmed words","is_final":false,"start":0,"duration":1})"));
                        } else if (mode != "empty") {
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello","is_final":true,"start":0,"duration":1})"));
                            socket->write(frame(R"({"type":"transcript.partial","text":"Hello world.","is_final":true,"speech_final":true,"start":0,"duration":2})"));
                        }
                        const bool emptyDone = mode == "empty" || mode == "finalized-empty" || mode == "chunk-only" || mode == "repeated-words" || mode == "interim-only";
                        socket->write(frame(emptyDone ? R"({"type":"transcript.done","text":""})" : R"({"type":"transcript.done","text":"Hello world."})"));
                    }
                }
            });
        });
        deadline.start(4000);
        transcriber.start(config, format);
        check(transcriber.isStreaming(), "active during connection");
        if (mode == "pacing") feeder.start();
        else {
            transcriber.streamAudio(QByteArray(6800, 'a'));
            if (mode == "early-stop") { transcriber.finish(); transcriber.finish(); }
            else QTimer::singleShot(250, &transcriber, &GrokStreamingTranscriber::finish);
        }
        loop.exec();
        check(deadline.isActive(), "stream finished before test timeout");
        if (mode == "success" || mode == "early-stop" || mode == "pacing" || mode == "finalized-empty" || mode == "chunk-only" || mode == "repeated-words") {
            const QString expected = mode == "finalized-empty"
                ? QStringLiteral("And this is a recording for testing. I'm wondering whether the transcription service can do its job.")
                : (mode == "repeated-words" ? QStringLiteral("Hello. Hello.") : QStringLiteral("Hello world."));
            check(successes == 1 && errors == 0 && transcript == expected, "exactly one complete transcript");
            check(doneCount == 1, "exactly one audio.done");
        } else if (mode == "cancel") check(successes == 0 && errors == 0, "silent cancellation");
        else check(errors == 1 && successes == 0, "exactly one stream error");
        check(!transcriber.isStreaming(), "session cleaned up");
        std::cout << "PASS: " << mode.toStdString() << '\n';
    }
    GrokStreamingTranscriber transcriber(nullptr, QUrl(QStringLiteral("ws://127.0.0.1:1/v1/stt")));
    int errors = 0;
    QObject::connect(&transcriber, &GrokStreamingTranscriber::errorOccurred, &app, [&](const QString &) { ++errors; });
    format.setSampleFormat(QAudioFormat::Float);
    transcriber.start(config, format);
    check(errors == 1 && !transcriber.isStreaming(), "unsupported microphone format");
    format.setSampleFormat(QAudioFormat::Int16);
    transcriber.start(config, format);
    transcriber.streamAudio(QByteArray(960001, 'a'));
    check(errors == 2 && !transcriber.isStreaming(), "bounded startup queue");
    std::cout << "PASS: format validation and queue limit\n";
}
