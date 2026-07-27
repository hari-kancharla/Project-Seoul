import Foundation
import AppKit   // NSBezierPath / NSPoint / NSRect only. No views, no drawing state.

/// Hand-drawn stroke geometry.
///
/// Every function here is pure: same `SeededRNG` state in, same path out. There
/// is no drawing, no graphics context and no view. `AnnotationLayer` owns all
/// the presentation; this file owns only shapes.
///
/// TECHNIQUE
///
/// A straight run is sampled into 6...10 points, each pushed off the true line
/// along the segment normal by a jittered amount scaled by roughness and by the
/// run's length. A smooth cubic is then fitted through those points
/// (Catmull-Rom, converted to Bezier), which turns the polyline of wobbles into
/// a continuous pen stroke rather than a jagged one.
///
/// Closed shapes are walked by arc length so a box and an ellipse can be
/// treated identically, and both are walked *past* their own start point by a
/// few degrees. Nothing drawn by a human closes cleanly, and the overshoot is
/// most of what sells it.
///
/// Each shape is drawn twice by the caller, `pass: 0` then `pass: 1`, from the
/// same `inout` stream. The second pass therefore consumes different numbers
/// than the first and lands slightly differently — the same reason a re-traced
/// pencil line never lies exactly on top of the first one.
public enum Sketch {

    // MARK: - Straight runs

    public static func roughLine(from a: NSPoint,
                                 to b: NSPoint,
                                 roughness: Double,
                                 pass: Int,
                                 rng: inout SeededRNG) -> NSBezierPath {
        smoothPath(through: jitteredPoints(from: a, to: b,
                                           roughness: roughness, pass: pass,
                                           bow: 0, rng: &rng))
    }

    // MARK: - Closed shapes

    /// - Parameter overshoot: degrees of travel past the closing point, 4...12.
    ///   Rolled by the caller rather than here so both passes overshoot by a
    ///   comparable amount instead of one looping visibly further than the other.
    public static func roughEllipse(in rect: NSRect,
                                    roughness: Double,
                                    overshoot: Double,
                                    pass: Int,
                                    rng: inout SeededRNG) -> NSBezierPath {
        let outline = Outline(ellipseOutline(in: rect))
        // An ellipse is two half-arcs; each gets the 6...10 subdivision.
        let samples = subdivisions(&rng) * 2
        let start = rng.double(in: 0...max(outline.length, 1))
        return roughOutline(outline,
                            samples: samples,
                            overshootDegrees: overshoot,
                            start: start,
                            characteristicSize: (rect.width + rect.height) / 2,
                            roughness: roughness,
                            pass: pass,
                            rng: &rng)
    }

    public static func roughRoundedRect(in rect: NSRect,
                                        cornerRadius: Double,
                                        roughness: Double,
                                        pass: Int,
                                        rng: inout SeededRNG) -> NSBezierPath {
        let outline = Outline(roundedRectOutline(in: rect, cornerRadius: cornerRadius))
        // Four edges, each subdivided in the 6...10 range.
        let samples = subdivisions(&rng) * 4
        let overshoot = rng.double(in: 4...12)
        let start = rng.double(in: 0...max(outline.length, 1))
        return roughOutline(outline,
                            samples: samples,
                            overshootDegrees: overshoot,
                            start: start,
                            characteristicSize: (rect.width + rect.height) / 2,
                            roughness: roughness,
                            pass: pass,
                            rng: &rng)
    }

    // MARK: - Gestures

