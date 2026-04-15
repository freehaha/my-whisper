#include "app_controller.h"

#include "audio_recorder.h"
#include "config.h"
#include "global_hotkey_manager.h"
#include "history_dialog.h"
#include "llm_refiner.h"
#include "platform_integration.h"
#include "settings_dialog.h"
#include "status_overlay.h"
#include "transcriber.h"
#include "transcription_history_store.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPen>
#include <QSystemTrayIcon>
#include <QTimer>

AppController::AppController(std::optional<bool> overrideShowDoneScreen, QObject *parent)
    : QObject(parent)
    , m_config(Config::load())
    , m_recorder(new AudioRecorder(this))
    , m_transcriber(new Transcriber(this))
    , m_refiner(new LLMRefiner(this))
    , m_historyStore(new TranscriptionHistoryStore(this))
    , m_statusOverlay(new StatusOverlay())
    , m_historyDialog(new HistoryDialog(m_historyStore))
    , m_settingsDialog(new SettingsDialog())
    , m_resetTimer(new QTimer(this))
    , m_cliShowDoneScreenOverride(overrideShowDoneScreen)
    , m_showDoneScreen(overrideShowDoneScreen.value_or(m_config.showDoneScreen)) {
    m_resetTimer->setSingleShot(true);

    createTrayIcon();
    m_statusOverlay->hide();
    PlatformIntegration::initializeSounds();
    m_recorder->setPreferredInputDeviceId(m_config.audioInputDeviceId);

    connect(m_recorder, &AudioRecorder::audioLevelsChanged, m_statusOverlay, &StatusOverlay::setAudioLevels);
    connect(m_recorder, &AudioRecorder::errorOccurred, this, &AppController::handleRecorderError);
    connect(m_transcriber, &Transcriber::transcriptionReady, this, &AppController::handleTranscriptionReady);
    connect(m_transcriber, &Transcriber::errorOccurred, this, &AppController::handleProcessingError);
    connect(m_refiner, &LLMRefiner::refined, this, &AppController::handleRefinementReady);
    connect(m_refiner, &LLMRefiner::errorOccurred, this, &AppController::handleProcessingError);
    connect(m_settingsDialog, &SettingsDialog::configSaved, this, &AppController::applyConfig);
    connect(m_resetTimer, &QTimer::timeout, this, &AppController::resetToIdle);

    GlobalHotkeyManager &hotkeys = GlobalHotkeyManager::instance();
    hotkeys.setToggleHandler([this]() { toggleRecording(); });
    hotkeys.setAbortHandler([this]() { abortRecording(); });
    hotkeys.setHistoryHandler([this]() { openHistory(); });

    if (hotkeys.isSupported()) {
        if (!hotkeys.registerHotkeys(m_config.toggleHotkey, m_config.abortHotkey, m_config.historyHotkey)) {
            qWarning("Failed to register one or more global hotkeys.");
        }
    } else {
        qWarning("Global hotkeys are unavailable. On Linux, run the app under X11/xcb (for example QT_QPA_PLATFORM=xcb).");
    }

    if (!PlatformIntegration::ensureAccessibilityPermissionPrompted()) {
        qWarning("Accessibility permission is not granted; synthetic paste may not work until enabled.");
    }
}

AppController::~AppController() {
    cleanupPendingAudioFile();
    if (m_statusOverlay) {
        m_statusOverlay->close();
        delete m_statusOverlay;
        m_statusOverlay = nullptr;
    }
    if (m_historyDialog) {
        m_historyDialog->close();
        delete m_historyDialog;
        m_historyDialog = nullptr;
    }
    if (m_settingsDialog) {
        m_settingsDialog->close();
        delete m_settingsDialog;
        m_settingsDialog = nullptr;
    }
}

void AppController::toggleRecording() {
    if (m_recorder->isRecording()) {
        stopAndProcess();
    } else {
        startRecording();
    }
}

void AppController::abortRecording() {
    if (!m_recorder->isRecording()) {
        return;
    }

    m_resetTimer->stop();
    m_recorder->abortRecording();
    PlatformIntegration::playStopSound();
    setState(AppState::Idle);
}

void AppController::openHistory() {
    m_historyDialog->showAndFocus();
}

