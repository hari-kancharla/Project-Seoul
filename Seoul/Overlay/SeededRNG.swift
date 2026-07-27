import Foundation

/// Deterministic, value-type PRNG (SplitMix64).
///
/// WHY THIS EXISTS AT ALL
///
/// An annotation is re-rendered on every scroll frame. The jitter that makes a
/// sketch look hand-drawn must therefore be a pure function of *which element*
/// is annotated, never of when it was drawn. If it re-rolls between frames the
/// stroke crawls and the whole effect reads as a glitch rather than as ink.
///
/// So: same elementID -> same stream of numbers -> byte-identical path, every
/// time, in every process, forever.
///
/// WHY NOT `Hasher`
///
/// Swift seeds `Hasher` per process from system entropy, so
/// `"submit-button".hashValue` is a different number on every launch. It is
/// explicitly documented as unstable across executions. FNV-1a below is fixed
/// by its constants and cannot drift.
///
/// WHY THE DOUBLE CONVERSION IS HAND-WRITTEN
///
/// `Double.random(in:using:)` consumes an unspecified number of bits in an
/// unspecified way. It is deterministic for a given standard library, but the
/// standard library does not promise the algorithm across toolchain versions.
/// `unit()` below pins it, so a Swift upgrade cannot silently reshape every
/// sketch in the product.
public struct SeededRNG: RandomNumberGenerator {

    private var state: UInt64

    public init(seed: UInt64) {
        // Mix once up front: SplitMix64 tolerates any seed, but adjacent raw
        // seeds produce first outputs that visibly correlate, and adjacent
        // element IDs ("row-1", "row-2") hash to nearby values.
        self.state = seed &+ 0x9E37_79B9_7F4A_7C15
    }

    public init(elementID: String) {
        self.init(seed: SeededRNG.stableHash(elementID))
    }

    /// FNV-1a, 64-bit. Stable across processes, machines and Swift versions.
    public static func stableHash(_ string: String) -> UInt64 {
        var hash: UInt64 = 0xcbf2_9ce4_8422_2325          // offset basis
        for byte in string.utf8 {
            hash ^= UInt64(byte)
            hash = hash &* 0x0000_0100_0000_01B3          // prime
        }
        return hash
    }

    public mutating func next() -> UInt64 {
        state = state &+ 0x9E37_79B9_7F4A_7C15
        var z = state
        z = (z ^ (z >> 30)) &* 0xBF58_476D_1CE4_E5B9
        z = (z ^ (z >> 27)) &* 0x94D0_49BB_1331_11EB
        return z ^ (z >> 31)
    }
}

extension SeededRNG {

    /// Uniform in [0, 1). Takes the top 53 bits, which is the widest slice that
    /// maps exactly onto Double's significand.
    public mutating func unit() -> Double {
        Double(next() >> 11) * (1.0 / 9_007_199_254_740_992.0)   // 1 / 2^53
    }

    public mutating func double(in range: ClosedRange<Double>) -> Double {
        range.lowerBound + unit() * (range.upperBound - range.lowerBound)
    }

    /// Modulo reduction. The bias is on the order of 2^-62 for the tiny ranges
    /// this is used with (6...10), and unlike rejection sampling it consumes
    /// exactly one draw, which keeps the stream position predictable.
    public mutating func int(in range: ClosedRange<Int>) -> Int {
        let span = UInt64(range.upperBound - range.lowerBound + 1)
        return range.lowerBound + Int(next() % span)
    }

    /// Symmetric jitter in [-magnitude, +magnitude].
    public mutating func jitter(_ magnitude: Double) -> Double {
        double(in: -magnitude ... magnitude)
    }

    /// A seeded coin, for choices that must be stable per element (which way an
    /// arrow bows, for instance).
    public mutating func coin() -> Bool {
        next() & 1 == 0
    }
}
