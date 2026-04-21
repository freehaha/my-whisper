import Cocoa
import SwiftUI
import Carbon

class SettingsWindowController: NSWindowController {
    static let shared = SettingsWindowController()

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 680),
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
        let settingsView = SettingsView(initialConfig: config)

        window?.contentView = NSHostingView(rootView: settingsView)
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}

private enum CaptureTarget {
    case toggle
    case abort
    case history
}

struct SettingsView: View {
    @State private var deepgramApiKey: String
    @State private var deepgramKeywordsText: String
    @State private var enableRefinement: Bool
    @State private var openaiApiKey: String
    @State private var refinementPrompt: String
    @State private var toggleHotkey: HotkeyBinding
    @State private var abortHotkey: HotkeyBinding
    @State private var historyHotkey: HotkeyBinding
    @State private var captureTarget: CaptureTarget?
    @State private var keyboardMonitor: Any?
    @State private var message: String?
    @State private var isError = false

    init(initialConfig: Config) {
        _deepgramApiKey = State(initialValue: initialConfig.deepgramApiKey)
        _deepgramKeywordsText = State(initialValue: initialConfig.deepgramKeywords.joined(separator: "\n"))
        _enableRefinement = State(initialValue: initialConfig.enableRefinement)
        _openaiApiKey = State(initialValue: initialConfig.openaiApiKey ?? "")
        _refinementPrompt = State(initialValue: initialConfig.refinementPrompt ?? Config.defaultRefinementPrompt)
        _toggleHotkey = State(initialValue: initialConfig.toggleHotkey)
        _abortHotkey = State(initialValue: initialConfig.abortHotkey)
        _historyHotkey = State(initialValue: initialConfig.historyHotkey)
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                GroupBox("API Settings") {
                    VStack(alignment: .leading, spacing: 12) {
                        VStack(alignment: .leading, spacing: 6) {
                            Text("Deepgram API key")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            SecureField("YOUR_DEEPGRAM_API_KEY", text: $deepgramApiKey)
                        }

                        VStack(alignment: .leading, spacing: 6) {
                            Text("Deepgram keyterms")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            MultilineInput(
                                text: $deepgramKeywordsText,
                                placeholder: "One keyterm/phrase per line. Example:\nAcmeCloud\nMyWhisper\nGPU"
                            )
                            .frame(height: 92)
                        }
                    }
                    .padding(.top, 6)
                }

                GroupBox("Text Refinement") {
                    VStack(alignment: .leading, spacing: 12) {
                        Toggle("Enable OpenAI refinement", isOn: $enableRefinement)

                        VStack(alignment: .leading, spacing: 6) {
                            Text("OpenAI API key")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            SecureField("YOUR_OPENAI_API_KEY", text: $openaiApiKey)
                                .disabled(!enableRefinement)
                        }

                        VStack(alignment: .leading, spacing: 6) {
                            Text("Refinement prompt")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                            MultilineInput(
                                text: $refinementPrompt,
                                placeholder: Config.defaultRefinementPrompt
                            )
                            .frame(height: 92)
                            .disabled(!enableRefinement)
                        }
                    }
                    .padding(.top, 6)
                }

                GroupBox("Hotkeys") {
                    VStack(alignment: .leading, spacing: 12) {
                        hotkeyRow(title: "Start / Stop Recording", hotkey: toggleHotkey, target: .toggle)
                        hotkeyRow(title: "Abort Recording", hotkey: abortHotkey, target: .abort)
                        hotkeyRow(title: "Show History", hotkey: historyHotkey, target: .history)

                        Text(captureTarget == nil ? "Use at least one modifier key (⌘, ⌥, ⌃, ⇧)." : "Press a key combination now (Esc to cancel).")
                            .font(.footnote)
                            .foregroundColor(.secondary)
                    }
                    .padding(.top, 6)
                }

                if let message {
                    Text(message)
                        .font(.footnote)
                        .foregroundColor(isError ? .red : .green)
                }

                Text("Config file: \(Config.configURL.path)")
                    .font(.footnote)
                    .foregroundColor(.secondary)
                    .textSelection(.enabled)

                HStack {
                    Spacer()
                    Button("Close") {
                        NSApp.keyWindow?.close()
                    }
                    Button("Save") {
                        saveSettings()
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
            .padding(18)
        }
        .frame(width: 560, height: 680)
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
        case .history:
            historyHotkey = captured
        }

        self.captureTarget = nil
        message = nil
        return true
    }

    private func saveSettings() {
        if toggleHotkey == abortHotkey || toggleHotkey == historyHotkey || abortHotkey == historyHotkey {
            isError = true
            message = "All shortcuts must be different."
            return
        }

        guard HotkeyManager.shared.registerHotkeys(toggle: toggleHotkey, abort: abortHotkey, history: historyHotkey) else {
            isError = true
            message = "Unable to register one or more shortcuts. Try a different combination."
            return
        }

        do {
            var config = Config.load()
            config.deepgramApiKey = deepgramApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
            config.deepgramKeywords = Config.normalizeKeywords(fromMultilineText: deepgramKeywordsText)
            config.enableRefinement = enableRefinement
            let trimmedOpenAIKey = openaiApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
            config.openaiApiKey = trimmedOpenAIKey.isEmpty ? nil : trimmedOpenAIKey

            let trimmedPrompt = refinementPrompt.trimmingCharacters(in: .whitespacesAndNewlines)
            config.refinementPrompt = trimmedPrompt.isEmpty ? Config.defaultRefinementPrompt : trimmedPrompt

            config.toggleHotkey = toggleHotkey
            config.abortHotkey = abortHotkey
            config.historyHotkey = historyHotkey
            try config.save()
            isError = false
            message = "Settings saved."
        } catch {
            isError = true
            message = "Failed to save settings: \(error.localizedDescription)"
        }
    }
}

private struct MultilineInput: View {
    @Binding var text: String
    let placeholder: String

    var body: some View {
        ZStack(alignment: .topLeading) {
            RoundedRectangle(cornerRadius: 6)
                .fill(Color(nsColor: .textBackgroundColor))
                .overlay(
                    RoundedRectangle(cornerRadius: 6)
                        .stroke(Color.secondary.opacity(0.25), lineWidth: 1)
                )

            if text.isEmpty {
                Text(placeholder)
                    .foregroundColor(.secondary)
                    .padding(.horizontal, 6)
                    .padding(.vertical, 8)
            }

            TextEditor(text: $text)
                .scrollContentBackground(.hidden)
                .padding(4)
        }
    }
}
