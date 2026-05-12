import Cocoa
import SwiftUI
import Carbon

class SettingsWindowController: NSWindowController {
    static let shared = SettingsWindowController()

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 560, height: 840),
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
    @State private var transcriptionBackend: TranscriptionBackend
    @State private var deepgramApiKey: String
    @State private var assemblyAiApiKey: String
    @State private var assemblyAiSpeechModel: String
    @State private var whisperModelPath: String
    @State private var whisperLanguage: String
    @State private var whisperUseGPU: Bool
    @State private var vocabularyHintsText: String
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
        _transcriptionBackend = State(initialValue: initialConfig.transcriptionBackend)
        _deepgramApiKey = State(initialValue: initialConfig.deepgramApiKey)
        _assemblyAiApiKey = State(initialValue: initialConfig.assemblyAiApiKey)
        _assemblyAiSpeechModel = State(initialValue: initialConfig.normalizedAssemblyAiSpeechModel)
        _whisperModelPath = State(initialValue: initialConfig.normalizedWhisperModelPath ?? "")
        _whisperLanguage = State(initialValue: initialConfig.normalizedWhisperLanguage)
        _whisperUseGPU = State(initialValue: initialConfig.whisperUseGPU)
        _vocabularyHintsText = State(initialValue: initialConfig.deepgramKeywords.joined(separator: "\n"))
        _enableRefinement = State(initialValue: initialConfig.enableRefinement)
        _openaiApiKey = State(initialValue: initialConfig.openaiApiKey ?? "")
        _refinementPrompt = State(initialValue: initialConfig.refinementPrompt ?? Config.defaultRefinementPrompt)
        _toggleHotkey = State(initialValue: initialConfig.toggleHotkey)
        _abortHotkey = State(initialValue: initialConfig.abortHotkey)
        _historyHotkey = State(initialValue: initialConfig.historyHotkey)
    }

    private var backendDescription: String {
        switch transcriptionBackend {
        case .deepgram:
            return "Cloud transcription via Deepgram API after recording stops."
        case .assemblyAI:
            return "Cloud transcription via AssemblyAI streaming for faster turn-around."
        case .localWhisper:
            return "On-device transcription via whisper.cpp XCFramework."
        }
    }

    var body: some View {
        ScrollView {
            VStack(alignment: .leading, spacing: 14) {
                GroupBox("Transcription Backend") {
                    VStack(alignment: .leading, spacing: 12) {
                        Picker("Backend", selection: $transcriptionBackend) {
                            ForEach(TranscriptionBackend.allCases) { backend in
                                Text(backend.displayName).tag(backend)
                            }
                        }
                        .pickerStyle(.segmented)

                        Text(backendDescription)
                            .font(.footnote)
                            .foregroundColor(.secondary)
                    }
                    .padding(.top, 6)
                }

                if transcriptionBackend == .deepgram {
                    GroupBox("Deepgram") {
                        VStack(alignment: .leading, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("Deepgram API key")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                SecureField("YOUR_DEEPGRAM_API_KEY", text: $deepgramApiKey)
                            }

                            vocabularyHintsSection(
                                title: "Deepgram keyterms",
                                placeholder: "One keyterm/phrase per line. Example:\nAcmeCloud\nMyWhisper\nGPU",
                                footnote: "Sent as Deepgram keyterms to bias recognition toward product names and jargon."
                            )
                        }
                        .padding(.top, 6)
                    }
                } else if transcriptionBackend == .assemblyAI {
                    GroupBox("AssemblyAI Streaming") {
                        VStack(alignment: .leading, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("AssemblyAI API key")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                SecureField("YOUR_ASSEMBLYAI_API_KEY", text: $assemblyAiApiKey)
                            }

                            VStack(alignment: .leading, spacing: 6) {
                                Text("Speech model")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                TextField("u3-rt-pro", text: $assemblyAiSpeechModel)
                                    .textFieldStyle(.roundedBorder)
                                Text("Default: u3-rt-pro")
                                    .font(.footnote)
                                    .foregroundColor(.secondary)
                            }
                        }
                        .padding(.top, 6)
                    }
                } else {
                    GroupBox("Local Whisper") {
                        VStack(alignment: .leading, spacing: 12) {
                            VStack(alignment: .leading, spacing: 6) {
                                Text("Whisper model path")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                TextField("Auto-detect bundled ggml-*.bin model", text: $whisperModelPath)
                                    .textFieldStyle(.roundedBorder)
                                Text("Leave empty to auto-detect a bundled model in Resources/Whisper.")
                                    .font(.footnote)
                                    .foregroundColor(.secondary)
                            }

                            VStack(alignment: .leading, spacing: 6) {
                                Text("Language")
                                    .font(.subheadline)
                                    .foregroundColor(.secondary)
                                TextField("en or auto", text: $whisperLanguage)
                                    .textFieldStyle(.roundedBorder)
                                Text("Use ISO code like en, de, fr. Use auto for language detection.")
                                    .font(.footnote)
                                    .foregroundColor(.secondary)
                            }

                            Toggle("Use GPU when available", isOn: $whisperUseGPU)

                            vocabularyHintsSection(
                                title: "Vocabulary hints",
                                placeholder: "Optional prompt bias. One phrase per line. Example:\nAcmeCloud\nMyWhisper\nGPU",
                                footnote: "Passed to whisper.cpp as initial_prompt to bias recognition toward product names and jargon."
                            )
                        }
                        .padding(.top, 6)
                    }
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
        .frame(width: 560, height: 840)
        .onAppear {
            installKeyboardMonitor()
        }
        .onDisappear {
            removeKeyboardMonitor()
        }
    }

    @ViewBuilder
    private func vocabularyHintsSection(title: String, placeholder: String, footnote: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(title)
                .font(.subheadline)
                .foregroundColor(.secondary)
            MultilineInput(
                text: $vocabularyHintsText,
                placeholder: placeholder
            )
            .frame(height: 92)
            Text(footnote)
                .font(.footnote)
                .foregroundColor(.secondary)
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
            config.transcriptionBackend = transcriptionBackend
            config.deepgramApiKey = deepgramApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
            config.assemblyAiApiKey = assemblyAiApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
            config.assemblyAiSpeechModel = Config.normalizeAssemblyAiSpeechModel(assemblyAiSpeechModel)
            config.whisperModelPath = Config.normalizeOptionalPath(whisperModelPath)
            config.whisperLanguage = Config.normalizeLanguage(whisperLanguage)
            config.whisperUseGPU = whisperUseGPU
            config.deepgramKeywords = Config.normalizeKeywords(fromMultilineText: vocabularyHintsText)
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
