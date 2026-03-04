import Cocoa
import SwiftUI

class HistoryWindowController: NSWindowController {
    static let shared = HistoryWindowController()

    init() {
        let window = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 520, height: 320),
            styleMask: [.titled, .closable],
            backing: .buffered,
            defer: false
        )

        window.title = "Transcription History"
        window.isReleasedWhenClosed = false
        window.center()

        super.init(window: window)
        self.window?.contentView = NSHostingView(rootView: HistoryView())
    }

    required init?(coder: NSCoder) {
        fatalError("init(coder:) has not been implemented")
    }

    func show() {
        NSApp.activate(ignoringOtherApps: true)
        window?.makeKeyAndOrderFront(nil)
    }
}

struct HistoryView: View {
    @ObservedObject private var historyStore = TranscriptionHistoryStore.shared

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Select a transcription to copy it to clipboard")
                .font(.headline)

            if historyStore.entries.isEmpty {
                Spacer()
                Text("No transcription history yet.")
                    .foregroundColor(.secondary)
                Spacer()
            } else {
                List(historyStore.entries) { entry in
                    Button {
                        historyStore.select(entry)
                        NSApp.keyWindow?.close()
                    } label: {
                        VStack(alignment: .leading, spacing: 4) {
                            Text(entry.text)
                                .lineLimit(2)
                                .multilineTextAlignment(.leading)
                                .frame(maxWidth: .infinity, alignment: .leading)

                            Text(Self.dateFormatter.localizedString(for: entry.timestamp, relativeTo: Date()))
                                .font(.caption)
                                .foregroundColor(.secondary)
                        }
                        .padding(.vertical, 4)
                    }
                    .buttonStyle(.plain)
                }
                .listStyle(.inset)
            }
        }
        .padding(16)
        .frame(width: 520, height: 320)
    }

    private static let dateFormatter: RelativeDateTimeFormatter = {
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .full
        return formatter
    }()
}
