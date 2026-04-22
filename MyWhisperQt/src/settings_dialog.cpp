#include "settings_dialog.h"

#include "config.h"
#include "global_hotkey_manager.h"
#include "hotkey_formatter.h"

#include <QApplication>
#include <QAudioDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMediaDevices>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QString encodeAudioDeviceId(const QAudioDevice &device) {
    return QString::fromLatin1(device.id().toBase64());
}
}

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("MyWhisperQt Settings"));
    resize(560, 580);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    auto *apiGroup = new QGroupBox(tr("API Settings"), this);
    auto *apiLayout = new QFormLayout(apiGroup);
    m_deepgramApiKeyEdit = new QLineEdit(apiGroup);
    m_deepgramApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_deepgramKeywordsEdit = new QPlainTextEdit(apiGroup);
    m_deepgramKeywordsEdit->setPlaceholderText(tr("One keyterm/phrase per line. Example:\nAcmeCloud\nMyWhisper\nGPU"));
    m_deepgramKeywordsEdit->setFixedHeight(90);
    m_openAiApiKeyEdit = new QLineEdit(apiGroup);
    m_openAiApiKeyEdit->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_enableRefinementCheck = new QCheckBox(tr("Enable text refinement"), apiGroup);
    m_refinementProviderCombo = new QComboBox(apiGroup);
    m_refinementProviderCombo->addItem(tr("OpenAI"), QStringLiteral("openai"));
    m_refinementProviderCombo->addItem(tr("llama.cpp (local)"), QStringLiteral("llama_cpp"));
    m_llamaCppModelPathEdit = new QLineEdit(apiGroup);
    m_llamaCppModelPathEdit->setPlaceholderText(tr("/path/to/model.gguf"));
    m_llamaCppModelBrowseButton = new QPushButton(tr("Browse…"), apiGroup);
    auto *llamaModelRow = new QWidget(apiGroup);
    auto *llamaModelLayout = new QHBoxLayout(llamaModelRow);
    llamaModelLayout->setContentsMargins(0, 0, 0, 0);
    llamaModelLayout->addWidget(m_llamaCppModelPathEdit, 1);
    llamaModelLayout->addWidget(m_llamaCppModelBrowseButton);
    m_refinementPromptEdit = new QPlainTextEdit(apiGroup);
    m_refinementPromptEdit->setPlaceholderText(tr("Fix spelling and grammar. Return only the fixed text."));
    m_refinementPromptEdit->setFixedHeight(90);

    apiLayout->addRow(tr("Deepgram API key"), m_deepgramApiKeyEdit);
    apiLayout->addRow(tr("Deepgram keyterms"), m_deepgramKeywordsEdit);
    apiLayout->addRow(QString(), m_enableRefinementCheck);
    apiLayout->addRow(tr("Refinement backend"), m_refinementProviderCombo);
    apiLayout->addRow(tr("OpenAI API key"), m_openAiApiKeyEdit);
    apiLayout->addRow(tr("llama.cpp model"), llamaModelRow);
    apiLayout->addRow(tr("Refinement prompt"), m_refinementPromptEdit);

    m_mediaDevices = new QMediaDevices(this);
    m_deviceRefreshTimer = new QTimer(this);
    m_deviceRefreshTimer->setSingleShot(true);
    m_deviceRefreshTimer->setInterval(500);
    connect(m_deviceRefreshTimer, &QTimer::timeout, this, &SettingsDialog::refreshAudioInputDevices);

    auto *behaviorGroup = new QGroupBox(tr("Behavior"), this);
    auto *behaviorLayout = new QFormLayout(behaviorGroup);
    m_showDoneScreenCheck = new QCheckBox(tr("Show temporary Done overlay after successful transcription"), behaviorGroup);
    m_audioInputCombo = new QComboBox(behaviorGroup);
    behaviorLayout->addRow(QString(), m_showDoneScreenCheck);
    behaviorLayout->addRow(tr("Audio input device"), m_audioInputCombo);

    auto *hotkeyGroup = new QGroupBox(tr("Hotkeys"), this);
    auto *hotkeyLayout = new QGridLayout(hotkeyGroup);
    hotkeyLayout->setColumnStretch(1, 1);

    m_toggleValueLabel = new QLabel(hotkeyGroup);
    m_abortValueLabel = new QLabel(hotkeyGroup);
    m_historyValueLabel = new QLabel(hotkeyGroup);
    m_toggleButton = new QPushButton(tr("Change"), hotkeyGroup);
    m_abortButton = new QPushButton(tr("Change"), hotkeyGroup);
    m_historyButton = new QPushButton(tr("Change"), hotkeyGroup);

    hotkeyLayout->addWidget(new QLabel(tr("Start / Stop Recording"), hotkeyGroup), 0, 0);
    hotkeyLayout->addWidget(m_toggleValueLabel, 0, 1);
    hotkeyLayout->addWidget(m_toggleButton, 0, 2);

    hotkeyLayout->addWidget(new QLabel(tr("Abort Recording"), hotkeyGroup), 1, 0);
    hotkeyLayout->addWidget(m_abortValueLabel, 1, 1);
    hotkeyLayout->addWidget(m_abortButton, 1, 2);

    hotkeyLayout->addWidget(new QLabel(tr("Show History"), hotkeyGroup), 2, 0);
    hotkeyLayout->addWidget(m_historyValueLabel, 2, 1);
    hotkeyLayout->addWidget(m_historyButton, 2, 2);

    m_captureHelpLabel = new QLabel(this);
    m_messageLabel = new QLabel(this);
    m_messageLabel->setWordWrap(true);

    auto *pathLabel = new QLabel(tr("Config file: %1").arg(Config::configPath()), this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Close, this);

    layout->addWidget(apiGroup);
    layout->addWidget(behaviorGroup);
    layout->addWidget(hotkeyGroup);
    layout->addWidget(m_captureHelpLabel);
    layout->addWidget(m_messageLabel);
    layout->addWidget(pathLabel);
    layout->addWidget(buttons);

    connect(m_toggleButton, &QPushButton::clicked, this, &SettingsDialog::startCaptureToggle);
    connect(m_abortButton, &QPushButton::clicked, this, &SettingsDialog::startCaptureAbort);
    connect(m_historyButton, &QPushButton::clicked, this, &SettingsDialog::startCaptureHistory);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, this, &QDialog::close);
    connect(m_enableRefinementCheck, &QCheckBox::toggled, this, &SettingsDialog::syncRefinementState);
    connect(m_refinementProviderCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() { syncRefinementState(); });
    connect(m_llamaCppModelBrowseButton, &QPushButton::clicked, this, &SettingsDialog::browseLlamaCppModel);
    connect(m_mediaDevices, &QMediaDevices::audioInputsChanged, m_deviceRefreshTimer, QOverload<>::of(&QTimer::start));

    qApp->installEventFilter(this);
    populateAudioInputDevices(QString());
    updateHotkeyLabels();
    syncRefinementState();
}

