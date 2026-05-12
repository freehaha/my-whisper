import Foundation
import Carbon

struct HotkeyBinding: Codable, Equatable {
    var keyCode: UInt32
    var modifiers: UInt32

    static let defaultToggle = HotkeyBinding(
        keyCode: UInt32(kVK_ANSI_R),
        modifiers: UInt32(cmdKey | optionKey)
    )

    static let defaultAbort = HotkeyBinding(
        keyCode: UInt32(kVK_ANSI_X),
        modifiers: UInt32(cmdKey | optionKey)
    )

    static let defaultHistory = HotkeyBinding(
        keyCode: UInt32(kVK_ANSI_H),
        modifiers: UInt32(cmdKey | optionKey)
    )
}

enum TranscriptionBackend: String, Codable, CaseIterable, Identifiable {
    case localWhisper = "localWhisper"
    case deepgram = "deepgram"
    case assemblyAI = "assemblyai"

    var id: String { rawValue }

    var displayName: String {
        switch self {
        case .localWhisper:
            return "Local Whisper"
        case .deepgram:
            return "Deepgram"
        case .assemblyAI:
            return "AssemblyAI Streaming"
        }
    }
}

struct Config: Codable {
    static let defaultRefinementPrompt = "Fix spelling and grammar. Return only the fixed text."
    static let defaultWhisperLanguage = "en"

    var transcriptionBackend: TranscriptionBackend
    var deepgramApiKey: String
    var deepgramKeywords: [String]
    var assemblyAiApiKey: String
    var assemblyAiSpeechModel: String
    var whisperModelPath: String?
    var whisperLanguage: String
    var whisperUseGPU: Bool
    var openaiApiKey: String?
    var enableRefinement: Bool
    var refinementPrompt: String?
    var toggleHotkey: HotkeyBinding
    var abortHotkey: HotkeyBinding
    var historyHotkey: HotkeyBinding

    init(
        transcriptionBackend: TranscriptionBackend = .localWhisper,
        deepgramApiKey: String = "",
        deepgramKeywords: [String] = [],
        assemblyAiApiKey: String = "",
        assemblyAiSpeechModel: String = "u3-rt-pro",
        whisperModelPath: String? = nil,
        whisperLanguage: String = Self.defaultWhisperLanguage,
        whisperUseGPU: Bool = true,
        openaiApiKey: String? = nil,
        enableRefinement: Bool = false,
        refinementPrompt: String? = Self.defaultRefinementPrompt,
        toggleHotkey: HotkeyBinding = .defaultToggle,
        abortHotkey: HotkeyBinding = .defaultAbort,
        historyHotkey: HotkeyBinding = .defaultHistory
    ) {
        self.transcriptionBackend = transcriptionBackend
        self.deepgramApiKey = deepgramApiKey
        self.deepgramKeywords = Self.normalizeKeywords(deepgramKeywords)
        self.assemblyAiApiKey = assemblyAiApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        self.assemblyAiSpeechModel = Self.normalizeAssemblyAiSpeechModel(assemblyAiSpeechModel)
        self.whisperModelPath = Self.normalizeOptionalPath(whisperModelPath)
        self.whisperLanguage = Self.normalizeLanguage(whisperLanguage)
        self.whisperUseGPU = whisperUseGPU
        self.openaiApiKey = openaiApiKey
        self.enableRefinement = enableRefinement
        self.refinementPrompt = refinementPrompt
        self.toggleHotkey = toggleHotkey
        self.abortHotkey = abortHotkey
        self.historyHotkey = historyHotkey
    }

    enum CodingKeys: String, CodingKey {
        case transcriptionBackend
        case deepgramApiKey
        case deepgramKeywords
        case assemblyAiApiKey
        case assemblyAiSpeechModel
        case whisperModelPath
        case whisperLanguage
        case whisperUseGPU
        case openaiApiKey
        case enableRefinement
        case refinementPrompt
        case toggleHotkey
        case abortHotkey
        case historyHotkey
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        deepgramApiKey = try container.decodeIfPresent(String.self, forKey: .deepgramApiKey) ?? ""

        if let keywords = try? container.decode([String].self, forKey: .deepgramKeywords) {
            deepgramKeywords = Self.normalizeKeywords(keywords)
        } else if let keyword = try? container.decode(String.self, forKey: .deepgramKeywords) {
            deepgramKeywords = Self.normalizeKeywords([keyword])
        } else {
            deepgramKeywords = []
        }

        assemblyAiApiKey = (try container.decodeIfPresent(String.self, forKey: .assemblyAiApiKey) ?? "")
            .trimmingCharacters(in: .whitespacesAndNewlines)
        assemblyAiSpeechModel = Self.normalizeAssemblyAiSpeechModel(try container.decodeIfPresent(String.self, forKey: .assemblyAiSpeechModel) ?? "u3-rt-pro")

