import Foundation

/// Raw ChaCha20 stream cipher (RFC 8439 §2.3–§2.4).
///
/// CryptoKit only exposes the ChaChaPoly AEAD; OCP/1 needs the bare stream
/// cipher because it uses encrypt-then-HMAC instead of Poly1305.
///
/// Both control and media encryption use an initial block counter of 0;
/// packet/frame uniqueness comes entirely from the 12-byte nonce, which the
/// protocol constructs per packet (PROTOCOL.md §3.1, §4).
enum ChaCha20 {
    private static let sigma: [UInt32] = [0x61707865, 0x3320646E, 0x79622D32, 0x6B206574]

    static func crypt(key: Data, nonce: Data, counter: UInt32, input: Data) -> Data {
        precondition(key.count == 32, "ChaCha20 key must be 32 bytes")
        precondition(nonce.count == 12, "ChaCha20 nonce must be 12 bytes")
        var output = Data(capacity: input.count)
        var blockIndex: UInt32 = 0
        var offset = input.startIndex
        while offset < input.endIndex {
            let length = min(64, input.distance(from: offset, to: input.endIndex))
            let end = input.index(offset, offsetBy: length)
            let words = block(key: key, counter: counter &+ blockIndex, nonce: nonce)
            for (i, byte) in input[offset..<end].enumerated() {
                let word = words[i >> 2]
                let shift = UInt32((i & 3) * 8)
                output.append(byte ^ UInt8(truncatingIfNeeded: word >> shift))
            }
            offset = end
            blockIndex &+= 1
        }
        return output
    }

    /// The 16 output words of the ChaCha20 block function (RFC 8439 §2.3),
    /// serialized little-endian by the caller.
    static func block(key: Data, counter: UInt32, nonce: Data) -> [UInt32] {
        var state = [UInt32](repeating: 0, count: 16)
        for i in 0..<4 { state[i] = sigma[i] }
        key.withUnsafeBytes { raw in
            let words = raw.bindMemory(to: UInt32.self)
            for i in 0..<8 { state[4 + i] = words[i] }
        }
        state[12] = counter
        nonce.withUnsafeBytes { raw in
            let words = raw.bindMemory(to: UInt32.self)
            for i in 0..<3 { state[13 + i] = words[i] }
        }
        var working = state
        for _ in 0..<10 {
            quarterRound(&working, 0, 4, 8, 12)
            quarterRound(&working, 1, 5, 9, 13)
            quarterRound(&working, 2, 6, 10, 14)
            quarterRound(&working, 3, 7, 11, 15)
            quarterRound(&working, 0, 5, 10, 15)
            quarterRound(&working, 1, 6, 11, 12)
            quarterRound(&working, 2, 7, 8, 13)
            quarterRound(&working, 3, 4, 9, 14)
        }
        for i in 0..<16 { working[i] &+= state[i] }
        return working
    }

    private static func quarterRound(_ s: inout [UInt32], _ a: Int, _ b: Int, _ c: Int, _ d: Int) {
        s[a] &+= s[b]; s[d] ^= s[a]; s[d] = (s[d] << 16) | (s[d] >> 16)
        s[c] &+= s[d]; s[b] ^= s[c]; s[b] = (s[b] << 12) | (s[b] >> 20)
        s[a] &+= s[b]; s[d] ^= s[a]; s[d] = (s[d] << 8) | (s[d] >> 24)
        s[c] &+= s[d]; s[b] ^= s[c]; s[b] = (s[b] << 7) | (s[b] >> 25)
    }
}
