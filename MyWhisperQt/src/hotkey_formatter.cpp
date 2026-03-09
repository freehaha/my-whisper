#include "hotkey_formatter.h"

namespace {
quint32 keyCodeFromQtKey(int key) {
    switch (key) {
    case Qt::Key_A: return MacHotkey::KeyA;
    case Qt::Key_B: return MacHotkey::KeyB;
    case Qt::Key_C: return MacHotkey::KeyC;
    case Qt::Key_D: return MacHotkey::KeyD;
    case Qt::Key_E: return MacHotkey::KeyE;
    case Qt::Key_F: return MacHotkey::KeyF;
    case Qt::Key_G: return MacHotkey::KeyG;
    case Qt::Key_H: return MacHotkey::KeyH;
    case Qt::Key_I: return MacHotkey::KeyI;
    case Qt::Key_J: return MacHotkey::KeyJ;
    case Qt::Key_K: return MacHotkey::KeyK;
    case Qt::Key_L: return MacHotkey::KeyL;
    case Qt::Key_M: return MacHotkey::KeyM;
    case Qt::Key_N: return MacHotkey::KeyN;
    case Qt::Key_O: return MacHotkey::KeyO;
    case Qt::Key_P: return MacHotkey::KeyP;
    case Qt::Key_Q: return MacHotkey::KeyQ;
    case Qt::Key_R: return MacHotkey::KeyR;
    case Qt::Key_S: return MacHotkey::KeyS;
    case Qt::Key_T: return MacHotkey::KeyT;
    case Qt::Key_U: return MacHotkey::KeyU;
    case Qt::Key_V: return MacHotkey::KeyV;
    case Qt::Key_W: return MacHotkey::KeyW;
    case Qt::Key_X: return MacHotkey::KeyX;
    case Qt::Key_Y: return MacHotkey::KeyY;
    case Qt::Key_Z: return MacHotkey::KeyZ;

    case Qt::Key_0: return MacHotkey::Key0;
    case Qt::Key_1: return MacHotkey::Key1;
    case Qt::Key_2: return MacHotkey::Key2;
    case Qt::Key_3: return MacHotkey::Key3;
    case Qt::Key_4: return MacHotkey::Key4;
    case Qt::Key_5: return MacHotkey::Key5;
    case Qt::Key_6: return MacHotkey::Key6;
    case Qt::Key_7: return MacHotkey::Key7;
    case Qt::Key_8: return MacHotkey::Key8;
    case Qt::Key_9: return MacHotkey::Key9;

    case Qt::Key_Space: return MacHotkey::Space;
    case Qt::Key_Return: return MacHotkey::Return;
    case Qt::Key_Enter: return MacHotkey::KeypadEnter;
    case Qt::Key_Escape: return MacHotkey::Escape;
    case Qt::Key_Tab: return MacHotkey::Tab;
    case Qt::Key_Backspace: return MacHotkey::Delete;
    case Qt::Key_Delete: return MacHotkey::ForwardDelete;

    case Qt::Key_Left: return MacHotkey::LeftArrow;
    case Qt::Key_Right: return MacHotkey::RightArrow;
    case Qt::Key_Up: return MacHotkey::UpArrow;
    case Qt::Key_Down: return MacHotkey::DownArrow;

    case Qt::Key_F1: return MacHotkey::F1;
    case Qt::Key_F2: return MacHotkey::F2;
    case Qt::Key_F3: return MacHotkey::F3;
    case Qt::Key_F4: return MacHotkey::F4;
    case Qt::Key_F5: return MacHotkey::F5;
    case Qt::Key_F6: return MacHotkey::F6;
    case Qt::Key_F7: return MacHotkey::F7;
    case Qt::Key_F8: return MacHotkey::F8;
    case Qt::Key_F9: return MacHotkey::F9;
    case Qt::Key_F10: return MacHotkey::F10;
    case Qt::Key_F11: return MacHotkey::F11;
    case Qt::Key_F12: return MacHotkey::F12;
    default:
        return 0;
    }
}

quint32 internalKeyCodeForEvent(QKeyEvent *event) {
#ifdef Q_OS_MACOS
    if (event->nativeVirtualKey() != 0) {
        return event->nativeVirtualKey();
    }
#endif
    return keyCodeFromQtKey(event->key());
}
}

