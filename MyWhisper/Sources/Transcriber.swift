import Foundation
import whisper

final class Transcriber {
    private let runtime = WhisperRuntime.shared

    func transcribe(audioURL: URL, config: Config) async throws -> String {
        switch config.transcriptionBackend {
        case .deepgram:
            return try await transcribeWithDeepgram(audioURL: audioURL, config: config)
        case .localWhisper:
            return try await transcribeLocally(audioURL: audioURL, config: config)
        }
    }

    private func transcribeWithDeepgram(audioURL: URL, config: Config) async throws -> String {
        guard config.hasValidDeepgramKey else {
            throw TranscriberError.invalidDeepgramKey
        }

        let audioData = try Data(contentsOf: audioURL)

        var request = URLRequest(url: deepgramURL(for: config))
        request.httpMethod = "POST"
        request.addValue("Token \(config.deepgramApiKey)", forHTTPHeaderField: "Authorization")
        request.addValue("audio/wav", forHTTPHeaderField: "Content-Type")
        request.httpBody = audioData

        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
            let errorMsg = String(data: data, encoding: .utf8) ?? "Unknown Error"
            throw TranscriberError.deepgramAPI(errorMsg)
        }

        let json = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        let results = json?["results"] as? [String: Any]
        let channels = results?["channels"] as? [[String: Any]]
        let alternatives = channels?.first?["alternatives"] as? [[String: Any]]
        return (alternatives?.first?["transcript"] as? String ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
    }

    private func transcribeLocally(audioURL: URL, config: Config) async throws -> String {
        let modelURL = try resolveModelURL(for: config)
        let samples = try WAVPCM16Loader.loadSamples(from: audioURL)

        guard !samples.isEmpty else {
            throw TranscriberError.emptyAudio
        }

        return try await runtime.transcribe(samples: samples, modelURL: modelURL, config: config)
    }

    private func deepgramURL(for config: Config) -> URL {
        var components = URLComponents(string: "https://api.deepgram.com/v1/listen")!
        var queryItems = [
            URLQueryItem(name: "model", value: "nova-3"),
            URLQueryItem(name: "smart_format", value: "true")
        ]

        queryItems += config.deepgramKeywords.map {
            URLQueryItem(name: "keyterm", value: $0)
        }

        components.queryItems = queryItems
        return components.url!
    }

    private func resolveModelURL(for config: Config) throws -> URL {
        if let configuredPath = config.normalizedWhisperModelPath {
            let expandedPath = NSString(string: configuredPath).expandingTildeInPath
            let candidateURL = URL(fileURLWithPath: expandedPath)

            guard FileManager.default.fileExists(atPath: candidateURL.path) else {
                throw TranscriberError.missingConfiguredModel(candidateURL.path)
            }

            return candidateURL
        }

        let executableDirectory = Bundle.main.executableURL?.deletingLastPathComponent()
        let resourceRoots = [
            Bundle.main.resourceURL,
            executableDirectory?.deletingLastPathComponent().appendingPathComponent("Resources"),
            executableDirectory?.appendingPathComponent("MyWhisper_MyWhisper.bundle"),
            executableDirectory?.deletingLastPathComponent().appendingPathComponent("Resources/MyWhisper_MyWhisper.bundle"),
            URL(fileURLWithPath: FileManager.default.currentDirectoryPath).appendingPathComponent("Resources")
        ].compactMap { $0 }

        let preferredNames = [
            "ggml-base.en.bin",
            "ggml-base.bin",
            "ggml-small.en.bin",
            "ggml-small.bin",
            "ggml-medium.en.bin",
            "ggml-medium.bin",
            "ggml-large-v3.bin",
            "ggml-large-v3-turbo.bin"
        ]

        var discoveredModels: [URL] = []

        for root in resourceRoots {
            let whisperRoot = root.lastPathComponent == "Whisper" ? root : root.appendingPathComponent("Whisper")
            guard let enumerator = FileManager.default.enumerator(at: whisperRoot, includingPropertiesForKeys: nil) else {
                continue
            }

            for case let fileURL as URL in enumerator {
                guard fileURL.lastPathComponent.hasPrefix("ggml-"), fileURL.pathExtension == "bin" else {
                    continue
                }
                discoveredModels.append(fileURL)
            }
        }

        for preferredName in preferredNames {
            if let match = discoveredModels.first(where: { $0.lastPathComponent == preferredName }) {
                return match
            }
        }

        if let firstModel = discoveredModels.sorted(by: { $0.path < $1.path }).first {
            return firstModel
        }

        throw TranscriberError.missingBundledModel
    }
}

