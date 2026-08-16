import XCTest
@testable import Seoul

/// Lifecycle rules for in-flight overlay queries.
///
/// Every case here is a way a response can arrive when it is no longer welcome:
/// after a clear, after the user switched apps, after a newer query replaced it,
/// or for something that was never asked for. The invariant under test is that
/// none of those draw anything, and none of them leave overlay state behind.
final class OverlayRequestLedgerTests: XCTestCase {

    private let browser: pid_t = 501
    private let otherApp: pid_t = 902

    private func fired(_ ledger: inout OverlayRequestLedger,
                       id: String = "req-1",
                       owner: pid_t? = nil) {
        ledger.register(requestId: id, query: "the email field",
                        ownerPID: owner ?? browser, atNanos: 1_000)
        ledger.markSent(id, atNanos: 2_000)
    }

    // MARK: - Cancellation while a request is in flight

    func testClearWhileRequestPendingCancelsIt() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        XCTAssertTrue(ledger.hasPending)

        let cancelled = ledger.cancelAll()

        XCTAssertEqual(cancelled, ["req-1"])
        XCTAssertFalse(ledger.hasPending)
        XCTAssertFalse(ledger.hasOverlayState)
    }

    func testSwitchingAppsWhileRequestPendingCancelsIt() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)

        let outcome = ledger.frontmostChanged(to: otherApp)

        XCTAssertEqual(outcome.cancelledRequests, ["req-1"])
        // Nothing was drawn yet, so there is no overlay to tear down.
        XCTAssertFalse(outcome.clearedOverlay)
        XCTAssertFalse(ledger.hasPending)
    }

    func testBridgeDisconnectCancelsPendingRequests() {
        var ledger = OverlayRequestLedger()
        ledger.register(requestId: "req-1", query: "a", ownerPID: browser, atNanos: 1)
        // A second register supersedes the first, so fire them into a fresh
        // ledger each to prove disconnect drains whatever is actually in flight.
        XCTAssertEqual(ledger.pending.count, 1)

        let cancelled = ledger.cancelAll()

        XCTAssertEqual(cancelled, ["req-1"])
        XCTAssertFalse(ledger.hasPending)
        XCTAssertFalse(ledger.hasOverlayState)
    }

    // MARK: - Late answers

    func testDelayedResponseAfterClearIsRejected() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        ledger.cancelAll()

        let verdict = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.cancelled))
        // The whole point: a cancelled answer must not rebuild overlay state.
        XCTAssertFalse(ledger.hasOverlayState)
        XCTAssertNil(ledger.activeOwnerPID)
        XCTAssertTrue(ledger.liveElementIDs.isEmpty)
    }

    func testDelayedResponseAfterFocusChangeIsRejected() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        ledger.frontmostChanged(to: otherApp)

        let verdict = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: otherApp)

        XCTAssertEqual(verdict, .reject(.ownerChanged))
        XCTAssertFalse(ledger.hasOverlayState)
    }

    /// The request is still pending, but the user moved on before it answered.
    func testResponseIsRejectedWhenFrontmostNoLongerMatchesOwner() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)

        let verdict = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: otherApp)

        XCTAssertEqual(verdict, .reject(.ownerChanged))
        XCTAssertFalse(ledger.hasOverlayState)
        XCTAssertFalse(ledger.hasPending)
    }

    func testUnknownRequestIDIsRejected() {
        var ledger = OverlayRequestLedger()

        let verdict = ledger.admit(requestId: "never-asked", elementID: "el-z", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.unknownRequest))
        XCTAssertFalse(ledger.hasOverlayState)
    }

    func testOutOfOrderResponsesOnlyDrawTheNewest() {
        var ledger = OverlayRequestLedger()
        fired(&ledger, id: "req-1")
        let superseded = ledger.register(requestId: "req-2", query: "the password field",
                                         ownerPID: browser, atNanos: 3_000)
        XCTAssertEqual(superseded, ["req-1"])

        // The older answer arrives last and must be dropped, not drawn.
        let stale = ledger.admit(requestId: "req-1", elementID: "el-old", frontmostPID: browser)
        XCTAssertEqual(stale, .reject(.superseded))
        XCTAssertTrue(ledger.liveElementIDs.isEmpty)

        let fresh = ledger.admit(requestId: "req-2", elementID: "el-new", frontmostPID: browser)
        guard case .render(let request) = fresh else {
            return XCTFail("expected the newest request to render, got \(fresh)")
        }
        XCTAssertEqual(request.requestId, "req-2")
        XCTAssertEqual(ledger.liveElementIDs, ["el-new"])
    }

    // MARK: - The happy paths must keep working

    func testValidResponseRendersWhileOwnerIsStillFrontmost() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)

        let verdict = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        guard case .render(let request) = verdict else {
            return XCTFail("expected render, got \(verdict)")
        }
        XCTAssertEqual(request.query, "the email field")
        XCTAssertEqual(request.ownerPID, browser)
        XCTAssertEqual(request.sentAtNanos, 2_000)
        XCTAssertEqual(ledger.activeOwnerPID, browser)
        XCTAssertEqual(ledger.liveElementIDs, ["el-a"])
        XCTAssertFalse(ledger.hasPending, "the request should be consumed")
    }

    func testScrollUpdateIsAcceptedForALiveElement() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        // Scroll updates reuse the element id as the request id and have no
        // pending entry behind them.
        let verdict = ledger.admit(requestId: "el-a", elementID: "el-a", frontmostPID: browser)

        XCTAssertEqual(verdict, .reanchor(elementID: "el-a"))
        XCTAssertEqual(ledger.liveElementIDs, ["el-a"])
    }

    func testScrollUpdateIsRejectedForAnUnknownElement() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        let verdict = ledger.admit(requestId: "el-ghost", elementID: "el-ghost", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.unknownRequest))
        XCTAssertEqual(ledger.liveElementIDs, ["el-a"], "the live element must be untouched")
    }

    /// After a clear the element is no longer live, so scroll updates that were
    /// already in flight stop redrawing it.
    func testScrollUpdateAfterClearIsRejected() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)
        ledger.cancelAll()

        let verdict = ledger.admit(requestId: "el-a", elementID: "el-a", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.unknownRequest))
        XCTAssertFalse(ledger.hasOverlayState)
    }

    // MARK: - Focus behaviour around a drawn sketch

    func testSwitchingAwayFromADrawnSketchClearsIt() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        let outcome = ledger.frontmostChanged(to: otherApp)

        XCTAssertTrue(outcome.clearedOverlay)
        XCTAssertFalse(ledger.hasOverlayState)
    }

    func testSwitchingBackToTheOwnerKeepsTheSketch() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        let outcome = ledger.frontmostChanged(to: browser)

        XCTAssertFalse(outcome.clearedOverlay)
        XCTAssertEqual(ledger.liveElementIDs, ["el-a"])
        XCTAssertEqual(ledger.activeOwnerPID, browser)
    }

    func testTwoRequestsFromDifferentOwnersDoNotShareAnOwner() {
        var ledger = OverlayRequestLedger()
        ledger.register(requestId: "req-1", query: "a", ownerPID: browser, atNanos: 1)
        ledger.register(requestId: "req-2", query: "b", ownerPID: otherApp, atNanos: 2)

        // req-2 is owned by otherApp; answering it while `browser` is in front
        // must be rejected rather than attributed to the wrong owner.
        let verdict = ledger.admit(requestId: "req-2", elementID: "el-b", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.ownerChanged))
        XCTAssertNil(ledger.activeOwnerPID)
    }

    func testDuplicateResponseForAnAlreadyDrawnRequestIsRejected() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        _ = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        // Same request id, a different element: not a scroll update, so it must
        // not draw a second sketch.
        let verdict = ledger.admit(requestId: "req-1", elementID: "el-other", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.alreadyHandled))
        XCTAssertEqual(ledger.liveElementIDs, ["el-a"])
    }

    func testAbandonedRequestIsRejectedIfItAnswersAnyway() {
        var ledger = OverlayRequestLedger()
        fired(&ledger)
        ledger.abandon("req-1")

        let verdict = ledger.admit(requestId: "req-1", elementID: "el-a", frontmostPID: browser)

        XCTAssertEqual(verdict, .reject(.cancelled))
        XCTAssertFalse(ledger.hasOverlayState)
    }
}
