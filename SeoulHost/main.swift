// ============================================================================
//  STDOUT IS THE PROTOCOL CHANNEL. DO NOT WRITE TO IT.
//
//  Chrome reads native-messaging frames from this process's stdout. ANY stray
//  byte on fd 1 — a print(), a dump(), a debugPrint, a warning from a library,
//  a leftover "here" — lands in the middle of a length-prefixed frame and
//  corrupts the stream. It does not raise an error. Chrome reads the next four
//  bytes of your debug string as a message length, waits for a few hundred
//  megabytes that never arrive, and the extension simply stops receiving
//  anything, forever, with no diagnostic anywhere.
//
//  There is no print() in this file and there must never be one. Log to stderr
//  via log() below, which Chrome ignores and which shows up in the browser's
//  native-host stderr capture.
// ============================================================================

// ============================================================================
//  A MISSING SeoulApp IS NOT A FATAL ERROR.
//
//  Chrome decides when the extension's service worker runs, and therefore when
//  this process starts; MV3 tears that worker down and brings it back on its
//  own schedule. So this host is routinely alive BEFORE SeoulApp is listening,
//  and routinely has to outlive a SeoulApp that quits and comes back. "The
//  socket is not there right now" is a normal transient state.
//
//  This file used to exit(1) on that state, which handed Chrome a host that
//  died at startup for the most ordinary reason there is. It now retries
//  forever.
//
//  There is exactly ONE way out of this process, and it is Chrome closing the
//  port: end of stream on stdin. Nothing else — a refused connect, SeoulApp
//  quitting mid-session, a failed write, an oversized frame — may end it. Grep
//  this file for `exit(`: there are two calls, both on the stdin path at the
//  bottom, and that is the whole exit surface.
// ============================================================================

import Foundation
import SeoulBridge

let STDIN: Int32 = 0
let STDOUT: Int32 = 1

/// stderr only. See the first banner.
func log(_ message: String) {
    FileHandle.standardError.write(Data("[seoul-host] \(message)\n".utf8))
}

// A dead peer must not take this process with it. Writing to a socket SeoulApp
// has closed, or to a stdout Chrome has closed, raises SIGPIPE — whose default
// disposition is to kill the process instantly, with no log line and no exit
// path of our choosing. Ignoring it turns both into an ordinary EPIPE from
// write(), which the loops below already know how to handle.
signal(SIGPIPE, SIG_IGN)

/// Frames from Chrome held while the socket is down. 32 is the spec: deep
/// enough to cover a SeoulApp restart, shallow enough that what finally
/// arrives is still about the page the user is looking at.
let outboundCapacity = 32

/// Reconnect backoff: 100 ms, doubling, capped at 2 s, forever. Fast enough
/// that the usual case — SeoulApp is a second behind Chrome at login — costs
/// nobody anything, and slow enough that a machine with SeoulApp genuinely not
/// installed is not spinning on connect() all day.
let backoffFirstMicros: UInt32 = 100_000
let backoffCapMicros: UInt32 = 2_000_000

/// The SeoulApp end of the relay: one long-lived object that owns dialling,
/// redialling, and the frames waiting for a socket to write them to.
///
/// One connection at a time, and ONE THREAD owns each descriptor's lifetime.
/// The link thread creates the fd, writes on it, and is the only thing that
/// ever closes it. A per-connection reader thread only reads. When a
/// connection ends the link thread calls shutdown() — never close() — which
/// hands the reader an EOF it treats like any other, and only once that reader
/// has finished does the link thread close and dial again. That ordering is
/// what makes it impossible for a reader to still be sitting in read() on a
/// number the kernel has since handed back out for a new connection.
final class Link {

    /// Guards every stored property below, and wakes the writer when a frame
    /// arrives or the connection goes down.
    private let cond = NSCondition()
    private var queue = OutboundQueue(capacity: outboundCapacity)
    private var connected = false

    /// Link thread only, never under the lock: how many times connect() has
    /// been called and how many connections have been established. Both are in
    /// the log so "attempt 3" and "attempt 340" read differently at a glance.
    private var attempts = 0
    private var connections = 0
    private var delay = backoffFirstMicros

    // MARK: - Producer side (the stdin thread)

    /// Hands a frame to the link. Never blocks and never fails.
    ///
    /// A full queue drops its oldest entry and says so. That is deliberate: the
    /// alternative is making the stdin reader wait, and a stdin reader that is
    /// not reading cannot see Chrome close the port — which is this process's
    /// only exit condition.
    func enqueue(_ payload: Data) {
        cond.lock()
        let evicted = queue.append(payload)
        let depth = queue.count
        let up = connected
        cond.broadcast()
        cond.unlock()

        if let evicted {
            log("outbound queue is full at \(outboundCapacity); dropped the oldest " +
                "\(evicted.count)-byte frame to make room")
        }
        if !up {
            log("no SeoulApp connection; buffered a \(payload.count)-byte frame (\(depth) waiting)")
        }
    }

    /// Total frames lost to a full queue, for the exit line.
    var droppedCount: Int {
        cond.lock()
        defer { cond.unlock() }
        return queue.droppedCount
    }

    // MARK: - The link thread

    /// Connects, serves, reconnects. Never returns.
    func run() -> Never {
        while true {
            let fd = dial()
            serve(fd: fd)
            log("link to SeoulApp is down; reconnecting")
        }
    }