private actor WhisperRuntime {
    static let shared = WhisperRuntime()

    private var loadedModelPath: String?
    private var loadedUseGPU = true
    private var context: OpaquePointer?

    deinit {
        if let context {
            whisper_free(context)
        }
    }

    func transcribe(samples: [Float], modelURL: URL, config: Config) throws -> String {
        let context = try loadContext(modelURL: modelURL, useGPU: config.whisperUseGPU)

        var params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY)
        params.n_threads = Int32(max(1, ProcessInfo.processInfo.activeProcessorCount - 2))
        params.translate = false
        params.no_context = true
        params.no_timestamps = true
        params.print_special = false
        params.print_progress = false
        params.print_realtime = false
        params.print_timestamps = false
        params.single_segment = false
        params.detect_language = config.whisperLanguageOrNil == nil
        params.language = nil
        params.initial_prompt = nil

        let prompt = config.deepgramKeywords.isEmpty ? nil : config.deepgramKeywords.joined(separator: ", ")

        let result: Int32 = config.whisperLanguageOrNil.withOptionalCString { languagePointer in
            prompt.withOptionalCString { promptPointer in
                params.language = languagePointer
                params.initial_prompt = promptPointer

                return samples.withUnsafeBufferPointer { buffer in
                    guard let baseAddress = buffer.baseAddress else {
                        return Int32(-1)
                    }

                    return whisper_full(context, params, baseAddress, Int32(buffer.count))
                }
            }
        }

        guard result == 0 else {
            throw TranscriberError.transcriptionFailed(code: Int(result))
        }

        let segmentCount = Int(whisper_full_n_segments(context))
        let transcript = (0..<segmentCount)
            .compactMap { index -> String? in
                guard let segmentText = whisper_full_get_segment_text(context, Int32(index)) else {
                    return nil
                }

                return String(cString: segmentText)
            }
            .joined()
            .trimmingCharacters(in: .whitespacesAndNewlines)

        return transcript
    }

    private func loadContext(modelURL: URL, useGPU: Bool) throws -> OpaquePointer {
        if let context, loadedModelPath == modelURL.path, loadedUseGPU == useGPU {
            return context
        }

        if let context {
            whisper_free(context)
            self.context = nil
        }

        var contextParams = whisper_context_default_params()
        contextParams.use_gpu = useGPU

        guard let newContext = modelURL.path.withCString({ whisper_init_from_file_with_params($0, contextParams) }) else {
            throw TranscriberError.failedToLoadModel(modelURL.path)
        }

        loadedModelPath = modelURL.path
        loadedUseGPU = useGPU
        context = newContext
        return newContext
    }
}

