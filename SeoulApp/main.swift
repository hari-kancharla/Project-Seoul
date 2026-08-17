import AppKit
import Carbon.HIToolbox
import Seoul
import SeoulBridge

// The menu bar app. Owns the overlay and the socket; outlives every SeoulHost
// Chrome decides to spawn and kill.
//
// stdout is free here (unlike SeoulHost), but all diagnostics still go to
// stderr so a shell running both processes keeps them separable.

NSApplication.shared.setActivationPolicy(.accessory)

func elog(_ message: String) {
    FileHandle.standardError.write(Data("[seoul-app] \(message)\n".utf8))
}

/// Carbon hands back a bare C function pointer with no context, so the target
/// has to be reachable from a global. Takes the hot key's id because ONE
/// handler receives every registered hotkey — without discriminating, adding a
/// second one silently makes both do whatever the first one did.
nonisolated(unsafe) var hotKeyCallback: ((UInt32) -> Void)?

private enum HotKey {
    static let find: UInt32 = 1
    static let clear: UInt32 = 2
}

@MainActor
final class SeoulAppController: NSObject {

    static var shared: SeoulAppController?

    private var statusItem: NSStatusItem?
    private let server = UnixSocketServer()
    private var hotKeyRef: EventHotKeyRef?
    private var clearHotKeyRef: EventHotKeyRef?
    private var eventHandlerRef: EventHandlerRef?

    /// In-flight queries, what is drawn, and which app each belongs to.
    ///
    /// One object rather than three fields: the owner pid, the pending map and
    /// the live element set have to change together, and when they were separate
    /// they drifted — a response could be drawn against a request that had
    /// already been cancelled, using an owner captured for a different query.
    private var ledger = OverlayRequestLedger()

    /// Editable from the menu so several queries can be tried without a rebuild.
    private var queries = [
        "the email field",
        "the password field",
        "the search box",
        "the sign in button",
        "the submit button",
        "the shipping address field",
    ]
    private var activeQuery = 0

    private var requestCounter = 0

    /// Hotkey presses made while no SeoulHost was connected.
    ///
    /// Chrome starts and stops the host on its own schedule and MV3 restarts
    /// the service worker whenever it likes, so "nothing connected right now"
    /// is a normal gap of a second or two, not a reason to throw the user's
    /// keypress away. M4 specified holding it; this is that.
    private var deferred = DeferredRequestQueue(capacity: 8, window: 2.0)

    /// Scroll re-anchors. Counted rather than logged, then summarised on
    /// disconnect: per-event logging at frame rate would itself cost more than
    /// the work being measured.
    private var updateCount = 0
    private var updateNanosTotal: Double = 0
    private var updateNanosWorst: Double = 0

    private let selfTest: Bool
    private var selfTestFired = false

    init(selfTest: Bool, queueSelfTest: Bool = false) {
        self.selfTest = selfTest
        super.init()
        buildMenu()
        startServer()
        installHotKey()
        installFocusWatcher()

        // --selftest-queue: fire one query RIGHT NOW, before anything has had a
        // chance to connect. That is the M4 path — a hotkey pressed into a gap
        // in the bridge — and it is otherwise only reachable by pressing a
        // global hotkey at exactly the right moment, which no test can do
        // without taking cmd+shift+space off the user. Driven by
        // scripts/test-host-reconnect.py.
        if queueSelfTest {
            DispatchQueue.main.async { [weak self] in self?.fire() }
        }
    }

    /// A sketch pointing at a page you are no longer looking at is always wrong,
    /// so leaving the browser takes the overlay with it.
    private func installFocusWatcher() {
        // NSWorkspace.shared.notificationCenter, NOT NotificationCenter.default.
        // Workspace notifications are never posted to the default centre, and an
        // observer registered there simply never fires — with no error to say so.
        NSWorkspace.shared.notificationCenter.addObserver(
            forName: NSWorkspace.didActivateApplicationNotification,
            object: nil,
            queue: .main
        ) { [weak self] note in
            MainActor.assumeIsolated {
                guard let self else { return }
                let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication
                self.frontmostChanged(to: app)
            }
        }
    }

