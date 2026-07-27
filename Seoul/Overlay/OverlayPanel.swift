import AppKit

/// A transparent, click-through, screen-sized panel that carries one sketch.
///
/// One panel is created per `NSScreen`, lazily, and a target rect is routed to
/// whichever screen it mostly falls on via `CoordinateTransform.screen(for:in:)`.
///
/// THE STYLE MASK IS SET ONCE AND NEVER TOUCHED AGAIN
///
/// `.nonactivatingPanel` is in the *initial* mask and is never assigned
/// afterwards. Assigning it later leaves the WindowServer's activation tag in
/// place: the panel then looks key, draws key, and silently receives no key
/// events. There is no property on this class that mutates `styleMask`, and
/// there must never be one.
@MainActor
public final class OverlayPanel: NSPanel {

    public enum Gesture: Equatable {
        case auto, circle, box, underline, bracket, arrow
    }

    private let annotationView = AnnotationLayer(frame: .zero)

    // MARK: - Init

    public init(screen: NSScreen) {
        super.init(contentRect: screen.frame,
                   styleMask: [.borderless, .nonactivatingPanel],   // set once; never reassigned
                   backing: .buffered,
                   defer: false)

        level = .screenSaver
        isOpaque = false
        backgroundColor = .clear
        hasShadow = false
        ignoresMouseEvents = true
        sharingType = .none
        collectionBehavior = [.canJoinAllSpaces, .stationary, .fullScreenAuxiliary]
        isReleasedWhenClosed = false

        // Not in the spec, and the overlay does not work without it: NSPanel
        // defaults hidesOnDeactivate to true, and this app is an accessory that
        // is *never* the active app. Left at the default, the sketch disappears
        // the instant it would first be useful.
        hidesOnDeactivate = false

        annotationView.frame = NSRect(origin: .zero, size: screen.frame.size)
        annotationView.autoresizingMask = [.width, .height]
        contentView = annotationView

        // A borderless window already refuses key status, but say so explicitly:
        // "the app must never come to the front" is a requirement here, not an
        // incidental default.
        setFrame(screen.frame, display: false)
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("OverlayPanel is code-only") }

    public override var canBecomeKey: Bool { false }
    public override var canBecomeMain: Bool { false }

    // MARK: - Public API

    /// - Parameter rect: the target, in AppKit global screen coordinates — the
    ///   space `CoordinateTransform.pageRectToScreenRect` produces.
    public func annotate(rect: NSRect, gesture: Gesture = .auto, label: String?, elementID: String) {
        annotate(rect: rect, gesture: gesture, label: label, elementID: elementID, replayDrawOn: true)
    }

    func annotate(rect: NSRect, gesture: Gesture, label: String?, elementID: String,
                  replayDrawOn: Bool, debug: OverlayDebugInfo? = nil) {
        let local = NSRect(x: rect.minX - frame.minX,
                           y: rect.minY - frame.minY,
                           width: rect.width,
                           height: rect.height)
        annotationView.annotate(rect: local, gesture: gesture, label: label,
                                elementID: elementID, replayDrawOn: replayDrawOn, debug: debug)
        orderFrontRegardless()   // never makeKeyAndOrderFront: that would activate the app
    }

    /// Whether the coordinate diagnostics are drawn alongside the sketch.
    public static var debugEnabled: Bool {
        get { AnnotationLayer.debugEnabled }
        set { AnnotationLayer.debugEnabled = newValue }
    }

    public func clear() {
        annotationView.clear()
        orderOut(nil)
    }

    // MARK: - Per-screen routing

    private static var panels: [CGDirectDisplayID: OverlayPanel] = [:]
    private static var observingScreenChanges = false
    private static var lastElementID: String?

    /// The panel for `screen`, created on first use.
    public static func panel(for screen: NSScreen) -> OverlayPanel {
        observeScreenChangesIfNeeded()
        let id = displayID(of: screen)
        if let existing = panels[id] {
            // A display can be rearranged or rescaled without being removed.
            if existing.frame != screen.frame {
                existing.setFrame(screen.frame, display: false)
            }
            return existing
        }
        let panel = OverlayPanel(screen: screen)
        panels[id] = panel
        return panel
    }

    /// Routes to the screen the rect mostly falls on and annotates there.
    public static func annotate(rect: NSRect, gesture: Gesture = .auto, label: String?, elementID: String) {
        annotate(rect: rect, gesture: gesture, label: label, elementID: elementID, debug: nil)
    }

    /// As above, plus the coordinate diagnostics. The spec'd four-argument form
    /// is unchanged and still the one production code should call.
    public static func annotate(rect: NSRect, gesture: Gesture = .auto, label: String?,
                                elementID: String, debug: OverlayDebugInfo?) {
        guard let screen = CoordinateTransform.screen(for: rect) else {
            // Entirely offscreen. Drawing at the clamped edge would point at the
            // wrong thing; the caller's job is to scroll it into view first.
            clear()
            return
        }
        let target = panel(for: screen)
        // Exactly one annotation exists at a time. Without this, dragging a
        // window across displays leaves the previous sketch stranded on the old
        // one, with nothing that will ever clear it.
        for other in panels.values where other !== target && other.isVisible { other.clear() }

        // A window can straddle two displays, so scrolling one element can
        // re-route it between panels repeatedly. The receiving panel has never
        // seen the ID and would replay the draw-on each time; tracking the ID
        // here keeps the crossing silent.
        target.annotate(rect: rect, gesture: gesture, label: label, elementID: elementID,
                        replayDrawOn: elementID != lastElementID, debug: debug)
        lastElementID = elementID
    }

    /// Clears every screen.
    public static func clear() {
        lastElementID = nil          // a later annotate() of the same element is a fresh draw
        for panel in panels.values { panel.clear() }
    }

    private static func displayID(of screen: NSScreen) -> CGDirectDisplayID {
        (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?
            .uint32Value ?? 0
    }

    private static func observeScreenChangesIfNeeded() {
        guard !observingScreenChanges else { return }
        observingScreenChanges = true
        NotificationCenter.default.addObserver(
            forName: NSApplication.didChangeScreenParametersNotification,
            object: nil,
            queue: .main
        ) { _ in
            MainActor.assumeIsolated { screenParametersChanged() }
        }
    }

    /// Displays come and go. A panel whose display is gone is sized to a screen
    /// that no longer exists, so it is closed; the survivors are re-framed.
    private static func screenParametersChanged() {
        var live: [CGDirectDisplayID: NSScreen] = [:]
        for screen in NSScreen.screens { live[displayID(of: screen)] = screen }

        for (id, panel) in panels {
            guard let screen = live[id] else {
                panel.clear()
                panel.close()          // safe: isReleasedWhenClosed is false
                panels.removeValue(forKey: id)
                continue
            }
            if panel.frame != screen.frame {
                panel.setFrame(screen.frame, display: false)
            }
        }
    }
}
