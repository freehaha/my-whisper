#include "config.h"
#include "grok_streaming_transcriber.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <QtEndian>
#include <iostream>

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc < 2 || argc > 3) {
        std::cerr << "Usage: GrokStreamingProbe sample.wav [expected-transcript]\n"
                     "Uploads audio to xAI using grokApiKey from config or XAI_API_KEY.\n";
        return 2;
    }
    QFile file(app.arguments()[1]);
    if (!file.open(QIODevice::ReadOnly)) return 2;
    const auto wav = file.readAll();
    if (wav.size() < 12 || wav.left(4) != "RIFF" || wav.mid(8, 4) != "WAVE") return 2;
    auto u16 = [&](qsizetype offset) { return qFromLittleEndian<quint16>(reinterpret_cast<const uchar *>(wav.constData() + offset)); };
    auto u32 = [&](qsizetype offset) { return qFromLittleEndian<quint32>(reinterpret_cast<const uchar *>(wav.constData() + offset)); };
    QAudioFormat format;
    QByteArray pcm;
    bool validFormat = false;
    for (qsizetype offset = 12; offset + 8 <= wav.size();) {
        const auto size = u32(offset + 4);
        if (size > static_cast<quint64>(wav.size() - offset - 8)) return 2;
        const auto name = wav.mid(offset, 4);
        if (name == "fmt " && size >= 16) {
            validFormat = u16(offset + 8) == 1 && u16(offset + 22) == 16;
            format.setChannelCount(u16(offset + 10));
            format.setSampleRate(u32(offset + 12));
            format.setSampleFormat(QAudioFormat::Int16);
        } else if (name == "data") pcm = wav.mid(offset + 8, size);
        offset += 8 + static_cast<qint64>(size) + (size % 2);
    }
    if (!validFormat || pcm.isEmpty()) {
        std::cerr << "The probe requires a nonempty PCM16 WAV file.\n";
        return 2;
    }
    const auto config = Config::load();
    GrokStreamingTranscriber transcriber;
    QTimer feeder, deadline;
    feeder.setInterval(100);
    feeder.setTimerType(Qt::PreciseTimer);
    deadline.setSingleShot(true);
    QElapsedTimer elapsed;
    qsizetype offset = 0;
    qint64 finishedAt = 0;
    int result = 1;
    QObject::connect(&transcriber, &GrokStreamingTranscriber::transcriptionReady, &app, [&](const QString &text) {
        std::cout << "Transcript: " << text.toStdString() << '\n';
        std::cout << "Result after stop: " << elapsed.elapsed() - finishedAt << " ms\n";
        const bool matches = argc != 3 || text == app.arguments()[2];
        if (argc == 3) std::cout << "Exact reference match: " << (matches ? "PASS" : "FAIL") << '\n';
        result = matches ? 0 : 1;
        app.quit();
    });
    QObject::connect(&transcriber, &GrokStreamingTranscriber::errorOccurred, &app, [&](const QString &error) {
        std::cerr << error.toStdString() << '\n'; app.quit();
    });
    QObject::connect(&feeder, &QTimer::timeout, &app, [&] {
        const int chunkBytes = format.sampleRate() / 10 * 2 * format.channelCount();
        transcriber.streamAudio(pcm.mid(offset, chunkBytes));
        offset += qMin(static_cast<qsizetype>(chunkBytes), pcm.size() - offset);
        if (offset >= pcm.size()) {
            feeder.stop(); finishedAt = elapsed.elapsed(); transcriber.finish();
        }
    });
    QObject::connect(&deadline, &QTimer::timeout, &app, [&] {
        std::cerr << "Probe timed out.\n"; app.quit();
    });
    QTimer::singleShot(0, &app, [&] {
        elapsed.start();
        transcriber.start(config, format);
        if (transcriber.isStreaming()) { feeder.start(); deadline.start(120000); }
    });
    app.exec();
    return result;
}