    /// - Parameter rise: how much higher the right end finishes than the left.
    ///   A hand pivoting at the wrist tilts; a perfectly level underline reads
    ///   as a border, not as ink.
    public static func roughUnderline(under rect: NSRect,
                                      roughness: Double,
                                      rise: Double,
                                      pass: Int,
                                      rng: inout SeededRNG) -> NSBezierPath {
        let gap = 4.0
        let overhangLeft = rng.double(in: 2...7)
        let overhangRight = rng.double(in: 4...10)

        // Drawn on both passes, used only on pass 1. Keeping the two calls
        // symmetric in how many values they pull is what lets the caller run
        // pass 0 then pass 1 down one `inout` stream and still land on a
        // byte-identical path next frame.
        // Has to comfortably exceed pass 1's own wobble, or the two strokes just
        // cross each other repeatedly and read as one frayed line rather than
        // as two.
        let separation = rng.double(in: 3.0...5.5)

        // `rise` is ABSOLUTE — deliberately not scaled by the element's width.
        //
        // Scaling it looked right in isolation and wrong on a real page: a 600pt
        // field earned a ~14pt climb, which stops reading as an underline and
        // starts reading as a diagonal slash ruled across the paragraph. A hand
        // drawing a long underline does not tilt proportionally more; it tilts
        // about the same and just travels further. Flat is correct.
        let scaledRise = rise

        // Pass 1 is pushed clear of pass 0 rather than just made noisier. Two
        // nearly coincident strokes read as one badly aliased line; the visible
        // gap is what makes it look drawn twice.
        let passOffset = pass == 0 ? 0 : -separation
        // Only a slight boost: the separation above is doing the work, and more
        // wobble here would just put the strokes back on top of each other.
        let passRoughness = pass == 0 ? roughness : roughness * 1.15

        let a = NSPoint(x: rect.minX - overhangLeft, y: rect.minY - gap + passOffset)
        let b = NSPoint(x: rect.maxX + overhangRight, y: rect.minY - gap + scaledRise + passOffset)

        // Dips below the chord through the first third, then sweeps up into the
        // rise, so the whole stroke reads as one motion of the wrist instead of
        // a ruled line. The normal for a left-to-right run points up, so a
        // negative bow is a dip; bowSkew < 1 drags sin()'s peak back from the
        // midpoint to t = 1/3. Halved from the first attempt: the sweep should
        // be felt, not measured.
        let dip = -min(3.5, rect.width * 0.014)
        return smoothPath(through: jitteredPoints(from: a, to: b,
                                                  roughness: passRoughness, pass: pass,
                                                  bow: dip, bowSkew: 0.631, rng: &rng))
    }

    /// A `[` set just outside the rect's leading edge, for tall blocks where a
    /// box would enclose half the screen.
    public static func roughBracket(leftOf rect: NSRect,
                                    roughness: Double,
                                    pass: Int,
                                    rng: inout SeededRNG) -> NSBezierPath {
        let gap = 10.0
        // Arms scale with the block's HEIGHT, not its width. A bracket is a
        // vertical gesture: sized off width, a tall narrow block gets stubs too
        // short to read as a bracket at all, and the whole thing looks like a
        // stray vertical rule.
        let arm = min(28.0, max(14.0, rect.height * 0.10))
        let x = rect.minX - gap
        let top = rect.maxY + 4
        let bottom = rect.minY - 4
        let corners = [
            NSPoint(x: x + arm, y: top),
            NSPoint(x: x, y: top),
            NSPoint(x: x, y: bottom),
            NSPoint(x: x + arm, y: bottom),
        ]
        return smoothPath(through: jitteredChain(through: corners,
                                                 roughness: roughness, pass: pass,
                                                 rng: &rng))
    }

