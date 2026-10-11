import CryptoKit
import Foundation

/// 27-byte OCP/1 control frame header codec (PROTOCOL.md §3).
struct ControlFrame: Equatable {
    var flags: UInt8
    var tag: Data
    var payload: Data

    var isEncrypted: Bool { flags & 0x01 != 0 }

    static func encode(_ frame: ControlFrame) throws -> Data {
        guard (1...OCP.controlMaxPayload).contains(frame.payload.count) else {
            throw OCPError.badFrameLength(frame.payload.count)
        }
        guard frame.tag.count == OCP.controlTagSize else { throw OCPError.protocolViolation("tag size") }
        var data = Data(capacity: OCP.controlHeaderSize + frame.payload.count)
        data.append(OCP.controlMagic)
        var lengthBE = UInt32(frame.payload.count).bigEndian
        withUnsafeBytes(of: &lengthBE) { data.append(contentsOf: $0) }
        data.append(frame.flags)
        data.append(contentsOf: [0, 0])
        data.append(frame.tag)
        data.append(frame.payload)
        return data
    }

    /// Parses one full frame from the front of `buffer`, consuming its bytes.
    static func decode(from buffer: inout Data) throws -> ControlFrame {
        guard buffer.count >= OCP.controlHeaderSize else { throw OCPError.truncated }
        guard buffer.prefix(4) == OCP.controlMagic else { throw OCPError.badMagic }
        let length: UInt32 = buffer.subdata(in: 4..<8).withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }
        guard (1...UInt32(OCP.controlMaxPayload)).contains(length) else {
            throw OCPError.badFrameLength(Int(length))
        }
        let total = OCP.controlHeaderSize + Int(length)
        guard buffer.count >= total else { throw OCPError.truncated }
        let flags = buffer[8]
        let tag = buffer.subdata(in: 11..<27)
        let payload = buffer.subdata(in: 27..<total)
        buffer.removeSubrange(0..<total)
        return ControlFrame(flags: flags, tag: tag, payload: payload)
    }
}

/// Incremental reassembler for a TCP byte stream of control frames.
struct ControlFrameStreamDecoder {
    private var buffer = Data()

    mutating func append(_ data: Data) {
        buffer.append(data)
    }

    mutating func nextFrame() throws -> ControlFrame? {
        var work = buffer
        do {
            let frame = try ControlFrame.decode(from: &work)
            buffer = work
            return frame
        } catch OCPError.truncated {
            return nil
        }
    }
}

/// ChaCha20 + HMAC-SHA256/128 encrypt-then-MAC for control frames (§3.1).
/// Per-direction: the phone uses one instance for sending and one for receiving.
final class ControlCipher {
    private let encKey: Data
    private let macKey: SymmetricKey
    private var sendCounter: UInt64 = 0
    private var nextReceiveCounter: UInt64 = 0

    init(encKey: Data, macKey: Data) {
        precondition(encKey.count == 32 && macKey.count == 32)
        self.encKey = encKey
        self.macKey = SymmetricKey(data: macKey)
    }

    func seal(payload: Data) throws -> ControlFrame {
        let nonce = Self.nonce(for: sendCounter)
        sendCounter &+= 1
        let ciphertext = ChaCha20.crypt(key: encKey, nonce: nonce, counter: 0, input: payload)
        var header = Data(capacity: OCP.controlHeaderSize)
        header.append(OCP.controlMagic)
        var lengthBE = UInt32(ciphertext.count).bigEndian
        withUnsafeBytes(of: &lengthBE) { header.append(contentsOf: $0) }
        header.append(0x01)
        header.append(contentsOf: [0, 0])
        header.append(Data(repeating: 0, count: OCP.controlTagSize))
        let tag = Self.truncatedMAC(key: macKey, message: header + ciphertext)
        return ControlFrame(flags: 0x01, tag: tag, payload: ciphertext)
    }

    func open(_ frame: ControlFrame) throws -> Data {
        guard frame.isEncrypted else { throw OCPError.protocolViolation("frame not encrypted") }
        var header = Data(capacity: OCP.controlHeaderSize)
        header.append(OCP.controlMagic)
        var lengthBE = UInt32(frame.payload.count).bigEndian
        withUnsafeBytes(of: &lengthBE) { header.append(contentsOf: $0) }
        header.append(frame.flags)
        header.append(contentsOf: [0, 0])
        header.append(Data(repeating: 0, count: OCP.controlTagSize))
        let expectedTag = Self.truncatedMAC(key: macKey, message: header + frame.payload)
        guard expectedTag == frame.tag else { throw OCPError.tagMismatch }
        // Counters are strictly sequential per direction starting at 0 (§3.1);
        // a replayed frame decrypts to garbage under the next counter and is
        // rejected downstream as a protocol violation when JSON fails to parse.
        let counter = nextReceiveCounter
        nextReceiveCounter &+= 1
        let nonce = Self.nonce(for: counter)
        return ChaCha20.crypt(key: encKey, nonce: nonce, counter: 0, input: frame.payload)
    }

    static func nonce(for counter: UInt64) -> Data {
        var nonce = Data(capacity: 12)
        var le = counter.littleEndian
        withUnsafeBytes(of: &le) { nonce.append(contentsOf: $0) }
        nonce.append(contentsOf: [0, 0, 0, 0])
        return nonce
    }

    static func truncatedMAC(key: SymmetricKey, message: Data) -> Data {
        Data(HMAC<SHA256>.authenticationCode(for: message, using: key).prefix(16))
    }
}
