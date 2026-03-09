#include "global_hotkey_manager.h"

namespace {
constexpr OSType kToggleSignature = 0x5447474C;  // TGGL
constexpr OSType kAbortSignature = 0x41425254;   // ABRT
constexpr OSType kHistorySignature = 0x48535459; // HSTY
}

GlobalHotkeyManager &GlobalHotkeyManager::instance() {
    static GlobalHotkeyManager manager;
    return manager;
}

GlobalHotkeyManager::GlobalHotkeyManager() = default;

GlobalHotkeyManager::~GlobalHotkeyManager() {
    unregisterHotkeys();
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
    return true;
}

bool GlobalHotkeyManager::registerHotkeys(const HotkeyBinding &toggle, const HotkeyBinding &abort, const HotkeyBinding &history) {
    if (!installEventHandlerIfNeeded()) {
        return false;
    }

    unregisterHotkeys();

    const EventHotKeyID toggleId{ kToggleSignature, 1 };
    const EventHotKeyID abortId{ kAbortSignature, 2 };
    const EventHotKeyID historyId{ kHistorySignature, 3 };

    const OSStatus toggleStatus = RegisterEventHotKey(toggle.keyCode, toggle.modifiers, toggleId, GetApplicationEventTarget(), 0, &m_toggleHotkeyRef);
    const OSStatus abortStatus = RegisterEventHotKey(abort.keyCode, abort.modifiers, abortId, GetApplicationEventTarget(), 0, &m_abortHotkeyRef);
    const OSStatus historyStatus = RegisterEventHotKey(history.keyCode, history.modifiers, historyId, GetApplicationEventTarget(), 0, &m_historyHotkeyRef);

    return toggleStatus == noErr && abortStatus == noErr && historyStatus == noErr;
}

bool GlobalHotkeyManager::installEventHandlerIfNeeded() {
    if (m_eventHandlerRef) {
        return true;
    }

    EventTypeSpec eventType;
    eventType.eventClass = kEventClassKeyboard;
    eventType.eventKind = kEventHotKeyPressed;

    return InstallEventHandler(GetApplicationEventTarget(), &GlobalHotkeyManager::handleHotKey, 1, &eventType, this, &m_eventHandlerRef) == noErr;
}

void GlobalHotkeyManager::unregisterHotkeys() {
    if (m_toggleHotkeyRef) {
        UnregisterEventHotKey(m_toggleHotkeyRef);
        m_toggleHotkeyRef = nullptr;
    }
    if (m_abortHotkeyRef) {
        UnregisterEventHotKey(m_abortHotkeyRef);
        m_abortHotkeyRef = nullptr;
    }
    if (m_historyHotkeyRef) {
        UnregisterEventHotKey(m_historyHotkeyRef);
        m_historyHotkeyRef = nullptr;
    }
}

OSStatus GlobalHotkeyManager::handleHotKey(EventHandlerCallRef nextHandler, EventRef event, void *userData) {
    (void)nextHandler;

    auto *manager = static_cast<GlobalHotkeyManager *>(userData);
    EventHotKeyID hotKeyId{};
    const OSStatus status = GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr,
                                              sizeof(EventHotKeyID), nullptr, &hotKeyId);
    if (status != noErr || !manager) {
        return noErr;
    }

    switch (hotKeyId.id) {
    case 1:
        if (manager->m_toggleHandler) {
            manager->m_toggleHandler();
        }
        break;
    case 2:
        if (manager->m_abortHandler) {
            manager->m_abortHandler();
        }
        break;
    case 3:
        if (manager->m_historyHandler) {
            manager->m_historyHandler();
        }
        break;
    default:
        break;
    }

    return noErr;
}
