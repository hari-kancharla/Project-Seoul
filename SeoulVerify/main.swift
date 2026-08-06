import AppKit
import CoreGraphics
import Seoul

// Milestone 2-verify: prove, on real hardware, that a panel configured with
// sharingType = .none is absent from a screen capture — and prove the harness
// can see a sketch at all, by capturing an otherwise identical panel that is
// NOT excluded.
//
// An executable rather than an XCTest on purpose: XCTest under SwiftPM has no
// dependable app context for window ordering, and this needs a real on-screen
// panel composited by the WindowServer.

NSApplication.shared.setActivationPolicy(.accessory)

// The shipping accent, #FF7A45.
let accentR = 255.0, accentG = 122.0, accentB = 69.0

/// Euclidean RGB radius around the accent that counts as "the sketch".
///
/// Was 60, which produced a FALSE FAILURE: with a video playing behind the
/// capture region, warm skin and yellow tones fell inside a 60-radius ball and
/// scored 568 hits in a region containing no sketch at all. Measured on those
/// exact captures, the separation is wide and unambiguous:
///
///     tolerance   subject (no sketch)   control (sketch)
///        20              0                   6224
///        40              0                   6991
///        50              0                   7496
///        60            568                   8042
///
/// 40 sits in the middle of a large empty band: the real stroke is drawn at
/// full opacity and lands within ~20 of the target, while nothing photographic
/// gets within 50. A verifier that fails on the desktop wallpaper is worse than
/// no verifier, because the next real failure gets waved through as noise.
let tolerance = 40.0

let pathA = "/tmp/seoul-verify-a.png"     // SUBJECT  — sharingType .none
let pathB = "/tmp/seoul-verify-b.png"     // CONTROL  — sharingType .readOnly

// MARK: - Pixel matching

/// Counts pixels within `tolerance` Euclidean RGB distance of the accent.
/// The capture is converted to sRGB first: the shipping colour is defined in
/// sRGB, but a captured PNG carries the display's profile (Display P3 here), and
/// comparing raw P3 numbers against sRGB numbers would silently miss the orange.
func countAccentPixels(_ path: String) -> (matched: Int, total: Int, width: Int, height: Int)? {
    guard let data = FileManager.default.contents(atPath: path),
          let loaded = NSBitmapImageRep(data: data) else { return nil }
    let rep = loaded.converting(to: .sRGB, renderingIntent: .relativeColorimetric) ?? loaded

    // Read the real pixel dimensions; on Retina these are 2x the point size.
    let width = rep.pixelsWide
    let height = rep.pixelsHigh

    func isAccent(_ r: Double, _ g: Double, _ b: Double) -> Bool {
        let dr = r - accentR, dg = g - accentG, db = b - accentB
        return (dr * dr + dg * dg + db * db).squareRoot() <= tolerance
    }

    var matched = 0
    if let base = rep.bitmapData, !rep.isPlanar, rep.bitsPerPixel % 8 == 0 {
        let stride = rep.bitsPerPixel / 8
        let rowBytes = rep.bytesPerRow
        for y in 0..<height {
            let row = base + y * rowBytes
            for x in 0..<width {
                let px = row + x * stride
                if isAccent(Double(px[0]), Double(px[1]), Double(px[2])) { matched += 1 }
            }
        }
    } else {
        // Slower, but correct for any layout the converter hands back.
        for y in 0..<height {
            for x in 0..<width {
                guard let c = rep.colorAt(x: x, y: y)?.usingColorSpace(.sRGB) else { continue }
                if isAccent(c.redComponent * 255, c.greenComponent * 255, c.blueComponent * 255) {
                    matched += 1
                }
            }
        }
    }
    return (matched, width * height, width, height)
}

func sharingState(ofWindowNumber number: Int) -> Int? {
    let list = (CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]]) ?? []
    let entry = list.first { ($0[kCGWindowNumber as String] as? Int) == number }
    return entry?[kCGWindowSharingState as String] as? Int
}

/// Owning application of a window number, so a hit-test result reads as
/// something real rather than as a bare integer.
func ownerName(ofWindowNumber number: Int) -> String {
    guard number != 0 else { return "none — no window at this point" }
    let list = (CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as? [[String: Any]]) ?? []
    let entry = list.first { ($0[kCGWindowNumber as String] as? Int) == number }
    return (entry?[kCGWindowOwnerName as String] as? String) ?? "unknown"
}