SettingsDialog::~SettingsDialog() {
    qApp->removeEventFilter(this);
}

void SettingsDialog::setConfig(const AppConfig &config) {
    m_config = config;
    m_deepgramApiKeyEdit->setText(config.deepgramApiKey);
    m_deepgramKeywordsEdit->setPlainText(config.deepgramKeywords.join('\n'));
    m_enableRefinementCheck->setChecked(config.enableRefinement);
    const int providerIndex = m_refinementProviderCombo->findData(config.refinementProvider);
    m_refinementProviderCombo->setCurrentIndex(providerIndex >= 0 ? providerIndex : 0);
    m_openAiApiKeyEdit->setText(config.openaiApiKey);
    m_llamaCppModelPathEdit->setText(config.llamaCppModelPath);
    m_refinementPromptEdit->setPlainText(config.refinementPrompt);
    m_showDoneScreenCheck->setChecked(config.showDoneScreen);
    populateAudioInputDevices(config.audioInputDeviceId);
    setCaptureTarget(CaptureTarget::None);
    updateHotkeyLabels();
    syncRefinementState();
    showMessage(QString(), false);
}

bool SettingsDialog::eventFilter(QObject *watched, QEvent *event) {
    Q_UNUSED(watched)

    if (!isVisible() || m_captureTarget == CaptureTarget::None || event->type() != QEvent::KeyPress) {
        return QDialog::eventFilter(watched, event);
    }

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Escape) {
        setCaptureTarget(CaptureTarget::None);
        showMessage(QString(), false);
        return true;
    }

    const auto binding = HotkeyFormatter::bindingFromKeyEvent(keyEvent);
    if (!binding.has_value()) {
        showMessage(tr("Shortcut must include a non-modifier key plus at least one modifier."), true);
        return true;
    }

    if (HotkeyBinding *slot = bindingForTarget(m_captureTarget)) {
        *slot = *binding;
    }
    setCaptureTarget(CaptureTarget::None);
    showMessage(QString(), false);
    updateHotkeyLabels();
    return true;
}

