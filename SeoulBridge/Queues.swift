import Foundation

/// The two places a message has to wait for something that is not there yet.
///
/// Both halves of the bridge have the same problem from opposite sides. Chrome
/// starts and stops SeoulHost on its own schedule, so at any instant the socket
/// may be down while work keeps arriving: frames from Chrome with no SeoulApp
/// to relay them to, and hotkey presses with no SeoulHost to send them through.
/// Neither is an error. Both used to be a drop.
///
/// These are plain value types with no clock and no locking of their own, so
/// they can be unit tested at an exact instant instead of by sleeping.

// ---------------------------------------------------------------------------
// SeoulHost: frames from Chrome waiting for the SeoulApp socket.
// ---------------------------------------------------------------------------

/// A bounded FIFO that drops its OLDEST entry when full.
///
/// Bounded because the host must never block its stdin reader: a reader parked
/// on a full queue is a host that has stopped noticing Chrome closing the port,
/// which is the one event it exists to act on. Lossy at the front rather than
/// the back because the newest message is the one the user is waiting on — if
/// something has to go, it should be the response nobody is looking at any more.
public struct OutboundQueue {

    public let capacity: Int
    private var messages: [Data] = []

    /// Cumulative, for the log line on exit. A host that quietly lost eleven
    /// messages should say so somewhere.
    public private(set) var droppedCount = 0

    public init(capacity: Int) {
        precondition(capacity > 0, "an outbound queue with no room is just a drop")
        self.capacity = capacity
    }

    public var count: Int { messages.count }
    public var isEmpty: Bool { messages.isEmpty }

    /// Never fails. Returns whatever had to be evicted to make room.
    @discardableResult
    public mutating func append(_ message: Data) -> Data? {
        messages.append(message)
        guard messages.count > capacity else { return nil }
        droppedCount += 1
        return messages.removeFirst()
    }

    public mutating func popFirst() -> Data? {
        messages.isEmpty ? nil : messages.removeFirst()
    }

    /// Puts back a message that was popped but never made it onto the wire.
    ///
    /// Safe even when the write half-succeeded: the socket it failed on is
    /// finished, and the next connection is a fresh stream that has never seen
    /// any part of this frame. Re-entering at the front keeps delivery order.
    /// If that overflows, the drop rule is unchanged — oldest first, which is
    /// this message, because it is once again the oldest thing here.
    public mutating func requeue(_ message: Data) {
        messages.insert(message, at: 0)
        while messages.count > capacity {
            droppedCount += 1
            messages.removeFirst()
        }
    }
}

// ---------------------------------------------------------------------------
// SeoulApp: hotkey presses waiting for a SeoulHost.
// ---------------------------------------------------------------------------

/// A find request fired while no SeoulHost was connected.
public struct DeferredRequest {
    public let requestId: String
    public let query: String
    public let payload: Data
    /// Monotonic nanoseconds — `DispatchTime.now().uptimeNanoseconds`. Passed
    /// in rather than read here so the queue can be tested at an exact instant.
    public let firedAt: UInt64

    public init(requestId: String, query: String, payload: Data, firedAt: UInt64) {
        self.requestId = requestId
        self.query = query
        self.payload = payload
        self.firedAt = firedAt
    }
}

/// Holds hotkey presses across a brief gap in the bridge.
///
/// The window is short on purpose. Waiting two seconds for the extension to
/// come back is worth it; drawing a sketch for a query the user pressed a
/// minute ago, on whatever page happens to be in front now, is not. Past the
/// window the request is dropped — but it is dropped with a reason, which is
/// the part that was missing.
public struct DeferredRequestQueue {

    public let capacity: Int
    public let windowNanos: UInt64
    private var requests: [DeferredRequest] = []

    public init(capacity: Int = 8, window: TimeInterval = 2.0) {
        precondition(capacity > 0 && window > 0)
        self.capacity = capacity
        self.windowNanos = UInt64(window * 1_000_000_000)
    }

    public var count: Int { requests.count }
    public var isEmpty: Bool { requests.isEmpty }

    /// Never fails. Returns the request evicted to make room, if any.
    ///
    /// The cap only matters if the hotkey is held down or repeated inside a
    /// two-second gap; it exists so that case cannot grow without bound.
    @discardableResult
    public mutating func enqueue(_ request: DeferredRequest) -> DeferredRequest? {
        requests.append(request)
        guard requests.count > capacity else { return nil }
        return requests.removeFirst()
    }

    /// Empties the queue, splitting it at the deadline: what is still worth
    /// sending, and what waited too long. Called when a host connects.
    public mutating func drain(now: UInt64) -> (ready: [DeferredRequest], expired: [DeferredRequest]) {
        let all = requests
        requests.removeAll()
        return (all.filter { !isExpired($0, now: now) },
                all.filter { isExpired($0, now: now) })
    }

    /// Removes and returns only what has timed out, leaving the rest queued.
    /// Called by the timer, so a request that never gets a host still logs.
    public mutating func expire(now: UInt64) -> [DeferredRequest] {
        // Split first, assign after: reading `self` inside a closure that is
        // mutating `self.requests` is an exclusivity violation.
        let gone = requests.filter { isExpired($0, now: now) }
        let kept = requests.filter { !isExpired($0, now: now) }
        requests = kept
        return gone
    }

    /// Addition on this side, never `now - firedAt > window`: these are
    /// unsigned, and a `now` that predates `firedAt` — a timer firing a hair
    /// early — would wrap to something astronomical and expire everything.
    private func isExpired(_ request: DeferredRequest, now: UInt64) -> Bool {
        now > request.firedAt &+ windowNanos
    }
}
