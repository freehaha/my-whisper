import Foundation

class Transcriber {
    func transcribe(audioURL: URL, config: Config) async throws -> String {
        guard config.hasValidDeepgramKey else {
            throw NSError(domain: "Transcriber", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid Deepgram API Key. Set it in ~/.config/my-whisper/config.json"])
        }

        let audioData = try Data(contentsOf: audioURL)

        var request = URLRequest(url: deepgramURL(for: config))
        request.httpMethod = "POST"
        request.addValue("Token \(config.deepgramApiKey)", forHTTPHeaderField: "Authorization")
        request.addValue("audio/m4a", forHTTPHeaderField: "Content-Type")
        request.httpBody = audioData

        let (data, response) = try await URLSession.shared.data(for: request)

        guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
            let errorMsg = String(data: data, encoding: .utf8) ?? "Unknown Error"
            throw NSError(domain: "Transcriber", code: 2, userInfo: [NSLocalizedDescriptionKey: "Deepgram API Error: \(errorMsg)"])
        }

        let json = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        let results = json?["results"] as? [String: Any]
        let channels = results?["channels"] as? [[String: Any]]
        let alternatives = channels?.first?["alternatives"] as? [[String: Any]]
        let transcript = alternatives?.first?["transcript"] as? String ?? ""

        return transcript
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
}