void SettingsDialog::startCaptureToggle() {
    setCaptureTarget(m_captureTarget == CaptureTarget::Toggle ? CaptureTarget::None : CaptureTarget::Toggle);
}

void SettingsDialog::startCaptureAbort() {
    setCaptureTarget(m_captureTarget == CaptureTarget::Abort ? CaptureTarget::None : CaptureTarget::Abort);
}

void SettingsDialog::startCaptureHistory() {
    setCaptureTarget(m_captureTarget == CaptureTarget::History ? CaptureTarget::None : CaptureTarget::History);
}

void SettingsDialog::saveSettings() {
    AppConfig updated = m_config;
    updated.deepgramApiKey = m_deepgramApiKeyEdit->text().trimmed();
    updated.deepgramKeywords.clear();
    for (const QString &line : m_deepgramKeywordsEdit->toPlainText().split('\n')) {
        const QString trimmed = line.trimmed();
        if (!trimmed.isEmpty()) {
            updated.deepgramKeywords.append(trimmed);
        }
    }
    updated.enableRefinement = m_enableRefinementCheck->isChecked();
    updated.refinementProvider = m_refinementProviderCombo->currentData().toString().trimmed();
    updated.openaiApiKey = m_openAiApiKeyEdit->text().trimmed();
    updated.llamaCppModelPath = m_llamaCppModelPathEdit->text().trimmed();
    updated.refinementPrompt = m_refinementPromptEdit->toPlainText().trimmed();
    updated.showDoneScreen = m_showDoneScreenCheck->isChecked();
    updated.audioInputDeviceId = m_audioInputCombo->currentData().toString().trimmed();

    if (updated.refinementPrompt.isEmpty()) {
        updated.refinementPrompt = QStringLiteral("Fix spelling and grammar. Return only the fixed text.");
    }

    if (updated.toggleHotkey == updated.abortHotkey
        || updated.toggleHotkey == updated.historyHotkey
        || updated.abortHotkey == updated.historyHotkey) {
        showMessage(tr("All shortcuts must be different."), true);
        return;
    }

    GlobalHotkeyManager &hotkeys = GlobalHotkeyManager::instance();
    if (hotkeys.isSupported() && !hotkeys.registerHotkeys(updated.toggleHotkey, updated.abortHotkey, updated.historyHotkey)) {
        showMessage(tr("Unable to register one or more shortcuts. Try a different combination."), true);
        return;
    }

    QString error;
    if (!Config::save(updated, &error)) {
        showMessage(tr("Failed to save settings: %1").arg(error), true);
        return;
    }

    m_config = updated;
    emit configSaved(updated);

    if (hotkeys.isSupported()) {
        showMessage(tr("Settings saved."), false);
    } else {
        showMessage(tr("Settings saved. Global hotkeys are only implemented on macOS in this port."), false);
    }
}

void SettingsDialog::syncRefinementState() {
    const bool enabled = m_enableRefinementCheck->isChecked();
    const QString provider = m_refinementProviderCombo->currentData().toString();
    const bool useOpenAi = enabled && provider == QStringLiteral("openai");
    const bool useLlamaCpp = enabled && provider == QStringLiteral("llama_cpp");

    m_refinementProviderCombo->setEnabled(enabled);
    m_openAiApiKeyEdit->setEnabled(useOpenAi);
    m_llamaCppModelPathEdit->setEnabled(useLlamaCpp);
    m_llamaCppModelBrowseButton->setEnabled(useLlamaCpp);
    m_refinementPromptEdit->setEnabled(enabled);
}

