import Foundation
import XCTest

@testable import PhoneCamera

/// Protocol constants from PROTOCOL.md §§1–4, 6.
final class OCPConstantsTests: XCTestCase {
    func testControlAndMediaConstants() {
        XCTAssertEqual(OCP.controlMagic, Data([0x4F, 0x43, 0x50, 0x31])) // "OCP1"
        XCTAssertEqual(OCP.mediaMagic, Data([0x4F, 0x43, 0x50, 0x4D])) // "OCPM"
        XCTAssertEqual(OCP.protocolVersion, 1)
        XCTAssertEqual(OCP.protoIdentifier, "ocp/1")

        XCTAssertEqual(OCP.controlHeaderSize, 27)
        XCTAssertEqual(OCP.controlTagSize, 16)
        XCTAssertEqual(OCP.controlMaxPayload, 65536)

        XCTAssertEqual(OCP.mediaHeaderSize, 32)
        XCTAssertEqual(OCP.mediaTagSize, 16)
        XCTAssertEqual(OCP.mediaMaxDatagram, 1200)
        XCTAssertEqual(OCP.mediaMaxCiphertext, 1152)

        XCTAssertEqual(OCP.defaultControlPort, 47100)
        XCTAssertEqual(OCP.defaultMediaPort, 47101)
        XCTAssertEqual(OCP.discoveryPort, 47109)
        XCTAssertEqual(OCP.discoveryMulticastGroup, "239.255.74.79")

        XCTAssertEqual(OCP.streamVideo, 0)
        XCTAssertEqual(OCP.streamAudio, 1)
        XCTAssertEqual(OCP.mediaFlagKeyframe, 0x01)

        XCTAssertEqual(OCP.kdfHKDF, "hkdf-sha256")
        XCTAssertEqual(OCP.kdfPBKDF2, "pbkdf2-sha256")
        XCTAssertEqual(OCP.pbkdf2MinIterations, 100_000)
    }
}

/// ChaCha20 against RFC 8439 test vectors (same vectors as
/// tests/test_crypto.cpp:chacha20_rfc8439_section2_4_2).
final class ChaCha20Tests: XCTestCase {
    func testRFC8439BlockVector() {
        let key = ocpHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
        let nonce = ocpHex("000000090000004a00000000")
        let words = ChaCha20.block(key: key, counter: 1, nonce: nonce)
        XCTAssertEqual(words.count, 16)
        var serialized = Data(capacity: 64)
        for word in words {
            var le = word.littleEndian
            withUnsafeBytes(of: &le) { serialized.append(contentsOf: $0) }
        }
        XCTAssertEqual(
            Hex.encode(serialized),
            "10f1e7e4d13b5915500fdd1fa32071c4c7d1f4c733c068030422aa9ac3d46c4e" +
                "ed2826446079faa0914c2d705d98b02a2b5129cd1de164eb9cbd083e8a2503c4e"
        )
    }

    func testRFC8439EncryptionVector() {
        let key = ocpHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
        let nonce = ocpHex("000000000000004a00000000")
        let plaintext = Data(
            "Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."
                .utf8
        )
        let expected =
            "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b" +
            "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d80" +
            "7ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365a" +
            "f90bbf74a35be6b40b8eedf2785e42874d"

        let ciphertext = ChaCha20.crypt(key: key, nonce: nonce, counter: 1, input: plaintext)
        XCTAssertEqual(Hex.encode(ciphertext), expected)

        // Decrypting with the same parameters recovers the plaintext.
        let roundTrip = ChaCha20.crypt(key: key, nonce: nonce, counter: 1, input: ciphertext)
        XCTAssertEqual(roundTrip, plaintext)
    }

    func testCryptIsInvolutionPerKeystream() {
        let key = patternedBytes(count: 32, seed: 3)
        let nonce = patternedBytes(count: 12, seed: 7)
        let input = patternedBytes(count: 300, seed: 11)
        let once = ChaCha20.crypt(key: key, nonce: nonce, counter: 0, input: input)
        let twice = ChaCha20.crypt(key: key, nonce: nonce, counter: 0, input: once)
        XCTAssertEqual(twice, input)
        XCTAssertNotEqual(once, input)
    }
}

/// HKDF-SHA256 (RFC 5869) — same vectors as tests/test_crypto.cpp.
final class HKDFTests: XCTestCase {
    func testRFC5869Case1() {
        let ikm = ocpHex(String(repeating: "0b", count: 22))
        let salt = ocpHex("000102030405060708090a0b0c")
        let info = ocpHex("f0f1f2f3f4f5f6f7f8f9")
        let okm = HKDF.expand(prk: HKDF.extract(salt: salt, ikm: ikm), info: info, length: 42)
        XCTAssertEqual(
            Hex.encode(okm),
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"
        )
    }

    func testRFC5869Case3EmptySaltAndInfo() {
        let ikm = ocpHex(String(repeating: "0b", count: 22))
        let prk = HKDF.extract(salt: Data(), ikm: ikm)
        let okm = HKDF.expand(prk: prk, info: Data(), length: 42)
        XCTAssertEqual(
            Hex.encode(okm),
            "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8"
        )
    }

