import Foundation

struct Config: Codable {
    var deepgramApiKey: String
    var openaiApiKey: String?
    var enableRefinement: Bool
    var refinementPrompt: String?
    
    static func load() -> Config {
        let url = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".config")
            .appendingPathComponent("my-whisper")
            .appendingPathComponent("config.json")
        
        if let data = try? Data(contentsOf: url),
           let config = try? JSONDecoder().decode(Config.self, from: data) {
            return config
        }
        
        let defaultConfig = Config(
            deepgramApiKey: "YOUR_DEEPGRAM_API_KEY",
            openaiApiKey: nil,
            enableRefinement: false,
            refinementPrompt: "Fix spelling and grammar. Return only the fixed text."
        )
        
        try? FileManager.default.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        if let data = try? JSONEncoder().encode(defaultConfig) {
            try? data.write(to: url)
        }
        
        return defaultConfig
    }
}
