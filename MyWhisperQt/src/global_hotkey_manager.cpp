#include "global_hotkey_manager.h"

#if defined(Q_OS_LINUX)
#include <QGuiApplication>
#include <QSocketNotifier>

#include <X11/Xlib.h>
#include <X11/extensions/XTest.h>
#include <X11/keysym.h>

#include <array>

namespace {
KeySym keySymForHotkey(quint32 keyCode) {
    switch (keyCode) {
    case MacHotkey::KeyA: return XK_a;
    case MacHotkey::KeyB: return XK_b;
    case MacHotkey::KeyC: return XK_c;
    case MacHotkey::KeyD: return XK_d;
    case MacHotkey::KeyE: return XK_e;
    case MacHotkey::KeyF: return XK_f;
    case MacHotkey::KeyG: return XK_g;
    case MacHotkey::KeyH: return XK_h;
    case MacHotkey::KeyI: return XK_i;
    case MacHotkey::KeyJ: return XK_j;
    case MacHotkey::KeyK: return XK_k;
    case MacHotkey::KeyL: return XK_l;
    case MacHotkey::KeyM: return XK_m;
    case MacHotkey::KeyN: return XK_n;
    case MacHotkey::KeyO: return XK_o;
    case MacHotkey::KeyP: return XK_p;
    case MacHotkey::KeyQ: return XK_q;
    case MacHotkey::KeyR: return XK_r;
    case MacHotkey::KeyS: return XK_s;
    case MacHotkey::KeyT: return XK_t;
    case MacHotkey::KeyU: return XK_u;
    case MacHotkey::KeyV: return XK_v;
    case MacHotkey::KeyW: return XK_w;
    case MacHotkey::KeyX: return XK_x;
    case MacHotkey::KeyY: return XK_y;
    case MacHotkey::KeyZ: return XK_z;

    case MacHotkey::Key0: return XK_0;
    case MacHotkey::Key1: return XK_1;
    case MacHotkey::Key2: return XK_2;
    case MacHotkey::Key3: return XK_3;
    case MacHotkey::Key4: return XK_4;
    case MacHotkey::Key5: return XK_5;
    case MacHotkey::Key6: return XK_6;
    case MacHotkey::Key7: return XK_7;
    case MacHotkey::Key8: return XK_8;
    case MacHotkey::Key9: return XK_9;

    case MacHotkey::Space: return XK_space;
    case MacHotkey::Return: return XK_Return;
    case MacHotkey::KeypadEnter: return XK_KP_Enter;
    case MacHotkey::Escape: return XK_Escape;
    case MacHotkey::Tab: return XK_Tab;
    case MacHotkey::Delete: return XK_BackSpace;
    case MacHotkey::ForwardDelete: return XK_Delete;
    case MacHotkey::LeftArrow: return XK_Left;
    case MacHotkey::RightArrow: return XK_Right;
    case MacHotkey::UpArrow: return XK_Up;
    case MacHotkey::DownArrow: return XK_Down;
    case MacHotkey::F1: return XK_F1;
    case MacHotkey::F2: return XK_F2;
    case MacHotkey::F3: return XK_F3;
    case MacHotkey::F4: return XK_F4;
    case MacHotkey::F5: return XK_F5;
    case MacHotkey::F6: return XK_F6;
    case MacHotkey::F7: return XK_F7;
    case MacHotkey::F8: return XK_F8;
    case MacHotkey::F9: return XK_F9;
    case MacHotkey::F10: return XK_F10;
    case MacHotkey::F11: return XK_F11;
    case MacHotkey::F12: return XK_F12;
    default:
        return NoSymbol;
    }
}

unsigned int x11ModifiersFromBinding(quint32 modifiers) {
    unsigned int result = 0;
    if (modifiers & MacHotkey::Shift) {
        result |= ShiftMask;
    }
    if (modifiers & MacHotkey::Control) {
        result |= ControlMask;
    }
    if (modifiers & MacHotkey::Option) {
        result |= Mod1Mask;
    }
    if (modifiers & MacHotkey::Command) {
        result |= Mod4Mask;
    }
    return result;
}

std::array<unsigned int, 4> ignoredModifierVariants() {
    return {0u, LockMask, Mod2Mask, static_cast<unsigned int>(LockMask | Mod2Mask)};
}

KeyCode keyCodeForBinding(Display *display, const HotkeyBinding &binding) {
    const KeySym keySym = keySymForHotkey(binding.keyCode);
    if (keySym == NoSymbol) {
        return 0;
    }
    return XKeysymToKeycode(display, keySym);
}

bool hotkeyMatches(Display *display, const HotkeyBinding &binding, unsigned int eventKeycode, unsigned int eventState) {
    const KeyCode bindingKeyCode = keyCodeForBinding(display, binding);
    if (bindingKeyCode == 0) {
        return false;
    }

    const unsigned int normalizedState = eventState & ~(LockMask | Mod2Mask);
    return bindingKeyCode == eventKeycode && normalizedState == x11ModifiersFromBinding(binding.modifiers);
}

bool grabHotkey(Display *display, unsigned long rootWindow, const HotkeyBinding &binding) {
    const KeyCode keycode = keyCodeForBinding(display, binding);
    if (keycode == 0) {
        return false;
    }

    const unsigned int modifiers = x11ModifiersFromBinding(binding.modifiers);
    for (const unsigned int variant : ignoredModifierVariants()) {
        XGrabKey(display, static_cast<int>(keycode), modifiers | variant,
                 static_cast<Window>(rootWindow), False, GrabModeAsync, GrabModeAsync);
    }
    return true;
}

void ungrabHotkey(Display *display, unsigned long rootWindow, const HotkeyBinding &binding) {
    const KeyCode keycode = keyCodeForBinding(display, binding);
    if (keycode == 0) {
        return;
    }

    const unsigned int modifiers = x11ModifiersFromBinding(binding.modifiers);
    for (const unsigned int variant : ignoredModifierVariants()) {
        XUngrabKey(display, static_cast<int>(keycode), modifiers | variant, static_cast<Window>(rootWindow));
    }
}

int g_grabError = 0;
int x11GrabErrorHandler(Display *, XErrorEvent *) {
    g_grabError = 1;
    return 0;
}
}
#endif

