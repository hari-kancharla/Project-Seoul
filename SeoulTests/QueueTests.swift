import XCTest
@testable import SeoulBridge

/// The two queues that stand in for a connection that is not there.
///
/// Unit tests rather than sleeps: both types take the clock as an argument
/// precisely so the deadline cases can be checked at an exact nanosecond
/// instead of by waiting two seconds and hoping the machine was not busy.
/// The end-to-end behaviour they support is covered by
/// scripts/test-host-reconnect.sh, which runs real processes.

// MARK: - SeoulHost's stdin buffer

final class OutboundQueueTests: XCTestCase {

    private func message(_ n: Int) -> Data { Data("m\(n)".utf8) }

    func testHoldsUpToCapacityWithoutDropping() {
        var queue = OutboundQueue(capacity: 32)
        for i in 0..<32 {
            XCTAssertNil(queue.append(message(i)), "nothing should be evicted below the cap")
        }
        XCTAssertEqual(queue.count, 32)
        XCTAssertEqual(queue.droppedCount, 0)
    }

    /// The rule the brief asks for: buffer 32, then drop the OLDEST. A queue
    /// that dropped the newest would throw away the response the user is
    /// actually waiting on.
    func testThirtyThirdMessageEvictsTheOldest() {
        var queue = OutboundQueue(capacity: 32)
        for i in 0..<32 { queue.append(message(i)) }

        XCTAssertEqual(queue.append(message(32)), message(0))
        XCTAssertEqual(queue.count, 32, "the queue stays at its cap")
        XCTAssertEqual(queue.droppedCount, 1)
        XCTAssertEqual(queue.popFirst(), message(1), "m0 is gone; m1 is now the front")
    }

    func testOverflowingByALotStillLeavesTheNewestCapacityMessages() {
        var queue = OutboundQueue(capacity: 32)
        for i in 0..<1000 { queue.append(message(i)) }

        XCTAssertEqual(queue.count, 32)
        XCTAssertEqual(queue.droppedCount, 968)
        XCTAssertEqual(queue.popFirst(), message(968))
    }

    func testDrainsInOrder() {
        var queue = OutboundQueue(capacity: 4)
        for i in 0..<4 { queue.append(message(i)) }
        for i in 0..<4 { XCTAssertEqual(queue.popFirst(), message(i)) }
        XCTAssertNil(queue.popFirst())
        XCTAssertTrue(queue.isEmpty)
    }

    /// A write that fails because SeoulApp died must not lose the frame: the
    /// host puts it back and the next connection delivers it.
    func testRequeuePutsAFailedWriteBackAtTheFront() {
        var queue = OutboundQueue(capacity: 4)
        queue.append(message(1))
        queue.append(message(2))

        let inFlight = queue.popFirst()
        XCTAssertEqual(inFlight, message(1))
        queue.requeue(inFlight!)

        XCTAssertEqual(queue.popFirst(), message(1), "order is preserved across a reconnect")
        XCTAssertEqual(queue.popFirst(), message(2))
    }

    /// Requeuing into a full queue still obeys one rule and only one rule:
    /// oldest first. The message going back IS the oldest.
    func testRequeueIntoAFullQueueDropsTheOldest() {
        var queue = OutboundQueue(capacity: 2)
        queue.append(message(1))
        let inFlight = queue.popFirst()!
        queue.append(message(2))
        queue.append(message(3))

        queue.requeue(inFlight)

        XCTAssertEqual(queue.count, 2)
        XCTAssertEqual(queue.droppedCount, 1)
        XCTAssertEqual(queue.popFirst(), message(2))
        XCTAssertEqual(queue.popFirst(), message(3))
    }
}

// MARK: - SeoulApp's hotkey queue

final class DeferredRequestQueueTests: XCTestCase {

    private let second: UInt64 = 1_000_000_000

    private func request(_ id: String, at firedAt: UInt64, owner: pid_t = 4242) -> DeferredRequest {
        DeferredRequest(requestId: id, query: "the email field", ownerPID: owner,
                        payload: Data(id.utf8), firedAt: firedAt)
    }

