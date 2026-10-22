#include "config.h"
#include "grok_transcriber.h"
#include "transcriber.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <iostream>
#include <cstdlib>

static void check(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    // A real request is opt-in. No recording leaves the computer in default tests.
    if (app.arguments().size() == 3 && app.arguments()[1] == "--live") {
        AppConfig config;
        GrokTranscriber transcriber;
        int result = 1;
        QObject::connect(&transcriber, &GrokTranscriber::transcriptionReady, &app, [&](const QString &text) {
            std::cout << text.toStdString() << '\n'; result = 0; app.quit();
        });
        QObject::connect(&transcriber, &GrokTranscriber::errorOccurred, &app, [&](const QString &message) {
            std::cerr << message.toStdString() << '\n'; app.quit();
        });
        QTimer::singleShot(0, &app, [&] { transcriber.transcribe(app.arguments()[2], config); });
        app.exec();
        return result;
    }

    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary directory");
    qputenv("HOME", temporary.path().toUtf8()); // Never alter the user's settings.
    qunsetenv("XAI_API_KEY");
    AppConfig config = Config::defaultConfig();
    config.transcriptionBackend = "grok";
    check(Config::usesGrok(config), "Grok backend selection");
    check(!Config::hasValidGrokKey(config), "missing key");
    qputenv("XAI_API_KEY", "env-key");
    check(Config::grokApiKey(config) == "env-key", "environment key fallback");
    config.grokApiKey = " test-key ";
    check(Config::grokApiKey(config) == "test-key", "configured key precedence");
    check(Config::save(config), "save config");
    const auto loaded = Config::load();
    check(Config::usesGrok(loaded) && loaded.grokApiKey == config.grokApiKey, "config round trip");
    Transcriber router;
    router.startStreaming(config, QAudioFormat());
    check(!router.isStreaming(), "Grok must not start Deepgram streaming");

    QFile audio(temporary.filePath("sample.wav"));
    check(audio.open(QIODevice::WriteOnly), "create fixture");
    const QByteArray audioBytes("RIFF-test-audio-data");
    audio.write(audioBytes);
    audio.close();

    int cases = 0;
    auto run = [&](int status, const QByteArray &body, bool success, const QString &expected) {
        QTcpServer server;
        check(server.listen(QHostAddress::LocalHost), "mock listen");
        QByteArray request;
        bool responded = false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::readyRead, &server, [&, socket] {
                request += socket->readAll();
                const int split = request.indexOf("\r\n\r\n");
                if (split < 0 || responded) return;
                qint64 length = -1;
                for (const auto &line : request.left(split).split('\n')) {
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                }
                if (length < 0 || request.size() - split - 4 < length) return;
                responded = true;
                socket->write("HTTP/1.1 " + QByteArray::number(status) + " Test\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        GrokTranscriber transcriber(nullptr, QUrl(QString("http://127.0.0.1:%1/v1/stt").arg(server.serverPort())));
        QEventLoop loop;
        QString output, error;
        QObject::connect(&transcriber, &GrokTranscriber::transcriptionReady, &loop, [&](const QString &text) { output = text; loop.quit(); });
        QObject::connect(&transcriber, &GrokTranscriber::errorOccurred, &loop, [&](const QString &message) { error = message; loop.quit(); });
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
        timer.start(5000);
        transcriber.transcribe(audio.fileName(), config);
        loop.exec();
        check(timer.isActive(), "request completed before test timeout");
        check(responded, "mock received complete upload");
        check(request.startsWith("POST /v1/stt HTTP/1.1"), "endpoint and method");
        check(request.contains("Authorization: Bearer test-key"), "Bearer authorization");
        check(request.contains("multipart/form-data; boundary="), "multipart content type");
        check(request.contains("grok-voice-transcribe-2.0"), "model");
        check(request.indexOf("name=\"model\"") < request.indexOf("name=\"file\""), "file is last field");
        check(request.contains("audio/wav") && request.contains(audioBytes), "WAV upload");
        check(success ? (error.isEmpty() && output == expected) : (output.isEmpty() && error.contains(expected)), "response outcome");
        ++cases;
    };
    run(200, R"({"text":" Hello world. "})", true, "Hello world.");
    run(200, R"({"text":"你好，世界。"})", true, QString::fromUtf8("你好，世界。"));
    run(401, R"({"error":"unauthorized"})", false, "401");
    run(429, R"({"error":"rate limited"})", false, "429");
    run(500, "internal error", false, "500");
    run(200, "not json", false, "invalid");
    run(200, R"({"words":[]})", false, "invalid");
    run(200, R"({"text":123})", false, "invalid");
    run(200, R"({"text":"  "})", false, "empty");

    GrokTranscriber transcriber;
    int errors = 0;
    QObject::connect(&transcriber, &GrokTranscriber::errorOccurred, &app, [&](const QString &) { ++errors; });
    transcriber.transcribe(temporary.filePath("missing.wav"), config);
    config.grokApiKey.clear();
    qunsetenv("XAI_API_KEY");
    transcriber.transcribe(audio.fileName(), config);
    check(errors == 2, "missing file and missing key fail before request");
    std::cout << "PASS: " << cases << " HTTP cases, configuration, routing, and local validation\n";
}