    /// - Parameter bow: how far the shaft sweeps off the straight tail-to-tip
    ///   chord, signed, as a fraction of that chord's length. Around ±0.12...0.22
    ///   reads as a hand-drawn swing.
    ///
    ///   Rolled by the CALLER, once, and handed to both passes — exactly like
    ///   `roughEllipse`'s `overshoot` and `roughUnderline`'s `rise`, and for a
    ///   sharper version of the same reason. The bow is an order of magnitude
    ///   larger than the jitter, so it, not the jitter, decides the shaft's
    ///   shape. If each pass rolled its own, half of them would bow to opposite
    ///   sides and close into a lens with two arrowheads splayed apart, each
    ///   correctly aimed along its own pass's tangent.
    ///
    ///   It must come from the element's stream and nothing else. Deriving it
    ///   from the geometry instead looks equivalent and is not: the sweep then
    ///   flips sides whenever the target resizes, so an arrow thrashes left and
    ///   right through a zoom or a resize drag.
    public static func roughArrow(from a: NSPoint,
                                  to b: NSPoint,
                                  headLength: Double,
                                  roughness: Double,
                                  bow: Double,
                                  pass: Int,
                                  rng: inout SeededRNG) -> NSBezierPath {
        let length = hypot(b.x - a.x, b.y - a.y)
        let shaft = jitteredPoints(from: a, to: b,
                                   roughness: roughness, pass: pass,
                                   bow: bow * length, rng: &rng)
        let path = smoothPath(through: shaft)

        // Barbs hang off the shaft's *actual* final tangent, not off the a->b
        // chord: with a bow this large the two differ by tens of degrees, and a
        // head aimed along the chord looks detached from the stroke.
        let tip = shaft[shaft.count - 1]
        let previous = shaft[max(shaft.count - 2, 0)]
        var ux = tip.x - previous.x
        var uy = tip.y - previous.y
        let magnitude = hypot(ux, uy)
        if magnitude > 0 { ux /= magnitude; uy /= magnitude } else { ux = 1; uy = 0 }

        let spread = 28.0 * Double.pi / 180
        let incoming = atan2(uy, ux)
        for sign in [1.0, -1.0] {
            let angle = incoming + Double.pi + sign * spread
            let barbEnd = NSPoint(x: tip.x + cos(angle) * headLength,
                                  y: tip.y + sin(angle) * headLength)
            path.append(smoothPath(through: jitteredPoints(from: tip, to: barbEnd,
                                                           roughness: roughness, pass: pass,
                                                           bow: 0, rng: &rng)))
        }
        return path
    }

    // MARK: - Core: jittered polyline

    /// Samples per straight run.
    private static func subdivisions(_ rng: inout SeededRNG) -> Int {
        rng.int(in: 6...10)
    }

    /// Perpendicular offset amplitude.
    ///
    /// Grows with the run's length but sub-linearly and clamped: a 380pt bracket
    /// spine is not four times wobblier than a 90pt underline, because a hand
    /// does not scale that way. Pass 1 wanders ~35% wider than pass 0, which is
    /// what gives the two-stroke look its depth instead of just its weight.
    private static func amplitude(length: Double, roughness: Double, pass: Int) -> Double {
        let base = min(1.0 + length * 0.012, 3.2)
        return roughness * base * (pass == 0 ? 1.0 : 1.35)
    }

    /// Samples `a` -> `b` into 6...10 points, pushing each off the line along
    /// the segment normal.
    ///
    /// The offset tapers at the ends: a stroke starts and finishes near where it
    /// was aimed and only wanders in between. Without the taper, boxes and
    /// brackets tear visibly at their corners.
    /// - Parameter bowSkew: reshapes where along the run the bow reaches its
    ///   maximum. 1 leaves it at the midpoint (a symmetric sag); below 1 pulls
    ///   the peak toward the start, which is what turns a symmetric sag into a
    ///   swept motion. Defaulted, so every other gesture is untouched.
    private static func jitteredPoints(from a: NSPoint,
                                       to b: NSPoint,
                                       roughness: Double,
                                       pass: Int,
                                       bow: Double,
                                       bowSkew: Double = 1.0,
                                       rng: inout SeededRNG) -> [NSPoint] {
        let dx = b.x - a.x
        let dy = b.y - a.y
        let length = hypot(dx, dy)
        let count = subdivisions(&rng)
        guard length > 0.0001 else { return [a, b] }

        let nx = -dy / length          // unit normal
        let ny = dx / length
        let amp = amplitude(length: length, roughness: roughness, pass: pass)

        var points: [NSPoint] = []
        points.reserveCapacity(count)
        for i in 0..<count {
            let t = Double(i) / Double(count - 1)
            let taper = 0.25 + 0.75 * sin(t * Double.pi)
            let bowPhase = bowSkew == 1 ? t : pow(t, bowSkew)
            let offset = rng.jitter(amp) * taper + bow * sin(bowPhase * Double.pi)
            points.append(NSPoint(x: a.x + dx * t + nx * offset,
                                  y: a.y + dy * t + ny * offset))
        }
        return points
    }