void AppController::openSettings() {
    m_settingsDialog->setConfig(m_config);
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void AppController::handleRecorderError(const QString &message) {
    handleProcessingError(message);
}

void AppController::handleTranscriptionReady(const QString &text) {
    const QString transcript = text.trimmed();
    if (transcript.isEmpty()) {
        handleProcessingError(tr("Empty transcript"));
        return;
    }

    if (Config::hasUsableRefiner(m_config)) {
        setState(AppState::Refining);
        m_refiner->refine(transcript, m_config);
        return;
    }

    finalizeText(transcript);
}

void AppController::handleRefinementReady(const QString &text) {
    finalizeText(text.trimmed());
}

void AppController::handleProcessingError(const QString &message) {
    PlatformIntegration::playErrorSound();
    setState(AppState::Error, message);
    cleanupPendingAudioFile();
    scheduleIdleReset();
}

void AppController::resetToIdle() {
    setState(AppState::Idle);
}

void AppController::applyConfig(const AppConfig &config) {
    m_config = config;
    m_recorder->setPreferredInputDeviceId(m_config.audioInputDeviceId);
    m_showDoneScreen = m_cliShowDoneScreenOverride.value_or(m_config.showDoneScreen);
}

void AppController::createTrayIcon() {
    m_trayMenu = new QMenu();
    auto *historyAction = m_trayMenu->addAction(tr("Transcription History…"));
    auto *settingsAction = m_trayMenu->addAction(tr("Settings…"));
    m_trayMenu->addSeparator();
    auto *quitAction = m_trayMenu->addAction(tr("Quit MyWhisperQt"));

    connect(historyAction, &QAction::triggered, this, &AppController::openHistory);
    connect(settingsAction, &QAction::triggered, this, &AppController::openSettings);
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);

    m_trayIcon = new QSystemTrayIcon(createTrayAppIcon(), this);
    m_trayIcon->setToolTip(tr("MyWhisperQt"));
    m_trayIcon->setContextMenu(m_trayMenu);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
            openHistory();
        }
    });
    m_trayIcon->show();
}

QIcon AppController::createTrayAppIcon() const {
    QPixmap pixmap(18, 16);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(Qt::black);
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawLine(QPointF(3, 10), QPointF(3, 6));
    painter.drawLine(QPointF(7, 13), QPointF(7, 3));
    painter.drawLine(QPointF(11, 11), QPointF(11, 5));
    painter.drawLine(QPointF(15, 9), QPointF(15, 7));
    painter.end();

    return QIcon(pixmap);
}

void AppController::startRecording() {
    m_resetTimer->stop();
    setState(AppState::Recording);
    PlatformIntegration::playStartSound();
    m_recorder->startRecording();
}

void AppController::stopAndProcess() {
    m_recorder->stopRecording();
    PlatformIntegration::playStopSound();

    const QString path = m_recorder->audioFilePath();
    if (path.isEmpty()) {
        handleProcessingError(tr("No audio file"));
        return;
    }

    m_pendingAudioFile = path;
    setState(AppState::Transcribing);
    m_transcriber->transcribe(path, m_config);
}

void AppController::setState(AppState state, const QString &message) {
    m_state = state;
    m_stateMessage = message;
    m_statusOverlay->setState(state, message);

    if (state == AppState::Idle) {
        m_statusOverlay->hide();
    } else {
        m_statusOverlay->recenter();
        m_statusOverlay->show();
    }
}

void AppController::scheduleIdleReset() {
    const int resetDelayMs = (m_state == AppState::Done) ? 900 : 2000;
    m_resetTimer->start(resetDelayMs);
}

void AppController::cleanupPendingAudioFile() {
    if (m_pendingAudioFile.isEmpty()) {
        return;
    }

    QFile::remove(m_pendingAudioFile);
    m_pendingAudioFile.clear();
}

void AppController::finalizeText(const QString &text) {
    const QString finalText = text.trimmed();
    if (finalText.isEmpty()) {
        handleProcessingError(tr("Empty transcript"));
        return;
    }

    PlatformIntegration::playSuccessSound();
    m_historyStore->add(finalText);
    PlatformIntegration::pasteText(finalText);
    cleanupPendingAudioFile();

    if (m_showDoneScreen) {
        setState(AppState::Done);
        scheduleIdleReset();
    } else {
        m_resetTimer->stop();
        setState(AppState::Idle);
    }
}
