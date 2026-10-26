// T-764 placeholder art: replace this generator with owner artwork before a final release.
// Uses only the macOS SDK; no image tools, downloaded artwork or font are needed.
import AppKit

let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels,
            pixelsHigh: pixels, bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true,
            isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)
        let extent = CGFloat(pixels)
        NSColor(calibratedRed: 0.07, green: 0.10, blue: 0.16, alpha: 1).setFill()
        NSBezierPath(roundedRect: NSRect(x: 0, y: 0, width: extent, height: extent),
            xRadius: extent * 0.18, yRadius: extent * 0.18).fill()
        NSColor(calibratedRed: 0.30, green: 0.90, blue: 0.66, alpha: 1).setStroke()
        let mark = NSBezierPath()
        mark.lineWidth = extent * 0.09
        mark.lineJoinStyle = .round
        mark.move(to: NSPoint(x: extent * 0.18, y: extent * 0.73))
        for (x, y) in [(0.32, 0.27), (0.50, 0.55), (0.68, 0.27), (0.82, 0.73)] {
            mark.line(to: NSPoint(x: extent * x, y: extent * y))
        }
        mark.stroke()
        NSGraphicsContext.restoreGraphicsState()
        let suffix = scale == 2 ? "@2x" : ""
        let file = directory.appendingPathComponent("icon_\(size)x\(size)\(suffix).png")
        try bitmap.representation(using: .png, properties: [:])!.write(to: file)
    }
}
