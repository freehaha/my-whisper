#pragma once

#include "types.h"

#include <functional>
#include <utility>

class QSocketNotifier;

#if defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#elif defined(Q_OS_LINUX)
typedef struct _XDisplay Display;
#endif

class GlobalHotkeyManager {
public:
    static GlobalHotkeyManager &instance();

    void setToggleHandler(std::function<void()> handler);
    void setAbortHandler(std::function<void()> handler);
    void setHistoryHandler(std::function<void()> handler);

    bool isSupported() const;
    bool registerHotkeys(const HotkeyBinding &toggle, const HotkeyBinding &abort, const HotkeyBinding &history);

private:
    GlobalHotkeyManager();
    ~GlobalHotkeyManager();
    GlobalHotkeyManager(const GlobalHotkeyManager &) = delete;
    GlobalHotkeyManager &operator=(const GlobalHotkeyManager &) = delete;

#if defined(Q_OS_MACOS)
    bool installEventHandlerIfNeeded();
    void unregisterHotkeys();
    static OSStatus handleHotKey(EventHandlerCallRef nextHandler, EventRef event, void *userData);

    EventHotKeyRef m_toggleHotkeyRef = nullptr;
    EventHotKeyRef m_abortHotkeyRef = nullptr;
    EventHotKeyRef m_historyHotkeyRef = nullptr;
    EventHandlerRef m_eventHandlerRef = nullptr;
#elif defined(Q_OS_LINUX)
    void unregisterHotkeys();
    void processX11Events();

    Display *m_display = nullptr;
    unsigned long m_rootWindow = 0;
    QSocketNotifier *m_notifier = nullptr;
    HotkeyBinding m_toggleBinding;
    HotkeyBinding m_abortBinding;
    HotkeyBinding m_historyBinding;
#endif

    std::function<void()> m_toggleHandler;
    std::function<void()> m_abortHandler;
    std::function<void()> m_historyHandler;
};
