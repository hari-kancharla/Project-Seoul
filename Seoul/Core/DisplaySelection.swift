import AppKit

/// Which display a browser window is on.
///
/// Split out from the AppKit lookup on purpose: `NSScreen` cannot be constructed
/// in a test, so anything phrased in terms of `[NSScreen]` can only be verified
/// on whatever displays the machine running the tests happens to have. Phrased
/// in terms of `[NSRect]`, the interesting arrangements — a window pinned to the
/// top edge, a window straddling two displays, a window dragged off-screen —
/// are all reachable. `CoordinateTransform.screen(for:in:)` does the same
/// largest-intersection selection for the sketch itself; this is the same rule,
/// made testable, for the window.
public enum DisplaySelection {

    /// Converts a browser window's reported position into an AppKit rect.
    ///
    /// `screenX`/`screenY` are the window FRAME origin in TOP-LEFT screen
    /// coordinates — verified against the WindowServer, which reported the same
    /// origin the page did. AppKit's y runs the other way, hence the flip.
    public static func appKitWindowFrame(screenX: Double,
                                         screenY: Double,
                                         outerWidth: Double,
                                         outerHeight: Double,
                                         primaryScreenHeight: Double) -> NSRect {
        NSRect(x: screenX,
               y: primaryScreenHeight - screenY - outerHeight,
               width: outerWidth,
               height: outerHeight)
    }

    /// Index of the display the window mostly occupies, or nil if it overlaps none.
    ///
    /// Largest intersection, NOT containment of a corner. A window flush against
    /// the top of the display has its top edge exactly at the screen's `maxY`,
    /// and `NSRect.contains` treats `maxY` as outside — so a corner test on that
    /// window matches no screen at all and silently falls through to a fallback.
    /// On a single display the fallback is the same screen and the bug is
    /// invisible; on a mixed-DPI setup it silently picks the wrong backing scale.
    ///
    /// Ties resolve to the lowest index, so the answer never depends on the
    /// order `NSScreen.screens` happened to return.
    public static func displayIndex(for windowFrame: NSRect, in screenFrames: [NSRect]) -> Int? {
        var best: Int?
        var bestArea: CGFloat = 0
        for (index, frame) in screenFrames.enumerated() {
            let overlap = windowFrame.intersection(frame)
            guard !overlap.isNull else { continue }
            let area = overlap.width * overlap.height
            if area > bestArea {
                bestArea = area
                best = index
            }
        }
        return bestArea > 0 ? best : nil
    }

    /// The backing scale factor to use for a browser window, falling back to the
    /// primary display when the window overlaps no screen at all.
    @MainActor
    public static func backingScaleFactor(forWindowFrame windowFrame: NSRect,
                                          screens: [NSScreen] = NSScreen.screens) -> Double {
        let frames = screens.map(\.frame)
        if let index = displayIndex(for: windowFrame, in: frames) {
            return Double(screens[index].backingScaleFactor)
        }
        return Double(screens.first?.backingScaleFactor ?? 2)
    }
}
