import AppKit

// Seoul coordinate transform: page rect -> macOS AppKit screen rect.
//
// This is a line-for-line port of core/transform.mjs, whose 11 test vectors
// were executed and verified before this file was written. The XCTest cases in
// CoordinateTransformTests.swift use the identical numbers.
//
// TWO CORRECTIONS TO THE OBVIOUS APPROACH, both of which produce bugs that only
// show up on some machines and are therefore expensive to find later:
//
//  1. backingScaleFactor plays NO part in this transform.
//     NSWindow and NSRect frames are in POINTS, not device pixels, and
//     window.screenX / window.screenY are CSS px that map 1:1 to points on
//     macOS. Dividing by backingScaleFactor here is a bug visible only on
//     Retina hardware.
//
//  2. innerWidth / innerHeight are page CSS px and SHRINK as the user zooms in.
//     outerWidth / outerHeight are screen points and do not change. So the
//     browser chrome offset must be computed as
//         outerHeight - innerHeight * pageZoom
//     Using outerHeight - innerHeight drifts by 25pt at 125% zoom and 62pt at
//     200% zoom, which is the difference between landing on a field and landing
//     on a different row entirely.
//
// Coordinate spaces:
//   A. page      getBoundingClientRect(), CSS px, viewport-relative, y down
//   B. screen-TL macOS global points, origin top-left of primary, y down
//   C. AppKit    macOS global points, origin bottom-left of primary, y up

public struct PageRect: Equatable {
    public let x: Double
    public let y: Double
    public let width: Double
    public let height: Double

    public init(x: Double, y: Double, width: Double, height: Double) {
        self.x = x; self.y = y; self.width = width; self.height = height
    }
}

/// Reported by the content script in the same frame as the rect it accompanies.
/// Never reconstruct these values on the Swift side; they must come from the
/// same layout pass as the rect or the arrow will lag by one frame during scroll.
public struct ViewportContext: Equatable {
    public let screenX: Double           // window.screenX, points, top-left origin
    public let screenY: Double           // window.screenY
    public let innerWidth: Double        // page CSS px, shrinks with zoom
    public let innerHeight: Double
    public let outerWidth: Double        // screen points, constant under zoom
    public let outerHeight: Double
    public let devicePixelRatio: Double  // window.devicePixelRatio
    public let displayScaleFactor: Double // NSScreen.backingScaleFactor, known to Swift

    public init(screenX: Double, screenY: Double,
                innerWidth: Double, innerHeight: Double,
                outerWidth: Double, outerHeight: Double,
                devicePixelRatio: Double, displayScaleFactor: Double) {
        self.screenX = screenX; self.screenY = screenY
        self.innerWidth = innerWidth; self.innerHeight = innerHeight
        self.outerWidth = outerWidth; self.outerHeight = outerHeight
        self.devicePixelRatio = devicePixelRatio
        self.displayScaleFactor = displayScaleFactor
    }
}

public enum CoordinateTransform {

    /// Page zoom isolated from display scaling.
    /// Retina at 100%: dpr 2 / scale 2 = 1.0. Retina at 125%: 2.5 / 2 = 1.25.
    public static func pageZoom(_ ctx: ViewportContext) -> Double {
        guard ctx.displayScaleFactor > 0 else { return 1 }
        return ctx.devicePixelRatio / ctx.displayScaleFactor
    }

    /// The primary screen is the one whose origin is (0, 0), which is
    /// `NSScreen.screens.first`. It is NOT `NSScreen.main`, which is whichever
    /// screen currently has keyboard focus. Using `.main` here breaks the flip
    /// the moment the user moves the window to a second display.
    public static func primaryScreenHeight() -> Double {
        Double(NSScreen.screens.first?.frame.height ?? 0)
    }

    public static func pageRectToScreenRect(
        _ rect: PageRect,
        context ctx: ViewportContext,
        primaryScreenHeight: Double
    ) -> NSRect {
        let zoom = pageZoom(ctx)

        // Browser chrome. On macOS there is no side chrome, so chromeLeft is 0
        // and the whole vertical delta is tab strip plus toolbar. Computed
        // defensively so a later Windows port does not silently break.
        let chromeLeft = max(0, (ctx.outerWidth - ctx.innerWidth * zoom) / 2)
        let chromeTop = max(0, ctx.outerHeight - ctx.innerHeight * zoom - chromeLeft)

        // Page space -> screen top-left. The rect scales by zoom; screenX and
        // screenY are already points and must not be scaled.
        let tlX = ctx.screenX + chromeLeft + rect.x * zoom
        let tlY = ctx.screenY + chromeTop + rect.y * zoom
        let w = rect.width * zoom
        let h = rect.height * zoom

        // screen top-left -> AppKit. x is unchanged. Subtracting h anchors the
        // AppKit rect at its bottom edge. Negative tlX (a display to the left)
        // and negative tlY (a display above) need no special casing.
        let appKitY = primaryScreenHeight - tlY - h

        return NSRect(x: tlX, y: appKitY, width: w, height: h)
    }

    public static func appKitRectToScreenTopLeft(_ r: NSRect, primaryScreenHeight: Double) -> NSRect {
        NSRect(x: r.origin.x,
               y: primaryScreenHeight - r.origin.y - r.height,
               width: r.width,
               height: r.height)
    }

    /// Which display the arrow should be drawn on, by largest intersection area.
    /// Returns nil when the rect is entirely offscreen, in which case the caller
    /// should scroll the element into view rather than drawing anything.
    public static func screen(for rect: NSRect, in screens: [NSScreen] = NSScreen.screens) -> NSScreen? {
        var best: NSScreen?
        var bestArea: CGFloat = 0
        for s in screens {
            let inter = rect.intersection(s.frame)
            guard !inter.isNull else { continue }
            let area = inter.width * inter.height
            if area > bestArea { bestArea = area; best = s }
        }
        return bestArea > 0 ? best : nil
    }
}