private enum WAVPCM16Loader {
    static func loadSamples(from url: URL) throws -> [Float] {
        let data = try Data(contentsOf: url)

        guard data.count >= 44 else {
            throw TranscriberError.invalidAudioFormat("Audio file too small to be valid WAV PCM data.")
        }

        guard ascii(data, 0, 4) == "RIFF", ascii(data, 8, 4) == "WAVE" else {
            throw TranscriberError.invalidAudioFormat("Expected RIFF/WAVE PCM audio.")
        }

        var offset = 12
        var channelCount: UInt16?
        var sampleRate: UInt32?
        var bitsPerSample: UInt16?
        var audioFormat: UInt16?
        var audioDataRange: Range<Int>?

        while offset + 8 <= data.count {
            let chunkID = ascii(data, offset, 4)
            let chunkSize = Int(littleEndianUInt32(data, offset + 4))
            let chunkDataStart = offset + 8
            let paddedChunkSize = chunkSize + (chunkSize % 2)

            guard chunkDataStart + chunkSize <= data.count else {
                throw TranscriberError.invalidAudioFormat("Corrupt WAV chunk size.")
            }

            if chunkID == "fmt " {
                guard chunkSize >= 16 else {
                    throw TranscriberError.invalidAudioFormat("Invalid fmt chunk.")
                }

                audioFormat = littleEndianUInt16(data, chunkDataStart)
                channelCount = littleEndianUInt16(data, chunkDataStart + 2)
                sampleRate = littleEndianUInt32(data, chunkDataStart + 4)
                bitsPerSample = littleEndianUInt16(data, chunkDataStart + 14)
            } else if chunkID == "data" {
                audioDataRange = chunkDataStart..<(chunkDataStart + chunkSize)
            }

            offset = chunkDataStart + paddedChunkSize
        }

        guard audioFormat == 1 else {
            throw TranscriberError.invalidAudioFormat("Expected PCM WAV audio.")
        }

        guard channelCount == 1 else {
            throw TranscriberError.invalidAudioFormat("Expected mono audio.")
        }

        guard sampleRate == 16_000 else {
            throw TranscriberError.invalidAudioFormat("Expected 16 kHz audio.")
        }

        guard bitsPerSample == 16 else {
            throw TranscriberError.invalidAudioFormat("Expected 16-bit PCM audio.")
        }

        guard let audioDataRange else {
            throw TranscriberError.invalidAudioFormat("Missing WAV data chunk.")
        }

        guard audioDataRange.count.isMultiple(of: 2) else {
            throw TranscriberError.invalidAudioFormat("Invalid PCM sample byte count.")
        }

        return data.withUnsafeBytes { rawBuffer in
            let bytes = rawBuffer.bindMemory(to: UInt8.self)
            let sampleCount = audioDataRange.count / 2
            var samples: [Float] = []
            samples.reserveCapacity(sampleCount)

            var index = audioDataRange.lowerBound
            while index < audioDataRange.upperBound {
                let sample = Int16(bitPattern: UInt16(bytes[index]) | (UInt16(bytes[index + 1]) << 8))
                samples.append(Float(sample) / 32768.0)
                index += 2
            }

            return samples
        }
    }

    private static func ascii(_ data: Data, _ offset: Int, _ count: Int) -> String {
        String(decoding: data[offset..<(offset + count)], as: UTF8.self)
    }

    private static func littleEndianUInt16(_ data: Data, _ offset: Int) -> UInt16 {
        UInt16(data[offset]) | (UInt16(data[offset + 1]) << 8)
    }

    private static func littleEndianUInt32(_ data: Data, _ offset: Int) -> UInt32 {
        UInt32(data[offset])
            | (UInt32(data[offset + 1]) << 8)
            | (UInt32(data[offset + 2]) << 16)
            | (UInt32(data[offset + 3]) << 24)
    }
}

private enum TranscriberError: LocalizedError {
    case invalidDeepgramKey
    case deepgramAPI(String)
    case missingConfiguredModel(String)
    case missingBundledModel
    case failedToLoadModel(String)
    case invalidAudioFormat(String)
    case emptyAudio
    case transcriptionFailed(code: Int)

    var errorDescription: String? {
        switch self {
        case .invalidDeepgramKey:
            return "Invalid Deepgram API key. Set it in Settings or ~/.config/my-whisper/config.json."
        case .deepgramAPI(let message):
            return "Deepgram API error: \(message)"
        case .missingConfiguredModel(let path):
            return "Whisper model not found at \(path)."
        case .missingBundledModel:
            return "No whisper model found. Set whisperModelPath in ~/.config/my-whisper/config.json or bundle a ggml-*.bin model in Resources/Whisper."
        case .failedToLoadModel(let path):
            return "Failed to load whisper model at \(path)."
        case .invalidAudioFormat(let message):
            return message
        case .emptyAudio:
            return "Recorded audio was empty."
        case .transcriptionFailed(let code):
            return "whisper.cpp transcription failed with code \(code)."
        }
    }
}

private extension Optional where Wrapped == String {
    func withOptionalCString<Result>(_ body: (UnsafePointer<CChar>?) throws -> Result) rethrows -> Result {
        switch self {
        case .some(let value):
            return try value.withCString(body)
        case .none:
            return try body(nil)
        }
    }
}