    func testDeriveSixtyFourBytes() {
        // Same inputs as the OCP/1 key schedule with info "ocp1-ctrl"
        // (see KeyScheduleTests); exercises the multi-block expand path.
        let master = ocpHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
        let salt = ocpHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
        let okm = HKDF.derive(ikm: master, salt: salt, info: "ocp1-ctrl", length: 64)
        XCTAssertEqual(
            Hex.encode(okm),
            "7a6741eb6b85c4ec4c73ad2cf574127646e7a5d1706a3d6f0a379d504d651178" +
                "b6ba5374daee61b8f204c681f33a1570f0a9718abeee1d7120dbf5f64f0e2b3e"
        )
        XCTAssertNotEqual(okm.prefix(32), okm.suffix(32))
    }
}

/// PBKDF2-HMAC-SHA256 — vectors from tests/test_crypto.cpp plus the
/// pairing-code derivation described in PROTOCOL.md §6.2.
final class PBKDF2Tests: XCTestCase {
    private func deriveHex(_ password: String, _ salt: String, iterations: Int, keyLength: Int) -> String {
        Hex.encode(
            PBKDF2.derive(
                password: Data(password.utf8),
                salt: Data(salt.utf8),
                iterations: iterations,
                keyLength: keyLength
            )
        )
    }

    func testReferenceVectors() {
        XCTAssertEqual(
            deriveHex("password", "salt", iterations: 1, keyLength: 32),
            "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b"
        )
        XCTAssertEqual(
            deriveHex("password", "salt", iterations: 2, keyLength: 32),
            "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43"
        )
        XCTAssertEqual(
            deriveHex("password", "salt", iterations: 4096, keyLength: 32),
            "c5e478d59288c841aa530db6845c4c8d962893a001ce4e11a4963873aa98134a"
        )
        XCTAssertEqual(
            deriveHex(
                "passwordPASSWORDpassword",
                "saltSALTsaltSALTsaltSALTsaltSALTsalt",
                iterations: 4096,
                keyLength: 40
            ),
            "348c89dbcbd32b2f32d814b8116e84cf2b17347ebc1800181c4e2a1fb8dd53e1c635518c7dac47e9"
        )
        XCTAssertEqual(
            deriveHex("password", "NaCl1234", iterations: 1000, keyLength: 32),
            "986c856b216297f7c696d0a8e67e8c1ead27fab3e5e9557d15e3a87970aa2695"
        )
    }

    func testPairingCodeDerivation() {
        // PROTOCOL.md §6.2: 6-digit code, hello salt (16 bytes), c >= 100000.
        let salt = Data(repeating: 0x5A, count: 16)
        let master = PBKDF2.derive(
            password: Data("123456".utf8),
            salt: salt,
            iterations: 100_000,
            keyLength: 32
        )
        XCTAssertEqual(
            Hex.encode(master),
            "1bed33c604d449e58eb7189a8f0f15b689dc719a2964fc871f15c92d549a1736"
        )
    }

    func testPairingCodeSensitivity() {
        // Mirrors tests/test_pairing.cpp:pairing_code_derivation (cheap
        // iteration count; only the input sensitivity matters here).
        let saltA = Data(repeating: 0x5A, count: 16)
        let saltB = Data(repeating: 0x5B, count: 16)
        func master(_ password: String, _ salt: Data, _ iterations: Int) -> Data {
            PBKDF2.derive(
                password: Data(password.utf8),
                salt: salt,
                iterations: iterations,
                keyLength: 32
            )
        }
        let a = master("123456", saltA, 1_000)
        XCTAssertEqual(a, master("123456", saltA, 1_000))
        XCTAssertNotEqual(a, master("654321", saltA, 1_000))
        XCTAssertNotEqual(a, master("123456", saltA, 1_001))
        XCTAssertNotEqual(a, master("123456", saltB, 1_000))
    }
}

/// Hex codec (same expectations as tests/test_crypto.cpp:random_and_hex).
final class HexTests: XCTestCase {
    func testEncode() {
        XCTAssertEqual(Hex.encode(Data([0x00, 0x0F, 0xFF, 0xA5])), "000fffa5")
        XCTAssertEqual(Hex.encode(Data()), "")
    }

    func testDecodeAcceptsBothCases() {
        XCTAssertEqual(Hex.decode("000fffa5"), Data([0x00, 0x0F, 0xFF, 0xA5]))
        XCTAssertEqual(Hex.decode("000FFFA5"), Data([0x00, 0x0F, 0xFF, 0xA5]))
    }

    func testDecodeRejectsInvalidInput() {
        XCTAssertNil(Hex.decode(""))
        XCTAssertNil(Hex.decode("abc")) // odd length
        XCTAssertNil(Hex.decode("zz")) // non-hex
        XCTAssertNil(Hex.decode("0a 1b")) // embedded space
    }

    func testRoundTrip() {
        let bytes = patternedBytes(count: 32, seed: 5)
        XCTAssertEqual(Hex.decode(Hex.encode(bytes)), bytes)
    }
}