namespace HotkeyFormatter {
std::optional<HotkeyBinding> bindingFromKeyEvent(QKeyEvent *event) {
    const quint32 keyCode = internalKeyCodeForEvent(event);
    if (keyCode == 0 || isModifierKey(keyCode)) {
        return std::nullopt;
    }

    const quint32 modifiers = carbonModifiers(event->modifiers());
    if (modifiers == 0) {
        return std::nullopt;
    }

    return HotkeyBinding{keyCode, modifiers};
}

quint32 carbonModifiers(Qt::KeyboardModifiers modifiers) {
    quint32 result = 0;
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= MacHotkey::Control;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= MacHotkey::Option;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= MacHotkey::Shift;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        result |= MacHotkey::Command;
    }
    return result;
}

bool isModifierKey(quint32 keyCode) {
    switch (keyCode) {
    case MacHotkey::CommandKey:
    case MacHotkey::RightCommandKey:
    case MacHotkey::ShiftKey:
    case MacHotkey::RightShiftKey:
    case MacHotkey::OptionKey:
    case MacHotkey::RightOptionKey:
    case MacHotkey::ControlKey:
    case MacHotkey::RightControlKey:
    case MacHotkey::CapsLockKey:
    case MacHotkey::FunctionKey:
        return true;
    default:
        return false;
    }
}

QString keyName(quint32 keyCode) {
    switch (keyCode) {
    case MacHotkey::KeyA: return "A";
    case MacHotkey::KeyB: return "B";
    case MacHotkey::KeyC: return "C";
    case MacHotkey::KeyD: return "D";
    case MacHotkey::KeyE: return "E";
    case MacHotkey::KeyF: return "F";
    case MacHotkey::KeyG: return "G";
    case MacHotkey::KeyH: return "H";
    case MacHotkey::KeyI: return "I";
    case MacHotkey::KeyJ: return "J";
    case MacHotkey::KeyK: return "K";
    case MacHotkey::KeyL: return "L";
    case MacHotkey::KeyM: return "M";
    case MacHotkey::KeyN: return "N";
    case MacHotkey::KeyO: return "O";
    case MacHotkey::KeyP: return "P";
    case MacHotkey::KeyQ: return "Q";
    case MacHotkey::KeyR: return "R";
    case MacHotkey::KeyS: return "S";
    case MacHotkey::KeyT: return "T";
    case MacHotkey::KeyU: return "U";
    case MacHotkey::KeyV: return "V";
    case MacHotkey::KeyW: return "W";
    case MacHotkey::KeyX: return "X";
    case MacHotkey::KeyY: return "Y";
    case MacHotkey::KeyZ: return "Z";
    case MacHotkey::Key0: return "0";
    case MacHotkey::Key1: return "1";
    case MacHotkey::Key2: return "2";
    case MacHotkey::Key3: return "3";
    case MacHotkey::Key4: return "4";
    case MacHotkey::Key5: return "5";
    case MacHotkey::Key6: return "6";
    case MacHotkey::Key7: return "7";
    case MacHotkey::Key8: return "8";
    case MacHotkey::Key9: return "9";
    case MacHotkey::Space: return "Space";
    case MacHotkey::Return: return "Return";
    case MacHotkey::KeypadEnter: return "Keypad Enter";
    case MacHotkey::Escape: return "Esc";
    case MacHotkey::Tab: return "Tab";
    case MacHotkey::Delete: return "Delete";
    case MacHotkey::ForwardDelete: return "Forward Delete";
    case MacHotkey::LeftArrow: return "←";
    case MacHotkey::RightArrow: return "→";
    case MacHotkey::UpArrow: return "↑";
    case MacHotkey::DownArrow: return "↓";
    case MacHotkey::F1: return "F1";
    case MacHotkey::F2: return "F2";
    case MacHotkey::F3: return "F3";
    case MacHotkey::F4: return "F4";
    case MacHotkey::F5: return "F5";
    case MacHotkey::F6: return "F6";
    case MacHotkey::F7: return "F7";
    case MacHotkey::F8: return "F8";
    case MacHotkey::F9: return "F9";
    case MacHotkey::F10: return "F10";
    case MacHotkey::F11: return "F11";
    case MacHotkey::F12: return "F12";
    default:
        return QStringLiteral("Key(%1)").arg(keyCode);
    }
}

QString displayString(const HotkeyBinding &binding) {
    QString result;
    if (binding.modifiers & MacHotkey::Control) {
        result += QString::fromUtf8("⌃");
    }
    if (binding.modifiers & MacHotkey::Option) {
        result += QString::fromUtf8("⌥");
    }
    if (binding.modifiers & MacHotkey::Shift) {
        result += QString::fromUtf8("⇧");
    }
    if (binding.modifiers & MacHotkey::Command) {
        result += QString::fromUtf8("⌘");
    }
    result += keyName(binding.keyCode);
    return result;
}
}
