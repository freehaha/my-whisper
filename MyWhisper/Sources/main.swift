import Cocoa
import SwiftUI
import AVFoundation

class AppDelegate: NSObject, NSApplicationDelegate {
    var statusItem: NSStatusItem!
    
    func applicationDidFinishLaunching(_ notification: Notification) {
        // Create Status Item
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = statusItem.button {
            button.image = NSImage(systemSymbolName: "mic.fill", accessibilityDescription: "MyWhisper")
        }
        
        let menu = NSMenu()
        menu.addItem(NSMenuItem(title: "Quit MyWhisper", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q"))
        statusItem.menu = menu
        
        // Initialize the view model to register hotkeys
        _ = ViewModel.shared
        
        // Initialize the status window
        _ = StatusWindowController.shared
        
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
