import Foundation

class Transcriber {
    let config: Config
    
    init(config: Config) {
        self.config = config
    }
    
    func transcribe(audioURL: URL) async throws -> String {
        guard let apiKey = config.deepgramApiKey.isEmpty == false ? config.deepgramApiKey : nil, apiKey != "YOUR_DEEPGRAM_API_KEY" else {
            throw NSError(domain: "Transcriber", code: 1, userInfo: [NSLocalizedDescriptionKey: "Invalid Deepgram API Key. Set it in ~/.config/my-whisper/config.json"])
        }
        
        let audioData = try Data(contentsOf: audioURL)
        
        var request = URLRequest(url: URL(string: "https://api.deepgram.com/v1/listen?model=nova-2&smart_format=true")!)
        request.httpMethod = "POST"
        request.addValue("Token \(apiKey)", forHTTPHeaderField: "Authorization")
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
}