        if let backendRawValue = try container.decodeIfPresent(String.self, forKey: .transcriptionBackend) {
            transcriptionBackend = Self.decodeTranscriptionBackend(backendRawValue)
        } else {
            let trimmedKey = deepgramApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
            transcriptionBackend = (!trimmedKey.isEmpty && trimmedKey != "YOUR_DEEPGRAM_API_KEY") ? .deepgram : .localWhisper
        }

        whisperModelPath = Self.normalizeOptionalPath(try container.decodeIfPresent(String.self, forKey: .whisperModelPath))
        whisperLanguage = Self.normalizeLanguage(try container.decodeIfPresent(String.self, forKey: .whisperLanguage) ?? Self.defaultWhisperLanguage)
        whisperUseGPU = try container.decodeIfPresent(Bool.self, forKey: .whisperUseGPU) ?? true
        openaiApiKey = try container.decodeIfPresent(String.self, forKey: .openaiApiKey)
        enableRefinement = try container.decodeIfPresent(Bool.self, forKey: .enableRefinement) ?? false
        refinementPrompt = try container.decodeIfPresent(String.self, forKey: .refinementPrompt)
            ?? Self.defaultRefinementPrompt
        toggleHotkey = try container.decodeIfPresent(HotkeyBinding.self, forKey: .toggleHotkey) ?? .defaultToggle
        abortHotkey = try container.decodeIfPresent(HotkeyBinding.self, forKey: .abortHotkey) ?? .defaultAbort
        historyHotkey = try container.decodeIfPresent(HotkeyBinding.self, forKey: .historyHotkey) ?? .defaultHistory
    }

    static var configURL: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".config")
            .appendingPathComponent("my-whisper")
            .appendingPathComponent("config.json")
    }

    static func normalizeKeywords(_ values: [String]) -> [String] {
        values
            .map { $0.trimmingCharacters(in: .whitespacesAndNewlines) }
            .filter { !$0.isEmpty }
    }

    static func normalizeKeywords(fromMultilineText text: String) -> [String] {
        normalizeKeywords(text.components(separatedBy: .newlines))
    }

    static func normalizeOptionalPath(_ value: String?) -> String? {
        guard let trimmed = value?.trimmingCharacters(in: .whitespacesAndNewlines), !trimmed.isEmpty else {
            return nil
        }

        return trimmed
    }

    static func normalizeLanguage(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? defaultWhisperLanguage : trimmed
    }

    static func normalizeAssemblyAiSpeechModel(_ value: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? "u3-rt-pro" : trimmed
    }

    static func decodeTranscriptionBackend(_ value: String) -> TranscriptionBackend {
        let normalized = value.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        if normalized == "assemblyai" || normalized == "assembly_ai" || normalized == "assembly-ai" {
            return .assemblyAI
        }
        if normalized == "deepgram" {
            return .deepgram
        }
        return .localWhisper
    }

    var normalizedWhisperModelPath: String? {
        Self.normalizeOptionalPath(whisperModelPath)
    }

    var normalizedWhisperLanguage: String {
        Self.normalizeLanguage(whisperLanguage)
    }

    var whisperLanguageOrNil: String? {
        let normalized = normalizedWhisperLanguage
        return normalized.caseInsensitiveCompare("auto") == .orderedSame ? nil : normalized
    }

    var normalizedAssemblyAiSpeechModel: String {
        Self.normalizeAssemblyAiSpeechModel(assemblyAiSpeechModel)
    }

    var hasValidDeepgramKey: Bool {
        let key = deepgramApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        return !key.isEmpty && key != "YOUR_DEEPGRAM_API_KEY"
    }

    var hasValidAssemblyAiKey: Bool {
        let key = assemblyAiApiKey.trimmingCharacters(in: .whitespacesAndNewlines)
        return !key.isEmpty && key != "YOUR_ASSEMBLYAI_API_KEY"
    }

    var hasUsableRefiner: Bool {
        guard enableRefinement else {
            return false
        }

        let key = (openaiApiKey ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
        return !key.isEmpty && key != "YOUR_OPENAI_API_KEY"
    }

    static func load() -> Config {
        let url = configURL

        if let data = try? Data(contentsOf: url),
           let config = try? JSONDecoder().decode(Config.self, from: data) {
            return config
        }

        let defaultConfig = Config(
            transcriptionBackend: .localWhisper,
            deepgramApiKey: "",
            deepgramKeywords: [],
            assemblyAiApiKey: "",
            assemblyAiSpeechModel: "u3-rt-pro",
            whisperModelPath: nil,
            whisperLanguage: Self.defaultWhisperLanguage,
            whisperUseGPU: true,
            openaiApiKey: nil,
            enableRefinement: false,
            refinementPrompt: Self.defaultRefinementPrompt,
            toggleHotkey: .defaultToggle,
            abortHotkey: .defaultAbort,
            historyHotkey: .defaultHistory
        )

        try? defaultConfig.save()

        return defaultConfig
    }

    func save() throws {
        let url = Self.configURL
        try FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)

        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        let data = try encoder.encode(self)
        try data.write(to: url)
    }
}
