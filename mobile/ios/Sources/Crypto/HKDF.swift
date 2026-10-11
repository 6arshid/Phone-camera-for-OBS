import CryptoKit
import Foundation

/// HKDF-SHA256 (RFC 5869) built on CryptoKit's HMAC.
enum HKDF {
    static func extract(salt: Data, ikm: Data) -> Data {
        let key = SymmetricKey(data: salt.isEmpty ? Data(repeating: 0, count: 32) : salt)
        return Data(HMAC<SHA256>.authenticationCode(for: ikm, using: key))
    }

    static func expand(prk: Data, info: Data, length: Int) -> Data {
        precondition(length > 0 && length <= 255 * 32)
        let key = SymmetricKey(data: prk)
        var okm = Data(capacity: length)
        var previous = Data()
        var counter: UInt8 = 1
        while okm.count < length {
            var input = previous
            input.append(info)
            input.append(counter)
            previous = Data(HMAC<SHA256>.authenticationCode(for: input, using: key))
            okm.append(previous)
            counter &+= 1
        }
        return Data(okm.prefix(length))
    }

    static func derive(ikm: Data, salt: Data, info: String, length: Int) -> Data {
        expand(prk: extract(salt: salt, ikm: ikm), info: Data(info.utf8), length: length)
    }
}
