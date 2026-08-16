import Foundation

/// Tracks in-flight overlay queries and decides what may be drawn.
///
/// WHY THIS IS A SEPARATE TYPE
///
/// The overlay talks to a browser across a socket, so a response can arrive at
/// any time — including after the user cleared the sketch, switched to another
/// application, or fired a newer query. The rule "only draw for a request that
/// is still active and still belongs to the app that is still in front" is
/// pure bookkeeping, and keeping it here means it can be tested directly
/// instead of only through a live browser.
///
/// The ledger owns the three pieces of state that must stay consistent — the
/// pending requests, the element ids currently on screen, and which application
/// the drawn annotation belongs to. They were previously three independent
/// fields updated from five call sites, which is how they drifted apart.
public struct OverlayRequestLedger {

    /// One in-flight query. The owner pid lives HERE, per request, not in a
    /// single shared field: two queries fired against two different windows
    /// have two different owners, and a shared field silently attributes the
    /// second one's owner to the first one's response.
    public struct PendingRequest: Equatable {
        public let requestId: String
        public let query: String
        public let ownerPID: pid_t
        public let startedAtNanos: UInt64
        public var sentAtNanos: UInt64?

        public init(requestId: String, query: String, ownerPID: pid_t,
                    startedAtNanos: UInt64, sentAtNanos: UInt64? = nil) {
            self.requestId = requestId
            self.query = query
            self.ownerPID = ownerPID
            self.startedAtNanos = startedAtNanos
            self.sentAtNanos = sentAtNanos
        }
    }

    public enum Rejection: String, Equatable {
        /// No pending request and no live element by that id.
        case unknownRequest
        /// A newer query replaced this one before the answer came back.
        case superseded
        /// The overlay was cleared, or the bridge dropped, while it was in flight.
        case cancelled
        /// The app that asked is no longer the app in front.
        case ownerChanged
        /// A second response for a request that was already drawn.
        case alreadyHandled
    }

    public enum Admission: Equatable {
        /// Draw a fresh annotation for this request.
        case render(PendingRequest)
        /// Move an existing sketch; a scroll update for something already drawn.
        case reanchor(elementID: String)
        /// Draw nothing and change nothing.
        case reject(Rejection)
    }

    public struct FocusOutcome: Equatable {
        /// The drawn annotation belonged to an app that is no longer in front.
        public let clearedOverlay: Bool
        public let cancelledRequests: [String]
    }

    public private(set) var pending: [String: PendingRequest] = [:]
    public private(set) var liveElementIDs: Set<String> = []
    /// The app owning what is currently drawn, or nil when nothing is drawn.
    public private(set) var activeOwnerPID: pid_t?

    /// Why recently finished ids are no longer accepted. Bounded, because a long
    /// session must not accumulate one entry per query forever; the only cost of
    /// forgetting an old id is that its rejection reports `unknownRequest`
    /// rather than the more specific cause.
    private var retiredReason: [String: Rejection] = [:]
    private var retiredOrder: [String] = []
    private static let retiredCapacity = 64

    public init() {}

    public var hasPending: Bool { !pending.isEmpty }
    public var hasOverlayState: Bool { activeOwnerPID != nil || !liveElementIDs.isEmpty }

    // MARK: - Firing

    /// Registers a newly fired query.
    ///
    /// One active query at a time, so a new request supersedes every older one:
    /// their answers can no longer be what the user is waiting for, and drawing
    /// them would replace the newest sketch with an older one.
    /// - Returns: the ids that were superseded, for logging.
    @discardableResult
    public mutating func register(requestId: String, query: String,
                                  ownerPID: pid_t, atNanos: UInt64) -> [String] {
        let superseded = pending.keys.sorted()
        for id in superseded { retire(id, as: .superseded) }
        pending.removeAll()

        // A reused id becomes live again and must not inherit an old verdict.
        retiredReason.removeValue(forKey: requestId)
        retiredOrder.removeAll { $0 == requestId }

        pending[requestId] = PendingRequest(requestId: requestId, query: query,
                                            ownerPID: ownerPID, startedAtNanos: atNanos)
        return superseded
    }

    public mutating func markSent(_ requestId: String, atNanos: UInt64) {
        pending[requestId]?.sentAtNanos = atNanos
    }

    /// The request will never be answered — the write failed, or the browser
    /// answered with a non-success status.
    public mutating func abandon(_ requestId: String) {
        if pending.removeValue(forKey: requestId) != nil {
            retire(requestId, as: .cancelled)
        }
    }

    // MARK: - Answering

    /// Decides what a successful response is allowed to do.
    ///
    /// Nothing here mutates on a rejection: a response that arrives after a
    /// clear must not resurrect the owner pid or re-populate the live set, which
    /// is exactly how a cleared sketch used to come back.
    public mutating func admit(requestId: String, elementID: String,
                               frontmostPID: pid_t?) -> Admission {
        if let request = pending[requestId] {
            // The answer is only useful if the user is still looking at the
            // window that asked for it.
            guard let frontmostPID, frontmostPID == request.ownerPID else {
                pending.removeValue(forKey: requestId)
                retire(requestId, as: .ownerChanged)
                return .reject(.ownerChanged)
            }
            pending.removeValue(forKey: requestId)
            retire(requestId, as: .alreadyHandled)
            liveElementIDs.insert(elementID)
            activeOwnerPID = request.ownerPID
            return .render(request)
        }

        // Scroll updates carry no pending request; they are legitimate only for
        // something already on screen.
        if liveElementIDs.contains(elementID) {
            return .reanchor(elementID: elementID)
        }

        return .reject(retiredReason[requestId] ?? .unknownRequest)
    }

    // MARK: - Cancellation

    /// Clears everything: the user cleared the overlay, or the bridge dropped.
    /// - Returns: the ids that were cancelled in flight.
    @discardableResult
    public mutating func cancelAll(reason: Rejection = .cancelled) -> [String] {
        let ids = pending.keys.sorted()
        for id in ids { retire(id, as: reason) }
        pending.removeAll()
        liveElementIDs.removeAll()
        activeOwnerPID = nil
        return ids
    }

    /// The frontmost application changed.
    ///
    /// Cancels in-flight requests owned by anything that is not now in front,
    /// and reports whether the drawn annotation has to go with them. Note this
    /// acts even when nothing is drawn yet — a request fired at one app and
    /// answered after switching to another must not paint over the new app.
    @discardableResult
    public mutating func frontmostChanged(to pid: pid_t?) -> FocusOutcome {
        var cancelled: [String] = []
        for (id, request) in pending where pid == nil || request.ownerPID != pid! {
            retire(id, as: .ownerChanged)
            cancelled.append(id)
        }
        for id in cancelled { pending.removeValue(forKey: id) }

        var clearedOverlay = false
        if let owner = activeOwnerPID, pid == nil || owner != pid! {
            liveElementIDs.removeAll()
            activeOwnerPID = nil
            clearedOverlay = true
        }
        return FocusOutcome(clearedOverlay: clearedOverlay, cancelledRequests: cancelled.sorted())
    }

    // MARK: - Internals

    private mutating func retire(_ requestId: String, as reason: Rejection) {
        if retiredReason[requestId] == nil { retiredOrder.append(requestId) }
        retiredReason[requestId] = reason
        while retiredOrder.count > Self.retiredCapacity {
            let oldest = retiredOrder.removeFirst()
            retiredReason.removeValue(forKey: oldest)
        }
    }
}
