import Foundation

/// Minimal Unix-domain-socket plumbing for the SeoulApp <-> SeoulHost link.
///
/// WHY A SOCKET AND NOT ONE PROCESS
///
/// Chrome SPAWNS the native messaging host as a child and kills it the moment
/// the port closes — a tab navigating away, the extension reloading, the
/// browser quitting. The overlay must outlive all of that. So the host is a
/// disposable relay Chrome may start and stop at will, and the app is a
/// long-lived listener that does not care how many hosts have come and gone.

private func makeSockaddr(path: String) -> sockaddr_un {
    var addr = sockaddr_un()
    addr.sun_family = sa_family_t(AF_UNIX)
    let bytes = Array(path.utf8)
    // sun_path is 104 bytes on Darwin and is NOT null-terminated for us.
    precondition(bytes.count < MemoryLayout.size(ofValue: addr.sun_path),
                 "socket path too long for sockaddr_un")
    withUnsafeMutableBytes(of: &addr.sun_path) { raw in
        raw.copyBytes(from: bytes)
    }
    return addr
}

private func withSockaddr<T>(_ addr: inout sockaddr_un, _ body: (UnsafePointer<sockaddr>, socklen_t) -> T) -> T {
    withUnsafePointer(to: &addr) { ptr in
        ptr.withMemoryRebound(to: sockaddr.self, capacity: 1) { sa in
            body(sa, socklen_t(MemoryLayout<sockaddr_un>.size))
        }
    }
}

public enum SocketError: Error, CustomStringConvertible {
    case create(Int32)
    case bind(String, Int32)
    case listen(Int32)
    case connect(String, Int32)

    public var description: String {
        switch self {
        case .create(let e): return "socket() failed: \(String(cString: strerror(e)))"
        case .bind(let p, let e): return "bind(\(p)) failed: \(String(cString: strerror(e)))"
        case .listen(let e): return "listen() failed: \(String(cString: strerror(e)))"
        case .connect(let p, let e): return "connect(\(p)) failed: \(String(cString: strerror(e)))"
        }
    }
}

/// Server side. Lives in SeoulApp.
public final class UnixSocketServer {

    private let path: String
    private let queue = DispatchQueue(label: "com.seoul.bridge.server")
    private var listenFD: Int32 = -1
    private var acceptSource: DispatchSourceRead?

    /// fd -> live connection state. Keyed so a repeated connect/disconnect cycle
    /// cannot leave a dangling source or a leaked descriptor behind.
    private final class Connection {
        let fd: Int32
        var buffer = Data()
        var source: DispatchSourceRead?
        init(fd: Int32) { self.fd = fd }
    }
    private var connections: [Int32: Connection] = [:]
    /// The most recent connection: the host Chrome currently has running.
    private var currentFD: Int32 = -1

    /// Delivered on the main queue.
    public var onMessage: ((Data) -> Void)?
    public var onConnect: (() -> Void)?
    public var onDisconnect: (() -> Void)?

    public init(path: String = Bridge.socketPath) {
        self.path = path
    }

    public var isConnected: Bool {
        queue.sync { currentFD >= 0 }
    }

    public func start() throws {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketError.create(errno) }

        // A stale socket file survives a crash and makes bind() fail with
        // EADDRINUSE forever after. Unlink first, every time.
        unlink(path)

        var addr = makeSockaddr(path: path)
        let bound = withSockaddr(&addr) { sa, len in Darwin.bind(fd, sa, len) }
        guard bound == 0 else {
            let e = errno
            close(fd)
            throw SocketError.bind(path, e)
        }
        guard Darwin.listen(fd, 4) == 0 else {
            let e = errno
            close(fd)
            throw SocketError.listen(e)
        }
        // Only this user's processes may connect.
        chmod(path, 0o600)

        listenFD = fd
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.acceptOne() }
        source.setCancelHandler { close(fd) }
        acceptSource = source
        source.resume()
    }

    public func stop() {
        queue.sync {
            for (_, connection) in connections {
                connection.source?.cancel()
            }
            connections.removeAll()
            currentFD = -1
        }
        acceptSource?.cancel()
        acceptSource = nil
        listenFD = -1
        unlink(path)
    }

    /// Sends one framed message to the current host connection.
    @discardableResult
    public func send(_ payload: Data) -> Bool {
        queue.sync {
            guard currentFD >= 0, let framed = try? NativeMessaging.frame(payload) else { return false }
            return NativeMessaging.writeAll(fd: currentFD, framed)
        }
    }

    private func acceptOne() {
        let fd = accept(listenFD, nil, nil)
        guard fd >= 0 else { return }
        // Non-blocking, so the read handler can never stall the queue.
        let flags = fcntl(fd, F_GETFL, 0)
        _ = fcntl(fd, F_SETFL, flags | O_NONBLOCK)

        let connection = Connection(fd: fd)
        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [weak self] in self?.readFrom(connection) }
        source.setCancelHandler { close(fd) }
        connection.source = source
        connections[fd] = connection
        currentFD = fd
        source.resume()

        DispatchQueue.main.async { [weak self] in self?.onConnect?() }
    }

    private func readFrom(_ connection: Connection) {
        var chunk = [UInt8](repeating: 0, count: 16 * 1024)
        while true {
            let n = chunk.withUnsafeMutableBytes { read(connection.fd, $0.baseAddress!, $0.count) }
            if n > 0 {
                connection.buffer.append(contentsOf: chunk[0..<n])
                continue
            }
            if n < 0 && errno == EINTR { continue }
            if n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) { break }
            // n == 0 is a clean disconnect; anything else is a dead socket.
            drainFrames(connection)
            teardown(connection)
            return
        }
        drainFrames(connection)
    }

    private func drainFrames(_ connection: Connection) {
        while connection.buffer.count >= 4 {
            let length = connection.buffer.prefix(4).withUnsafeBytes {
                Int($0.loadUnaligned(as: UInt32.self))
            }
            guard length >= 0, connection.buffer.count >= 4 + length else { break }
            let payload = connection.buffer.subdata(in: 4..<(4 + length))
            connection.buffer.removeSubrange(0..<(4 + length))
            DispatchQueue.main.async { [weak self] in self?.onMessage?(payload) }
        }
    }

    private func teardown(_ connection: Connection) {
        connection.source?.cancel()
        connection.source = nil
        connections.removeValue(forKey: connection.fd)
        if currentFD == connection.fd {
            // Fall back to any other live connection, else "none".
            currentFD = connections.keys.max() ?? -1
        }
        DispatchQueue.main.async { [weak self] in self?.onDisconnect?() }
    }
}

/// Client side. Lives in SeoulHost.
public enum UnixSocketClient {
    public static func connect(path: String = Bridge.socketPath) throws -> Int32 {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw SocketError.create(errno) }
        var addr = makeSockaddr(path: path)
        let result = withSockaddr(&addr) { sa, len in Darwin.connect(fd, sa, len) }
        guard result == 0 else {
            let e = errno
            close(fd)
            throw SocketError.connect(path, e)
        }
        return fd
    }
}