@discardableResult
func screencapture(region: NSRect, to path: String) -> Int32 {
    try? FileManager.default.removeItem(atPath: path)   // never read a stale capture
    let process = Process()
    process.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
    let spec = "\(Int(region.origin.x.rounded())),\(Int(region.origin.y.rounded()))," +
               "\(Int(region.width.rounded())),\(Int(region.height.rounded()))"
    process.arguments = ["-x", "-R\(spec)", path]
    do { try process.run() } catch { return -1 }
    process.waitUntilExit()
    return process.terminationStatus
}

// MARK: - Run

let exitCode: Int32 = MainActor.assumeIsolated { () -> Int32 in

    guard let screen = NSScreen.main ?? NSScreen.screens.first else {
        print("no screen available")
        return 2
    }

    let version = ProcessInfo.processInfo.operatingSystemVersion
    print("macOS \(version.majorVersion).\(version.minorVersion).\(version.patchVersion)")
    print("screen frame: \(NSStringFromRect(screen.frame))  backingScaleFactor: \(screen.backingScaleFactor)")

    // Captured BEFORE any panel exists, or there is nothing to compare against.
    let frontmostBefore = NSWorkspace.shared.frontmostApplication
    let frontmostBeforeName = frontmostBefore?.localizedName ?? "nil"
    let frontmostBeforePID = frontmostBefore?.processIdentifier ?? -1

    // Two known, well separated rects so neither capture region can catch the
    // other's sketch.
    let size = NSSize(width: 320, height: 38)
    let rectA = NSRect(x: screen.frame.midX - size.width / 2,
                       y: screen.frame.midY + 80,
                       width: size.width, height: size.height)
    let rectB = NSRect(x: screen.frame.midX - size.width / 2,
                       y: screen.frame.midY - 120,
                       width: size.width, height: size.height)

    let panelA = OverlayPanel(screen: screen)   // SUBJECT: ships as sharingType .none
    let panelB = OverlayPanel(screen: screen)   // CONTROL
    // The one and only difference. Set here, in the test, never in the shipping
    // configuration — a control that required editing OverlayPanel would prove
    // something about the edit rather than about the product.
    panelB.sharingType = .readOnly

    panelA.annotate(rect: rectA, gesture: .box, label: nil, elementID: "verify-a")
    panelB.annotate(rect: rectB, gesture: .box, label: nil, elementID: "verify-b")

    // Draw-on is 200ms with pass 1 beginning at 60ms; 900ms is a wide settle
    // margin, and comfortably covers the 500ms the activation check needs.
    // RunLoop, not sleep, so the panels actually get composited.
    RunLoop.current.run(until: Date().addingTimeInterval(0.9))

    // MARK: Check — no activation
    //
    // An overlay that steals focus is unusable: every annotation would pull the
    // user out of whatever they were doing.

    let frontmostAfter = NSWorkspace.shared.frontmostApplication
    let frontmostAfterName = frontmostAfter?.localizedName ?? "nil"
    let frontmostAfterPID = frontmostAfter?.processIdentifier ?? -1
    let selfActive = NSRunningApplication.current.isActive
    let frontmostUnchanged = frontmostBeforePID == frontmostAfterPID
    let activationOK = !selfActive && frontmostUnchanged

    print("")
    print("frontmost BEFORE panels: \(frontmostBeforeName) (pid \(frontmostBeforePID))")
    print("frontmost AFTER  panels: \(frontmostAfterName) (pid \(frontmostAfterPID))")
    print("NSRunningApplication.current.isActive: \(selfActive)   (expect false)")

    // MARK: Check — click-through
    //
    // ignoresMouseEvents is asserted, not trusted: transparent borderless
    // windows have regressed on hit-testing in the macOS 26.x line, so ask the
    // WindowServer what it would actually hit.

    var samples: [(name: String, point: NSPoint)] = []
    for (label, rect) in [("subject", rectA), ("control", rectB)] {
        samples.append(("\(label) centre",       NSPoint(x: rect.midX, y: rect.midY)))
        samples.append(("\(label) top stroke",   NSPoint(x: rect.midX, y: rect.maxY + 4)))
        samples.append(("\(label) left stroke",  NSPoint(x: rect.minX - 5, y: rect.midY)))
        samples.append(("\(label) inset corner", NSPoint(x: rect.minX + 8, y: rect.minY + 8)))
    }
    let overlayNumbers = Set([Int(panelA.windowNumber), Int(panelB.windowNumber)])

    print("")
    print("overlay window numbers: subject \(panelA.windowNumber), control \(panelB.windowNumber)")
    var intercepted = 0
    for sample in samples {
        let hit = NSWindow.windowNumber(at: sample.point, belowWindowWithWindowNumber: 0)
        let isOverlay = overlayNumbers.contains(hit)
        if isOverlay { intercepted += 1 }
        print(String(format: "  hit-test %-22@ (%.0f, %.0f) -> window %d  [%@]%@",
                     sample.name as NSString, sample.point.x, sample.point.y, hit,
                     ownerName(ofWindowNumber: hit) as NSString,
                     isOverlay ? "  <- OVERLAY INTERCEPTED" : ""))
    }

    let stateA = sharingState(ofWindowNumber: Int(panelA.windowNumber))
    let stateB = sharingState(ofWindowNumber: Int(panelB.windowNumber))
    print("")
    print("SUBJECT panel: swift sharingType \(panelA.sharingType.rawValue), " +
          "kCGWindowSharingState \(stateA.map(String.init) ?? "nil")  (0 = none)")
    print("CONTROL panel: swift sharingType \(panelB.sharingType.rawValue), " +
          "kCGWindowSharingState \(stateB.map(String.init) ?? "nil")  (1 = readOnly)")

    // The sketch overruns its target rect (inset, jitter, corner overshoot), so
    // capture a generous margin around each.
    let primaryHeight = CoordinateTransform.primaryScreenHeight()
    func captureRegion(_ rect: NSRect) -> NSRect {
        // screencapture -R takes TOP-LEFT origin coordinates, so this conversion
        // is required — and it exercises the milestone-1 transform.
        CoordinateTransform.appKitRectToScreenTopLeft(rect.insetBy(dx: -40, dy: -40),
                                                      primaryScreenHeight: primaryHeight)
    }
    let regionA = captureRegion(rectA)
    let regionB = captureRegion(rectB)
    print("")
    print("SUBJECT rect \(NSStringFromRect(rectA))  -> capture -R \(NSStringFromRect(regionA))")
    print("CONTROL rect \(NSStringFromRect(rectB))  -> capture -R \(NSStringFromRect(regionB))")

    let statusA = screencapture(region: regionA, to: pathA)
    let statusB = screencapture(region: regionB, to: pathB)
    print("screencapture exit status: subject \(statusA), control \(statusB)")

    let clickThroughOK = intercepted == 0
    let subject = countAccentPixels(pathA)
    let control = countAccentPixels(pathB)

    func percent(_ m: Int, _ t: Int) -> String {
        String(format: "%.4f%%", t == 0 ? 0 : Double(m) / Double(t) * 100)
    }

    print("")
    if let subject, let control {
        print("SUBJECT capture: \(subject.width)x\(subject.height) px, " +
              "\(subject.matched) accent pixels (\(percent(subject.matched, subject.total)))")
        print("CONTROL capture: \(control.width)x\(control.height) px, " +
              "\(control.matched) accent pixels (\(percent(control.matched, control.total)))")
    } else {
        print("could not read back the captures — screencapture wrote nothing.")
    }

    let exclusionLeaked = (subject?.matched ?? 0) > 0
    let controlSawSketch = (control?.matched ?? 0) > 0
    let harnessOK = subject != nil && control != nil && controlSawSketch

    print("")
    print("================ VERDICT ================")
    if let subject, let control {
        print("CONTROL (sharingType .readOnly): \(control.matched) pixels matched  -> EXPECT > 0")
        print("SUBJECT (sharingType .none):     \(subject.matched) pixels matched  -> EXPECT == 0")
    } else {
        print("CONTROL (sharingType .readOnly): capture unreadable       -> EXPECT > 0")
        print("SUBJECT (sharingType .none):     capture unreadable       -> EXPECT == 0")
    }
    print("CLICK-THROUGH: overlay hit-tested in \(intercepted) of \(samples.count) samples" +
          "  -> EXPECT 0")
    print("ACTIVATION:    isActive \(selfActive), frontmost " +
          "\(frontmostUnchanged ? "unchanged" : "CHANGED")  -> EXPECT false, unchanged")
    print("=========================================")

    // Precedence: a leaked subject is conclusive on its own, so it outranks a
    // broken harness. Everything below that needs a harness that worked.
    if exclusionLeaked {
        print("FAIL: the excluded panel appears in the capture. Exclusion is NOT working.")
        return 1
    }
    if !harnessOK {
        print("INCONCLUSIVE: the control panel is missing from its capture too, so the")
        print("harness never saw a sketch and the subject's absence proves nothing.")
        print("Most likely cause: the invoking process lacks Screen Recording permission.")
        return 2
    }
    if !clickThroughOK {
        print("FAIL: the overlay was hit-tested. Clicks would not reach the app beneath.")
        return 3
    }
    if !activationOK {
        print("FAIL: the overlay activated the app or changed the frontmost application.")
        return 4
    }
    print("PASS: exclusion holds, the overlay is click-through, and nothing was activated.")
    return 0
}

exit(exitCode)
