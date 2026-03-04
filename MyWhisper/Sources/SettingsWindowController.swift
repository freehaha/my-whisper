import Cocoa
import SwiftUI
import Carbon

class SettingsWindowController: NSWindowController {
    static let shared = SettingsWindowController()

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 460, height: 210),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )

        window.title = "MyWhisper Settings"
        window.isReleasedWhenClosed = false
        window.center()

        super.init(window: window)
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func show() {
        let config = Config.load()
        let settingsView = SettingsView(
            initialToggleHotkey: config.toggleHotkey,
            initialAbortHotkey: config.abortHotkey
        )

        window?.contentView = NSHostingView(rootView: settingsView)
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}

private enum CaptureTarget {
    case toggle
    case abort
}

struct SettingsView: View {
    @State private var toggleHotkey: HotkeyBinding
    @State private var abortHotkey: HotkeyBinding
    @State private var captureTarget: CaptureTarget?
    @State private var keyboardMonitor: Any?
    @State private var message: String?
    @State private var isError = false

    init(initialToggleHotkey: HotkeyBinding, initialAbortHotkey: HotkeyBinding) {
        _toggleHotkey = State(initialValue: initialToggleHotkey)
        _abortHotkey = State(initialValue: initialAbortHotkey)
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("Hotkeys")
                .font(.title3)
                .bold()

            hotkeyRow(title: "Start / Stop Recording", hotkey: toggleHotkey, target: .toggle)
            hotkeyRow(title: "Abort Recording", hotkey: abortHotkey, target: .abort)

            Text(captureTarget == nil ? "Use at least one modifier key (⌘, ⌥, ⌃, ⇧)." : "Press a key combination now (Esc to cancel).")
                .font(.footnote)
                .foregroundColor(.secondary)

            if let message {
                Text(message)
                    .font(.footnote)
                    .foregroundColor(isError ? .red : .green)
            }

            HStack {
                Spacer()
                Button("Close") {
                    NSApp.keyWindow?.close()
                }
                Button("Save") {
                    saveHotkeys()
                }
                .keyboardShortcut(.defaultAction)
            }
        }
        .padding(18)
        .frame(width: 460)
        .onAppear {
            installKeyboardMonitor()
        }
        .onDisappear {
            removeKeyboardMonitor()
        }
    }

    @ViewBuilder
    private func hotkeyRow(title: String, hotkey: HotkeyBinding, target: CaptureTarget) -> some View {
        HStack(spacing: 10) {
            Text(title)
                .frame(width: 170, alignment: .leading)

            Text(captureTarget == target ? "Press shortcut…" : HotkeyFormatter.displayString(for: hotkey))
                .font(.system(.body, design: .monospaced))
                .frame(maxWidth: .infinity, alignment: .leading)

            Button(captureTarget == target ? "Cancel" : "Change") {
                if captureTarget == target {
                    captureTarget = nil
                } else {
                    captureTarget = target
                    message = nil
                }
            }
        }
    }

    private func installKeyboardMonitor() {
        guard keyboardMonitor == nil else {
            return
        }

        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if handleKeyEvent(event) {
                return nil
            }
            return event
        }
    }

    private func removeKeyboardMonitor() {
        if let keyboardMonitor {
            NSEvent.removeMonitor(keyboardMonitor)
            self.keyboardMonitor = nil
        }
    }

    private func handleKeyEvent(_ event: NSEvent) -> Bool {
        guard let captureTarget else {
            return false
        }

        if Int(event.keyCode) == kVK_Escape {
            self.captureTarget = nil
            message = nil
            return true
        }

        guard let captured = HotkeyFormatter.binding(from: event) else {
            isError = true
            message = "Shortcut must include a non-modifier key plus at least one modifier."
            return true
        }

        switch captureTarget {
        case .toggle:
            toggleHotkey = captured
        case .abort:
            abortHotkey = captured
        }

        self.captureTarget = nil
        message = nil
        return true
    }

    private func saveHotkeys() {
        if toggleHotkey == abortHotkey {
            isError = true
            message = "Start/Stop and Abort cannot use the same shortcut."
            return
        }

        guard HotkeyManager.shared.registerHotkeys(toggle: toggleHotkey, abort: abortHotkey) else {
            isError = true
            message = "Unable to register one or both shortcuts. Try a different combination."
            return
        }

        do {
            var config = Config.load()
            config.toggleHotkey = toggleHotkey
            config.abortHotkey = abortHotkey
            try config.save()
            isError = false
            message = "Hotkeys saved."
        } catch {
            isError = true
            message = "Failed to save settings: \(error.localizedDescription)"
        }
    }
}
