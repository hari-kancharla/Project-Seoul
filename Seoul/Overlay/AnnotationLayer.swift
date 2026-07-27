import AppKit
import QuartzCore

/// The view that actually holds a sketch. One per `OverlayPanel`, and it shows
/// at most one annotation at a time.
///
/// GEOMETRY MODEL
///
/// Every path is built with the target rect normalised to the origin, and the
/// whole annotation lives inside a single container `CALayer` whose
/// `anchorPoint` is `(0, 0)`. Re-anchoring during a scroll is then one
/// assignment — `container.position` — with no path arithmetic and, critically,
/// no reason to touch the random stream. That is what makes scroll tracking
/// wobble-free by construction rather than by care.
///
/// The view is unflipped (AppKit's y-up), matching the space
/// `CoordinateTransform` already produces, so a screen rect needs only a
/// translation to become a local rect.
/// Everything needed to see, in one glance, whether a misalignment is a
/// RENDERER bug or a COORDINATE bug.
///
/// If the cyan rectangle sits on the target element and only the sketch is
/// offset, the transform is right and the drawing is wrong. If the cyan
/// rectangle is offset too, the sketch is faithfully drawing a rect that was
/// computed wrong, and the bug is upstream in the coordinate math. Those two
/// have completely different fixes and are otherwise almost impossible to tell
/// apart by eye.
/// Not `Sendable`: it carries milestone-1's `PageRect` and `ViewportContext`,
/// which are not, and that file is not ours to change. It never leaves the main
/// actor anyway.
public struct OverlayDebugInfo {
    public let pageRect: PageRect
    public let context: ViewportContext
    public let pageZoom: Double
    public let chromeLeft: Double
    public let chromeTop: Double
    public let appKitRect: NSRect
    /// The browser's content area in AppKit screen coordinates.
    public let viewportRect: NSRect
    /// True when the viewport origin came from a real pointer event rather than
    /// from outer/inner arithmetic.
    public let calibrated: Bool

    /// The viewport's top edge in TOP-LEFT screen coordinates — the same space
    /// as `screenY`, so the two are directly comparable in the readout.
    /// `viewportRect` is AppKit, whose minY is the BOTTOM edge; printing that
    /// next to `screenY` invites exactly the misreading this line avoids.
    public var viewportTopLeftY: Double { context.screenY + chromeTop }

    public init(pageRect: PageRect, context: ViewportContext, pageZoom: Double,
                chromeLeft: Double, chromeTop: Double, appKitRect: NSRect,
                viewportRect: NSRect, calibrated: Bool) {
        self.pageRect = pageRect
        self.context = context
        self.pageZoom = pageZoom
        self.chromeLeft = chromeLeft
        self.chromeTop = chromeTop
        self.appKitRect = appKitRect
        self.viewportRect = viewportRect
        self.calibrated = calibrated
    }

    /// One-line-per-field dump, shared by the on-screen readout and stderr.
    public var readout: String {
        let c = context
        return """
        page rect     x \(f(pageRect.x)) y \(f(pageRect.y)) w \(f(pageRect.width)) h \(f(pageRect.height))
        screenX/Y     \(f(c.screenX)) , \(f(c.screenY))
        inner         \(f(c.innerWidth)) x \(f(c.innerHeight))
        outer         \(f(c.outerWidth)) x \(f(c.outerHeight))
        dpr / scale   \(f(c.devicePixelRatio)) / \(f(c.displayScaleFactor))
        pageZoom      \(f(pageZoom))
        chromeLeft    \(f(chromeLeft))
        chromeTop     \(f(chromeTop))
        viewport org  \(f(viewportRect.minX)) , \(f(viewportTopLeftY))  \(calibrated ? "(pointer-calibrated)" : "(DERIVED - unreliable)")
        AppKit rect   x \(f(appKitRect.minX)) y \(f(appKitRect.minY)) w \(f(appKitRect.width)) h \(f(appKitRect.height))
        """
    }

    private func f(_ v: Double) -> String { String(format: "%.1f", v) }
    private func f(_ v: CGFloat) -> String { String(format: "%.1f", Double(v)) }
}

@MainActor
public final class AnnotationLayer: NSView {

    public typealias Gesture = OverlayPanel.Gesture