    /// The owner is captured when the key is pressed and has to survive the
    /// wait: that is the entire reason it travels on the request instead of
    /// being read again at send time, when the frontmost app may have changed.
    func testOwnerTravelsWithAQueuedRequest() {
        var queue = DeferredRequestQueue(capacity: 4, window: 2.0)
        queue.enqueue(request("req-1", at: 0, owner: 111))
        queue.enqueue(request("req-2", at: 0, owner: 222))

        let (ready, expired) = queue.drain(now: second)
        XCTAssertTrue(expired.isEmpty)
        XCTAssertEqual(ready.map(\.ownerPID), [111, 222])
    }

    /// The M4 behaviour the logs showed missing: a hotkey pressed with no host
    /// connected is sent when one arrives, not dropped on the spot.
    func testRequestQueuedThenDeliveredWhenAHostConnects() {
        var queue = DeferredRequestQueue(capacity: 8, window: 2.0)
        queue.enqueue(request("req-1", at: 10 * second))
        XCTAssertEqual(queue.count, 1)

        // A host connects 300 ms later.
        let (ready, expired) = queue.drain(now: 10 * second + 300_000_000)

        XCTAssertEqual(ready.map(\.requestId), ["req-1"])
        XCTAssertTrue(expired.isEmpty)
        XCTAssertTrue(queue.isEmpty, "draining empties the queue")
    }

    func testRequestOlderThanTheWindowIsNotSentOnConnect() {
        var queue = DeferredRequestQueue(capacity: 8, window: 2.0)
        queue.enqueue(request("req-1", at: 10 * second))

        let (ready, expired) = queue.drain(now: 12 * second + 1)

        XCTAssertTrue(ready.isEmpty, "a sketch for a keypress this old points at the wrong page")
        XCTAssertEqual(expired.map(\.requestId), ["req-1"])
    }

    /// Exactly on the deadline still counts as in-window; the boundary is
    /// pinned so a timer that fires precisely on time does not lose the request.
    func testTheDeadlineItselfIsStillInsideTheWindow() {
        var queue = DeferredRequestQueue(capacity: 8, window: 2.0)
        queue.enqueue(request("req-1", at: 0))

        let (ready, _) = queue.drain(now: 2 * second)
        XCTAssertEqual(ready.map(\.requestId), ["req-1"])
    }

    func testExpireRemovesOnlyWhatTimedOut() {
        var queue = DeferredRequestQueue(capacity: 8, window: 2.0)
        queue.enqueue(request("old", at: 0))
        queue.enqueue(request("new", at: 2 * second))

        let gone = queue.expire(now: 3 * second)

        XCTAssertEqual(gone.map(\.requestId), ["old"])
        XCTAssertEqual(queue.count, 1, "the one still inside its window stays queued")

        let (ready, _) = queue.drain(now: 3 * second)
        XCTAssertEqual(ready.map(\.requestId), ["new"])
    }

    /// A clock that reads slightly BEFORE the request was fired must not wrap.
    /// These are unsigned nanoseconds, and `now - firedAt` would underflow to
    /// roughly 18 quintillion — expiring everything, always.
    func testAClockReadingEarlyDoesNotExpireEverything() {
        var queue = DeferredRequestQueue(capacity: 8, window: 2.0)
        queue.enqueue(request("req-1", at: 5 * second))

        let gone = queue.expire(now: 5 * second - 1000)

        XCTAssertTrue(gone.isEmpty)
        XCTAssertEqual(queue.count, 1)
    }

    func testHeldHotkeyCannotGrowTheQueuePastItsCap() {
        var queue = DeferredRequestQueue(capacity: 3, window: 2.0)
        for i in 1...5 { queue.enqueue(request("req-\(i)", at: UInt64(i) * 1_000_000)) }

        XCTAssertEqual(queue.count, 3)
        let (ready, _) = queue.drain(now: 6_000_000)
        XCTAssertEqual(ready.map(\.requestId), ["req-3", "req-4", "req-5"])
    }

    func testEvictionReportsWhatItThrewAway() {
        var queue = DeferredRequestQueue(capacity: 1, window: 2.0)
        XCTAssertNil(queue.enqueue(request("req-1", at: 0)))
        XCTAssertEqual(queue.enqueue(request("req-2", at: 1))?.requestId, "req-1")
    }
}
