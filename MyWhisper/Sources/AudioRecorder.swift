import AVFoundation
import Combine

class AudioRecorder: NSObject, ObservableObject {
    private var audioRecorder: AVAudioRecorder?
    private var timerCancellable: AnyCancellable?
    
    @Published var isRecording = false
    @Published var audioLevels: [Float] = Array(repeating: 0.1, count: 5)
    
    var audioFileURL: URL?
    
    func startRecording() {
        let tempDir = FileManager.default.temporaryDirectory
        audioFileURL = tempDir.appendingPathComponent(UUID().uuidString + ".wav")

        let settings: [String: Any] = [
            AVFormatIDKey: Int(kAudioFormatLinearPCM),
            AVSampleRateKey: 16_000,
            AVNumberOfChannelsKey: 1,
            AVLinearPCMBitDepthKey: 16,
            AVLinearPCMIsBigEndianKey: false,
            AVLinearPCMIsFloatKey: false,
            AVLinearPCMIsNonInterleaved: false,
            AVEncoderAudioQualityKey: AVAudioQuality.high.rawValue
        ]
        
        do {
            audioRecorder = try AVAudioRecorder(url: audioFileURL!, settings: settings)
            audioRecorder?.isMeteringEnabled = true
            audioRecorder?.record(forDuration: 1800) // max 30 mins
            isRecording = true
            
            timerCancellable = Timer.publish(every: 0.05, on: .main, in: .common)
                .autoconnect()
                .sink { [weak self] _ in
                    self?.updateMeters()
                }
        } catch {
            print("Recording failed: \(error)")
        }
    }
    
    private func updateMeters() {
        guard let recorder = audioRecorder, recorder.isRecording else { return }
        recorder.updateMeters()
        
        let power = recorder.averagePower(forChannel: 0)
        // Convert power from -160..0 dB to 0..1 scale roughly
        let level: Float
        if power < -60 {
            level = 0.05
        } else {
            level = max(0.05, (power + 60) / 60)
        }
        
        var newLevels = audioLevels
        newLevels.removeFirst()
        newLevels.append(level)
        audioLevels = newLevels
    }
    
    func stopRecording() {
        timerCancellable?.cancel()
        timerCancellable = nil
        audioRecorder?.stop()
        isRecording = false
    }
    
    func abortRecording() {
        timerCancellable?.cancel()
        timerCancellable = nil
        audioRecorder?.stop()
        isRecording = false
        if let url = audioFileURL {
            try? FileManager.default.removeItem(at: url)
        }
        audioFileURL = nil
    }
}