void SettingsDialog::browseLlamaCppModel() {
    const QString path = QFileDialog::getOpenFileName(
        this,
        tr("Select llama.cpp model"),
        m_llamaCppModelPathEdit->text().trimmed(),
        tr("GGUF models (*.gguf);;All files (*)"));
    if (!path.isEmpty()) {
        m_llamaCppModelPathEdit->setText(path);
    }
}

void SettingsDialog::refreshAudioInputDevices() {
    populateAudioInputDevices(m_audioInputCombo ? m_audioInputCombo->currentData().toString() : QString());
}

void SettingsDialog::populateAudioInputDevices(const QString &preferredDeviceId) {
    if (!m_audioInputCombo) {
        return;
    }

    const QSignalBlocker blocker(m_audioInputCombo);
    m_audioInputCombo->clear();
    m_audioInputCombo->addItem(tr("System default"), QString());

    const auto devices = QMediaDevices::audioInputs();
    for (const QAudioDevice &device : devices) {
        QString label = device.description().trimmed();
        if (label.isEmpty()) {
            label = tr("Unnamed input");
        }
        m_audioInputCombo->addItem(label, encodeAudioDeviceId(device));
    }

    int index = 0;
    if (!preferredDeviceId.trimmed().isEmpty()) {
        const int matching = m_audioInputCombo->findData(preferredDeviceId.trimmed());
        if (matching >= 0) {
            index = matching;
        }
    }
    m_audioInputCombo->setCurrentIndex(index);
}

void SettingsDialog::setCaptureTarget(CaptureTarget target) {
    m_captureTarget = target;
    switch (target) {
    case CaptureTarget::None:
        m_captureHelpLabel->setText(tr("Use at least one modifier key (⌘, ⌥, ⌃, ⇧)."));
        break;
    case CaptureTarget::Toggle:
    case CaptureTarget::Abort:
    case CaptureTarget::History:
        m_captureHelpLabel->setText(tr("Press a key combination now (Esc to cancel)."));
        break;
    }
    updateHotkeyLabels();
}

void SettingsDialog::updateHotkeyLabels() {
    m_toggleValueLabel->setText(m_captureTarget == CaptureTarget::Toggle
        ? tr("Press shortcut…")
        : HotkeyFormatter::displayString(m_config.toggleHotkey));
    m_abortValueLabel->setText(m_captureTarget == CaptureTarget::Abort
        ? tr("Press shortcut…")
        : HotkeyFormatter::displayString(m_config.abortHotkey));
    m_historyValueLabel->setText(m_captureTarget == CaptureTarget::History
        ? tr("Press shortcut…")
        : HotkeyFormatter::displayString(m_config.historyHotkey));

    m_toggleButton->setText(m_captureTarget == CaptureTarget::Toggle ? tr("Cancel") : tr("Change"));
    m_abortButton->setText(m_captureTarget == CaptureTarget::Abort ? tr("Cancel") : tr("Change"));
    m_historyButton->setText(m_captureTarget == CaptureTarget::History ? tr("Cancel") : tr("Change"));
}

void SettingsDialog::showMessage(const QString &message, bool isError) {
    m_messageLabel->setText(message);
    m_messageLabel->setStyleSheet(isError ? QStringLiteral("color: #c62828;") : QStringLiteral("color: #2e7d32;"));
}

HotkeyBinding *SettingsDialog::bindingForTarget(CaptureTarget target) {
    switch (target) {
    case CaptureTarget::Toggle:
        return &m_config.toggleHotkey;
    case CaptureTarget::Abort:
        return &m_config.abortHotkey;
    case CaptureTarget::History:
        return &m_config.historyHotkey;
    case CaptureTarget::None:
        return nullptr;
    }
    return nullptr;
}
