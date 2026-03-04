import Cocoa

extension NSImage {
    static func customWaveformIcon() -> NSImage {
        let size = NSSize(width: 18, height: 16)
        let image = NSImage(size: size, flipped: false) { _ in
            NSColor.black.setStroke()
            let path = NSBezierPath()
            path.lineWidth = 2.0
            path.lineCapStyle = .round
            
            // Bar 1
            path.move(to: NSPoint(x: 3, y: 5))
            path.line(to: NSPoint(x: 3, y: 11))
            
            // Bar 2
            path.move(to: NSPoint(x: 7, y: 2))
            path.line(to: NSPoint(x: 7, y: 14))
            
            // Bar 3
            path.move(to: NSPoint(x: 11, y: 4))
            path.line(to: NSPoint(x: 11, y: 12))
            
            // Bar 4
            path.move(to: NSPoint(x: 15, y: 6))
            path.line(to: NSPoint(x: 15, y: 10))
            
            path.stroke()
            return true
        }
        // Setting isTemplate to true ensures the icon automatically
        // switches colors based on macOS Light/Dark mode.
        image.isTemplate = true
        return image
    }
}
