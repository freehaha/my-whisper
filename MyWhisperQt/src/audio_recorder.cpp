#include "audio_recorder.h"

#include <QAudioDevice>
#include <QAudioSource>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMediaDevices>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
QByteArray makeWaveHeader(const QAudioFormat &format, quint32 dataBytes) {
    QByteArray header(44, '\0');
    char *buffer = header.data();

    const quint16 channels = static_cast<quint16>(format.channelCount());
    const quint32 sampleRate = static_cast<quint32>(format.sampleRate());
    const quint16 bitsPerSample = static_cast<quint16>(qMax(1, format.bytesPerSample()) * 8);
    const quint16 audioFormatCode = format.sampleFormat() == QAudioFormat::Float ? 3 : 1;
    const quint32 byteRate = sampleRate * channels * bitsPerSample / 8;
    const quint16 blockAlign = channels * bitsPerSample / 8;
    const quint32 chunkSize = 36 + dataBytes;

    std::memcpy(buffer + 0, "RIFF", 4);
    qToLittleEndian(chunkSize, reinterpret_cast<uchar *>(buffer + 4));
    std::memcpy(buffer + 8, "WAVE", 4);
    std::memcpy(buffer + 12, "fmt ", 4);
    qToLittleEndian<quint32>(16, reinterpret_cast<uchar *>(buffer + 16));
    qToLittleEndian(audioFormatCode, reinterpret_cast<uchar *>(buffer + 20));
    qToLittleEndian(channels, reinterpret_cast<uchar *>(buffer + 22));
    qToLittleEndian(sampleRate, reinterpret_cast<uchar *>(buffer + 24));
    qToLittleEndian(byteRate, reinterpret_cast<uchar *>(buffer + 28));
    qToLittleEndian(blockAlign, reinterpret_cast<uchar *>(buffer + 32));
    qToLittleEndian(bitsPerSample, reinterpret_cast<uchar *>(buffer + 34));
    std::memcpy(buffer + 36, "data", 4);
    qToLittleEndian(dataBytes, reinterpret_cast<uchar *>(buffer + 40));
    return header;
}

float visualLevelFromMagnitude(float peak, float rms) {
    const float blended = std::max(peak * 0.55f, rms);
    const float clamped = std::clamp(blended, 0.0001f, 1.0f);
    const float db = 20.0f * std::log10(clamped);
    const float normalized = std::clamp((db + 45.0f) / 45.0f, 0.0f, 1.0f);
    const float boosted = std::pow(normalized, 0.6f);
    return 0.05f + boosted * 0.95f;
}

float peakLevelForBuffer(const QByteArray &data, const QAudioFormat &format) {
    auto fromNormalizedSamples = [](auto sampleAt, int count) -> float {
        if (count <= 0) {
            return 0.05f;
        }

        float peak = 0.0f;
        double sumSquares = 0.0;
        for (int i = 0; i < count; ++i) {
            const float normalized = std::clamp(std::abs(sampleAt(i)), 0.0f, 1.0f);
            peak = std::max(peak, normalized);
            sumSquares += static_cast<double>(normalized) * static_cast<double>(normalized);
        }

        const float rms = static_cast<float>(std::sqrt(sumSquares / static_cast<double>(count)));
        return visualLevelFromMagnitude(peak, rms);
    };

    switch (format.sampleFormat()) {
    case QAudioFormat::UInt8: {
        const auto *samples = reinterpret_cast<const quint8 *>(data.constData());
        const int count = data.size();
        return fromNormalizedSamples([samples](int i) {
            return (static_cast<int>(samples[i]) - 128) / 128.0f;
        }, count);
    }
    case QAudioFormat::Int16: {
        const auto *samples = reinterpret_cast<const qint16 *>(data.constData());
        const int count = data.size() / static_cast<int>(sizeof(qint16));
        return fromNormalizedSamples([samples](int i) {
            return samples[i] / 32767.0f;
        }, count);
    }
    case QAudioFormat::Int32: {
        const auto *samples = reinterpret_cast<const qint32 *>(data.constData());
        const int count = data.size() / static_cast<int>(sizeof(qint32));
        return fromNormalizedSamples([samples](int i) {
            return samples[i] / 2147483647.0f;
        }, count);
    }
    case QAudioFormat::Float: {
        const auto *samples = reinterpret_cast<const float *>(data.constData());
        const int count = data.size() / static_cast<int>(sizeof(float));
        return fromNormalizedSamples([samples](int i) {
            return samples[i];
        }, count);
    }
    case QAudioFormat::Unknown:
        break;
    }

    return 0.05f;
}
}

AudioRecorder::AudioRecorder(QObject *parent)
    : QObject(parent)
    , m_audioLevels(defaultAudioLevels()) {
}

AudioRecorder::~AudioRecorder() {
    abortRecording();
}

