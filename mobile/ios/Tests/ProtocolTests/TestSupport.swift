import Foundation

/// Shared fixtures for the OCP/1 protocol tests.
///
/// The key/payload derivations below mirror the helper functions in the C++
/// reference tests (tests/test_control_frame.cpp, tests/test_media.cpp) so
/// that both suites exercise byte-identical inputs.

/// Decodes a hex string from a checked-in test vector; traps on a typo.
func ocpHex(_ hex: String) -> Data {
    guard let data = Hex.decode(hex) else {
        fatalError("invalid hex literal in test vector: \(hex)")
    }
    return data
}

/// Mirrors `makeKeys()` from tests/test_control_frame.cpp.
func controlTestKeys(seed: Int = 0) -> (enc: Data, mac: Data) {
    var enc = Data(capacity: 32)
    var mac = Data(capacity: 32)
    for i in 0..<32 {
        enc.append(UInt8(truncatingIfNeeded: i * 3 + 1 + seed * 17))
        mac.append(UInt8(truncatingIfNeeded: 255 - i - seed * 13))
    }
    return (enc, mac)
}

/// Mirrors `makeMediaKeys()` from tests/test_media.cpp.
func mediaTestKeys() -> (enc: Data, mac: Data) {
    var enc = Data(capacity: 32)
    var mac = Data(capacity: 32)
    for i in 0..<32 {
        enc.append(UInt8(truncatingIfNeeded: i * 7 + 3))
        mac.append(UInt8(truncatingIfNeeded: i ^ 0x5a))
    }
    return (enc, mac)
}

/// Mirrors `bytes(n, seed)` from tests/test_media.cpp.
func patternedBytes(count: Int, seed: Int) -> Data {
    var data = Data(capacity: count)
    for i in 0..<count {
        data.append(UInt8(truncatingIfNeeded: seed + i * 31))
    }
    return data
}