    /// Draws the coordinate diagnostics alongside the sketch. Default ON while
    /// the alignment work is live.
    public static var debugEnabled = true

    // MARK: - Appearance

    /// One accent, always. Contrast comes from the shadow layer beneath, never
    /// from sampling the page — sampling is a frame behind during scroll and
    /// flickers on gradients and images.
    private static let accent = NSColor(srgbRed: 1.0, green: 0x7A / 255.0, blue: 0x45 / 255.0, alpha: 1)
    private static let pass0Width: CGFloat = 2.5
    private static let pass1Width: CGFloat = 1.5
    private static let pass1Alpha: Float = 0.45
    private static let roughness: Double = 1.0
    /// Boxes get their own, calmer roughness. Every other gesture is a free
    /// stroke where wander reads as character; a box has four straight sides and
    /// the same wander there just reads as a failure to draw a rectangle.
    private static let boxRoughness: Double = 0.45

    private static let drawOnDuration: CFTimeInterval = 0.20
    private static let pass1Delay: CFTimeInterval = 0.06
    private static let labelFadeDuration: CFTimeInterval = 0.15

    // MARK: - Live state

    private var container: CALayer?
    private var currentID: String?
    private var currentGesture: Gesture = .box
    private var currentSize: NSSize = .zero
    private var currentLabel: String?

    // MARK: - Init

    public override init(frame frameRect: NSRect) {
        super.init(frame: frameRect)
        wantsLayer = true
        layer?.masksToBounds = false
    }

    @available(*, unavailable)
    required init?(coder: NSCoder) { fatalError("AnnotationLayer is code-only") }

    /// Belt and braces alongside the panel's `ignoresMouseEvents`: nothing in
    /// this view is ever a click target.
    public override func hitTest(_ point: NSPoint) -> NSView? { nil }

    public override func viewDidChangeBackingProperties() {
        super.viewDidChangeBackingProperties()
        let scale = window?.backingScaleFactor ?? 2
        container?.contentsScale = scale
        container?.sublayers?.forEach { $0.contentsScale = scale }
    }

    // MARK: - Public API

    /// - Parameter rect: the target, in this view's coordinates.
    /// - Parameter elementID: identity, not content. The same ID must mean the
    ///   same element across frames; that is the whole contract this class rests on.
    public func annotate(rect: NSRect, gesture: Gesture = .auto, label: String?, elementID: String) {
        annotate(rect: rect, gesture: gesture, label: label, elementID: elementID,
                 replayDrawOn: true, debug: nil)
    }

    func annotate(rect: NSRect, gesture: Gesture, label: String?, elementID: String,
                  replayDrawOn: Bool) {
        annotate(rect: rect, gesture: gesture, label: label, elementID: elementID,
                 replayDrawOn: replayDrawOn, debug: nil)
    }

    /// - Parameter replayDrawOn: false when this element was already on screen a
    ///   moment ago on *another* screen's panel. A window can straddle two
    ///   displays, and an element scrolling across the seam re-routes to a panel
    ///   that has never seen its ID — which would otherwise replay the draw-on
    ///   every few frames while the user scrolls. The router knows this; the view
    ///   cannot.
    func annotate(rect: NSRect, gesture: Gesture, label: String?, elementID: String,
                  replayDrawOn: Bool, debug: OverlayDebugInfo?) {
        updateDebugOverlay(target: rect, info: debug)
        let resolved = gesture == .auto ? AnnotationLayer.autoGesture(for: rect) : gesture
        let origin = CGPoint(x: rect.minX, y: rect.minY)

        // The single rule: the draw-on animation replays if and only if the
        // element changed. Everything else about the same element — it moved, it
        // resized, its label changed — is a silent update, because a sketch that
        // re-animates while you scroll is unusable.
        guard elementID == currentID, let container else {
            rebuildFromScratch(rect: rect, gesture: resolved, label: label,
                               elementID: elementID, animated: replayDrawOn)
            return
        }

        let sameShape = resolved == currentGesture
            && abs(rect.width - currentSize.width) < 0.5
            && abs(rect.height - currentSize.height) < 0.5
            && label == currentLabel

        CATransaction.begin()
        CATransaction.setDisableActions(true)
        CATransaction.setAnimationDuration(0)
        if sameShape {
            // Pure translation. No geometry is recomputed at all.
            container.position = origin
        } else {
            // The element genuinely changed shape. Rebuild the paths, but from a
            // stream re-seeded with the same elementID, so the jitter pattern is
            // the one this element has always had — and still without replaying
            // the draw-on.
            rebuildFromScratch(rect: rect, gesture: resolved, label: label,
                               elementID: elementID, animated: false)
        }
        CATransaction.commit()

        // currentSize/currentGesture/currentLabel are owned exclusively by
        // rebuildFromScratch, and must NOT be refreshed here. They describe the
        // geometry that was actually built, not the last rect seen. Advancing
        // them on the translate-only path turns the 0.5pt epsilon above from a
        // bound on how far the drawing may drift from the element into a
        // per-call rate limit: a rect growing 0.4pt a frame is then accepted
        // forever, and the sketch stays sized to the very first frame while the
        // element doubles underneath it. On the fast path sameShape has already
        // proved gesture and label unchanged, so there is nothing to refresh.
    }

