#pragma once

#include "types.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;
class QCheckBox;
class QComboBox;
class QMediaDevices;
class QPlainTextEdit;
class QTimer;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void setConfig(const AppConfig &config);

signals:
    void configSaved(const AppConfig &config);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void startCaptureToggle();
    void startCaptureAbort();
    void startCaptureHistory();
    void saveSettings();
    void syncRefinementState();
    void refreshAudioInputDevices();

private:
    enum class CaptureTarget {
        None,
        Toggle,
        Abort,
        History,
    };

    void setCaptureTarget(CaptureTarget target);
    void updateHotkeyLabels();
    void showMessage(const QString &message, bool isError);
    void populateAudioInputDevices(const QString &preferredDeviceId);
    HotkeyBinding *bindingForTarget(CaptureTarget target);

    AppConfig m_config;
    CaptureTarget m_captureTarget = CaptureTarget::None;

    QLineEdit *m_deepgramApiKeyEdit = nullptr;
    QCheckBox *m_enableRefinementCheck = nullptr;
    QLineEdit *m_openAiApiKeyEdit = nullptr;
    QPlainTextEdit *m_refinementPromptEdit = nullptr;
    QCheckBox *m_showDoneScreenCheck = nullptr;
    QComboBox *m_audioInputCombo = nullptr;
    QMediaDevices *m_mediaDevices = nullptr;
    QTimer *m_deviceRefreshTimer = nullptr;
    QLabel *m_toggleValueLabel = nullptr;
    QLabel *m_abortValueLabel = nullptr;
    QLabel *m_historyValueLabel = nullptr;
    QPushButton *m_toggleButton = nullptr;
    QPushButton *m_abortButton = nullptr;
    QPushButton *m_historyButton = nullptr;
    QLabel *m_captureHelpLabel = nullptr;
    QLabel *m_messageLabel = nullptr;
};
