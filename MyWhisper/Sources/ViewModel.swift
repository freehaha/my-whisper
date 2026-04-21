import Foundation
import Cocoa
import Combine

class ViewModel: ObservableObject, @unchecked Sendable {
    static let shared = ViewModel()

    private let recorder = AudioRecorder()
    private let transcriber = Transcriber()
    private let refiner = LLMRefiner()

    @Published var audioLevels: [Float] = Array(repeating: 0.1, count: 5)
    @Published var state: AppState = .idle

    private var cancellables = Set<AnyCancellable>()

    init() {
        recorder.$audioLevels
            .receive(on: RunLoop.main)
            .sink { [weak self] levels in
                self?.audioLevels = levels
            }
            .store(in: &cancellables)

        HotkeyManager.shared.onToggle = { [weak self] in
            self?.toggleRecording()
        }

        HotkeyManager.shared.onAbort = { [weak self] in
            self?.abortRecording()
        }

        HotkeyManager.shared.onShowHistory = {
            DispatchQueue.main.async {
                HistoryWindowController.shared.show()
            }
        }

        HotkeyManager.shared.registerHotkeysFromConfig()
    }

    func toggleRecording() {
        if recorder.isRecording {
            stopAndProcess()
        } else {
            startRecording()
        }
    }

    func startRecording() {
        state = .recording
        SoundManager.shared.playStart()
        recorder.startRecording()
    }

    func abortRecording() {
        guard recorder.isRecording else { return }
        recorder.abortRecording()
        state = .idle
        SoundManager.shared.playStop()
    }

    func stopAndProcess() {
        recorder.stopRecording()
        SoundManager.shared.playStop()

        guard let url = recorder.audioFileURL else {
            state = .error("No audio file")
            resetStateAfterDelay()
            return
        }

        let config = Config.load()
        state = .transcribing

        Task {
            do {
                let transcript = try await transcriber.transcribe(audioURL: url, config: config)
                if transcript.isEmpty {
                    DispatchQueue.main.async {
                        self.state = .error("Empty transcript")
                        self.resetStateAfterDelay()
                    }
                    return
                }

                let finalText: String
                if config.hasUsableRefiner {
                    DispatchQueue.main.async { self.state = .refining }
                    finalText = try await refiner.refine(text: transcript, config: config)
                } else {
                    finalText = transcript
                }

                DispatchQueue.main.async {
                    self.state = .done
                    SoundManager.shared.playSuccess()
                    TranscriptionHistoryStore.shared.add(finalText)
                    Paster.paste(text: finalText)
                    self.resetStateAfterDelay()
                }
            } catch {
                DispatchQueue.main.async {
                    self.state = .error(error.localizedDescription)
                    SoundManager.shared.playError()
                    self.resetStateAfterDelay()
                }
            }
        }
    }

    private func resetStateAfterDelay() {
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            self.state = .idle
        }
    }
}
