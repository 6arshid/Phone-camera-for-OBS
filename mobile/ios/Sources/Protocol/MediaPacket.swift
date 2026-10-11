import CryptoKit
import Foundation

/// 32-byte media packet header (PROTOCOL.md §4), all fields big endian.
struct MediaHeader: Equatable {
    var stream: UInt8
    var flags: UInt8
    var sessionTag: Data // first 8 bytes of the 16-byte session id
    var frameID: UInt32
    var fragmentIndex: UInt16
    var fragmentCount: UInt16
    var timestamp: UInt64

    var isKeyframe: Bool { flags & OCP.mediaFlagKeyframe != 0 }

    func encode() -> Data {
        precondition(sessionTag.count == 8)
        var data = Data(capacity: OCP.mediaHeaderSize)
        data.append(OCP.mediaMagic)
        data.append(1)
        data.append(stream)
        data.append(flags)
        data.append(0)
        data.append(sessionTag)
        var frameBE = frameID.bigEndian
        withUnsafeBytes(of: &frameBE) { data.append(contentsOf: $0) }
        var idxBE = fragmentIndex.bigEndian
        withUnsafeBytes(of: &idxBE) { data.append(contentsOf: $0) }
        var countBE = fragmentCount.bigEndian
        withUnsafeBytes(of: &countBE) { data.append(contentsOf: $0) }
        var tsBE = timestamp.bigEndian
        withUnsafeBytes(of: &tsBE) { data.append(contentsOf: $0) }
        return data
    }

    static func decode(_ data: Data) throws -> MediaHeader {
        guard data.count >= OCP.mediaHeaderSize else { throw OCPError.truncated }
        guard data.prefix(4) == OCP.mediaMagic else { throw OCPError.badMagic }
        guard data[4] == 1 else { throw OCPError.badVersion }
        let stream = data[5]
        let flags = data[6]
        let sessionTag = data.subdata(in: 8..<16)
        let frameID: UInt32 = data.subdata(in: 16..<20).withUnsafeBytes { $0.load(as: UInt32.self).bigEndian }
        let fragmentIndex: UInt16 = data.subdata(in: 20..<22).withUnsafeBytes { $0.load(as: UInt16.self).bigEndian }
        let fragmentCount: UInt16 = data.subdata(in: 22..<24).withUnsafeBytes { $0.load(as: UInt16.self).bigEndian }
        let timestamp: UInt64 = data.subdata(in: 24..<32).withUnsafeBytes { $0.load(as: UInt64.self).bigEndian }
        guard fragmentCount >= 1 else { throw OCPError.protocolViolation("fragment count") }
        guard fragmentIndex < fragmentCount else { throw OCPError.protocolViolation("fragment index") }
        return MediaHeader(
            stream: stream,
            flags: flags,
            sessionTag: sessionTag,
            frameID: frameID,
            fragmentIndex: fragmentIndex,
            fragmentCount: fragmentCount,
            timestamp: timestamp
        )
    }
}

/// Media channel seal/open (PROTOCOL.md §4):
/// ChaCha20 nonce = session_tag[0..4) || stream || u32 frame_id || u16 frag_idx || 0x00,
/// tag = first 16 bytes of HMAC-SHA256(k_media_mac, header[0..31] || ciphertext),
/// k_media_enc = k_media[0..32), k_media_mac = k_media[32..64).
enum MediaPacket {
    static func nonce(sessionTag: Data, stream: UInt8, frameID: UInt32, fragmentIndex: UInt16) -> Data {
        precondition(sessionTag.count >= 4)
        var nonce = Data(sessionTag.prefix(4))
        nonce.append(stream)
        var frameBE = frameID.bigEndian
        withUnsafeBytes(of: &frameBE) { nonce.append(contentsOf: $0) }
        var idxBE = fragmentIndex.bigEndian
        withUnsafeBytes(of: &idxBE) { nonce.append(contentsOf: $0) }
        nonce.append(0)
        return nonce
    }