    public func clear() {
        removeDebugOverlay()
        // annotate() runs on every scroll frame, and the panel router calls
        // clear() on every non-target screen each time. Without this guard that
        // is a CATransaction per idle display per frame, forever.
        guard container != nil else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        container?.removeFromSuperlayer()
        CATransaction.commit()
        container = nil
        currentID = nil
        currentSize = .zero
        currentLabel = nil
    }

    // MARK: - Debug overlay

    private var debugCyan: CAShapeLayer?
    private var debugMagenta: CAShapeLayer?
    private var debugText: CATextLayer?

    /// Draws the raw geometry the transform produced, with no jitter, no
    /// roughness and no inset, so it can be compared against the sketch and
    /// against the element underneath.
    ///
    /// These live on the ROOT layer in view coordinates rather than inside the
    /// annotation's container, because the container translates on every scroll
    /// re-anchor and the browser viewport does not move with the element.
    private func updateDebugOverlay(target: NSRect, info: OverlayDebugInfo?) {
        guard Self.debugEnabled, let info else {
            removeDebugOverlay()
            return
        }
        let scale = window?.backingScaleFactor ?? 2
        let origin = window?.frame.origin ?? .zero
        // Screen (AppKit) -> this view's coordinates.
        let viewportLocal = info.viewportRect.offsetBy(dx: -origin.x, dy: -origin.y)

        CATransaction.begin()
        CATransaction.setDisableActions(true)

        func hairline(_ existing: CAShapeLayer?, rect: NSRect, color: CGColor) -> CAShapeLayer {
            let layer = existing ?? CAShapeLayer()
            // Inset by half a line width so a 1pt stroke lands ON the boundary
            // rather than straddling it; otherwise the marker reads half a point
            // off from the thing it is supposed to be measuring.
            layer.path = CGPath(rect: rect.insetBy(dx: 0.5, dy: 0.5), transform: nil)
            layer.fillColor = nil
            layer.strokeColor = color
            layer.lineWidth = 1
            layer.contentsScale = scale
            layer.masksToBounds = false
            if existing == nil { self.layer?.addSublayer(layer) }
            return layer
        }

        debugCyan = hairline(debugCyan, rect: target, color: NSColor.cyan.cgColor)
        debugMagenta = hairline(debugMagenta, rect: viewportLocal, color: NSColor.magenta.cgColor)

        let body = NSAttributedString(string: info.readout, attributes: [
            .font: NSFont.monospacedSystemFont(ofSize: 10, weight: .regular),
            .foregroundColor: NSColor.cyan,
        ])
        let text = debugText ?? CATextLayer()
        text.string = body
        text.contentsScale = scale
        text.isWrapped = false
        text.alignmentMode = .left
        text.backgroundColor = NSColor.black.withAlphaComponent(0.72).cgColor
        let size = body.size()
        text.frame = CGRect(x: 12, y: 12, width: ceil(size.width) + 16, height: ceil(size.height) + 12)
        if debugText == nil { layer?.addSublayer(text) }
        debugText = text

        CATransaction.commit()
    }

