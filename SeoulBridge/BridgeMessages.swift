import Foundation

/// The wire vocabulary between SeoulApp, SeoulHost, and the extension.
///
/// Deliberately tiny. The harvest and the candidate list never cross this
/// boundary — a query goes out, rects come back. Everything the resolver needs
/// to think with stays in the page, where it was measured.
public enum Bridge {

    /// Where the two halves meet.
    ///
    /// Overridable so an integration test can run a host and a listener on a
    /// private socket without fighting a SeoulApp the user has open for real —
    /// the tests here start and kill listeners repeatedly, and doing that to
    /// the live socket would take the user's overlay down with it.
    ///
    /// Keep any override SHORT. sun_path is 104 bytes on Darwin and the
    /// precondition in UnixSocket.swift is a hard stop, not a truncation.
    public static let socketPath =
        ProcessInfo.processInfo.environment["SEOUL_SOCKET_PATH"] ?? "/tmp/seoul.sock"

    /// SeoulApp -> SeoulHost -> extension.
    public struct FindRequest: Codable {
        public let op: String
        public let query: String
        public let requestId: String

        public init(query: String, requestId: String) {
            self.op = "find"
            self.query = query
            self.requestId = requestId
        }
    }

    /// A rect as the page measured it: CSS pixels, viewport-relative, y down.
    public struct WireRect: Codable {
        public let x: Double
        public let y: Double
        public let width: Double
        public let height: Double
    }

    /// The viewport numbers, measured in the SAME animation frame as the rects.
    ///
    /// If these are sampled in a different frame from the rects, the sketch
    /// trails the element by exactly one frame during a scroll — which reads as
    /// the overlay being "laggy" and is nearly impossible to diagnose after the
    /// fact. The extension is responsible for the co-measurement; this struct
    /// only carries it.
    public struct WireContext: Codable {
        public let screenX: Double
        public let screenY: Double
        public let innerWidth: Double
        public let innerHeight: Double
        public let outerWidth: Double
        public let outerHeight: Double
        public let devicePixelRatio: Double

        // Pointer calibration: one real mouse event's screen and client
        // coordinates, from which the viewport's screen origin follows exactly.
        //
        // This exists because outer/inner arithmetic CANNOT locate the viewport
        // when the browser chrome is asymmetric. Measured on this machine with
        // Chrome's vertical tabs enabled: the window is 1200x815 with a 960x768
        // viewport, and the real insets are 224 left / 16 right / 47 top / 0
        // bottom. No function of those four numbers yields 224, because the
        // horizontal split is simply not encoded in them.
        //
        // Optional: a page that has not seen a pointer event yet sends nothing
        // and the native side falls back to the (approximate) arithmetic.
        public let calibScreenX: Double?
        public let calibScreenY: Double?
        public let calibClientX: Double?
        public let calibClientY: Double?
    }

    /// extension -> SeoulHost -> SeoulApp.
    public struct FindResponse: Codable {
        public let requestId: String
        /// "confident" | "ambiguous" | "none" | "error"
        public let status: String
        public let rects: [WireRect]?
        public let ctx: WireContext?
        public let label: String?
        public let elementId: String?
        public let error: String?
    }

    public static func encode<T: Encodable>(_ value: T) throws -> Data {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        return try encoder.encode(value)
    }

    public static func decode<T: Decodable>(_ type: T.Type, from data: Data) throws -> T {
        try JSONDecoder().decode(type, from: data)
    }
}
