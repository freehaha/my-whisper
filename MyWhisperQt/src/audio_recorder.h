#pragma once

#include "types.h"

#include <QAudioFormat>
#include <QByteArray>
#include <QObject>
#include <QString>

class QAudioDevice;
class QAudioSource;
class QIODevice;
class QFile;

class AudioRecorder : public QObject {
    Q_OBJECT

public:
    explicit AudioRecorder(QObject *parent = nullptr);
    ~AudioRecorder() override;

    bool isRecording() const;
    QString audioFilePath() const;
    QVector<float> audioLevels() const;
    QAudioFormat audioFormat() const;
    QString preferredInputDeviceId() const;

    void setPreferredInputDeviceId(const QString &deviceId);

public slots:
    void startRecording();
    void stopRecording();
    void abortRecording();

signals:
    void audioLevelsChanged(const QVector<float> &levels);
    void audioChunkCaptured(const QByteArray &data);
    void recordingChanged(bool recording);
    void errorOccurred(const QString &message);

private slots:
    void handleReadyRead();

private:
    void resetLevels();
    void cleanupAudioObjects();
    void finalizeWaveFile();
    void writeWaveHeader(quint32 dataBytes);

    QAudioFormat m_format;
    QAudioSource *m_audioSource = nullptr;
    QIODevice *m_inputDevice = nullptr;
    QFile *m_outputFile = nullptr;
    QString m_audioFilePath;
    QVector<float> m_audioLevels;
    QString m_preferredInputDeviceId;
    quint32 m_dataBytes = 0;
    bool m_recording = false;
};