    /// Retries until it succeeds. There is no attempt limit, on purpose: the
    /// caller has nothing useful to do with a failure, because the only correct
    /// response to "SeoulApp is not up yet" is to ask again.
    private func dial() -> Int32 {
        while true {
            attempts += 1
            do {
                let fd = try UnixSocketClient.connect()
                connections += 1
                log("connect attempt \(attempts) to \(Bridge.socketPath): CONNECTED " +
                    "(connection #\(connections))")
                delay = backoffFirstMicros
                return fd
            } catch {
                log("connect attempt \(attempts) to \(Bridge.socketPath) failed: \(error) — " +
                    "retrying in \(delay / 1000) ms. This is normal while SeoulApp is not running.")
                usleep(delay)
                delay = min(delay &* 2, backoffCapMicros)
            }
        }
    }

    /// Runs one connection to completion. Returns when it is down.
    private func serve(fd: Int32) {
        cond.lock()
        connected = true
        let backlog = queue.count
        cond.broadcast()
        cond.unlock()
        if backlog > 0 {
            log("delivering \(backlog) frame(s) buffered while the socket was down")
        }

        let readerFinished = DispatchSemaphore(value: 0)
        let reader = Thread { [self] in
            readLoop(fd: fd)
            readerFinished.signal()
        }
        reader.stackSize = 512 * 1024
        reader.name = "seoul-host.socket-reader"
        reader.start()

        writeLoop(fd: fd)

        // The writer has stopped, so this connection is over. The reader may
        // still be parked in read(); shutdown() gives it the EOF that ends its
        // loop. Only after it has finished is the descriptor closed — see the
        // ownership note on the class.
        shutdown(fd, SHUT_RDWR)
        readerFinished.wait()
        close(fd)
    }

    /// Queue -> socket. Returns as soon as the connection is down.
    private func writeLoop(fd: Int32) {
        while true {
            cond.lock()
            while connected && queue.isEmpty { cond.wait() }
            guard connected, let payload = queue.popFirst() else {
                cond.unlock()
                return
            }
            cond.unlock()

            guard let framed = try? NativeMessaging.frame(payload) else {
                // Only reachable above Chrome's 1 MB cap, which Chrome itself
                // will not have sent. Dropped rather than fatal: nothing that
                // happens on this socket is allowed to end the process.
                log("DROPPED a \(payload.count)-byte frame from Chrome: could not frame it")
                continue
            }
            if NativeMessaging.writeAll(fd: fd, framed) { continue }

            // SeoulApp went away mid-write. Put the frame back so the next
            // connection still delivers it — a half-written frame on a dead
            // socket costs nothing, because the reconnect is a fresh stream
            // that has never seen any part of it.
            log("write to SeoulApp failed; requeuing the frame and dropping the connection")
            cond.lock()
            queue.requeue(payload)
            connected = false
            cond.unlock()
            return
        }
    }

    /// Socket -> stdout. The ONLY writer to fd 1 in this process.
    private func readLoop(fd: Int32) {
        while true {
            let payload: Data?
            do {
                payload = try NativeMessaging.readMessage(fd: fd)
            } catch {
                log("reading from SeoulApp: \(error)")
                break
            }
            guard let payload else {
                log("SeoulApp closed the socket")
                break
            }

            // Chrome enforces its cap by DROPPING the message with no error,
            // no callback and no log. Saying so here is the only trace such a
            // message will ever leave. It used to be fatal for exactly that
            // reason; it is now loud instead, because Chrome closing the port
            // is the one thing allowed to end this process.
            guard payload.count <= NativeMessaging.maxMessageBytes else {
                log("DROPPED a \(payload.count)-byte message from SeoulApp: over Chrome's " +
                    "\(NativeMessaging.maxMessageBytes)-byte cap, which Chrome would have " +
                    "discarded silently anyway")
                continue
            }
            guard let framed = try? NativeMessaging.frame(payload) else {
                log("DROPPED a \(payload.count)-byte message from SeoulApp: could not frame it")
                continue
            }
            guard NativeMessaging.writeAll(fd: STDOUT, framed) else {
                // stdout is gone — almost always Chrome having closed the port,
                // in which case stdin is about to report the same thing and the
                // exit happens down there where it belongs. Nothing is
                // recoverable from this side either way: a partly written frame
                // has already desynchronised the stream.
                log("write to stdout failed; Chrome will receive nothing further on this stream")
                break
            }
        }

        cond.lock()
        connected = false
        cond.broadcast()
        cond.unlock()
    }
}

let link = Link()

let linkThread = Thread { link.run() }
linkThread.stackSize = 512 * 1024
linkThread.name = "seoul-host.link"
linkThread.start()

// ---------------------------------------------------------------------------
// Chrome -> SeoulApp, on the main thread. Frames are relayed verbatim: this
// process does not parse or rewrite the JSON, it only moves bytes.
//
// This loop keeps reading whether or not the socket is up. Everything it reads
// goes to the link, which either writes it now or holds it until there is
// somewhere to write it to.
//
// Both exit(...) calls in this file are below.
// ---------------------------------------------------------------------------
while true {
    let payload: Data?
    do {
        payload = try NativeMessaging.readMessage(fd: STDIN)
    } catch {
        // stdin ended INSIDE a frame: Chrome died rather than closing the port
        // cleanly. Same practical outcome — there is nothing left to relay —
        // but it is a real fault, so it does not get a clean exit code.
        log("EXIT 1: stdin ended mid-frame (\(error)); \(link.droppedCount) frame(s) " +
            "were dropped this session")
        exit(1)
    }
    guard let payload else {
        // The normal end of a host: Chrome closed the port. Exiting closes the
        // SeoulApp socket with it, so the app sees the disconnect immediately.
        log("EXIT 0: Chrome closed the port (stdin EOF); \(link.droppedCount) frame(s) " +
            "were dropped this session")
        exit(0)
    }
    link.enqueue(payload)
}