    private func frontmostChanged(to app: NSRunningApplication?) {
        // Runs even when nothing is drawn: a query fired at one app and answered
        // after switching to another must not paint over the new app, so the
        // in-flight request has to be cancelled here too.
        let outcome = ledger.frontmostChanged(to: app?.processIdentifier)
        guard outcome.clearedOverlay || !outcome.cancelledRequests.isEmpty else { return }

        let where_ = app?.localizedName ?? "unknown"
        if outcome.clearedOverlay { OverlayPanel.clear() }
        if !outcome.cancelledRequests.isEmpty {
            elog("frontmost app -> \(where_); cancelled in-flight " +
                 outcome.cancelledRequests.joined(separator: ", "))
        }
        if outcome.clearedOverlay { elog("overlay cleared (frontmost app -> \(where_))") }
    }

    // MARK: - Menu

    private func buildMenu() {
        let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        if let button = item.button {
            if let image = NSImage(systemSymbolName: "scribble", accessibilityDescription: "Seoul") {
                image.isTemplate = true
                button.image = image
            } else {
                button.title = "Seoul"
            }
        }

        let menu = NSMenu()
        menu.autoenablesItems = false

        let status = NSMenuItem(title: "Bridge: waiting for Chrome", action: nil, keyEquivalent: "")
        status.isEnabled = false
        status.tag = 1
        menu.addItem(status)
        menu.addItem(.separator())

        let findItem = NSMenuItem(title: "Find now", action: #selector(findNow), keyEquivalent: "")
        findItem.target = self
        menu.addItem(findItem)

        let querySub = NSMenu()
        for (index, query) in queries.enumerated() {
            let entry = NSMenuItem(title: query, action: #selector(selectQuery(_:)), keyEquivalent: "")
            entry.target = self
            entry.tag = index
            entry.state = index == activeQuery ? .on : .off
            querySub.addItem(entry)
        }
        let queryItem = NSMenuItem(title: "Query", action: nil, keyEquivalent: "")
        queryItem.submenu = querySub
        menu.addItem(queryItem)

        menu.addItem(.separator())
        let debug = NSMenu()
        func addDebug(_ title: String, _ action: Selector) {
            let entry = NSMenuItem(title: title, action: action, keyEquivalent: "")
            entry.target = self
            debug.addItem(entry)
        }
        addDebug("Auto at mouse", #selector(autoAtMouse))
        addDebug("Circle small", #selector(circleSmall))
        addDebug("Box field", #selector(boxField))
        addDebug("Underline text", #selector(underlineText))
        addDebug("Bracket block", #selector(bracketBlock))
        addDebug("Arrow to target", #selector(arrowToTarget))
        debug.addItem(.separator())
        addDebug("Redraw same ID", #selector(redrawSameID))
        addDebug("New ID", #selector(newID))
        let debugItem = NSMenuItem(title: "Debug gestures", action: nil, keyEquivalent: "")
        debugItem.submenu = debug
        menu.addItem(debugItem)

        menu.addItem(.separator())
        let debugToggle = NSMenuItem(title: "Coordinate debug overlay",
                                     action: #selector(toggleDebug(_:)), keyEquivalent: "")
        debugToggle.target = self
        debugToggle.state = OverlayPanel.debugEnabled ? .on : .off
        menu.addItem(debugToggle)

        menu.addItem(.separator())
        let clear = NSMenuItem(title: "Clear", action: #selector(clearOverlay), keyEquivalent: "")
        clear.target = self
        menu.addItem(clear)
        let quit = NSMenuItem(title: "Quit", action: #selector(quit), keyEquivalent: "")
        quit.target = self
        menu.addItem(quit)

        item.menu = menu
        statusItem = item
    }

    private func setStatus(_ text: String) {
        statusItem?.menu?.items.first(where: { $0.tag == 1 })?.title = "Bridge: \(text)"
    }

    @objc private func selectQuery(_ sender: NSMenuItem) {
        activeQuery = sender.tag
        for entry in sender.menu?.items ?? [] { entry.state = entry.tag == activeQuery ? .on : .off }
        elog("query set to \"\(queries[activeQuery])\"")
    }

    // MARK: - Bridge

    private func startServer() {
        server.onConnect = { [weak self] in
            guard let self else { return }
            self.setStatus("connected")
            elog("SeoulHost connected")
            self.flushDeferred()
            if self.selfTest && !self.selfTestFired {
                self.selfTestFired = true
                // Twice: the first find pays for creating the overlay panel and
                // its layer tree, which is one-time cost that would otherwise be
                // misread as the steady-state render time.
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) { self.fire() }
                DispatchQueue.main.asyncAfter(deadline: .now() + 1.2) { self.fire() }
            }
        }
        server.onDisconnect = { [weak self] in
            guard let self else { return }
            self.setStatus("waiting for Chrome")
            if self.updateCount > 0 {
                let mean = self.updateNanosTotal / Double(self.updateCount) / 1_000_000
                let worst = self.updateNanosWorst / 1_000_000
                elog(String(format: "scroll re-anchors: %d, mean %.2f ms, worst %.2f ms",
                            self.updateCount, mean, worst))
                self.updateCount = 0
                self.updateNanosTotal = 0
                self.updateNanosWorst = 0
            }
            // The browser is gone, so no in-flight query can still be answered
            // truthfully. Drop them rather than leaving them to match a
            // response from whatever connects next.
            let cancelled = self.ledger.cancelAll()
            if !cancelled.isEmpty {
                elog("cancelled in-flight " + cancelled.joined(separator: ", ") + " (bridge disconnected)")
            }
            OverlayPanel.clear()
            elog("SeoulHost disconnected")
        }
        server.onMessage = { [weak self] data in
            self?.handle(data)
        }
        do {
            try server.start()
            elog("listening on \(Bridge.socketPath)")
        } catch {
            elog("FATAL: cannot listen on \(Bridge.socketPath): \(error)")
            NSApplication.shared.terminate(nil)
        }
    }

    private func handle(_ data: Data) {
        let t2 = DispatchTime.now()
        let response: Bridge.FindResponse
        do {
            response = try Bridge.decode(Bridge.FindResponse.self, from: data)
        } catch {
            elog("undecodable response (\(data.count) bytes): \(error)")
            return
        }

        guard response.status == "confident", let rects = response.rects, !rects.isEmpty,
              let ctx = response.ctx else {
            elog("find [\(response.requestId)] -> \(response.status)" +
                 (response.error.map { " (\($0))" } ?? ""))
            ledger.abandon(response.requestId)
            return
        }

        let elementID = response.elementId ?? response.requestId

        // DECIDE BEFORE DRAWING.
        //
        // Everything past this point touches the screen, so a response that is
        // no longer wanted must never reach it. The previous order drew first
        // and consulted the pending map afterwards, which meant a slow answer
        // repainted a sketch the user had already cleared — and did it against
        // whichever application happened to be in front by then.
        let frontmost = NSWorkspace.shared.frontmostApplication?.processIdentifier
        let verdict = ledger.admit(requestId: response.requestId,
                                   elementID: elementID,
                                   frontmostPID: frontmost)
        let request: OverlayRequestLedger.PendingRequest?
        switch verdict {
        case .reject(let reason):
            elog("dropped response [\(response.requestId)] -> \(reason.rawValue)")
            return
        case .reanchor:
            request = nil
        case .render(let admitted):
            request = admitted
        }

        let (screenRect, debugInfo) = transform(rects: rects, ctx: ctx)
        let isUpdate = request == nil

        // Logged on every find, not only when the overlay is visible: the whole
        // point is to be able to read the numbers back after a bad landing.
        if !isUpdate {
            elog("coordinates for [\(response.requestId)] \(response.label ?? "")\n" + debugInfo.readout)
            if !debugInfo.calibrated {
                elog("  WARNING: no pointer calibration yet — the viewport origin is DERIVED " +
                     "from outer/inner and will be wrong if the browser chrome is asymmetric " +
                     "(vertical tabs, docked DevTools, a side panel). Move the mouse over the " +
                     "page once and re-run.")
            }
        }

        OverlayPanel.annotate(rect: screenRect,
                              gesture: .auto,
                              label: isUpdate ? nil : response.label,
                              elementID: elementID,
                              debug: debugInfo)
        let t3 = DispatchTime.now()

        guard let request else {
            // A scroll update: no hotkey behind it, so there is no t0/t1 to
            // report. Not logged per event — these arrive at frame rate — but
            // counted, so a scroll session can be confirmed after the fact.
            updateCount += 1
            let ns = Double(t3.uptimeNanoseconds &- t2.uptimeNanoseconds)
            updateNanosTotal += ns
            updateNanosWorst = max(updateNanosWorst, ns)
            return
        }
        report(request: request, t2: t2, t3: t3)
    }

    /// Page rects (CSS px, viewport-relative, y down) -> one AppKit screen rect.
    ///
    /// The union is used rather than the first rect: a wrapped link or a
    /// multi-line field reports one rect per line, and boxing only the first
    /// line looks like a mis-hit.
    private func transform(rects: [Bridge.WireRect], ctx: Bridge.WireContext) -> (NSRect, OverlayDebugInfo) {
        var union = NSRect(x: rects[0].x, y: rects[0].y, width: rects[0].width, height: rects[0].height)
        for r in rects.dropFirst() {
            union = union.union(NSRect(x: r.x, y: r.y, width: r.width, height: r.height))
        }

        let primaryHeight = CoordinateTransform.primaryScreenHeight()
        // displayScaleFactor is Swift's to supply — the page cannot know it.
        // Pick it from the screen the browser window is actually on, or the
        // pageZoom division is wrong on a mixed-DPI setup.
        //
        // By largest intersection, not by testing whether a corner is contained:
        // a window flush against the top of a display has its top edge exactly
        // at the screen's maxY, which NSRect.contains treats as outside.
        let windowFrame = DisplaySelection.appKitWindowFrame(
            screenX: ctx.screenX, screenY: ctx.screenY,
            outerWidth: ctx.outerWidth, outerHeight: ctx.outerHeight,
            primaryScreenHeight: primaryHeight)
        let scale = DisplaySelection.backingScaleFactor(forWindowFrame: windowFrame)

        let zoom = ctx.devicePixelRatio / scale

        // Locate the browser's content area on screen.
        //
        // Preferred: a real pointer event, which reports the same point in
        // screen and viewport coordinates at once. Their difference IS the
        // viewport origin, exactly, whatever the chrome looks like.
        //
        // Fallback: outer/inner arithmetic, which is only correct when the side
        // chrome is symmetric. Note it does NOT subtract chromeLeft from
        // chromeTop — the two axes are independent, and coupling them made the
        // top offset collapse to zero the moment a vertical tab strip appeared.
        let viewportLeft: Double
        let viewportTop: Double
        let calibrated: Bool
        if let sx = ctx.calibScreenX, let sy = ctx.calibScreenY,
           let cx = ctx.calibClientX, let cy = ctx.calibClientY {
            // clientX is page CSS px and screenX is screen points, so the page
            // coordinate is scaled into points before subtracting.
            viewportLeft = sx - cx * zoom
            viewportTop = sy - cy * zoom
            calibrated = true
        } else {
            viewportLeft = ctx.screenX + max(0, (ctx.outerWidth - ctx.innerWidth * zoom) / 2)
            viewportTop = ctx.screenY + max(0, ctx.outerHeight - ctx.innerHeight * zoom)
            calibrated = false
        }

        // The chrome offsets are already resolved, so the transform is told
        // there are none: screenX/screenY ARE the viewport origin, and outer
        // equals inner. CoordinateTransform then does exactly the work it is
        // good at — zoom scaling, the y-flip, and multi-display origins — with
        // no guessing about window furniture. Milestone 1 is untouched.
        let context = ViewportContext(
            screenX: viewportLeft, screenY: viewportTop,
            innerWidth: ctx.innerWidth, innerHeight: ctx.innerHeight,
            outerWidth: ctx.innerWidth * zoom, outerHeight: ctx.innerHeight * zoom,
            devicePixelRatio: ctx.devicePixelRatio,
            displayScaleFactor: scale)

        let pageRect = PageRect(x: union.minX, y: union.minY,
                                width: union.width, height: union.height)
        let appKitRect = CoordinateTransform.pageRectToScreenRect(
            pageRect, context: context, primaryScreenHeight: primaryHeight)

        // The viewport box, in AppKit coordinates, for the magenta marker.
        let viewportSize = NSSize(width: ctx.innerWidth * zoom, height: ctx.innerHeight * zoom)
        let viewportRect = NSRect(x: viewportLeft,
                                  y: primaryHeight - viewportTop - viewportSize.height,
                                  width: viewportSize.width, height: viewportSize.height)

        let info = OverlayDebugInfo(
            pageRect: pageRect,
            context: ViewportContext(
                screenX: ctx.screenX, screenY: ctx.screenY,
                innerWidth: ctx.innerWidth, innerHeight: ctx.innerHeight,
                outerWidth: ctx.outerWidth, outerHeight: ctx.outerHeight,
                devicePixelRatio: ctx.devicePixelRatio,
                displayScaleFactor: scale),
            pageZoom: zoom,
            chromeLeft: viewportLeft - ctx.screenX,
            chromeTop: viewportTop - ctx.screenY,
            appKitRect: appKitRect,
            viewportRect: viewportRect,
            calibrated: calibrated)

        return (appKitRect, info)
    }

    private func report(request: OverlayRequestLedger.PendingRequest,
                        t2: DispatchTime, t3: DispatchTime) {
        func ms(_ a: UInt64, _ b: UInt64) -> String {
            String(format: "%.2f ms", Double(b &- a) / 1_000_000)
        }
        let t0 = request.startedAtNanos
        let t1 = request.sentAtNanos ?? t0
        let t2 = t2.uptimeNanoseconds
        let t3 = t3.uptimeNanoseconds
        elog("""
        timing for "\(request.query)"
          t0 -> t1  write request to socket   \(ms(t0, t1))
          t1 -> t2  browser round trip        \(ms(t1, t2))
          t2 -> t3  transform + render sketch \(ms(t2, t3))
          t0 -> t3  TOTAL hotkey to sketch    \(ms(t0, t3))
        """)
    }

    // MARK: - Hotkey

    private func installHotKey() {
        hotKeyCallback = { [weak self] id in
            guard let self else { return }
            switch id {
            case HotKey.find: self.fire()
            case HotKey.clear: self.clear(reason: "cmd+shift+escape")
            default: break
            }
        }

        var eventType = EventTypeSpec(eventClass: OSType(kEventClassKeyboard),
                                      eventKind: UInt32(kEventHotKeyPressed))
        // One handler serves every hotkey, so it must read back WHICH one fired.
        let handler: EventHandlerUPP = { _, event, _ in
            guard let event else { return noErr }
            var id = EventHotKeyID()
            let status = GetEventParameter(event,
                                           EventParamName(kEventParamDirectObject),
                                           EventParamType(typeEventHotKeyID),
                                           nil,
                                           MemoryLayout<EventHotKeyID>.size,
                                           nil,
                                           &id)
            guard status == noErr else { return noErr }
            hotKeyCallback?(id.id)
            return noErr
        }
        InstallEventHandler(GetApplicationEventTarget(), handler, 1, &eventType, nil, &eventHandlerRef)

        // Carbon rather than NSEvent.addGlobalMonitorForEvents: the monitor API
        // requires Accessibility permission, which this app has no other use
        // for and should not be asking a user to grant.
        let signature = OSType(0x53_45_4F_55) // 'SEOU'
        register(keyCode: UInt32(kVK_Space), id: HotKey.find,
                 signature: signature, ref: &hotKeyRef, label: "cmd+shift+space (find)")
        register(keyCode: UInt32(kVK_Escape), id: HotKey.clear,
                 signature: signature, ref: &clearHotKeyRef, label: "cmd+shift+escape (clear)")
    }

    private func register(keyCode: UInt32, id: UInt32, signature: OSType,
                          ref: inout EventHotKeyRef?, label: String) {
        let hotKeyID = EventHotKeyID(signature: signature, id: id)
        let status = RegisterEventHotKey(keyCode,
                                         UInt32(cmdKey | shiftKey),
                                         hotKeyID,
                                         GetApplicationEventTarget(),
                                         0,
                                         &ref)
        if status == noErr {
            elog("hotkey registered: \(label)")
        } else {
            elog("WARNING: could not register \(label) (OSStatus \(status)). " +
                 "Something else already owns it; the menu still works.")
        }
    }

    @objc private func findNow() { fire() }

    /// One query, out the socket — or into the queue if there is no socket yet.
    private func fire() {
        let t0 = DispatchTime.now()
        // Captured now, while the browser is still frontmost: the hotkey is
        // global and this app never activates, so whatever is in front at this
        // instant is the window the answer will land on. Captured BEFORE the
        // connection is checked, because a queued request lands on the window
        // that was in front when the key was pressed, not when the host turns
        // up. Carried PER REQUEST rather than in one shared field: two queries
        // fired at two windows have two different owners, and a single field
        // attributes the second one's owner to the first one's answer.
        guard let owner = NSWorkspace.shared.frontmostApplication?.processIdentifier else {
            elog("no frontmost application — nothing to annotate")
            return
        }

        requestCounter += 1
        let requestId = "req-\(requestCounter)"
        let query = queries[activeQuery]

        let request = Bridge.FindRequest(query: query, requestId: requestId)
        guard let payload = try? Bridge.encode(request) else {
            elog("could not encode request")
            return
        }

        guard server.isConnected else {
            hold(DeferredRequest(requestId: requestId, query: query, ownerPID: owner,
                                 payload: payload, firedAt: t0.uptimeNanoseconds))
            return
        }
        deliver(requestId: requestId, query: query, ownerPID: owner, payload: payload, t0: t0)
    }

    /// Writes one already-encoded request and records it as in flight.
    ///
    /// The ledger entry is created here rather than in `fire()` so that a queued
    /// request is not counted as in flight while it waits: nothing has been sent,
    /// so nothing can be superseded or answered yet.
    private func deliver(requestId: String, query: String, ownerPID: pid_t,
                         payload: Data, t0: DispatchTime) {
        let superseded = ledger.register(requestId: requestId, query: query,
                                         ownerPID: ownerPID, atNanos: t0.uptimeNanoseconds)
        let ok = server.send(payload)
        ledger.markSent(requestId, atNanos: DispatchTime.now().uptimeNanoseconds)
        if !ok {
            elog("failed to write request to socket")
            ledger.abandon(requestId)
            return
        }
        if !superseded.isEmpty {
            elog("superseded in-flight " + superseded.joined(separator: ", "))
        }
        elog("find [\(requestId)] \"\(query)\"")
    }

    /// Holds a request for the window, and schedules the log line that says so
    /// if no host ever arrives.
    private func hold(_ request: DeferredRequest) {
        if let evicted = deferred.enqueue(request) {
            elog("request queue full — dropped [\(evicted.requestId)] \"\(evicted.query)\"")
        }
        let seconds = Double(deferred.windowNanos) / 1_000_000_000
        elog(String(format: "no SeoulHost connected — holding [%@] \"%@\" for up to %.1f s " +
                    "(is Chrome running with the extension loaded?)",
                    request.requestId, request.query, seconds))

        // Fires just past the deadline so a request that is exactly on it is
        // already expired by the time this runs.
        DispatchQueue.main.asyncAfter(deadline: .now() + seconds + 0.01) { [weak self] in
            guard let self else { return }
            let gone = self.deferred.expire(now: DispatchTime.now().uptimeNanoseconds)
            for request in gone {
                elog(String(format: "queued [%@] \"%@\" expired after %.1f s with no " +
                            "SeoulHost; dropped", request.requestId, request.query, seconds))
            }
            // Nothing to unwind on the ledger: a held request was never
            // registered, because it was never sent. The owner it captured
            // lives on the queue entry and expires with it, so an expired
            // request cannot leave a stale owner behind for the next app
            // switch to report clearing.
        }
    }

    /// Sends everything still inside its window. Called the moment a host
    /// connects, which is the whole point of the queue.
    private func flushDeferred() {
        guard !deferred.isEmpty else { return }
        let now = DispatchTime.now()
        let (ready, expired) = deferred.drain(now: now.uptimeNanoseconds)

        for gone in expired {
            elog("queued [\(gone.requestId)] \"\(gone.query)\" was already past its window; dropped")
        }
        for request in ready {
            let waited = Double(now.uptimeNanoseconds &- request.firedAt) / 1_000_000
            elog(String(format: "sending queued [%@] — waited %.0f ms for a SeoulHost",
                        request.requestId, waited))
            deliver(requestId: request.requestId, query: request.query,
                    ownerPID: request.ownerPID, payload: request.payload,
                    t0: DispatchTime(uptimeNanoseconds: request.firedAt))
        }
    }

    // MARK: - Debug gestures (milestone 2)

    private var lastRect = NSRect(x: 200, y: 200, width: 320, height: 38)
    private var lastGesture: OverlayPanel.Gesture = .box
    private var lastLabel: String? = "email"
    private var baseElementID = "demo.box"
    private var idSuffix = 0
    private var currentElementID: String {
        idSuffix == 0 ? baseElementID : "\(baseElementID)#\(idSuffix)"
    }

    @objc private func autoAtMouse() {
        let mouse = NSEvent.mouseLocation
        show(NSRect(x: mouse.x + 24, y: mouse.y - 20, width: 200, height: 40),
             gesture: .auto, label: "auto", id: "demo.auto")
    }
    @objc private func circleSmall() { show(centered(48, 48), gesture: .circle, label: nil, id: "demo.circle") }
    @objc private func boxField() { show(centered(320, 38), gesture: .box, label: "email", id: "demo.box") }
    @objc private func underlineText() { show(centered(260, 22), gesture: .underline, label: "read this", id: "demo.underline") }
    @objc private func bracketBlock() { show(centered(380, 240), gesture: .bracket, label: "this block", id: "demo.bracket") }
    @objc private func arrowToTarget() { show(centered(120, 36), gesture: .arrow, label: "here", id: "demo.arrow") }

    @objc private func redrawSameID() {
        lastRect = lastRect.offsetBy(dx: 0, dy: -40)
        OverlayPanel.annotate(rect: lastRect, gesture: lastGesture, label: lastLabel, elementID: currentElementID)
    }
    @objc private func newID() {
        idSuffix += 1
        OverlayPanel.annotate(rect: lastRect, gesture: lastGesture, label: lastLabel, elementID: currentElementID)
    }

    @objc private func toggleDebug(_ sender: NSMenuItem) {
        OverlayPanel.debugEnabled.toggle()
        sender.state = OverlayPanel.debugEnabled ? .on : .off
        elog("coordinate debug overlay \(OverlayPanel.debugEnabled ? "ON" : "OFF")")
        // Through the same centralised path as every other clear, so the
        // pending requests and the owner go with it rather than being left
        // behind to redraw later.
        if !OverlayPanel.debugEnabled { clear(reason: "debug overlay off") }
    }

    @objc private func clearOverlay() {
        clear(reason: "menu")
    }

    /// The ONE route to an empty overlay, and it says why.
    ///
    /// Cancelling the in-flight requests is part of clearing, not a separate
    /// step: leaving them pending is what let a slow answer redraw a sketch the
    /// user had just dismissed.
    private func clear(reason: String) {
        let hadSomething = ledger.hasOverlayState
        let cancelled = ledger.cancelAll()
        OverlayPanel.clear()
        if !cancelled.isEmpty {
            elog("cancelled in-flight " + cancelled.joined(separator: ", ") + " (\(reason))")
        }
        if hadSomething { elog("overlay cleared (\(reason))") }
    }

    @objc private func quit() {
        server.stop()
        NSApplication.shared.terminate(nil)
    }

    private func show(_ rect: NSRect, gesture: OverlayPanel.Gesture, label: String?, id: String) {
        lastRect = rect
        lastGesture = gesture
        lastLabel = label
        baseElementID = id
        idSuffix = 0
        OverlayPanel.annotate(rect: rect, gesture: gesture, label: label, elementID: id)
    }

    private func centered(_ width: Double, _ height: Double) -> NSRect {
        let point = NSEvent.mouseLocation
        let screen = NSScreen.screens.first { $0.frame.contains(point) } ?? NSScreen.screens.first
        let frame = screen?.frame ?? NSRect(x: 0, y: 0, width: 1440, height: 900)
        return NSRect(x: frame.midX - width / 2, y: frame.midY - height / 2, width: width, height: height)
    }
}

let wantsSelfTest = CommandLine.arguments.contains("--selftest")
let wantsQueueSelfTest = CommandLine.arguments.contains("--selftest-queue")

MainActor.assumeIsolated {
    SeoulAppController.shared = SeoulAppController(selfTest: wantsSelfTest,
                                                   queueSelfTest: wantsQueueSelfTest)
}

NSApplication.shared.run()