    /// Chains several straight runs into one polyline, sharing a single point at
    /// each junction so the stroke does not tear where the direction changes.
    private static func jitteredChain(through corners: [NSPoint],
                                      roughness: Double,
                                      pass: Int,
                                      rng: inout SeededRNG) -> [NSPoint] {
        var points: [NSPoint] = []
        for i in 0..<(corners.count - 1) {
            let run = jitteredPoints(from: corners[i], to: corners[i + 1],
                                     roughness: roughness, pass: pass,
                                     bow: 0, rng: &rng)
            // Drop each run's first point: the previous run's last point already
            // stands in for that corner.
            points.append(contentsOf: i == 0 ? run : Array(run.dropFirst()))
        }
        return points
    }

    // MARK: - Core: smooth cubic through points

    /// Fits a smooth open cubic through `points` (uniform Catmull-Rom, converted
    /// to Bezier control points). Endpoint tangents are clamped by duplicating
    /// the terminal points, which keeps the curve from flaring past them.
    static func smoothPath(through points: [NSPoint]) -> NSBezierPath {
        let path = NSBezierPath()
        guard points.count >= 2 else { return path }
        path.move(to: points[0])
        guard points.count > 2 else {
            path.line(to: points[1])
            return path
        }

        let n = points.count
        for i in 0..<(n - 1) {
            let p0 = points[max(i - 1, 0)]
            let p1 = points[i]
            let p2 = points[i + 1]
            let p3 = points[min(i + 2, n - 1)]
            let c1 = NSPoint(x: p1.x + (p2.x - p0.x) / 6, y: p1.y + (p2.y - p0.y) / 6)
            let c2 = NSPoint(x: p2.x - (p3.x - p1.x) / 6, y: p2.y - (p3.y - p1.y) / 6)
            path.curve(to: p2, controlPoint1: c1, controlPoint2: c2)
        }
        return path
    }

    // MARK: - Core: closed outlines walked by arc length

    /// Walks a closed outline at constant speed, offsetting each sample along
    /// the local normal, and keeps walking `overshootDegrees` past the start.
    private static func roughOutline(_ outline: Outline,
                                     samples: Int,
                                     overshootDegrees: Double,
                                     start: Double,
                                     characteristicSize: Double,
                                     roughness: Double,
                                     pass: Int,
                                     rng: inout SeededRNG) -> NSBezierPath {
        // Scale the wobble to the shape's size rather than to the gap between
        // samples: a big box should look loosely drawn, not finely serrated.
        let amp = amplitude(length: characteristicSize, roughness: roughness, pass: pass)
        let travel = outline.length * (1 + overshootDegrees / 360)

        var points: [NSPoint] = []
        points.reserveCapacity(samples)
        for i in 0..<samples {
            let f = Double(i) / Double(max(samples - 1, 1))
            let s = start + travel * f
            let p = outline.point(at: s)
            let t = outline.tangent(at: s)
            let offset = rng.jitter(amp)
            // Normal is the tangent turned 90 degrees.
            points.append(NSPoint(x: p.x - t.y * offset, y: p.y + t.x * offset))
        }
        return smoothPath(through: points)
    }

    private static func ellipseOutline(in rect: NSRect, steps: Int = 240) -> [NSPoint] {
        let cx = rect.midX, cy = rect.midY
        let rx = rect.width / 2, ry = rect.height / 2
        return (0..<steps).map { i in
            let a = 2 * Double.pi * Double(i) / Double(steps)
            return NSPoint(x: cx + rx * cos(a), y: cy + ry * sin(a))
        }
    }

