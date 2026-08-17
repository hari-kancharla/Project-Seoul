import XCTest
@testable import SeoulBridge

/// SeoulApp's side of the bridge, on a private socket path.
///
/// The case that matters here is the one the host's retry loop runs into: a
/// socket FILE left behind by a SeoulApp that crashed or was killed. It is not
/// a listener, so connect() gets ECONNREFUSED forever, and bind() gets
/// EADDRINUSE forever — the app cannot come back and the host can never
/// succeed. Both halves are stuck on a zero-byte file nobody owns.
final class UnixSocketServerTests: XCTestCase {

    /// Short on purpose: sun_path is 104 bytes on Darwin, and the temp
    /// directory alone can eat most of that.
    private var path = ""

    override func setUp() {
        super.setUp()
        path = "/tmp/seoul-test-\(getpid())-\(UInt32.random(in: 0..<100_000)).sock"
        unlink(path)
    }

    override func tearDown() {
        unlink(path)
        super.tearDown()
    }

    /// Leaves the exact debris a killed SeoulApp leaves: a bound socket file
    /// with no process behind it.
    private func makeStaleSocketFile() {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        XCTAssertGreaterThanOrEqual(fd, 0)
        var addr = sockaddr_un()
        addr.sun_family = sa_family_t(AF_UNIX)
        withUnsafeMutableBytes(of: &addr.sun_path) { $0.copyBytes(from: Array(path.utf8)) }
        let bound = withUnsafePointer(to: &addr) { ptr in
            ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
                // Darwin.bind, not XCTestCase's bind(_:) — the unqualified
                // name resolves to the instance method and does not compile.
                Darwin.bind(fd, sa, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
        XCTAssertEqual(bound, 0, "could not set up the stale socket")
        close(fd)   // deliberately NOT unlinked: that is the whole point
        XCTAssertTrue(FileManager.default.fileExists(atPath: path))
    }

    func testStartUnlinksAStaleSocketFileInsteadOfFailingToBind() throws {
        makeStaleSocketFile()

        let server = UnixSocketServer(path: path)
        // Without the unlink in start(), this throws SocketError.bind with
        // EADDRINUSE and SeoulApp never listens again until someone deletes the
        // file by hand.
        XCTAssertNoThrow(try server.start())
        defer { server.stop() }

        // Bound is not enough — prove a host can actually reach it.
        let fd = try UnixSocketClient.connect(path: path)
        XCTAssertGreaterThanOrEqual(fd, 0)
        close(fd)
    }

    func testStopUnlinksSoTheNextStartIsClean() throws {
        let server = UnixSocketServer(path: path)
        try server.start()
        XCTAssertTrue(FileManager.default.fileExists(atPath: path))

        server.stop()
        XCTAssertFalse(FileManager.default.fileExists(atPath: path),
                       "a clean quit should not leave debris for the next launch")
    }

    /// A stale file is exactly what makes connect() fail with "Connection
    /// refused" rather than "No such file" — the error in the report that
    /// started this. Pinned so the host's retry loop is known to be looping on
    /// a recoverable condition and not on something permanent.
    func testConnectingToAStaleSocketIsRefusedRatherThanMissing() {
        makeStaleSocketFile()

        XCTAssertThrowsError(try UnixSocketClient.connect(path: path)) { error in
            guard case SocketError.connect(_, let code)? = error as? SocketError else {
                return XCTFail("expected a connect error, got \(error)")
            }
            XCTAssertEqual(code, ECONNREFUSED)
        }
    }

    func testAConnectingClientIsReportedAndItsFramesArriveWhole() throws {
        let server = UnixSocketServer(path: path)
        let connected = expectation(description: "onConnect")
        let delivered = expectation(description: "onMessage")
        var received: Data?

        server.onConnect = { connected.fulfill() }
        server.onMessage = { received = $0; delivered.fulfill() }
        try server.start()
        defer { server.stop() }

        let fd = try UnixSocketClient.connect(path: path)
        defer { close(fd) }
        wait(for: [connected], timeout: 2)

        let payload = Data(#"{"op":"find"}"#.utf8)
        XCTAssertTrue(NativeMessaging.writeAll(fd: fd, try NativeMessaging.frame(payload)))
        wait(for: [delivered], timeout: 2)
        XCTAssertEqual(received, payload)
    }
}
