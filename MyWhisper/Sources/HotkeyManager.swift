import Cocoa
import Carbon

class HotkeyManager {
    static let shared = HotkeyManager()

    var onToggle: (() -> Void)?
    var onAbort: (() -> Void)?
    var onShowHistory: (() -> Void)?

    private var toggleHotKeyRef: EventHotKeyRef?
    private var abortHotKeyRef: EventHotKeyRef?
    private var historyHotKeyRef: EventHotKeyRef?
    private var eventHandlerRef: EventHandlerRef?

    private let toggleID = EventHotKeyID(signature: OSType(0x5447474C), id: 1) // TGGL
    private let abortID = EventHotKeyID(signature: OSType(0x41425254), id: 2)  // ABRT
    private let historyID = EventHotKeyID(signature: OSType(0x48535459), id: 3) // HSTY

    @discardableResult
    func registerHotkeys(toggle: HotkeyBinding, abort: HotkeyBinding, history: HotkeyBinding) -> Bool {
        installEventHandlerIfNeeded()

        unregisterHotkeys()

        let toggleStatus = RegisterEventHotKey(
            toggle.keyCode,
            toggle.modifiers,
            toggleID,
            GetApplicationEventTarget(),
            0,
            &toggleHotKeyRef
        )

        let abortStatus = RegisterEventHotKey(
            abort.keyCode,
            abort.modifiers,
            abortID,
            GetApplicationEventTarget(),
            0,
            &abortHotKeyRef
        )

        let historyStatus = RegisterEventHotKey(
            history.keyCode,
            history.modifiers,
            historyID,
            GetApplicationEventTarget(),
            0,
            &historyHotKeyRef
        )

        if toggleStatus != noErr || abortStatus != noErr || historyStatus != noErr {
            print("Failed to register hotkeys. Toggle status: \(toggleStatus), Abort status: \(abortStatus), History status: \(historyStatus)")
            return false
        }

        return true
    }

    @discardableResult
    func registerHotkeysFromConfig() -> Bool {
        let config = Config.load()
        return registerHotkeys(toggle: config.toggleHotkey, abort: config.abortHotkey, history: config.historyHotkey)
    }

    private func installEventHandlerIfNeeded() {
        guard eventHandlerRef == nil else {
            return
        }

        let eventHandler: EventHandlerUPP = { (_, eventRef, _) -> OSStatus in
            var hotKeyID = EventHotKeyID()
            let status = GetEventParameter(
                eventRef,
                EventParamName(kEventParamDirectObject),
                EventParamType(typeEventHotKeyID),
                nil,
                MemoryLayout<EventHotKeyID>.size,
                nil,
                &hotKeyID
            )

            if status == noErr {
                switch hotKeyID.id {
                case 1:
                    HotkeyManager.shared.onToggle?()
                case 2:
                    HotkeyManager.shared.onAbort?()
                case 3:
                    HotkeyManager.shared.onShowHistory?()
                default:
                    break
                }
            }

            return noErr
        }

        var eventType = EventTypeSpec(
            eventClass: OSType(kEventClassKeyboard),
            eventKind: UInt32(kEventHotKeyPressed)
        )

        InstallEventHandler(
            GetApplicationEventTarget(),
            eventHandler,
            1,
            &eventType,
            nil,
            &eventHandlerRef
        )
    }

    private func unregisterHotkeys() {
        if let toggleHotKeyRef {
            UnregisterEventHotKey(toggleHotKeyRef)
            self.toggleHotKeyRef = nil
        }

        if let abortHotKeyRef {
            UnregisterEventHotKey(abortHotKeyRef)
            self.abortHotKeyRef = nil
        }

        if let historyHotKeyRef {
            UnregisterEventHotKey(historyHotKeyRef)
            self.historyHotKeyRef = nil
        }
    }
}
