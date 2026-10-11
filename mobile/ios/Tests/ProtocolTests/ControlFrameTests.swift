import CryptoKit
import Foundation
import Foundation
import XCTest

@testable import PhoneCamera

/// 27-byte control frame header codec (PROTOCOL.md §3).
final class ControlFrameTests: XCTestCase {
    private func plaintextFrame(_ payload: String) -> ControlFrame {
        ControlFrame(
            flags: 0,
            tag: Data(repeating: 0, count: OCP.controlTagSize),
            payload: Data(payload.utf8)
        )
    }

    func testEncodeProducesExactHeaderLayout() throws {
        let payload = Data(#"{"type":"hello","proto":"ocp/1"}"#.utf8)
        let frame = ControlFrame(
            flags: 0,
            tag: Data(repeating: 0, count: OCP.controlTagSize),
            payload: payload
        )
        let encoded = try ControlFrame.encode(frame)

        var expected = Data([0x4F, 0x43, 0x50, 0x31]) // magic "OCP1"
        var lengthBE = UInt32(payload.count).bigEndian
        withUnsafeBytes(of: &lengthBE) { expected.append(contentsOf: $0) }
        expected.append(contentsOf: [0x00, 0x00, 0x00]) // flags + reserved
        expected.append(Data(repeating: 0, count: OCP.controlTagSize))
        expected.append(payload)

        XCTAssertEqual(encoded, expected)
        XCTAssertEqual(encoded.count, OCP.controlHeaderSize + payload.count)
        XCTAssertEqual(Hex.encode(encoded.prefix(4)), "4f435031")
    }

    func testPlainTextRoundTrip() throws {
        let payload = #"{"type":"hello","proto":"ocp/1"}"#
        var buffer = try ControlFrame.encode(plaintextFrame(payload))
        let decoded = try ControlFrame.decode(from: &buffer)
        XCTAssertEqual(decoded.flags, 0)
        XCTAssertEqual(decoded.tag, Data(repeating: 0, count: OCP.controlTagSize))
        XCTAssertEqual(String(data: decoded.payload, encoding: .utf8), payload)
        XCTAssertTrue(buffer.isEmpty)
    }

    func testDecodeConsumesConcatenatedFrames() throws {
        var buffer = Data()
        buffer.append(try ControlFrame.encode(plaintextFrame(#"{"a":1}"#)))
        buffer.append(try ControlFrame.encode(plaintextFrame(#"{"b":2}"#)))
        XCTAssertEqual(buffer.count, 2 * (OCP.controlHeaderSize + 7))

        let first = try ControlFrame.decode(from: &buffer)
        XCTAssertEqual(String(data: first.payload, encoding: .utf8), #"{"a":1}"#)
        XCTAssertEqual(buffer.count, OCP.controlHeaderSize + 7)

        let second = try ControlFrame.decode(from: &buffer)
        XCTAssertEqual(String(data: second.payload, encoding: .utf8), #"{"b":2}"#)
        XCTAssertTrue(buffer.isEmpty)
    }

    func testStreamDecoderHandlesByteByByteFeed() throws {
        // Mirrors tests/test_control_frame.cpp:control_frame_incremental_feed.
        var stream = Data()
        stream.append(try ControlFrame.encode(plaintextFrame(#"{"a":1}"#)))
        stream.append(try ControlFrame.encode(plaintextFrame(#"{"b":2}"#)))

        var decoder = ControlFrameStreamDecoder()
        var payloads: [Data] = []
        for byte in stream {
            decoder.append(Data([byte]))
            if let frame = try decoder.nextFrame() {
                payloads.append(frame.payload)
            }
        }

        XCTAssertEqual(payloads.count, 2)
        XCTAssertEqual(String(data: payloads[0], encoding: .utf8), #"{"a":1}"#)
        XCTAssertEqual(String(data: payloads[1], encoding: .utf8), #"{"b":2}"#)
    }

    func testStreamDecoderWaitsForCompleteFrame() throws {
        var decoder = ControlFrameStreamDecoder()
        let frame = try ControlFrame.encode(plaintextFrame(#"{"type":"ping","t":1}"#))
        decoder.append(Data(frame.prefix(frame.count - 1)))
        XCTAssertNil(try decoder.nextFrame())
        decoder.append(Data(frame.suffix(1)))
        let decoded = try XCTUnwrap(decoder.nextFrame())
        XCTAssertEqual(String(data: decoded.payload, encoding: .utf8), #"{"type":"ping","t":1}"#)
    }

    func testDecodeRejectsBadMagic() {
        var junk = Data(repeating: 0x41, count: 64)
        XCTAssertThrowsError(try ControlFrame.decode(from: &junk)) { error in
            XCTAssertEqual(error as? OCPError, .badMagic)
        }
    }

    func testDecodeRejectsShortBuffer() {
        var short = Data(repeating: 0x41, count: OCP.controlHeaderSize - 1)
        XCTAssertThrowsError(try ControlFrame.decode(from: &short)) { error in
            XCTAssertEqual(error as? OCPError, .truncated)
        }
    }

    func testDecodeRejectsMissingPayloadBytes() throws {
        var partial = try ControlFrame.encode(plaintextFrame(#"{"type":"ready"}"#))
        partial = Data(partial.prefix(partial.count - 3))
        XCTAssertThrowsError(try ControlFrame.decode(from: &partial)) { error in
            XCTAssertEqual(error as? OCPError, .truncated)
        }
    }

    func testDecodeRejectsZeroLength() {
        // PROTOCOL.md §3: payload length N is 1..65536.
        var frame = Data([0x4F, 0x43, 0x50, 0x31, 0x00, 0x00, 0x00, 0x00])
        frame.append(Data(repeating: 0, count: 19)) // complete 27-byte header
        XCTAssertThrowsError(try ControlFrame.decode(from: &frame)) { error in
            XCTAssertEqual(error as? OCPError, .badFrameLength(0))
        }
    }

    func testDecodeRejectsOversizedLength() {
        var frame = Data([0x4F, 0x43, 0x50, 0x31, 0x00, 0x01, 0x00, 0x01]) // 65537
        frame.append(Data(repeating: 0, count: 19))
        XCTAssertThrowsError(try ControlFrame.decode(from: &frame)) { error in
            XCTAssertEqual(error as? OCPError, .badFrameLength(65537))
        }
    }

    func testEncodeRejectsEmptyPayload() {
        XCTAssertThrowsError(
            try ControlFrame.encode(ControlFrame(flags: 0, tag: Data(repeating: 0, count: 16), payload: Data()))
        ) { error in
            XCTAssertEqual(error as? OCPError, .badFrameLength(0))
        }
    }

    func testEncodeRejectsOversizedPayload() {
        let payload = Data(repeating: 0x58, count: OCP.controlMaxPayload + 1)
        XCTAssertThrowsError(
            try ControlFrame.encode(ControlFrame(flags: 0, tag: Data(repeating: 0, count: 16), payload: payload))
        ) { error in
            XCTAssertEqual(error as? OCPError, .badFrameLength(OCP.controlMaxPayload + 1))
        }
    }

    func testMaxPayloadRoundTrip() throws {
        let payload = patternedBytes(count: OCP.controlMaxPayload, seed: 1)
        let encoded = try ControlFrame.encode(ControlFrame(flags: 0, tag: Data(repeating: 0, count: 16), payload: payload))
        var buffer = encoded
        let decoded = try ControlFrame.decode(from: &buffer)
        XCTAssertEqual(decoded.payload, payload)
        XCTAssertTrue(buffer.isEmpty)
    }

    func testEncodeRejectsWrongTagSize() {
        XCTAssertThrowsError(
            try ControlFrame.encode(
                ControlFrame(flags: 0, tag: Data(repeating: 0, count: 15), payload: Data("{}".utf8))
            )
        ) { error in
            XCTAssertEqual(error as? OCPError, .protocolViolation("tag size"))
        }
    }
}

/// ChaCha20 + HMAC-SHA256/128 encrypt-then-MAC for control frames (§3.1).
final class ControlCipherTests: XCTestCase {
    private func makePair(seed: Int = 0) -> (sender: ControlCipher, receiver: ControlCipher, keys: (enc: Data, mac: Data)) {
        let keys = controlTestKeys(seed: seed)
        return (
            ControlCipher(encKey: keys.enc, macKey: keys.mac),
            ControlCipher(encKey: keys.enc, macKey: keys.mac),
            keys
        )
    }

    func testSealMatchesReferenceFrameBytes() throws {
        // Golden vector generated from the RFC 8439 ChaCha20 construction and
        // HMAC-SHA256/128 exactly as PROTOCOL.md §3.1 defines it, using the
        // key schedule of tests/test_control_frame.cpp (makeKeys, seed 0).
        let keys = controlTestKeys(seed: 0)
        let cipher = ControlCipher(encKey: keys.enc, macKey: keys.mac)

        let first = try cipher.seal(payload: Data(#"{"type":"ready"}"#.utf8))
        XCTAssertTrue(first.isEncrypted)
        XCTAssertEqual(first.flags, 0x01)
        XCTAssertEqual(first.tag.count, OCP.controlTagSize)
        XCTAssertEqual(
            Hex.encode(try ControlFrame.encode(first)),
            "4f4350310000001001000055f301a0ad98d0333d4436991daa843ead5bb7cb4750fc96b2eab6f8cabbb8e8"
        )

        // Second frame uses frame counter 1 (per-direction, starts at 0).
        let second = try cipher.seal(payload: Data(#"{"type":"ping","t":1}"#.utf8))
        XCTAssertEqual(
            Hex.encode(try ControlFrame.encode(second)),
            "4f43503100000015010000950ac1cd0bb291762833b39d5e7b9bb76b33f680962a75784d5133dede63bcbb5d24c8f30d"
        )
    }

    func testNonceLayout() {
        // §3.1: 12-byte nonce = u64 counter little-endian ++ 0x00 * 4.
        XCTAssertEqual(Hex.encode(ControlCipher.nonce(for: 0)), String(repeating: "0", count: 24))
        XCTAssertEqual(
            Hex.encode(ControlCipher.nonce(for: 0x0102030405060708)),
            "080706050403020100000000"
        )
        XCTAssertEqual(ControlCipher.nonce(for: 42).count, 12)
    }

    func testSealedTagCoversZeroedHeaderAndCiphertext() throws {
        let (sender, _, keys) = makePair()
        let frame = try sender.seal(payload: Data(#"{"type":"hint"}"#.utf8))

        var header = Data([0x4F, 0x43, 0x50, 0x31])
        var lengthBE = UInt32(frame.payload.count).bigEndian
        withUnsafeBytes(of: &lengthBE) { header.append(contentsOf: $0) }
        header.append(contentsOf: [0x01, 0x00, 0x00])
        header.append(Data(repeating: 0, count: OCP.controlTagSize))

        let expectedTag = Data(
            HMAC<SHA256>.authenticationCode(
                for: header + frame.payload,
                using: SymmetricKey(data: keys.mac)
            ).prefix(16)
        )
        XCTAssertEqual(frame.tag, expectedTag)
    }

    func testEncryptedRoundTripWithSequentialCounters() throws {
        let (sender, receiver, _) = makePair()
        let messages = [
            #"{"type":"ready"}"#,
            #"{"type":"config"}"#,
            String(repeating: "x", count: 1000),
            #"{"type":"ping","t":1}"#,
        ]

        for message in messages {
            let frame = try sender.seal(payload: Data(message.utf8))
            let opened = try receiver.open(frame)
            XCTAssertEqual(String(data: opened, encoding: .utf8), message)
        }
    }

    func testFreshCounterProducesFreshCiphertextForSamePayload() throws {
        let (sender, receiver, _) = makePair()
        let payload = Data(#"{"type":"ping","t":7}"#.utf8)
        let first = try sender.seal(payload: payload)
        let second = try sender.seal(payload: payload)
        XCTAssertNotEqual(first.payload, second.payload)
        XCTAssertNotEqual(first.tag, second.tag)
        XCTAssertEqual(try receiver.open(first), payload)
        XCTAssertEqual(try receiver.open(second), payload)
    }

    func testTamperedCiphertextRejected() throws {
        let (sender, receiver, _) = makePair()
        let frame = try sender.seal(payload: Data(#"{"type":"auth_ok"}"#.utf8))
        var tampered = frame
        var payload = Data(frame.payload)
        payload[payload.startIndex] ^= 0x40
        tampered.payload = payload
        XCTAssertThrowsError(try receiver.open(tampered)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testTamperedTagRejected() throws {
        let (sender, receiver, _) = makePair()
        let frame = try sender.seal(payload: Data(#"{"type":"auth_ok"}"#.utf8))
        var tampered = frame
        var tag = Data(frame.tag)
        tag[tag.startIndex] ^= 0x01
        tampered.tag = tag
        XCTAssertThrowsError(try receiver.open(tampered)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testTamperedFlagsRejected() throws {
        // The flags byte is part of the authenticated header (§3.1).
        let (sender, receiver, _) = makePair()
        let frame = try sender.seal(payload: Data(#"{"type":"ready"}"#.utf8))
        var tampered = frame
        tampered.flags = 0x03
        XCTAssertThrowsError(try receiver.open(tampered)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testWrongKeyRejected() throws {
        let senderKeys = controlTestKeys(seed: 0)
        let receiverKeys = controlTestKeys(seed: 1)
        let sender = ControlCipher(encKey: senderKeys.enc, macKey: senderKeys.mac)
        let receiver = ControlCipher(encKey: receiverKeys.enc, macKey: receiverKeys.mac)
        let frame = try sender.seal(payload: Data(#"{"secret":true}"#.utf8))
        XCTAssertThrowsError(try receiver.open(frame)) { error in
            XCTAssertEqual(error as? OCPError, .tagMismatch)
        }
    }

    func testPlaintextFrameRejectedByOpen() {
        let (_, receiver, _) = makePair()
        let plaintext = ControlFrame(
            flags: 0,
            tag: Data(repeating: 0, count: OCP.controlTagSize),
            payload: Data(#"{"type":"ready"}"#.utf8)
        )
        XCTAssertThrowsError(try receiver.open(plaintext)) { error in
            XCTAssertEqual(error as? OCPError, .protocolViolation("frame not encrypted"))
        }
    }

    func testReplayIsRejectedDownstreamByJsonValidation() throws {
        // PROTOCOL.md §3.1 requires receivers to reject counters that are not
        // strictly increasing. ControlCipher tracks the expected counter but a
        // replayed frame still authenticates (the tag does not cover the
        // counter), so it decrypts under the next counter to garbage that the
        // JSON layer rejects. See AppModel.handle(frame:).
        let (sender, receiver, _) = makePair()
        let payload = Data(#"{"type":"hint","loss_pct":0.0}"#.utf8)
        let frame = try sender.seal(payload: payload)
        XCTAssertEqual(try receiver.open(frame), payload)
        let replayed = try receiver.open(frame)
        XCTAssertNotEqual(replayed, payload)
        XCTAssertNil(try? JSONSerialization.jsonObject(with: replayed))
    }
}
