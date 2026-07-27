import Foundation

/// Chrome native-messaging framing, shared by SeoulHost and SeoulApp.
///
/// This lives in one place on purpose. The two executables sit on opposite ends
/// of the same pipe, and a framing mismatch between them does not raise an
/// error — it produces a length prefix read as garbage and a reader that blocks
/// forever waiting for bytes that will never arrive. One definition, both sides.
public enum NativeMessaging {

    /// Chrome silently DROPS host-to-browser messages above this. No error, no
    /// callback, no log: the message simply never arrives.
    public static let maxMessageBytes = 1024 * 1024

    public enum FramingError: Error, CustomStringConvertible {
        case messageTooLarge(Int)
        case truncatedHeader
        case truncatedBody(expected: Int, got: Int)

        public var description: String {
            switch self {
            case .messageTooLarge(let n):
                return "outbound message is \(n) bytes, over Chrome's \(NativeMessaging.maxMessageBytes)-byte cap"
            case .truncatedHeader:
                return "stream ended inside a 4-byte length header"
            case .truncatedBody(let expected, let got):
                return "stream ended inside a message body (expected \(expected) bytes, got \(got))"
            }
        }
    }

    /// Prefixes `payload` with its length as a UInt32 in NATIVE byte order.
    ///
    /// Native order is the specification, not a shortcut: Chrome reads the
    /// header with the host machine's endianness. Writing big-endian here is the
    /// classic way to get a host that appears to run and never delivers a single
    /// message — a 27-byte payload announces itself as 452,984,832 bytes and the
    /// browser waits forever. Copying the raw bytes of a UInt32 gives native
    /// order by construction, which is why there is no byte-swap in sight.
    public static func frame(_ payload: Data) throws -> Data {
        guard payload.count <= maxMessageBytes else {
            throw FramingError.messageTooLarge(payload.count)
        }
        var length = UInt32(payload.count)
        var out = Data(capacity: payload.count + 4)
        withUnsafeBytes(of: &length) { out.append(contentsOf: $0) }
        out.append(payload)
        return out
    }

    /// Reads one length-prefixed message. Returns nil at a clean end of stream.
    public static func readMessage(fd: Int32) throws -> Data? {
        guard let header = readExactly(fd: fd, count: 4) else { return nil }
        guard header.count == 4 else { throw FramingError.truncatedHeader }
        let length = header.withUnsafeBytes { $0.loadUnaligned(as: UInt32.self) }
        if length == 0 { return Data() }
        guard let body = readExactly(fd: fd, count: Int(length)) else {
            throw FramingError.truncatedBody(expected: Int(length), got: 0)
        }
        guard body.count == Int(length) else {
            throw FramingError.truncatedBody(expected: Int(length), got: body.count)
        }
        return body
    }

    /// Blocking read of exactly `count` bytes. Returns nil on clean EOF, and a
    /// short buffer only when the stream ended mid-message.
    public static func readExactly(fd: Int32, count: Int) -> Data? {
        var buffer = [UInt8](repeating: 0, count: count)
        var filled = 0
        while filled < count {
            let n = buffer.withUnsafeMutableBytes { raw -> Int in
                read(fd, raw.baseAddress!.advanced(by: filled), count - filled)
            }
            if n == 0 { return filled == 0 ? nil : Data(buffer.prefix(filled)) }
            if n < 0 {
                if errno == EINTR { continue }
                return filled == 0 ? nil : Data(buffer.prefix(filled))
            }
            filled += n
        }
        return Data(buffer)
    }

    /// Blocking write of the whole buffer, tolerating partial writes.
    @discardableResult
    public static func writeAll(fd: Int32, _ data: Data) -> Bool {
        var sent = 0
        let total = data.count
        return data.withUnsafeBytes { raw -> Bool in
            guard let base = raw.baseAddress else { return true }
            while sent < total {
                let n = write(fd, base.advanced(by: sent), total - sent)
                if n <= 0 {
                    if n < 0 && errno == EINTR { continue }
                    return false
                }
                sent += n
            }
            return true
        }
    }
}
