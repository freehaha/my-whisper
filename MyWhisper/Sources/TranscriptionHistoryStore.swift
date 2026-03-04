import Foundation
import AppKit
import Combine

struct TranscriptionHistoryEntry: Codable, Identifiable, Equatable {
    let id: UUID
    let text: String
    let timestamp: Date
}

@MainActor
final class TranscriptionHistoryStore: ObservableObject {
    static let shared = TranscriptionHistoryStore()

    @Published private(set) var entries: [TranscriptionHistoryEntry] = []

    private static let maxEntries = 10
    private let historyURL: URL
    private let encoder = JSONEncoder()
    private let decoder = JSONDecoder()

    private init() {
        let configDirectory = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent(".config")
            .appendingPathComponent("my-whisper")

        historyURL = configDirectory.appendingPathComponent("history.json")
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        encoder.dateEncodingStrategy = .iso8601
        decoder.dateDecodingStrategy = .iso8601

        load()
    }

    func add(_ text: String) {
        let cleaned = text.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !cleaned.isEmpty else {
            return
        }

        if let index = entries.firstIndex(where: { $0.text == cleaned }) {
            entries.remove(at: index)
        }

        let entry = TranscriptionHistoryEntry(id: UUID(), text: cleaned, timestamp: Date())
        entries.insert(entry, at: 0)

        if entries.count > Self.maxEntries {
            entries = Array(entries.prefix(Self.maxEntries))
        }

        save()
    }

    func select(_ entry: TranscriptionHistoryEntry) {
        Paster.copyToClipboard(text: entry.text)

        if let index = entries.firstIndex(where: { $0.id == entry.id }) {
            let selected = entries.remove(at: index)
            let refreshed = TranscriptionHistoryEntry(id: selected.id, text: selected.text, timestamp: Date())
            entries.insert(refreshed, at: 0)
            save()
        }
    }

    private func load() {
        guard let data = try? Data(contentsOf: historyURL),
              let loaded = try? decoder.decode([TranscriptionHistoryEntry].self, from: data) else {
            entries = []
            return
        }

        entries = Array(loaded.prefix(Self.maxEntries))
    }

    private func save() {
        do {
            try FileManager.default.createDirectory(
                at: historyURL.deletingLastPathComponent(),
                withIntermediateDirectories: true
            )

            let data = try encoder.encode(entries)
            try data.write(to: historyURL)
        } catch {
            print("Failed to save transcription history: \(error)")
        }
    }
}
