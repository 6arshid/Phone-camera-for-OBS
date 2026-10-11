import CryptoKit
import Foundation
import Foundation
import XCTest

@testable import PhoneCamera

/// 32-byte media packet header codec (PROTOCOL.md §4).
final class MediaHeaderTests: XCTestCase {
    private func sampleHeader() -> MediaHeader {
        MediaHeader(
            stream: OCP.streamVideo,
            flags: OCP.mediaFlagKeyframe,
            sessionTag: ocpHex("1122334455667788"),
            frameID: 42,
            fragmentIndex: 0,
            fragmentCount: 1,
            timestamp: 123_456_789
        )
    }

    func testEncodeProducesExactHeaderLayout() {
        let encoded = sampleHeader().encode()
        XCTAssertEqual(encoded.count, OCP.mediaHeaderSize)
        XCTAssertEqual(
            Hex.encode(encoded),
            "4f43504d" + // magic "OCPM"
                "01" + // version
                "00" + // stream = video
                "01" + // flags = keyframe
                "00" + // reserved
                "1122334455667788" + // session tag (first 8 bytes)
                "0000002a" + // frame id (big endian)
                "0000" + // fragment index
                "0001" + // fragment count
                "00000000075bcd15" // timestamp (big endian)
        )
    }

    func testDecodeRoundTrip() throws {
        let header = sampleHeader()
        let decoded = try MediaHeader.decode(header.encode())
        XCTAssertEqual(decoded, header)
        XCTAssertTrue(decoded.isKeyframe)
    }

    func testDecodeRejectsShortBuffer() {
        XCTAssertThrowsError(try MediaHeader.decode(Data(repeating: 0, count: 31))) { error in
            XCTAssertEqual(error as? OCPError, .truncated)
        }
    }

    func testDecodeRejectsBadMagic() {
        var data = sampleHeader().encode()
        data[0] = 0x58 // 'X'
        XCTAssertThrowsError(try MediaHeader.decode(data)) { error in
            XCTAssertEqual(error as? OCPError, .badMagic)
        }
    }

    func testDecodeRejectsBadVersion() {
        var data = sampleHeader().encode()
        data[4] = 2
        XCTAssertThrowsError(try MediaHeader.decode(data)) { error in
            XCTAssertEqual(error as? OCPError, .badVersion)
        }
    }

    func testDecodeRejectsZeroFragmentCount() {
        var header = sampleHeader()
        header.fragmentCount = 0
        XCTAssertThrowsError(try MediaHeader.decode(header.encode())) { error in
            XCTAssertEqual(error as? OCPError, .protocolViolation("fragment count"))
        }
    }

    func testDecodeRejectsFragmentIndexAtOrPastCount() {
        var header = sampleHeader()
        header.fragmentIndex = 1
        header.fragmentCount = 1
        XCTAssertThrowsError(try MediaHeader.decode(header.encode())) { error in
            XCTAssertEqual(error as? OCPError, .protocolViolation("fragment index"))
        }
    }
}

/// Media channel seal/open and fragmentation (PROTOCOL.md §4).
final class MediaPacketTests: XCTestCase {
    private func sealSampleDatagram() throws -> Data {
        // Mirrors tests/test_media.cpp:media_packet_round_trip.
        let keys = mediaTestKeys()
        let header = MediaHeader(
            stream: OCP.streamVideo,
            flags: OCP.mediaFlagKeyframe,
            sessionTag: ocpHex("1122334455667788"),
            frameID: 42,
            fragmentIndex: 0,
            fragmentCount: 1,
            timestamp: 123_456_789
        )
        return try MediaPacket.seal(
            encKey: keys.enc,
            macKey: keys.mac,
            header: header,
            plaintext: patternedBytes(count: 100, seed: 9)
        )
    }

    func testNonceLayout() {
        // §4: nonce = session_tag[0..4) || u8 stream || u32 frame_id ||
        //            u16 frag_idx || 0x00   (frame id / index big endian).
        let nonce = MediaPacket.nonce(
            sessionTag: ocpHex("1122334455667788"),
            stream: OCP.streamVideo,
            frameID: 0x01020304,
            fragmentIndex: 0x0A0B
        )
        XCTAssertEqual(nonce.count, 12)
        XCTAssertEqual(Hex.encode(nonce), "1122334400010203040a0b00")
    }