    static func seal(
        encKey: Data,
        macKey: Data,
        header: MediaHeader,
        plaintext: Data
    ) throws -> Data {
        guard plaintext.count <= OCP.mediaMaxCiphertext else { throw OCPError.oversizedDatagram }
        let nonce = self.nonce(
            sessionTag: header.sessionTag,
            stream: header.stream,
            frameID: header.frameID,
            fragmentIndex: header.fragmentIndex
        )
        let ciphertext = ChaCha20.crypt(key: encKey, nonce: nonce, counter: 0, input: plaintext)
        let encodedHeader = header.encode()
        var macInput = encodedHeader
        macInput.append(ciphertext)
        let tag = Data(HMAC<SHA256>.authenticationCode(for: macInput, using: SymmetricKey(data: macKey)).prefix(16))
        var datagram = encodedHeader
        datagram.append(ciphertext)
        datagram.append(tag)
        guard datagram.count <= OCP.mediaMaxDatagram else { throw OCPError.oversizedDatagram }
        return datagram
    }

    /// Opens and authenticates one datagram; returns the header and plaintext fragment.
    static func open(encKey: Data, macKey: Data, datagram: Data) throws -> (MediaHeader, Data) {
        guard datagram.count <= OCP.mediaMaxDatagram else { throw OCPError.oversizedDatagram }
        guard datagram.count >= OCP.mediaHeaderSize + OCP.mediaTagSize else { throw OCPError.truncated }
        let header = try MediaHeader.decode(datagram)
        let ciphertextEnd = datagram.count - OCP.mediaTagSize
        let ciphertext = datagram.subdata(in: OCP.mediaHeaderSize..<ciphertextEnd)
        let tag = datagram.subdata(in: ciphertextEnd..<datagram.count)
        var macInput = datagram.prefix(OCP.mediaHeaderSize)
        macInput.append(ciphertext)
        let expected = Data(HMAC<SHA256>.authenticationCode(for: macInput, using: SymmetricKey(data: macKey)).prefix(16))
        guard expected == tag else { throw OCPError.tagMismatch }
        let nonce = self.nonce(
            sessionTag: header.sessionTag,
            stream: header.stream,
            frameID: header.frameID,
            fragmentIndex: header.fragmentIndex
        )
        let plaintext = ChaCha20.crypt(key: encKey, nonce: nonce, counter: 0, input: ciphertext)
        return (header, plaintext)
    }

    /// Splits a frame payload into ≤1152-byte fragments.
    static func fragments(of payload: Data) -> [(index: UInt16, chunk: Data)] {
        if payload.isEmpty {
            return [(index: 0, chunk: Data())]
        }
        var result: [(UInt16, Data)] = []
        var offset = payload.startIndex
        var index: UInt16 = 0
        while offset < payload.endIndex {
            let end = payload.index(offset, offsetBy: min(OCP.mediaMaxCiphertext, payload.distance(from: offset, to: payload.endIndex)))
            result.append((index, Data(payload[offset..<end])))
            offset = end
            index &+= 1
        }
        return result
    }

    /// Seals a complete media frame (video access unit or audio frame) into
    /// one or more datagrams sharing frame id, flags and timestamp.
    static func sealFrame(
        encKey: Data,
        macKey: Data,
        sessionTag: Data,
        stream: UInt8,
        isKeyframe: Bool,
        frameID: UInt32,
        timestamp: UInt64,
        payload: Data
    ) throws -> [Data] {
        let parts = fragments(of: payload)
        let count = UInt16(parts.count)
        var datagrams: [Data] = []
        datagrams.reserveCapacity(parts.count)
        for part in parts {
            let header = MediaHeader(
                stream: stream,
                flags: isKeyframe ? OCP.mediaFlagKeyframe : 0,
                sessionTag: sessionTag,
                frameID: frameID,
                fragmentIndex: part.index,
                fragmentCount: count,
                timestamp: timestamp
            )
            datagrams.append(try seal(encKey: encKey, macKey: macKey, header: header, plaintext: part.chunk))
        }
        return datagrams
    }
}
