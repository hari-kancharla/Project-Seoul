import XCTest
import AppKit
@testable import Seoul

// These are the exact vectors from reference/transform.mjs, which were executed
// and verified in Node before this file was written. If a number here changes,
// the JS reference must change with it and both must be re-run.

final class CoordinateTransformTests: XCTestCase {

    private func assertRect(_ got: NSRect, _ want: NSRect, _ label: String,
                            file: StaticString = #filePath, line: UInt = #line) {
        XCTAssertEqual(got.origin.x, want.origin.x, accuracy: 0.001, "\(label).x", file: file, line: line)
        XCTAssertEqual(got.origin.y, want.origin.y, accuracy: 0.001, "\(label).y", file: file, line: line)
        XCTAssertEqual(got.width, want.width, accuracy: 0.001, "\(label).w", file: file, line: line)
        XCTAssertEqual(got.height, want.height, accuracy: 0.001, "\(label).h", file: file, line: line)
    }

    /// Non-Retina 1920x1080 primary, Chrome maximized, 100% zoom.
    private let nonRetina = ViewportContext(
        screenX: 0, screenY: 25,
        innerWidth: 1920, innerHeight: 980,
        outerWidth: 1920, outerHeight: 1055,
        devicePixelRatio: 1, displayScaleFactor: 1)

    /// MacBook Pro 14" default scaling: 1512x982 points, backingScaleFactor 2.
    private func retina(_ zoom: Double) -> ViewportContext {
        ViewportContext(
            screenX: 0, screenY: 38,
            innerWidth: 1512 / zoom, innerHeight: 880 / zoom,
            outerWidth: 1512, outerHeight: 944,
            devicePixelRatio: 2 * zoom, displayScaleFactor: 2)
    }

    func testNonRetinaNoZoom() {
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 100, y: 200, width: 300, height: 40),
            context: nonRetina, primaryScreenHeight: 1080)
        assertRect(r, NSRect(x: 100, y: 740, width: 300, height: 40), "nonRetina")
    }

    /// Guards correction 1: backingScaleFactor must not appear in the output.
    func testRetinaNoZoom() {
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 50, y: 100, width: 200, height: 30),
            context: retina(1), primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 50, y: 750, width: 200, height: 30), "retina100")
    }

    func testRetina125PercentZoom() {
        let ctx = retina(1.25)
        XCTAssertEqual(CoordinateTransform.pageZoom(ctx), 1.25, accuracy: 0.001)
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 50, y: 100, width: 200, height: 30),
            context: ctx, primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 62.5, y: 717.5, width: 250, height: 37.5), "retina125")
    }

    func testRetina90PercentZoom() {
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 100, y: 100, width: 100, height: 20),
            context: retina(0.9), primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 90, y: 772, width: 90, height: 18), "retina90")
    }

    /// Guards correction 2. Fails by 25pt at 1.25 and 62pt at 2.0 if the chrome
    /// offset is computed as outerHeight - innerHeight without the zoom factor.
    func testChromeOffsetIsConstantAcrossZoomLevels() {
        let unit = PageRect(x: 0, y: 0, width: 1, height: 1)
        let base = CoordinateTransform.pageRectToScreenRect(unit, context: retina(1), primaryScreenHeight: 982)
        for z in [1.1, 1.25, 1.5, 2.0, 0.8, 0.67] {
            let r = CoordinateTransform.pageRectToScreenRect(unit, context: retina(z), primaryScreenHeight: 982)
            XCTAssertEqual(r.origin.x, base.origin.x, accuracy: 0.001, "origin x at zoom \(z)")
            XCTAssertEqual(r.origin.y + r.height, base.origin.y + base.height, accuracy: 0.001, "origin top y at zoom \(z)")
        }
    }

    func testSecondaryDisplayToTheLeft() {
        let ctx = ViewportContext(
            screenX: -1920, screenY: 0,
            innerWidth: 1920, innerHeight: 1000,
            outerWidth: 1920, outerHeight: 1064,
            devicePixelRatio: 1, displayScaleFactor: 1)
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 10, y: 10, width: 100, height: 20),
            context: ctx, primaryScreenHeight: 982)
        assertRect(r, NSRect(x: -1910, y: 888, width: 100, height: 20), "left")
    }

    func testSecondaryDisplayAbove() {
        let ctx = ViewportContext(
            screenX: 0, screenY: -1080,
            innerWidth: 1920, innerHeight: 1000,
            outerWidth: 1920, outerHeight: 1064,
            devicePixelRatio: 1, displayScaleFactor: 1)
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 0, y: 0, width: 50, height: 50),
            context: ctx, primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 0, y: 1948, width: 50, height: 50), "above")
    }

    func testMixedDPIRetinaPrimaryNonRetinaSecondary() {
        let ctx = ViewportContext(
            screenX: 1512, screenY: 0,
            innerWidth: 1920, innerHeight: 1000,
            outerWidth: 1920, outerHeight: 1064,
            devicePixelRatio: 1, displayScaleFactor: 1)
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 10, y: 10, width: 100, height: 20),
            context: ctx, primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 1522, y: 888, width: 100, height: 20), "mixedDPI")
    }

    func testElementScrolledPartiallyAboveViewport() {
        let r = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 0, y: -20, width: 100, height: 40),
            context: retina(1), primaryScreenHeight: 982)
        assertRect(r, NSRect(x: 0, y: 860, width: 100, height: 40), "clipped")
    }

    func testRoundTripIsLossless() {
        let a = CoordinateTransform.pageRectToScreenRect(
            PageRect(x: 33, y: 77, width: 120, height: 44),
            context: retina(1.25), primaryScreenHeight: 982)
        let tl = CoordinateTransform.appKitRectToScreenTopLeft(a, primaryScreenHeight: 982)
        let back = CoordinateTransform.appKitRectToScreenTopLeft(tl, primaryScreenHeight: 982)
        assertRect(back, a, "roundTrip")
    }
}