    func testSealMatchesReferenceDatagramBytes() throws {
        // Golden vector: ChaCha20 (RFC 8439) + HMAC-SHA256/128 exactly as
        // PROTOCOL.md §4 defines them, with the keys and header of
        // tests/test_media.cpp:media_packet_round_trip.
        let datagram = try sealSampleDatagram()
        XCTAssertEqual(datagram.count, OCP.mediaHeaderSize + 100 + OCP.mediaTagSize)
        XCTAssertLessThanOrEqual(datagram.count, OCP.mediaMaxDatagram)
        XCTAssertEqual(
            Hex.encode(datagram),
            "4f43504d0100010011223344556677880000002a0000000100000000075bcd15" +
                "cdf2d0a7afc062ed4eb75b7bd332e8a2fa87c60195e443d685ed610f971e8524" +
                "acc211301e2f1a157ca15558d73ec8e74ce47562d8681b695dbdfddb1d482120" +
                "4be52be63cc037a31f9a1223353c5b97814a77c259661c4b19f620f530957dfe" +
                "eb7fa55c7a29e6da6fdf16f284bed844ab3a8bc8"
        )
    }

    func testOpenRoundTrip() throws {
        let keys = mediaTestKeys()
        let datagram = try sealSampleDatagram()
        let (header, plaintext) = try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: datagram)
        XCTAssertEqual(header.stream, OCP.streamVideo)
        XCTAssertEqual(header.flags, OCP.mediaFlagKeyframe)
        XCTAssertEqual(header.sessionTag, ocpHex("1122334455667788"))
        XCTAssertEqual(header.frameID, 42)
        XCTAssertEqual(header.fragmentIndex, 0)
        XCTAssertEqual(header.fragmentCount, 1)
        XCTAssertEqual(header.timestamp, 123_456_789)
        XCTAssertEqual(plaintext, patternedBytes(count: 100, seed: 9))
    }

    func testTagCoversHeaderAndCiphertext() throws {
        let keys = mediaTestKeys()
        let datagram = try sealSampleDatagram()
        let ciphertextEnd = datagram.count - OCP.mediaTagSize
        let authenticated = datagram.prefix(OCP.mediaHeaderSize) + datagram.subdata(in: OCP.mediaHeaderSize..<ciphertextEnd)
        let expectedTag = Data(
            HMAC<SHA256>.authenticationCode(
                for: authenticated,
                using: SymmetricKey(data: keys.mac)
            ).prefix(16)
        )
        XCTAssertEqual(datagram.subdata(in: ciphertextEnd..<datagram.count), expectedTag)
    }

    func testBitFlippedCiphertextRejected() throws {
        let keys = mediaTestKeys()
        var datagram = try sealSampleDatagram()
        datagram[OCP.mediaHeaderSize + 5] ^= 0x01
        XCTAssertThrowsError(try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: datagram)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testWrongKeyRejected() throws {
        let datagram = try sealSampleDatagram()
        let wrongEnc = Data(repeating: 0x11, count: 32)
        let wrongMac = Data(repeating: 0x22, count: 32)
        XCTAssertThrowsError(try MediaPacket.open(encKey: wrongEnc, macKey: wrongMac, datagram: datagram)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testOversizedPlaintextRejected() {
        let keys = mediaTestKeys()
        let header = MediaHeader(
            stream: OCP.streamVideo,
            flags: 0,
            sessionTag: ocpHex("1122334455667788"),
            frameID: 1,
            fragmentIndex: 0,
            fragmentCount: 1,
            timestamp: 0
        )
        XCTAssertThrowsError(
            try MediaPacket.seal(
                encKey: keys.enc,
                macKey: keys.mac,
                header: header,
                plaintext: Data(repeating: 0x5A, count: OCP.mediaMaxCiphertext + 1)
            )
        ) { error in
            XCTAssertEqual(error as? OCPError, .oversizedDatagram)
        }
    }

    func testOpenRejectsOversizedAndTruncatedDatagrams() {
        let keys = mediaTestKeys()
        XCTAssertThrowsError(
            try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: Data(repeating: 0, count: OCP.mediaMaxDatagram + 1))
        ) { error in
            XCTAssertEqual(error as? OCPError, .oversizedDatagram)
        }
        XCTAssertThrowsError(
            try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: Data(repeating: 0, count: 40))
        ) { error in
            XCTAssertEqual(error as? OCPError, .truncated)
        }
    }

    // MARK: - Fragmentation

    func testFragmentBoundaries() {
        // §4: ciphertext ≤ 1152 bytes ⇒ ceil(len / 1152) fragments.
        let empty = MediaPacket.fragments(of: Data())
        XCTAssertEqual(empty.count, 1)
        XCTAssertEqual(empty[0].index, 0)
        XCTAssertTrue(empty[0].chunk.isEmpty)

        let exact = MediaPacket.fragments(of: Data(repeating: 0x11, count: OCP.mediaMaxCiphertext))
        XCTAssertEqual(exact.count, 1)
        XCTAssertEqual(exact[0].chunk.count, OCP.mediaMaxCiphertext)

        let overByOne = MediaPacket.fragments(of: Data(repeating: 0x11, count: OCP.mediaMaxCiphertext + 1))
        XCTAssertEqual(overByOne.count, 2)
        XCTAssertEqual(overByOne[0].chunk.count, OCP.mediaMaxCiphertext)
        XCTAssertEqual(overByOne[1].chunk.count, 1)
        XCTAssertEqual(overByOne[0].index, 0)
        XCTAssertEqual(overByOne[1].index, 1)
    }

    func testFragmentLargeFrame() {
        let payload = patternedBytes(count: 40_000, seed: 11)
        let parts = MediaPacket.fragments(of: payload)
        XCTAssertEqual(parts.count, 35) // ceil(40000 / 1152)
        var reassembled = Data()
        for (expectedIndex, part) in parts.enumerated() {
            XCTAssertEqual(Int(part.index), expectedIndex)
            XCTAssertLessThanOrEqual(part.chunk.count, OCP.mediaMaxCiphertext)
            reassembled.append(part.chunk)
        }
        XCTAssertEqual(reassembled, payload)
        XCTAssertEqual(parts.last?.chunk.count, 40_000 - 34 * OCP.mediaMaxCiphertext)
    }

    func testSealFrameAndReassembleOutOfOrder() throws {
        // Mirrors tests/test_media.cpp:fragment_and_reassemble_large_frame.
        let keys = mediaTestKeys()
        let payload = patternedBytes(count: 40_000, seed: 11)
        let datagrams = try MediaPacket.sealFrame(
            encKey: keys.enc,
            macKey: keys.mac,
            sessionTag: ocpHex("aabbccdd00000000"),
            stream: OCP.streamVideo,
            isKeyframe: true,
            frameID: 5,
            timestamp: 999,
            payload: payload
        )
        XCTAssertEqual(datagrams.count, 35)
        for datagram in datagrams {
            XCTAssertLessThanOrEqual(datagram.count, OCP.mediaMaxDatagram)
        }

        // Open in reverse order (network reordering), then reassemble by index.
        var fragments: [(index: UInt16, chunk: Data)] = []
        for datagram in datagrams.reversed() {
            let (header, chunk) = try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: datagram)
            XCTAssertEqual(header.frameID, 5)
            XCTAssertEqual(header.stream, OCP.streamVideo)
            XCTAssertEqual(header.fragmentCount, 35)
            XCTAssertEqual(header.timestamp, 999)
            XCTAssertTrue(header.isKeyframe)
            fragments.append((header.fragmentIndex, chunk))
        }

        fragments.sort { $0.index < $1.index }
        XCTAssertEqual(fragments.map { $0.index }, (0..<35).map { UInt16($0) })
        var reassembled = Data()
        for fragment in fragments {
            reassembled.append(fragment.chunk)
        }
        XCTAssertEqual(reassembled, payload)
    }

    func testSinglePacketAudioFrame() throws {
        // §4.2: 10 ms of mono s16le @48 kHz = 960 bytes ⇒ exactly one packet.
        let keys = mediaTestKeys()
        let payload = patternedBytes(count: 960, seed: 4)
        let datagrams = try MediaPacket.sealFrame(
            encKey: keys.enc,
            macKey: keys.mac,
            sessionTag: ocpHex("0102030405060708"),
            stream: OCP.streamAudio,
            isKeyframe: false,
            frameID: 1,
            timestamp: 10_000_000,
            payload: payload
        )
        XCTAssertEqual(datagrams.count, 1)
        let (header, plaintext) = try MediaPacket.open(encKey: keys.enc, macKey: keys.mac, datagram: datagrams[0])
        XCTAssertEqual(header.stream, OCP.streamAudio)
        XCTAssertEqual(header.fragmentCount, 1)
        XCTAssertEqual(header.fragmentIndex, 0)
        XCTAssertFalse(header.isKeyframe)
        XCTAssertEqual(plaintext, payload)
    }
}