GlobalHotkeyManager &GlobalHotkeyManager::instance() {
    static GlobalHotkeyManager manager;
    return manager;
}

GlobalHotkeyManager::GlobalHotkeyManager() {
#if defined(Q_OS_LINUX)
    if (!QGuiApplication::platformName().contains(QStringLiteral("xcb"), Qt::CaseInsensitive)) {
        return;
    }

    m_display = XOpenDisplay(nullptr);
    if (!m_display) {
        return;
    }

    m_rootWindow = static_cast<unsigned long>(DefaultRootWindow(m_display));
    m_notifier = new QSocketNotifier(ConnectionNumber(m_display), QSocketNotifier::Read);
    QObject::connect(m_notifier, &QSocketNotifier::activated, m_notifier, [this](int) {
        processX11Events();
    });
#endif
}

GlobalHotkeyManager::~GlobalHotkeyManager() {
#if defined(Q_OS_LINUX)
    unregisterHotkeys();
    delete m_notifier;
    m_notifier = nullptr;
    if (m_display) {
        XCloseDisplay(m_display);
        m_display = nullptr;
    }
#endif
}

void GlobalHotkeyManager::setToggleHandler(std::function<void()> handler) {
    m_toggleHandler = std::move(handler);
}

void GlobalHotkeyManager::setAbortHandler(std::function<void()> handler) {
    m_abortHandler = std::move(handler);
}

void GlobalHotkeyManager::setHistoryHandler(std::function<void()> handler) {
    m_historyHandler = std::move(handler);
}

bool GlobalHotkeyManager::isSupported() const {
#if defined(Q_OS_LINUX)
    return m_display != nullptr;
#else
    return false;
#endif
}

bool GlobalHotkeyManager::registerHotkeys(const HotkeyBinding &toggle, const HotkeyBinding &abort, const HotkeyBinding &history) {
#if defined(Q_OS_LINUX)
    if (!m_display) {
        return false;
    }

    unregisterHotkeys();
    m_toggleBinding = toggle;
    m_abortBinding = abort;
    m_historyBinding = history;

    g_grabError = 0;
    auto previousHandler = XSetErrorHandler(x11GrabErrorHandler);

    const bool toggleOk = grabHotkey(m_display, m_rootWindow, toggle);
    const bool abortOk = grabHotkey(m_display, m_rootWindow, abort);
    const bool historyOk = grabHotkey(m_display, m_rootWindow, history);

    XSync(m_display, False);
    XSetErrorHandler(previousHandler);

    if (!toggleOk || !abortOk || !historyOk || g_grabError != 0) {
        unregisterHotkeys();
        return false;
    }

    return true;
#else
    (void)toggle;
    (void)abort;
    (void)history;
    return false;
#endif
}

#if defined(Q_OS_LINUX)
void GlobalHotkeyManager::unregisterHotkeys() {
    if (!m_display) {
        return;
    }

    ungrabHotkey(m_display, m_rootWindow, m_toggleBinding);
    ungrabHotkey(m_display, m_rootWindow, m_abortBinding);
    ungrabHotkey(m_display, m_rootWindow, m_historyBinding);
    XSync(m_display, False);
}

void GlobalHotkeyManager::processX11Events() {
    if (!m_display) {
        return;
    }

    while (XPending(m_display) > 0) {
        XEvent event{};
        XNextEvent(m_display, &event);
        if (event.type != KeyPress) {
            continue;
        }

        const auto &keyEvent = event.xkey;
        if (hotkeyMatches(m_display, m_toggleBinding, keyEvent.keycode, keyEvent.state)) {
            if (m_toggleHandler) {
                m_toggleHandler();
            }
            continue;
        }

        if (hotkeyMatches(m_display, m_abortBinding, keyEvent.keycode, keyEvent.state)) {
            if (m_abortHandler) {
                m_abortHandler();
            }
            continue;
        }

        if (hotkeyMatches(m_display, m_historyBinding, keyEvent.keycode, keyEvent.state)) {
            if (m_historyHandler) {
                m_historyHandler();
            }
        }
    }
}
#endif
