import XCTest
import AppKit
@testable import Seoul

/// Display selection for a browser window.
///
/// The case that motivated all of this: a window flush against the top of a
/// display. Its top edge lands exactly on the screen's `maxY`, and `NSRect`
/// treats `maxY` as outside, so the corner-containment test this replaced
/// matched NO screen and fell back to `screens.first`. On one display the
/// fallback is the right answer and the bug is invisible; on two it silently
/// hands back the wrong backing scale factor, and every rect is then scaled by
/// the wrong pageZoom.
final class DisplaySelectionTests: XCTestCase {

    /// Two identical displays side by side. Same height, so a window flush to
    /// the top of either has its top edge at y = 982 in both.
    private let primary = NSRect(x: 0, y: 0, width: 1512, height: 982)
    private let secondary = NSRect(x: 1512, y: 0, width: 1512, height: 982)
    private var screens: [NSRect] { [primary, secondary] }

    private let primaryHeight = 982.0

    // MARK: - The regression

    func testWindowFlushWithTopOfPrimaryIsNotMissed() {
        // screenY 0 == the window's frame touches the very top of the screen.
        let frame = DisplaySelection.appKitWindowFrame(
            screenX: 122, screenY: 0, outerWidth: 1200, outerHeight: 815,
            primaryScreenHeight: primaryHeight)

        XCTAssertEqual(frame.maxY, primary.maxY, accuracy: 0.001,
                       "the window's top edge should sit exactly on the screen's maxY")
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 0)
    }

    /// The same window on the SECOND display, where the old fallback was wrong
    /// rather than merely lucky.
    func testWindowFlushWithTopOfSecondaryResolvesToSecondary() {
        let frame = DisplaySelection.appKitWindowFrame(
            screenX: 1512, screenY: 0, outerWidth: 1200, outerHeight: 815,
            primaryScreenHeight: primaryHeight)

        XCTAssertEqual(frame.maxY, secondary.maxY, accuracy: 0.001)
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 1,
                       "a window on the second display must not resolve to the first")
    }

    /// Documents precisely why the old test failed, so this cannot regress
    /// quietly back to a containment check.
    func testCornerContainmentWouldHaveMissedBothScreens() {
        let topLeftCorner = NSPoint(x: 1512, y: primaryHeight - 0)
        XCTAssertFalse(primary.contains(topLeftCorner),
                       "NSRect.contains excludes maxY, and maxX")
        XCTAssertFalse(secondary.contains(topLeftCorner),
                       "NSRect.contains excludes maxY")
        // ...whereas intersection finds it without trouble.
        let frame = DisplaySelection.appKitWindowFrame(
            screenX: 1512, screenY: 0, outerWidth: 1200, outerHeight: 815,
            primaryScreenHeight: primaryHeight)
        XCTAssertNotNil(DisplaySelection.displayIndex(for: frame, in: screens))
    }

    // MARK: - The coordinate flip

    func testAppKitWindowFrameFlipsTopLeftToBottomLeft() {
        // A window 72pt below the top of a 982pt-tall primary.
        let frame = DisplaySelection.appKitWindowFrame(
            screenX: 122, screenY: 72, outerWidth: 1200, outerHeight: 815,
            primaryScreenHeight: primaryHeight)
        XCTAssertEqual(frame.origin.x, 122, accuracy: 0.001)
        XCTAssertEqual(frame.origin.y, 982 - 72 - 815, accuracy: 0.001)
        XCTAssertEqual(frame.maxY, 982 - 72, accuracy: 0.001, "top edge is 72pt down")
        XCTAssertEqual(frame.width, 1200, accuracy: 0.001)
        XCTAssertEqual(frame.height, 815, accuracy: 0.001)
    }

    // MARK: - Ordinary cases

    func testWindowWhollyInsidePrimary() {
        let frame = NSRect(x: 100, y: 100, width: 800, height: 600)
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 0)
    }

    func testWindowWhollyInsideSecondary() {
        let frame = NSRect(x: 1700, y: 100, width: 800, height: 600)
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 1)
    }

    func testStraddlingWindowGoesToTheDisplayItMostlyOccupies() {
        // 200pt on the primary, 600pt on the secondary.
        let frame = NSRect(x: 1312, y: 200, width: 800, height: 400)
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 1)

        // Mirror image: 600 on the primary, 200 on the secondary.
        let other = NSRect(x: 912, y: 200, width: 800, height: 400)
        XCTAssertEqual(DisplaySelection.displayIndex(for: other, in: screens), 0)
    }

    func testWindowEntirelyOffscreenSelectsNothing() {
        // Dragged to a display that has since been unplugged.
        let frame = NSRect(x: 5000, y: 5000, width: 800, height: 600)
        XCTAssertNil(DisplaySelection.displayIndex(for: frame, in: screens))
    }

    func testZeroAreaWindowSelectsNothingRatherThanGuessing() {
        let frame = NSRect(x: 100, y: 100, width: 0, height: 0)
        XCTAssertNil(DisplaySelection.displayIndex(for: frame, in: screens))
    }

    func testExactTieResolvesToTheLowestIndex() {
        // Exactly half on each display: the answer must not depend on the order
        // NSScreen.screens happened to return.
        let frame = NSRect(x: 1112, y: 200, width: 800, height: 400)
        XCTAssertEqual(frame.intersection(primary).width, 400, accuracy: 0.001)
        XCTAssertEqual(frame.intersection(secondary).width, 400, accuracy: 0.001)
        XCTAssertEqual(DisplaySelection.displayIndex(for: frame, in: screens), 0)
    }

    func testEmptyScreenListSelectsNothing() {
        XCTAssertNil(DisplaySelection.displayIndex(for: primary, in: []))
    }
}