    private static func roundedRectOutline(in rect: NSRect,
                                           cornerRadius: Double,
                                           arcSteps: Int = 10) -> [NSPoint] {
        let r = max(0, min(cornerRadius, min(rect.width, rect.height) / 2))
        let minX = rect.minX, maxX = rect.maxX, minY = rect.minY, maxY = rect.maxY
        var out: [NSPoint] = []

        func arc(cx: Double, cy: Double, from a0: Double, to a1: Double) {
            guard r > 0 else { return }
            for i in 0...arcSteps {
                let a = a0 + (a1 - a0) * Double(i) / Double(arcSteps)
                out.append(NSPoint(x: cx + r * cos(a), y: cy + r * sin(a)))
            }
        }

        // Clockwise from the top-left tangent point: the direction a
        // right-handed person draws a box.
        out.append(NSPoint(x: minX + r, y: maxY))
        out.append(NSPoint(x: maxX - r, y: maxY))
        arc(cx: maxX - r, cy: maxY - r, from: Double.pi / 2, to: 0)
        out.append(NSPoint(x: maxX, y: minY + r))
        arc(cx: maxX - r, cy: minY + r, from: 0, to: -Double.pi / 2)
        out.append(NSPoint(x: minX + r, y: minY))
        arc(cx: minX + r, cy: minY + r, from: -Double.pi / 2, to: -Double.pi)
        out.append(NSPoint(x: minX, y: maxY - r))
        arc(cx: minX + r, cy: maxY - r, from: Double.pi, to: Double.pi / 2)
        return out
    }

    /// A closed outline flattened into straight sub-segments and addressable by
    /// arc length, so a box and an ellipse can be walked by identical code — and
    /// so either can be walked past its own start point to overshoot.
    private struct Outline {
        private let points: [NSPoint]
        private let cumulative: [Double]   // cumulative[i] = length from points[0] to points[i]
        let length: Double

        init(_ dense: [NSPoint]) {
            var cumulative: [Double] = [0]
            cumulative.reserveCapacity(dense.count + 1)
            var total = 0.0
            for i in 0..<dense.count {
                let a = dense[i]
                let b = dense[(i + 1) % dense.count]
                total += hypot(b.x - a.x, b.y - a.y)
                cumulative.append(total)
            }
            self.points = dense
            self.cumulative = cumulative
            self.length = total
        }

        func point(at s: Double) -> NSPoint {
            let (i, t) = locate(s)
            let a = points[i]
            let b = points[(i + 1) % points.count]
            return NSPoint(x: a.x + (b.x - a.x) * t, y: a.y + (b.y - a.y) * t)
        }

        func tangent(at s: Double) -> NSPoint {
            var (i, _) = locate(s)
            // Zero-length sub-segments appear where a corner radius collapses;
            // step forward until there is a real direction to report.
            for _ in 0..<points.count {
                let a = points[i]
                let b = points[(i + 1) % points.count]
                let dx = b.x - a.x, dy = b.y - a.y
                let m = hypot(dx, dy)
                if m > 0 { return NSPoint(x: dx / m, y: dy / m) }
                i = (i + 1) % points.count
            }
            return NSPoint(x: 1, y: 0)
        }

        private func locate(_ s: Double) -> (Int, Double) {
            guard length > 0, points.count > 1 else { return (0, 0) }
            var d = s.truncatingRemainder(dividingBy: length)
            if d < 0 { d += length }
            var lo = 0
            var hi = points.count - 1
            while lo < hi {
                let mid = (lo + hi + 1) / 2
                if cumulative[mid] <= d { lo = mid } else { hi = mid - 1 }
            }
            let segment = cumulative[lo + 1] - cumulative[lo]
            return (lo, segment > 0 ? (d - cumulative[lo]) / segment : 0)
        }
    }
}
