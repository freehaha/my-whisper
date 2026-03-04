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
}

struct Config: Codable {
    var deepgramApiKey: String
    var openaiApiKey: String?
    var enableRefinement: Bool
    var refinementPrompt: String?
    var toggleHotkey: HotkeyBinding
    var abortHotkey: HotkeyBinding

    init(
        deepgramApiKey: String,
        openaiApiKey: String?,
        enableRefinement: Bool,
        refinementPrompt: String?,
        toggleHotkey: HotkeyBinding = .defaultToggle,
        abortHotkey: HotkeyBinding = .defaultAbort
    ) {
        self.deepgramApiKey = deepgramApiKey
        self.openaiApiKey = openaiApiKey
        self.enableRefinement = enableRefinement
        self.refinementPrompt = refinementPrompt
        self.toggleHotkey = toggleHotkey
        self.abortHotkey = abortHotkey
    }

    enum CodingKeys: String, CodingKey {
        case deepgramApiKey
        case openaiApiKey
        case enableRefinement
        case refinementPrompt
        case toggleHotkey
        case abortHotkey
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        deepgramApiKey = try container.decode(String.self, forKey: .deepgramApiKey)
        openaiApiKey = try container.decodeIfPresent(String.self, forKey: .openaiApiKey)
        enableRefinement = try container.decodeIfPresent(Bool.self, forKey: .enableRefinement) ?? false
        refinementPrompt = try container.decodeIfPresent(String.self, forKey: .refinementPrompt)
            ?? "Fix spelling and grammar. Return only the fixed text."
        toggleHotkey = try container.decodeIfPresent(HotkeyBinding.self, forKey: .toggleHotkey) ?? .defaultToggle
        abortHotkey = try container.decodeIfPresent(HotkeyBinding.self, forKey: .abortHotkey) ?? .defaultAbort
    }

    static var configURL: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".config")
            .appendingPathComponent("my-whisper")
            .appendingPathComponent("config.json")
    }

    static func load() -> Config {
        let url = configURL

        if let data = try? Data(contentsOf: url),
           let config = try? JSONDecoder().decode(Config.self, from: data) {
            return config
        }

        let defaultConfig = Config(
            deepgramApiKey: "YOUR_DEEPGRAM_API_KEY",
            openaiApiKey: nil,
            enableRefinement: false,
            refinementPrompt: "Fix spelling and grammar. Return only the fixed text.",
            toggleHotkey: .defaultToggle,
            abortHotkey: .defaultAbort
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
