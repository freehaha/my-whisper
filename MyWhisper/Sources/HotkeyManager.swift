import Cocoa
import Carbon

class HotkeyManager {
    static let shared = HotkeyManager()
    
    var onToggle: (() -> Void)?
    var onAbort: (() -> Void)?
    
    private var toggleHotKeyRef: EventHotKeyRef?
    private var abortHotKeyRef: EventHotKeyRef?
    
    func registerHotkeys() {
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
                if hotKeyID.id == 1 {
                    HotkeyManager.shared.onToggle?()
                } else if hotKeyID.id == 2 {
                    HotkeyManager.shared.onAbort?()
                }
            }
            
            return noErr
        }
        
        var eventType = EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed))
        InstallEventHandler(GetApplicationEventTarget(), eventHandler, 1, &eventType, nil, nil)
        
        // Register Toggle Hotkey: Cmd+Option+R (KeyCode 15 = R)
        let toggleID = EventHotKeyID(signature: OSType(0x5447474C), id: 1) // TGGL
        RegisterEventHotKey(
            UInt32(15),
            UInt32(cmdKey | optionKey),
            toggleID,
            GetApplicationEventTarget(),
            0,
            &toggleHotKeyRef
        )
        
        // Register Abort Hotkey: Cmd+Option+X (KeyCode 7 = X)
        let abortID = EventHotKeyID(signature: OSType(0x41425254), id: 2) // ABRT
        RegisterEventHotKey(
            UInt32(7),
            UInt32(cmdKey | optionKey),
            abortID,
            GetApplicationEventTarget(),
            0,
            &abortHotKeyRef
        )
    }
}
