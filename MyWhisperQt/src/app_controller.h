#pragma once

#include "types.h"

#include <QObject>

class AudioRecorder;
class Transcriber;
class LLMRefiner;
class TranscriptionHistoryStore;
class StatusOverlay;
class HistoryDialog;
class SettingsDialog;
class QSystemTrayIcon;
class QMenu;
class QTimer;

class AppController : public QObject {
    Q_OBJECT

public:
    explicit AppController(QObject *parent = nullptr);
    ~AppController() override;

public slots:
    void toggleRecording();
    void abortRecording();
    void openHistory();
    void openSettings();

private slots:
    void handleRecorderError(const QString &message);
    void handleTranscriptionReady(const QString &text);
    void handleRefinementReady(const QString &text);
    void handleProcessingError(const QString &message);
    void resetToIdle();
    void applyConfig(const AppConfig &config);

private:
    void createTrayIcon();
    QIcon createTrayAppIcon() const;
    void startRecording();
    void stopAndProcess();
    void setState(AppState state, const QString &message = QString());
    void scheduleIdleReset();
    void cleanupPendingAudioFile();
    void finalizeText(const QString &text);

    AppConfig m_config;
    AudioRecorder *m_recorder = nullptr;
    Transcriber *m_transcriber = nullptr;
    LLMRefiner *m_refiner = nullptr;
    TranscriptionHistoryStore *m_historyStore = nullptr;
    StatusOverlay *m_statusOverlay = nullptr;
    HistoryDialog *m_historyDialog = nullptr;
    SettingsDialog *m_settingsDialog = nullptr;
    QSystemTrayIcon *m_trayIcon = nullptr;
    QMenu *m_trayMenu = nullptr;
    QTimer *m_resetTimer = nullptr;
    AppState m_state = AppState::Idle;
    QString m_stateMessage;
    QString m_pendingAudioFile;
};
