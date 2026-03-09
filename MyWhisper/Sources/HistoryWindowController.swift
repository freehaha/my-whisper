import Cocoa
import SwiftUI
import Carbon

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
    @State private var selectedEntryID: UUID?
    @State private var keyboardMonitor: Any?

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
                ScrollViewReader { proxy in
                    List(selection: $selectedEntryID) {
                        ForEach(historyStore.entries) { entry in
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
                            .id(entry.id)
                            .tag(entry.id)
                            .contentShape(Rectangle())
                            .onTapGesture(count: 2) {
                                activateSelection()
                            }
                        }
                    }
                    .listStyle(.inset)
                    .onAppear {
                        if let selectedEntryID {
                            scrollToSelection(selectedEntryID, with: proxy, animated: false)
                        }
                    }
                    .onChange(of: selectedEntryID) { _, newValue in
                        guard let newValue else {
                            return
                        }
                        scrollToSelection(newValue, with: proxy)
                    }
                }

                Text("Use ↑/↓ to navigate, Enter to copy selected transcription, Esc to close")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
        }
        .padding(16)
        .frame(width: 520, height: 320)
        .onAppear {
            ensureValidSelection()
            installKeyboardMonitor()
        }
        .onDisappear {
            removeKeyboardMonitor()
        }
        .onChange(of: historyStore.entries.map(\.id)) {
            ensureValidSelection()
        }
    }

    private func installKeyboardMonitor() {
        guard keyboardMonitor == nil else {
            return
        }

        keyboardMonitor = NSEvent.addLocalMonitorForEvents(matching: .keyDown) { event in
            if handleKeyEvent(event) {
                return nil
            }
            return event
        }
    }

    private func removeKeyboardMonitor() {
        if let keyboardMonitor {
            NSEvent.removeMonitor(keyboardMonitor)
            self.keyboardMonitor = nil
        }
    }

    private func handleKeyEvent(_ event: NSEvent) -> Bool {
        guard NSApp.keyWindow === HistoryWindowController.shared.window else {
            return false
        }

        switch Int(event.keyCode) {
        case kVK_Escape:
            HistoryWindowController.shared.window?.close()
            return true
        case kVK_UpArrow:
            guard !historyStore.entries.isEmpty else { return true }
            moveSelection(delta: -1)
            return true
        case kVK_DownArrow:
            guard !historyStore.entries.isEmpty else { return true }
            moveSelection(delta: 1)
            return true
        case kVK_Return, kVK_ANSI_KeypadEnter:
            guard !historyStore.entries.isEmpty else { return true }
            activateSelection()
            return true
        default:
            return false
        }
    }

    private func ensureValidSelection() {
        let entries = historyStore.entries
        guard !entries.isEmpty else {
            selectedEntryID = nil
            return
        }

        if let selectedEntryID,
           entries.contains(where: { $0.id == selectedEntryID }) {
            return
        }

        self.selectedEntryID = entries.first?.id
    }

    private func moveSelection(delta: Int) {
        let entries = historyStore.entries
        guard !entries.isEmpty else {
            selectedEntryID = nil
            return
        }

        let currentIndex: Int
        if let selectedEntryID,
           let foundIndex = entries.firstIndex(where: { $0.id == selectedEntryID }) {
            currentIndex = foundIndex
        } else {
            currentIndex = delta > 0 ? -1 : entries.count
        }

        let newIndex = min(max(currentIndex + delta, 0), entries.count - 1)
        selectedEntryID = entries[newIndex].id
    }

    private func scrollToSelection(_ entryID: UUID, with proxy: ScrollViewProxy, animated: Bool = true) {
        DispatchQueue.main.async {
            if animated {
                withAnimation {
                    proxy.scrollTo(entryID, anchor: .center)
                }
            } else {
                proxy.scrollTo(entryID, anchor: .center)
            }
        }
    }

    private func activateSelection() {
        let entries = historyStore.entries
        guard !entries.isEmpty else {
            return
        }

        let selectedEntry: TranscriptionHistoryEntry
        if let selectedEntryID,
           let entry = entries.first(where: { $0.id == selectedEntryID }) {
            selectedEntry = entry
        } else {
            selectedEntry = entries[0]
        }

        historyStore.select(selectedEntry)
        NSApp.keyWindow?.close()
    }

    private static let dateFormatter: RelativeDateTimeFormatter = {
        let formatter = RelativeDateTimeFormatter()
        formatter.unitsStyle = .full
        return formatter
    }()
}