    private func removeDebugOverlay() {
        guard debugCyan != nil || debugMagenta != nil || debugText != nil else { return }
        CATransaction.begin()
        CATransaction.setDisableActions(true)
        debugCyan?.removeFromSuperlayer()
        debugMagenta?.removeFromSuperlayer()
        debugText?.removeFromSuperlayer()
        CATransaction.commit()
        debugCyan = nil
        debugMagenta = nil
        debugText = nil
    }

    // MARK: - Gesture selection

    /// Picked from shape alone. `.arrow` is never chosen automatically: an arrow
    /// asserts a direction, and nothing in a bare rect says which way to point.
    ///
    /// `nonisolated` because it is a pure function of a rectangle — it reads no
    /// view state and has no reason to demand the main thread.
    public nonisolated static func autoGesture(for rect: NSRect) -> Gesture {
        let w = Double(rect.width)
        let h = Double(rect.height)
        guard h > 0, w > 0 else { return .box }
        let aspect = w / h

        if h > 120 { return .bracket }
        if aspect > 3 && h < 40 { return .underline }
        if (0.6...1.6).contains(aspect) && max(w, h) < 64 { return .circle }
        if aspect > 2 && (24...80).contains(h) { return .box }
        return .box
    }

    // MARK: - Building

    private func rebuildFromScratch(rect: NSRect, gesture: Gesture, label: String?,
                                    elementID: String, animated: Bool) {
        container?.removeFromSuperlayer()

        let scale = window?.backingScaleFactor ?? NSScreen.screens.first?.backingScaleFactor ?? 2
        let local = NSRect(origin: .zero, size: rect.size)

        // One stream for the whole annotation, seeded only by identity.
        var rng = SeededRNG(elementID: elementID)
        let (pass0Path, pass1Path) = strokePaths(for: gesture, in: local, rng: &rng)

        let box = CGRect(origin: .zero,
                         size: CGSize(width: max(rect.width, 1) + 240,
                                      height: max(rect.height, 1) + 240))

        let container = CALayer()
        container.anchorPoint = .zero
        container.bounds = box
        container.position = CGPoint(x: rect.minX, y: rect.minY)
        container.contentsScale = scale
        container.masksToBounds = false

        // Beneath everything: the same stroke in black, whose blurred shadow is
        // the only reason one accent colour survives both a white page and a
        // dark one. shadowPath is deliberately left nil — CA derives the shadow
        // from the rendered stroke, whereas a shadowPath would shadow the
        // path's *fill*, which for an open stroke is a solid blob.
        let shadow = makeStrokeLayer(path: pass0Path, color: NSColor.black.cgColor,
                                     width: Self.pass0Width, alpha: 1, box: box, scale: scale)
        shadow.shadowColor = NSColor.black.cgColor
        shadow.shadowOpacity = 0.35
        shadow.shadowRadius = 6
        shadow.shadowOffset = .zero

        let pass0 = makeStrokeLayer(path: pass0Path, color: Self.accent.cgColor,
                                    width: Self.pass0Width, alpha: 1, box: box, scale: scale)
        let pass1 = makeStrokeLayer(path: pass1Path, color: Self.accent.cgColor,
                                    width: Self.pass1Width, alpha: Self.pass1Alpha, box: box, scale: scale)

        container.addSublayer(shadow)
        container.addSublayer(pass0)
        container.addSublayer(pass1)

        // Built last, so adding or removing a label can never shift the strokes:
        // the label's draws come after theirs in the stream.
        var labelLayers: [CALayer] = []
        if let label, !label.isEmpty {
            labelLayers = makeLabelLayers(label, gesture: gesture, size: rect.size,
                                          scale: scale, rng: &rng)
            labelLayers.forEach(container.addSublayer)
        }

        layer?.addSublayer(container)
        self.container = container
        self.currentID = elementID
        self.currentGesture = gesture
        self.currentSize = rect.size
        self.currentLabel = label

        if animated {
            drawOn(shadow, delay: 0)
            drawOn(pass0, delay: 0)
            drawOn(pass1, delay: Self.pass1Delay)
            fadeIn(labelLayers, after: Self.drawOnDuration + Self.pass1Delay)
        } else {
            // Silent rebuild (same element, new shape). The layers are born at
            // opacity 0 for the fade-in; with no animation to run they must be
            // shown outright, or the label vanishes for the rest of its life.
            labelLayers.forEach { $0.opacity = 1 }
        }
    }

