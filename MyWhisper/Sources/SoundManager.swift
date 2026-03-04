import Cocoa

class SoundManager {
    static let shared = SoundManager()
    
    private let startSound = NSSound(named: NSSound.Name("Pop"))
    private let stopSound = NSSound(named: NSSound.Name("Tink"))
    private let successSound = NSSound(named: NSSound.Name("Glass"))
    private let errorSound = NSSound(named: NSSound.Name("Basso"))
    
    func playStart() { startSound?.play() }
    func playStop() { stopSound?.play() }
    func playSuccess() { successSound?.play() }
    func playError() { errorSound?.play() }
}