bool AudioRecorder::isRecording() const {
    return m_recording;
}

QString AudioRecorder::audioFilePath() const {
    return m_audioFilePath;
}

QVector<float> AudioRecorder::audioLevels() const {
    return m_audioLevels;
}

QString AudioRecorder::preferredInputDeviceId() const {
    return m_preferredInputDeviceId;
}

void AudioRecorder::setPreferredInputDeviceId(const QString &deviceId) {
    m_preferredInputDeviceId = deviceId.trimmed();
}

void AudioRecorder::startRecording() {
    if (m_recording) {
        return;
    }

    resetLevels();
    cleanupAudioObjects();
    m_dataBytes = 0;

    QAudioDevice device;
    if (!m_preferredInputDeviceId.isEmpty()) {
        const QByteArray requestedId = QByteArray::fromBase64(m_preferredInputDeviceId.toLatin1());
        const auto inputs = QMediaDevices::audioInputs();
        for (const QAudioDevice &candidate : inputs) {
            if (candidate.id() == requestedId) {
                device = candidate;
                break;
            }
        }

        if (device.isNull()) {
            qWarning() << "Configured audio input device not found, falling back to default.";
        }
    }

    if (device.isNull()) {
        device = QMediaDevices::defaultAudioInput();
    }
    if (device.isNull()) {
        emit errorOccurred(tr("No audio input device found."));
        return;
    }

    m_format = device.preferredFormat();
    m_format.setSampleRate(16000);
    m_format.setChannelCount(1);
    m_format.setSampleFormat(QAudioFormat::Int16);

    if (!device.isFormatSupported(m_format)) {
        m_format = device.preferredFormat();
    }

    m_audioFilePath = QDir::temp().filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".wav");
    m_outputFile = new QFile(m_audioFilePath, this);
    if (!m_outputFile->open(QIODevice::WriteOnly)) {
        emit errorOccurred(tr("Unable to open temporary audio file: %1").arg(m_outputFile->errorString()));
        cleanupAudioObjects();
        m_audioFilePath.clear();
        return;
    }

    m_outputFile->write(QByteArray(44, '\0'));

    m_audioSource = new QAudioSource(device, m_format, this);
    m_inputDevice = m_audioSource->start();
    if (!m_inputDevice) {
        emit errorOccurred(tr("Unable to start audio recording."));
        cleanupAudioObjects();
        QFile::remove(m_audioFilePath);
        m_audioFilePath.clear();
        return;
    }

    connect(m_inputDevice, &QIODevice::readyRead, this, &AudioRecorder::handleReadyRead);
    m_recording = true;
    emit recordingChanged(true);
}

void AudioRecorder::stopRecording() {
    if (!m_recording) {
        return;
    }

    handleReadyRead();
    if (m_audioSource) {
        m_audioSource->stop();
    }

    finalizeWaveFile();
    cleanupAudioObjects();
    m_recording = false;
    emit recordingChanged(false);
}

void AudioRecorder::abortRecording() {
    const QString path = m_audioFilePath;

    if (m_audioSource) {
        m_audioSource->stop();
    }

    cleanupAudioObjects();
    m_recording = false;
    if (!path.isEmpty()) {
        QFile::remove(path);
    }
    m_audioFilePath.clear();
    resetLevels();
    emit recordingChanged(false);
}

void AudioRecorder::handleReadyRead() {
    if (!m_inputDevice || !m_outputFile) {
        return;
    }

    const QByteArray data = m_inputDevice->readAll();
    if (data.isEmpty()) {
        return;
    }

    m_outputFile->write(data);
    m_dataBytes += static_cast<quint32>(data.size());

    const float level = peakLevelForBuffer(data, m_format);
    if (!m_audioLevels.isEmpty()) {
        m_audioLevels.pop_front();
    }
    m_audioLevels.push_back(level);
    emit audioLevelsChanged(m_audioLevels);
}

void AudioRecorder::resetLevels() {
    m_audioLevels = defaultAudioLevels();
    emit audioLevelsChanged(m_audioLevels);
}

void AudioRecorder::cleanupAudioObjects() {
    if (m_inputDevice) {
        m_inputDevice->disconnect(this);
        m_inputDevice = nullptr;
    }

    if (m_audioSource) {
        m_audioSource->deleteLater();
        m_audioSource = nullptr;
    }

    if (m_outputFile) {
        if (m_outputFile->isOpen()) {
            m_outputFile->close();
        }
        m_outputFile->deleteLater();
        m_outputFile = nullptr;
    }
}

void AudioRecorder::finalizeWaveFile() {
    if (!m_outputFile) {
        return;
    }

    if (m_outputFile->isOpen()) {
        m_outputFile->seek(0);
        m_outputFile->write(makeWaveHeader(m_format, m_dataBytes));
        m_outputFile->flush();
    }
}
