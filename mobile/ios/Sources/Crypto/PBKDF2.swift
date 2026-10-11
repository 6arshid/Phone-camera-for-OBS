import CryptoKit
import Foundation

/// PBKDF2-HMAC-SHA256 (RFC 8018 §5.2). CryptoKit does not provide PBKDF2,
/// and OCP/1 requires it for pairing-code key stretching (PROTOCOL.md §3.2).
enum PBKDF2 {
    static func derive(password: Data, salt: Data, iterations: Int, keyLength: Int) -> Data {
        precondition(iterations > 0 && keyLength > 0)
        let hashLength = 32
        let blockCount = (keyLength + hashLength - 1) / hashLength
        let passwordKey = SymmetricKey(data: password)
        var derived = Data(capacity: keyLength)
        for blockIndex in 1...blockCount {
            var block = salt
            let indexBE = UInt32(blockIndex).bigEndian
            withUnsafeBytes(of: indexBE) { block.append(contentsOf: $0) }
            var u = Data(HMAC<SHA256>.authenticationCode(for: block, using: passwordKey))
            var t = u
            if iterations > 1 {
                for _ in 1..<iterations {
                    u = Data(HMAC<SHA256>.authenticationCode(for: u, using: passwordKey))
                    for i in 0..<t.count { t[i] ^= u[i] }
                }
            }
            derived.append(t)
        }
        return Data(derived.prefix(keyLength))
    }
}
