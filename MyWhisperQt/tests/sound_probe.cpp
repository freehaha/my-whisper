#include "platform_integration.h"

#include <QAudio>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QAudioOutput>
#include <QAudioSink>
#include <QApplication>
#include <QBuffer>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QSoundEffect>
#include <QTimer>
#include <QUrl>

#include <cmath>
#include <cstdio>

namespace {
void logLine(const QString &message) {
    std::fprintf(stderr, "%s\n", message.toUtf8().constData());
    std::fflush(stderr);
}

QString yesNo(bool value) {
    return value ? QStringLiteral("yes") : QStringLiteral("no");
}

QString hexId(const QByteArray &value) {
    return QString::fromLatin1(value.toHex());
}

QString soundStatusName(QSoundEffect::Status status) {
    switch (status) {
    case QSoundEffect::Null:
        return QStringLiteral("Null");
    case QSoundEffect::Loading:
        return QStringLiteral("Loading");
    case QSoundEffect::Ready:
        return QStringLiteral("Ready");
    case QSoundEffect::Error:
        return QStringLiteral("Error");
    }

    return QStringLiteral("Unknown");
}

QString playerStateName(QMediaPlayer::PlaybackState state) {
    switch (state) {
    case QMediaPlayer::StoppedState:
        return QStringLiteral("StoppedState");
    case QMediaPlayer::PlayingState:
        return QStringLiteral("PlayingState");
    case QMediaPlayer::PausedState:
        return QStringLiteral("PausedState");
    }

    return QStringLiteral("Unknown");
}

QString mediaStatusName(QMediaPlayer::MediaStatus status) {
    switch (status) {
    case QMediaPlayer::NoMedia:
        return QStringLiteral("NoMedia");
    case QMediaPlayer::LoadingMedia:
        return QStringLiteral("LoadingMedia");
    case QMediaPlayer::LoadedMedia:
        return QStringLiteral("LoadedMedia");
    case QMediaPlayer::StalledMedia:
        return QStringLiteral("StalledMedia");
    case QMediaPlayer::BufferingMedia:
        return QStringLiteral("BufferingMedia");
    case QMediaPlayer::BufferedMedia:
        return QStringLiteral("BufferedMedia");
    case QMediaPlayer::EndOfMedia:
        return QStringLiteral("EndOfMedia");
    case QMediaPlayer::InvalidMedia:
        return QStringLiteral("InvalidMedia");
    }

    return QStringLiteral("Unknown");
}

QString audioStateName(QAudio::State state) {
    switch (state) {
    case QAudio::StoppedState:
        return QStringLiteral("StoppedState");
    case QAudio::ActiveState:
        return QStringLiteral("ActiveState");
    case QAudio::SuspendedState:
        return QStringLiteral("SuspendedState");
    case QAudio::IdleState:
        return QStringLiteral("IdleState");
    }

    return QStringLiteral("Unknown");
}

QString audioErrorName(QAudio::Error error) {
    switch (error) {
    case QAudio::NoError:
        return QStringLiteral("NoError");
    case QAudio::OpenError:
        return QStringLiteral("OpenError");
    case QAudio::IOError:
        return QStringLiteral("IOError");
    case QAudio::UnderrunError:
        return QStringLiteral("UnderrunError");
    case QAudio::FatalError:
        return QStringLiteral("FatalError");
    }

    return QStringLiteral("Unknown");
}

QString describeDevice(const QAudioDevice &device) {
    return QStringLiteral("desc='%1' id=%2 default=%3 mode=%4")
        .arg(device.description())
        .arg(hexId(device.id()))
        .arg(yesNo(device == QMediaDevices::defaultAudioOutput()))
        .arg(device.mode() == QAudioDevice::Output ? QStringLiteral("output") : QStringLiteral("input"));
}

void listAudioOutputs() {
    const auto devices = QMediaDevices::audioOutputs();
    logLine(QStringLiteral("audio outputs: %1").arg(devices.size()));
    for (int i = 0; i < devices.size(); ++i) {
        logLine(QStringLiteral("  [%1] %2").arg(i).arg(describeDevice(devices[i])));
    }
}

QAudioDevice chooseOutputDevice(const QString &selector) {
    const auto devices = QMediaDevices::audioOutputs();
    if (selector.trimmed().isEmpty()) {
        return QMediaDevices::defaultAudioOutput();
    }

    const QString needle = selector.trimmed().toLower();
    for (const QAudioDevice &device : devices) {
        if (device.description().toLower().contains(needle) || hexId(device.id()).contains(needle)) {
            return device;
        }
    }

    return QAudioDevice();
}

QUrl resolveSource(const QString &filePath) {
    if (!filePath.trimmed().isEmpty()) {
        return QUrl::fromLocalFile(filePath);
    }
    return QUrl(QStringLiteral("qrc:/sounds/ding.wav"));
}

void playNamedPlatformSound(const QString &soundName) {
    if (soundName == QStringLiteral("start")) {
        PlatformIntegration::playStartSound();
        return;
    }
    if (soundName == QStringLiteral("stop")) {
        PlatformIntegration::playStopSound();
        return;
    }
    if (soundName == QStringLiteral("success")) {
        PlatformIntegration::playSuccessSound();
        return;
    }

    PlatformIntegration::playErrorSound();
}

void appendSample(QByteArray &pcm, QAudioFormat::SampleFormat format, double value) {
    const double clamped = std::clamp(value, -1.0, 1.0);

    switch (format) {
    case QAudioFormat::UInt8: {
        const quint8 sample = static_cast<quint8>((clamped + 1.0) * 127.5);
        pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
        break;
    }
    case QAudioFormat::Int16: {
        const qint16 sample = static_cast<qint16>(clamped * 32767.0);
        pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
        break;
    }
    case QAudioFormat::Int32: {
        const qint32 sample = static_cast<qint32>(clamped * 2147483647.0);
        pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
        break;
    }
    case QAudioFormat::Float: {
        const float sample = static_cast<float>(clamped);
        pcm.append(reinterpret_cast<const char *>(&sample), sizeof(sample));
        break;
    }
    case QAudioFormat::Unknown:
        break;
    }
}

QByteArray generateSinePcm(const QAudioFormat &format, int durationMs, double hz) {
    if (format.sampleRate() <= 0 || format.channelCount() <= 0 || format.sampleFormat() == QAudioFormat::Unknown) {
        return {};
    }

    const int frameCount = static_cast<int>((static_cast<qint64>(format.sampleRate()) * durationMs) / 1000);
    const int bytesPerSample = format.bytesPerSample();
    if (frameCount <= 0 || bytesPerSample <= 0) {
        return {};
    }

    QByteArray pcm;
    pcm.reserve(frameCount * format.channelCount() * bytesPerSample);

    constexpr double amplitude = 0.20;
    constexpr double twoPi = 6.28318530717958647692;

    for (int frame = 0; frame < frameCount; ++frame) {
        const double t = static_cast<double>(frame) / static_cast<double>(format.sampleRate());
        const double sample = std::sin(twoPi * hz * t) * amplitude;
        for (int channel = 0; channel < format.channelCount(); ++channel) {
            appendSample(pcm, format.sampleFormat(), sample);
        }
    }

    return pcm;
}
}

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption engineOption(
        QStringList() << "engine",
        "Engine: platform, soundeffect, mediaplayer, tone.",
        "name",
        "soundeffect"
    );
    QCommandLineOption soundOption(
        QStringList() << "sound",
        "Platform engine only. One of: start, stop, success, error.",
        "name",
        "start"
    );
    QCommandLineOption fileOption(
        QStringList() << "file",
        "Filesystem media path. Default uses qrc:/sounds/ding.wav.",
        "path"
    );
    QCommandLineOption deviceOption(
        QStringList() << "device",
        "Audio output selector substring. Matches description or hex id.",
        "text"
    );
    QCommandLineOption durationOption(
        QStringList() << "duration-ms",
        "Playback duration in ms.",
        "ms",
        "10000"
    );
    QCommandLineOption holdOption(
        QStringList() << "hold-ms",
        "Extra wait after stop, useful for wpctl/pactl inspection.",
        "ms",
        "1500"
    );
    QCommandLineOption intervalOption(
        QStringList() << "interval-ms",
        "Platform engine replay interval in ms.",
        "ms",
        "900"
    );
    QCommandLineOption volumeOption(
        QStringList() << "volume",
        "Volume 0.0 - 1.0.",
        "value",
        "0.8"
    );
    QCommandLineOption toneHzOption(
        QStringList() << "tone-hz",
        "Tone frequency for tone engine.",
        "hz",
        "880"
    );
    QCommandLineOption listDevicesOption(
        QStringList() << "list-devices",
        "List audio outputs and exit."
    );
    QCommandLineOption directOption(
        QStringList() << "direct",
        "Compatibility alias for --engine soundeffect."
    );

    parser.addOption(engineOption);
    parser.addOption(soundOption);
    parser.addOption(fileOption);
    parser.addOption(deviceOption);
    parser.addOption(durationOption);
    parser.addOption(holdOption);
    parser.addOption(intervalOption);
    parser.addOption(volumeOption);
    parser.addOption(toneHzOption);
    parser.addOption(listDevicesOption);
    parser.addOption(directOption);
    parser.process(app);

    QString engine = parser.value(engineOption).trimmed().toLower();
    if (parser.isSet(directOption)) {
        engine = QStringLiteral("soundeffect");
    }

    QCoreApplication::setApplicationName(QStringLiteral("SoundProbe-%1").arg(engine));
    QCoreApplication::setApplicationVersion(QStringLiteral("2.0"));

    listAudioOutputs();
    logLine(QStringLiteral("default audio output: %1").arg(describeDevice(QMediaDevices::defaultAudioOutput())));

    if (parser.isSet(listDevicesOption)) {
        return 0;
    }

    const int durationMs = parser.value(durationOption).toInt();
    const int holdMs = parser.value(holdOption).toInt();
    const int intervalMs = parser.value(intervalOption).toInt();
    const float volume = parser.value(volumeOption).toFloat();
    const double toneHz = parser.value(toneHzOption).toDouble();

    const QString deviceSelector = parser.value(deviceOption);
    const QAudioDevice outputDevice = chooseOutputDevice(deviceSelector);
    if (outputDevice.isNull()) {
        logLine(QStringLiteral("ERROR: audio output device not found for selector '%1'").arg(deviceSelector));
        return 2;
    }

    const QUrl source = resolveSource(parser.value(fileOption));

    logLine(QStringLiteral("pid=%1").arg(QCoreApplication::applicationPid()));
    logLine(QStringLiteral("engine=%1 durationMs=%2 holdMs=%3 volume=%4")
                .arg(engine)
                .arg(durationMs)
                .arg(holdMs)
                .arg(volume, 0, 'f', 2));
    logLine(QStringLiteral("selected output: %1").arg(describeDevice(outputDevice)));
    logLine(QStringLiteral("source=%1").arg(source.toString()));
    logLine(QStringLiteral("tip: run `wpctl status` or `pactl list sink-inputs` while probe active"));

    auto finishLater = [&app, holdMs]() {
        QTimer::singleShot(std::max(0, holdMs), &app, &QCoreApplication::quit);
    };

    if (engine == QStringLiteral("platform")) {
        const QString soundName = parser.value(soundOption).trimmed().toLower();
        logLine(QStringLiteral("platform sound=%1 intervalMs=%2").arg(soundName).arg(intervalMs));
        PlatformIntegration::initializeSounds();

        auto *timer = new QTimer(&app);
        QObject::connect(timer, &QTimer::timeout, &app, [soundName]() {
            logLine(QStringLiteral("platform play sound=%1").arg(soundName));
            playNamedPlatformSound(soundName);
        });
        timer->start(std::max(50, intervalMs));
        QTimer::singleShot(0, &app, [soundName]() {
            logLine(QStringLiteral("platform initial play sound=%1").arg(soundName));
            playNamedPlatformSound(soundName);
        });
        QTimer::singleShot(std::max(0, durationMs), &app, [timer, finishLater]() {
            timer->stop();
            finishLater();
        });
        return app.exec();
    }

    if (engine == QStringLiteral("soundeffect")) {
        auto *effect = new QSoundEffect(outputDevice, &app);
        effect->setAudioDevice(outputDevice);
        effect->setLoopCount(QSoundEffect::Infinite);
        effect->setVolume(volume);

        logLine(QStringLiteral("soundeffect audioDevice=%1").arg(describeDevice(effect->audioDevice())));

        QObject::connect(effect, &QSoundEffect::statusChanged, &app, [effect]() {
            logLine(QStringLiteral("soundeffect status=%1 loaded=%2 playing=%3")
                        .arg(soundStatusName(effect->status()))
                        .arg(yesNo(effect->isLoaded()))
                        .arg(yesNo(effect->isPlaying())));
            if (effect->status() == QSoundEffect::Ready && !effect->isPlaying()) {
                logLine(QStringLiteral("soundeffect play() after Ready"));
                effect->play();
            }
        });
        QObject::connect(effect, &QSoundEffect::loadedChanged, &app, [effect]() {
            logLine(QStringLiteral("soundeffect loadedChanged loaded=%1 status=%2")
                        .arg(yesNo(effect->isLoaded()))
                        .arg(soundStatusName(effect->status())));
            if (effect->isLoaded() && !effect->isPlaying()) {
                logLine(QStringLiteral("soundeffect play() after load"));
                effect->play();
            }
        });
        QObject::connect(effect, &QSoundEffect::playingChanged, &app, [effect]() {
            logLine(QStringLiteral("soundeffect playingChanged playing=%1 status=%2")
                        .arg(yesNo(effect->isPlaying()))
                        .arg(soundStatusName(effect->status())));
        });
        QObject::connect(effect, &QSoundEffect::audioDeviceChanged, &app, [effect]() {
            logLine(QStringLiteral("soundeffect audioDeviceChanged -> %1").arg(describeDevice(effect->audioDevice())));
        });

        effect->setSource(source);
        if (effect->isLoaded() && !effect->isPlaying()) {
            logLine(QStringLiteral("soundeffect play() immediate-ready"));
            effect->play();
        }

        QTimer::singleShot(std::max(0, durationMs), &app, [effect, finishLater]() {
            logLine(QStringLiteral("soundeffect stop()"));
            effect->stop();
            finishLater();
        });

        return app.exec();
    }

    if (engine == QStringLiteral("mediaplayer")) {
        auto *audioOutput = new QAudioOutput(outputDevice, &app);
        audioOutput->setVolume(volume);

        auto *player = new QMediaPlayer(&app);
        player->setAudioOutput(audioOutput);
        player->setLoops(QMediaPlayer::Infinite);

        QObject::connect(player, &QMediaPlayer::playbackStateChanged, &app, [player]() {
            logLine(QStringLiteral("mediaplayer playbackState=%1")
                        .arg(playerStateName(player->playbackState())));
        });
        QObject::connect(player, &QMediaPlayer::mediaStatusChanged, &app, [player]() {
            logLine(QStringLiteral("mediaplayer mediaStatus=%1")
                        .arg(mediaStatusName(player->mediaStatus())));
        });
        QObject::connect(player, &QMediaPlayer::errorOccurred, &app, [player](QMediaPlayer::Error error, const QString &errorString) {
            logLine(QStringLiteral("mediaplayer error=%1 errorString=%2")
                        .arg(static_cast<int>(error))
                        .arg(errorString));
            Q_UNUSED(player);
        });
        QObject::connect(audioOutput, &QAudioOutput::deviceChanged, &app, [audioOutput]() {
            logLine(QStringLiteral("mediaplayer deviceChanged -> %1")
                        .arg(describeDevice(audioOutput->device())));
        });

        logLine(QStringLiteral("mediaplayer device=%1").arg(describeDevice(audioOutput->device())));
        player->setSource(source);
        QTimer::singleShot(0, &app, [player]() {
            logLine(QStringLiteral("mediaplayer play()"));
            player->play();
        });
        QTimer::singleShot(std::max(0, durationMs), &app, [player, finishLater]() {
            logLine(QStringLiteral("mediaplayer stop()"));
            player->stop();
            finishLater();
        });

        return app.exec();
    }

    if (engine == QStringLiteral("tone")) {
        QAudioFormat format = outputDevice.preferredFormat();
        if (format.sampleFormat() == QAudioFormat::Unknown || format.sampleRate() <= 0 || format.channelCount() <= 0) {
            format.setSampleRate(48000);
            format.setChannelCount(2);
            format.setSampleFormat(QAudioFormat::Int16);
        }

        logLine(QStringLiteral("tone format sampleRate=%1 channels=%2 sampleFormat=%3 bytesPerFrame=%4")
                    .arg(format.sampleRate())
                    .arg(format.channelCount())
                    .arg(static_cast<int>(format.sampleFormat()))
                    .arg(format.bytesPerFrame()));

        QByteArray pcm = generateSinePcm(format, durationMs, toneHz);
        if (pcm.isEmpty()) {
            logLine(QStringLiteral("ERROR: unable to generate tone buffer for format"));
            return 3;
        }

        auto *buffer = new QBuffer(&app);
        buffer->setData(pcm);
        buffer->open(QIODevice::ReadOnly);

        auto *sink = new QAudioSink(outputDevice, format, &app);
        sink->setVolume(volume);

        QObject::connect(sink, &QAudioSink::stateChanged, &app, [sink]() {
            logLine(QStringLiteral("tone state=%1 error=%2 processedUSecs=%3")
                        .arg(audioStateName(sink->state()))
                        .arg(audioErrorName(sink->error()))
                        .arg(sink->processedUSecs()));
        });

        logLine(QStringLiteral("tone bytes=%1 hz=%2").arg(pcm.size()).arg(toneHz, 0, 'f', 1));
        sink->start(buffer);
        QTimer::singleShot(std::max(0, durationMs) + std::max(0, holdMs), &app, [&app]() {
            app.quit();
        });

        return app.exec();
    }

    logLine(QStringLiteral("ERROR: unknown engine '%1'").arg(engine));
    return 4;
}
