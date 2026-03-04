import Cocoa
import SwiftUI
import AVFoundation

class AppDelegate: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    var statusMenu: NSMenu!

    @objc func openSettings() {
        SettingsWindowController.shared.show()
    }

    @objc func openHistory() {
        HistoryWindowController.shared.show()
    }

    @objc func statusItemClicked(_ sender: NSStatusBarButton) {
        guard let event = NSApp.currentEvent else {
            openHistory()
            return
        }

        switch event.type {
        case .rightMouseUp:
            statusItem.menu = statusMenu
            statusItem.button?.performClick(nil)
            statusItem.menu = nil
        default:
            openHistory()
        }
    }
    
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Create Status Item
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.image = NSImage.customWaveformIcon()
            button.image?.accessibilityDescription = "MyWhisper"
            button.target = self
            button.action = #selector(statusItemClicked(_:))
            button.sendAction(on: [.leftMouseUp, .rightMouseUp])
        }

        statusMenu = NSMenu()
        let historyItem = NSMenuItem(title: "Transcription History…", action: #selector(openHistory), keyEquivalent: "")
        historyItem.target = self
        statusMenu.addItem(historyItem)

        let settingsItem = NSMenuItem(title: "Settings…", action: #selector(openSettings), keyEquivalent: ",")
        settingsItem.target = self
        statusMenu.addItem(settingsItem)
        statusMenu.addItem(.separator())
        statusMenu.addItem(NSMenuItem(title: "Quit MyWhisper", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        
        // Initialize the view model to register hotkeys
        _ = ViewModel.shared
        
        // Initialize windows
        _ = StatusWindowController.shared
        _ = HistoryWindowController.shared
        
        // Hide dock icon
        NSApp.setActivationPolicy(.accessory)
        
        // Request Accessibility permissions if not granted
        let options = [kAXTrustedCheckOptionPrompt.takeUnretainedValue() as String: true]
        let accessEnabled = AXIsProcessTrustedWithOptions(options as CFDictionary)
        
        if !accessEnabled {
            print("Please grant Accessibility permissions in System Settings > Privacy & Security > Accessibility.")
        }
        
        // Check Microphone permissions
        if #available(macOS 14.0, *) {
            AVAudioApplication.requestRecordPermission { granted in
                if !granted {
                    print("Please grant Microphone permissions.")
                }
            }
        }
    }
}

let app = NSApplication.shared
let delegate = AppDelegate()
app.delegate = delegate
app.run()
