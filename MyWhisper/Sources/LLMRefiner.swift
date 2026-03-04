import Foundation

class LLMRefiner {
    let config: Config
    
    init(config: Config) {
        self.config = config
    }
    
    func refine(text: String) async throws -> String {
        guard config.enableRefinement, let apiKey = config.openaiApiKey, !apiKey.isEmpty, apiKey != "YOUR_OPENAI_API_KEY" else {
            return text // Return original if not enabled or no key
        }
        
        let url = URL(string: "https://api.openai.com/v1/chat/completions")!
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.addValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.addValue("application/json", forHTTPHeaderField: "Content-Type")
        
        let prompt = config.refinementPrompt ?? "Fix spelling and grammar. Return only the fixed text."
        
        let body: [String: Any] = [
            "model": "gpt-4o-mini", // or gpt-3.5-turbo
            "messages": [
                ["role": "system", "content": prompt],
                ["role": "user", "content": text]
            ],
            "temperature": 0.3
        ]
        
        request.httpBody = try JSONSerialization.data(withJSONObject: body)
        
        let (data, response) = try await URLSession.shared.data(for: request)
        
        guard let httpResponse = response as? HTTPURLResponse, httpResponse.statusCode == 200 else {
            let errorMsg = String(data: data, encoding: .utf8) ?? "Unknown Error"
            throw NSError(domain: "LLMRefiner", code: 2, userInfo: [NSLocalizedDescriptionKey: "OpenAI Error: \(errorMsg)"])
        }
        
        let json = try JSONSerialization.jsonObject(with: data) as? [String: Any]
        let choices = json?["choices"] as? [[String: Any]]
        let message = choices?.first?["message"] as? [String: Any]
        let refinedText = message?["content"] as? String ?? text
        
        return refinedText.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