    private func strokePaths(for gesture: Gesture, in local: NSRect,
                             rng: inout SeededRNG) -> (NSBezierPath, NSBezierPath) {
        // Parameters that both passes must share are rolled once, up front.
        // Letting each pass roll its own overshoot makes one loop visibly longer
        // than the other, which reads as a mistake rather than as a second pass.
        switch gesture {
        case .circle:
            let target = local.insetBy(dx: -7, dy: -7)
            let overshoot = rng.double(in: 4...12)
            return (Sketch.roughEllipse(in: target, roughness: Self.roughness,
                                        overshoot: overshoot, pass: 0, rng: &rng),
                    Sketch.roughEllipse(in: target, roughness: Self.roughness,
                                        overshoot: overshoot, pass: 1, rng: &rng))

        case .underline:
            // Quoted for a 260pt span; roughUnderline scales it to the real width.
            // Absolute points, not scaled by width — see roughUnderline. On a
            // wide field anything more reads as a slash across the page.
            let rise = rng.double(in: 1...4)
            return (Sketch.roughUnderline(under: local, roughness: Self.roughness,
                                          rise: rise, pass: 0, rng: &rng),
                    Sketch.roughUnderline(under: local, roughness: Self.roughness,
                                          rise: rise, pass: 1, rng: &rng))

        case .bracket:
            return (Sketch.roughBracket(leftOf: local, roughness: Self.roughness, pass: 0, rng: &rng),
                    Sketch.roughBracket(leftOf: local, roughness: Self.roughness, pass: 1, rng: &rng))

        case .arrow:
            let (tail, tip) = Self.arrowPoints(size: local.size)
            // Rolled from the element's own stream, so it survives a resize: the
            // rect changes but the seed does not, and the arrow keeps sweeping
            // the way it always has.
            let bow = (rng.coin() ? 1.0 : -1.0) * rng.double(in: 0.12...0.22)
            return (Sketch.roughArrow(from: tail, to: tip, headLength: 15,
                                      roughness: Self.roughness, bow: bow, pass: 0, rng: &rng),
                    Sketch.roughArrow(from: tail, to: tip, headLength: 15,
                                      roughness: Self.roughness, bow: bow, pass: 1, rng: &rng))

        case .box, .auto:
            let target = local.insetBy(dx: -6, dy: -5)
            // A form input is a rectangle and should still look like one. At
            // radius 7 with full roughness the corners round off, the edges
            // wander, and a 320x38 field comes out as a wobbly ellipse. Tighter
            // corners and calmer sides keep the sides straight-ish — a
            // hand-drawn box, not a hand-drawn blob.
            return (Sketch.roughRoundedRect(in: target, cornerRadius: 3,
                                            roughness: Self.boxRoughness, pass: 0, rng: &rng),
                    Sketch.roughRoundedRect(in: target, cornerRadius: 3,
                                            roughness: Self.boxRoughness, pass: 1, rng: &rng))
        }
    }

    /// Where an arrow starts and ends for a given target, in local coordinates.
    /// Shared by the path and the label so the capsule always sits on the tail.
    private static func arrowPoints(size: NSSize) -> (tail: NSPoint, tip: NSPoint) {
        let tip = NSPoint(x: -10, y: size.height / 2)
        return (NSPoint(x: tip.x - 118, y: tip.y + 72), tip)
    }

    private func makeStrokeLayer(path: NSBezierPath, color: CGColor, width: CGFloat,
                                 alpha: Float, box: CGRect, scale: CGFloat) -> CAShapeLayer {
        let layer = CAShapeLayer()
        layer.path = path.cgPath
        layer.fillColor = nil
        layer.strokeColor = color
        layer.lineWidth = width
        layer.lineCap = .round
        layer.lineJoin = .round
        layer.opacity = alpha
        layer.anchorPoint = .zero
        layer.bounds = box
        layer.position = .zero
        layer.contentsScale = scale
        layer.masksToBounds = false
        return layer
    }

    // MARK: - Label

