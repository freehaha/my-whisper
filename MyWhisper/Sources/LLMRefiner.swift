import Foundation

class LLMRefiner {
    func refine(text: String, config: Config) async throws -> String {
        guard config.hasUsableRefiner, let apiKey = config.openaiApiKey, !apiKey.isEmpty else {
            return text
        }

        let url = URL(string: "https://api.openai.com/v1/chat/completions")!
        var request = URLRequest(url: url)
        request.httpMethod = "POST"
        request.addValue("Bearer \(apiKey)", forHTTPHeaderField: "Authorization")
        request.addValue("application/json", forHTTPHeaderField: "Content-Type")

        let prompt = (config.refinementPrompt?.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty == false)
            ? config.refinementPrompt!
            : "Fix spelling and grammar. Return the corrected text in the refined_text field."

        let body: [String: Any] = [
            "model": "gpt-5.4-nano",
            "temperature": 0.7,
            "response_format": [
                "type": "json_schema",
                "json_schema": [
                    "name": "refined_result",
                    "strict": true,
                    "schema": [
                        "type": "object",
                        "properties": [
                            "refined_text": [
                                "type": "string"
                            ]
                        ],
                        "required": ["refined_text"],
                        "additionalProperties": false
                    ]
                ]
            ],
            "messages": [
                [
                    "role": "system",
                    "content": prompt
                ],
                [
                    "role": "user",
                    "content": text
                ]
            ]
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
        let content = message?["content"] as? String ?? text

        if let contentData = content.data(using: .utf8),
           let parsed = try? JSONSerialization.jsonObject(with: contentData) as? [String: Any],
           let refinedText = parsed["refined_text"] as? String {
            return refinedText.trimmingCharacters(in: .whitespacesAndNewlines)
        }

        return content.trimmingCharacters(in: .whitespacesAndNewlines)
    }
}
