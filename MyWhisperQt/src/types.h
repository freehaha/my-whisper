#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

namespace MacHotkey {
inline constexpr quint32 Command = 1u << 8;
inline constexpr quint32 Shift = 1u << 9;
inline constexpr quint32 Option = 1u << 11;
inline constexpr quint32 Control = 1u << 12;

inline constexpr quint32 KeyA = 0x00;
inline constexpr quint32 KeyB = 0x0B;
inline constexpr quint32 KeyC = 0x08;
inline constexpr quint32 KeyD = 0x02;
inline constexpr quint32 KeyE = 0x0E;
inline constexpr quint32 KeyF = 0x03;
inline constexpr quint32 KeyG = 0x05;
inline constexpr quint32 KeyH = 0x04;
inline constexpr quint32 KeyI = 0x22;
inline constexpr quint32 KeyJ = 0x26;
inline constexpr quint32 KeyK = 0x28;
inline constexpr quint32 KeyL = 0x25;
inline constexpr quint32 KeyM = 0x2E;
inline constexpr quint32 KeyN = 0x2D;
inline constexpr quint32 KeyO = 0x1F;
inline constexpr quint32 KeyP = 0x23;
inline constexpr quint32 KeyQ = 0x0C;
inline constexpr quint32 KeyR = 0x0F;
inline constexpr quint32 KeyS = 0x01;
inline constexpr quint32 KeyT = 0x11;
inline constexpr quint32 KeyU = 0x20;
inline constexpr quint32 KeyV = 0x09;
inline constexpr quint32 KeyW = 0x0D;
inline constexpr quint32 KeyX = 0x07;
inline constexpr quint32 KeyY = 0x10;
inline constexpr quint32 KeyZ = 0x06;

inline constexpr quint32 Key0 = 0x1D;
inline constexpr quint32 Key1 = 0x12;
inline constexpr quint32 Key2 = 0x13;
inline constexpr quint32 Key3 = 0x14;
inline constexpr quint32 Key4 = 0x15;
inline constexpr quint32 Key5 = 0x17;
inline constexpr quint32 Key6 = 0x16;
inline constexpr quint32 Key7 = 0x1A;
inline constexpr quint32 Key8 = 0x1C;
inline constexpr quint32 Key9 = 0x19;

inline constexpr quint32 Space = 0x31;
inline constexpr quint32 Return = 0x24;
inline constexpr quint32 Escape = 0x35;
inline constexpr quint32 Tab = 0x30;
inline constexpr quint32 Delete = 0x33;
inline constexpr quint32 ForwardDelete = 0x75;
inline constexpr quint32 LeftArrow = 0x7B;
inline constexpr quint32 RightArrow = 0x7C;
inline constexpr quint32 DownArrow = 0x7D;
inline constexpr quint32 UpArrow = 0x7E;
inline constexpr quint32 F1 = 0x7A;
inline constexpr quint32 F2 = 0x78;
inline constexpr quint32 F3 = 0x63;
inline constexpr quint32 F4 = 0x76;
inline constexpr quint32 F5 = 0x60;
inline constexpr quint32 F6 = 0x61;
inline constexpr quint32 F7 = 0x62;
inline constexpr quint32 F8 = 0x64;
inline constexpr quint32 F9 = 0x65;
inline constexpr quint32 F10 = 0x6D;
inline constexpr quint32 F11 = 0x67;
inline constexpr quint32 F12 = 0x6F;

inline constexpr quint32 CommandKey = 0x37;
inline constexpr quint32 RightCommandKey = 0x36;
inline constexpr quint32 ShiftKey = 0x38;
inline constexpr quint32 RightShiftKey = 0x3C;
inline constexpr quint32 OptionKey = 0x3A;
inline constexpr quint32 RightOptionKey = 0x3D;
inline constexpr quint32 ControlKey = 0x3B;
inline constexpr quint32 RightControlKey = 0x3E;
inline constexpr quint32 CapsLockKey = 0x39;
inline constexpr quint32 FunctionKey = 0x3F;
inline constexpr quint32 KeypadEnter = 0x4C;
}

struct HotkeyBinding {
    quint32 keyCode = 0;
    quint32 modifiers = 0;

    bool operator==(const HotkeyBinding &other) const {
        return keyCode == other.keyCode && modifiers == other.modifiers;
    }

    bool operator!=(const HotkeyBinding &other) const {
        return !(*this == other);
    }

    static constexpr quint32 platformDefaultModifiers() {
#ifdef Q_OS_MACOS
        return MacHotkey::Command | MacHotkey::Option;
#else
        return MacHotkey::Control | MacHotkey::Option;
#endif
    }

    static HotkeyBinding defaultToggle() {
        return {MacHotkey::KeyR, platformDefaultModifiers()};
    }

    static HotkeyBinding defaultAbort() {
        return {MacHotkey::KeyX, platformDefaultModifiers()};
    }

    static HotkeyBinding defaultHistory() {
        return {MacHotkey::KeyH, platformDefaultModifiers()};
    }
};

struct AppConfig {
    QString transcriptionBackend = QStringLiteral("deepgram");
    QString deepgramApiKey;
    QStringList deepgramKeywords;
    QString assemblyAiApiKey;
    QString assemblyAiSpeechModel = QStringLiteral("u3-rt-pro");
    QString openaiApiKey;
    bool enableRefinement = false;
    QString refinementProvider = QStringLiteral("openai");
    QString refinementPrompt;
    QString llamaCppBinaryPath;
    QString llamaCppAdditionalArgs;
    QString llamaCppModelPath;
    QString audioInputDeviceId;
    HotkeyBinding toggleHotkey = HotkeyBinding::defaultToggle();
    HotkeyBinding abortHotkey = HotkeyBinding::defaultAbort();
    HotkeyBinding historyHotkey = HotkeyBinding::defaultHistory();
    bool showDoneScreen = false;

    QString normalizedAssemblyAiSpeechModel() const {
        const QString trimmed = assemblyAiSpeechModel.trimmed();
        return trimmed.isEmpty() ? QStringLiteral("u3-rt-pro") : trimmed;
    }
};

enum class AppState {
    Idle,
    Recording,
    Transcribing,
    Refining,
    Done,
    Error,
};

struct TranscriptionHistoryEntry {
    QString id;
    QString text;
    QDateTime timestamp;
};

inline QVector<float> defaultAudioLevels() {
    return {0.1f, 0.1f, 0.1f, 0.1f, 0.1f};
}
