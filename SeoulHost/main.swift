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

import Foundation
import SeoulBridge

let STDIN: Int32 = 0
let STDOUT: Int32 = 1

/// stderr only. See the banner above.
func log(_ message: String) {
    FileHandle.standardError.write(Data("[seoul-host] \(message)\n".utf8))
}

/// Fails the process loudly. Used where continuing would silently corrupt the
/// stream or silently drop a message — both of which are far worse than dying.
func fail(_ message: String, code: Int32) -> Never {
    log("FATAL: \(message)")
    exit(code)
}

// Chrome starts this process; SeoulApp may still be coming up, or may not be
// running at all. Retry briefly, then give up with a legible reason rather than
// dying instantly on a race at login.
func connectWithRetry(attempts: Int = 10, delay: useconds_t = 100_000) -> Int32 {
    for attempt in 1...attempts {
        do {
            return try UnixSocketClient.connect()
        } catch {
            if attempt == attempts {
                fail("cannot reach SeoulApp on \(Bridge.socketPath): \(error). Is SeoulApp running?", code: 1)
            }
            usleep(delay)
        }
    }
    fail("unreachable", code: 1)
}

let socketFD = connectWithRetry()
log("connected to SeoulApp at \(Bridge.socketPath)")

// ---------------------------------------------------------------------------
// SeoulApp -> Chrome. Runs on its own thread so both directions are independent
// blocking loops; stdout is written from HERE and nowhere else.
// ---------------------------------------------------------------------------
let pump = Thread {
    while true {
        let payload: Data?
        do {
            payload = try NativeMessaging.readMessage(fd: socketFD)
        } catch {
            fail("reading from SeoulApp: \(error)", code: 3)
        }
        guard let payload else {
            log("SeoulApp closed the socket; exiting")
            exit(0)
        }

        // The cap is Chrome's, and Chrome enforces it by DROPPING the message
        // with no error. A host that silently loses responses is much harder to
        // diagnose than one that dies, so this is fatal on purpose.
        guard payload.count <= NativeMessaging.maxMessageBytes else {
            fail("outbound message is \(payload.count) bytes, over Chrome's " +
                 "\(NativeMessaging.maxMessageBytes)-byte cap; Chrome would drop it silently", code: 4)
        }

        do {
            let framed = try NativeMessaging.frame(payload)
            guard NativeMessaging.writeAll(fd: STDOUT, framed) else {
                fail("short write to stdout; the frame is now corrupt", code: 5)
            }
        } catch {
            fail("framing outbound message: \(error)", code: 4)
        }
    }
}
pump.stackSize = 512 * 1024
pump.start()

// ---------------------------------------------------------------------------
// Chrome -> SeoulApp, on the main thread. Frames are relayed verbatim: this
// process does not parse or rewrite the JSON, it only moves bytes.
// ---------------------------------------------------------------------------
while true {
    let payload: Data?
    do {
        payload = try NativeMessaging.readMessage(fd: STDIN)
    } catch {
        fail("reading from Chrome: \(error)", code: 3)
    }
    guard let payload else {
        // Clean EOF: Chrome closed the port. This is the normal way a host
        // dies, and SeoulApp must survive it.
        log("Chrome closed the port; exiting")
        close(socketFD)
        exit(0)
    }

    do {
        let framed = try NativeMessaging.frame(payload)
        guard NativeMessaging.writeAll(fd: socketFD, framed) else {
            fail("short write to SeoulApp socket", code: 5)
        }
    } catch {
        fail("framing inbound message: \(error)", code: 4)
    }
}