    private func makeLabelLayers(_ text: String, gesture: Gesture, size: NSSize,
                                 scale: CGFloat, rng: inout SeededRNG) -> [CALayer] {
        let font = NSFont.systemFont(ofSize: 12, weight: .semibold)
        let attributed = NSAttributedString(string: text, attributes: [
            .font: font,
            .foregroundColor: NSColor.white,
        ])
        let textSize = attributed.size()
        let padding = NSSize(width: 11, height: 5)
        let capsuleSize = NSSize(width: ceil(textSize.width) + padding.width * 2,
                                 height: ceil(textSize.height) + padding.height * 2)

        let anchor = Self.labelAnchor(gesture: gesture, size: size, capsule: capsuleSize)
        let capsuleRect = NSRect(origin: anchor, size: capsuleSize)

        // A rough capsule, not a rounded rect: a crisp pill next to a sketched
        // stroke looks like a system tooltip that wandered in.
        let capsulePath = Sketch.roughRoundedRect(in: capsuleRect,
                                                  cornerRadius: capsuleSize.height / 2,
                                                  roughness: 0.55, pass: 0, rng: &rng)

        let box = CGRect(origin: .zero,
                         size: CGSize(width: max(size.width, 1) + 240,
                                      height: max(size.height, 1) + 240))
        let capsule = makeStrokeLayer(path: capsulePath, color: Self.accent.cgColor,
                                      width: 1.5, alpha: 1, box: box, scale: scale)
        // Filled so the text stays legible over whatever is behind it. The fill
        // is a fixed dark, not a sampled one — same reasoning as the stroke.
        capsule.fillColor = NSColor.black.withAlphaComponent(0.72).cgColor
        capsule.shadowColor = NSColor.black.cgColor
        capsule.shadowOpacity = 0.35
        capsule.shadowRadius = 6
        capsule.shadowOffset = .zero
        capsule.opacity = 0

        let textLayer = CATextLayer()
        textLayer.string = attributed
        textLayer.alignmentMode = .center
        textLayer.contentsScale = scale
        textLayer.anchorPoint = .zero
        textLayer.bounds = CGRect(origin: .zero, size: textSize)
        textLayer.position = CGPoint(x: capsuleRect.minX + padding.width,
                                     y: capsuleRect.minY + padding.height)
        textLayer.opacity = 0

        return [capsule, textLayer]
    }

    /// Near the gesture's tail — the end a hand would have finished on.
    private static func labelAnchor(gesture: Gesture, size: NSSize, capsule: NSSize) -> NSPoint {
        switch gesture {
        case .bracket:
            return NSPoint(x: -28 - capsule.width, y: size.height - capsule.height / 2)
        case .underline:
            return NSPoint(x: -6, y: -14 - capsule.height)
        case .arrow:
            let (tail, _) = arrowPoints(size: size)
            return NSPoint(x: tail.x - capsule.width * 0.5, y: tail.y + 8)
        case .circle, .box, .auto:
            return NSPoint(x: -8, y: size.height + 14)
        }
    }

    // MARK: - Animation
    //
    // Core Animation drives these on the render server. There is no timer and no
    // CVDisplayLink anywhere in this class; nothing here ticks between calls to
    // annotate() and clear().

    private func drawOn(_ layer: CAShapeLayer, delay: CFTimeInterval) {
        let animation = CABasicAnimation(keyPath: "strokeEnd")
        animation.fromValue = 0
        animation.toValue = 1
        animation.duration = Self.drawOnDuration
        animation.timingFunction = CAMediaTimingFunction(name: .easeOut)
        animation.beginTime = CACurrentMediaTime() + delay
        // Without .backwards the layer renders fully stroked during the delay,
        // so pass 1 would appear complete before it starts drawing.
        animation.fillMode = .backwards
        layer.strokeEnd = 1
        layer.add(animation, forKey: "drawOn")
    }

    private func fadeIn(_ layers: [CALayer], after delay: CFTimeInterval) {
        for layer in layers {
            let animation = CABasicAnimation(keyPath: "opacity")
            animation.fromValue = 0
            animation.toValue = 1
            animation.duration = Self.labelFadeDuration
            animation.timingFunction = CAMediaTimingFunction(name: .easeOut)
            animation.beginTime = CACurrentMediaTime() + delay
            animation.fillMode = .backwards
            layer.opacity = 1
            layer.add(animation, forKey: "fadeIn")
        }
    }
}
