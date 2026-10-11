import Foundation
import XCTest

@testable import PhoneCamera

/// OCP/1 key schedule (PROTOCOL.md §3.2):
/// transcript = SHA256(hello || challenge)
/// salt_kdf   = client_nonce || server_nonce
/// k_auth/k_ctrl/k_media via HKDF-SHA256 with info "ocp1-auth"/"ocp1-ctrl"/"ocp1-media".
///
/// The golden values were computed with an independent HKDF/HMAC-SHA256
/// implementation using the same inputs as the C++ reference tests
/// (tests/test_pairing.cpp).
final class KeyScheduleTests: XCTestCase {
    // Fixed inputs shared by the golden vectors below.
    private let master = ocpHex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
    private let clientNonce = ocpHex("000102030405060708090a0b0c0d0e0f")
    private let serverNonce = ocpHex("101112131415161718191a1b1c1d1e1f")
    private let helloPayload = Data(#"{"type":"hello","proto":"ocp/1"}"#.utf8)
    private let challengePayload = Data(
        #"{"type":"challenge","nonce":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"}"#.utf8
    )

    private func makeSchedule(
        master: Data? = nil,
        clientNonce: Data? = nil,
        serverNonce: Data? = nil,
        hello: Data? = nil,
        challenge: Data? = nil
    ) -> KeySchedule {
        KeySchedule(
            master: master ?? self.master,
            clientNonce: clientNonce ?? self.clientNonce,
            serverNonce: serverNonce ?? self.serverNonce,
            helloPayload: hello ?? helloPayload,
            challengePayload: challenge ?? challengePayload
        )
    }

    func testTranscriptIsSha256OfHelloThenChallenge() {
        let schedule = makeSchedule()
        XCTAssertEqual(
            Hex.encode(schedule.transcript),
            "e86eb527c464c990642a72e2b6ff78866d2685ec813e10a3b29b4d2042391871"
        )
        XCTAssertEqual(schedule.transcript.count, 32)
    }

    func testTranscriptOrderMatters() {
        let forward = makeSchedule()
        let swapped = makeSchedule(hello: challengePayload, challenge: helloPayload)
        XCTAssertNotEqual(forward.transcript, swapped.transcript)
    }

    func testDerivedKeyGoldenValues() {
        let schedule = makeSchedule()
        XCTAssertEqual(Hex.encode(schedule.kAuth), "7fd1b39870190993f4d219698620b1e895d36586426325cb540d7a545da0e989")
        XCTAssertEqual(
            Hex.encode(schedule.kCtrlEnc),
            "7a6741eb6b85c4ec4c73ad2cf574127646e7a5d1706a3d6f0a379d504d651178"
        )
        XCTAssertEqual(
            Hex.encode(schedule.kCtrlMac),
            "b6ba5374daee61b8f204c681f33a1570f0a9718abeee1d7120dbf5f64f0e2b3e"
        )
        XCTAssertEqual(
            Hex.encode(schedule.kMediaEnc),
            "db07a93ac0f036e782ce40a0f85ebddd427780645a0f52453f732710cf4350fe"
        )
        XCTAssertEqual(
            Hex.encode(schedule.kMediaMac),
            "4f345cecd648d3d6897d070b48a41d7cc4a9a263a2bd2dfb3f53c02b6aff8f62"
        )

        // §3.1/§4: k_ctrl and k_media are 64 bytes split enc|mac 32|32.
        XCTAssertEqual(schedule.kCtrlEnc.count, 32)
        XCTAssertEqual(schedule.kCtrlMac.count, 32)
        XCTAssertEqual(schedule.kMediaEnc.count, 32)
        XCTAssertEqual(schedule.kMediaMac.count, 32)
        XCTAssertNotEqual(schedule.kCtrlEnc, schedule.kCtrlMac)
        XCTAssertNotEqual(schedule.kMediaEnc, schedule.kMediaMac)
        XCTAssertNotEqual(schedule.kAuth, schedule.kCtrlEnc)
        XCTAssertNotEqual(schedule.kCtrlEnc, schedule.kMediaEnc)
    }

    func testSaltIsClientNonceThenServerNonce() {
        let schedule = makeSchedule()
        let reversed = makeSchedule(clientNonce: serverNonce, serverNonce: clientNonce)
        XCTAssertNotEqual(schedule.kAuth, reversed.kAuth)
        // Swapping the nonces of both sides leaves the schedule unchanged
        // only if client and server nonces are identical, which they are not.
        XCTAssertNotEqual(clientNonce, serverNonce)
    }

    func testPhoneProofGoldenValue() {
        let schedule = makeSchedule()
        let proof = schedule.phoneProof(clientNonce: clientNonce, serverNonce: serverNonce)
        XCTAssertEqual(proof.count, 32)
        XCTAssertEqual(
            Hex.encode(proof),
            "1bc2ba1ea03b9cfee86714763ebeeb05baa85f792ea663bcb8bdec746b104ee6"
        )
    }

    func testObsProofGoldenValueFollowsProtocolOrder() {
        // PROTOCOL.md §3.2: proof = HMAC-SHA256(k_auth, "obs" || server_nonce
        // || client_nonce || transcript). NOTE: the C++ reference
        // (src/core/pairing/pairing.cpp computeProof) instead always appends
        // client_nonce || server_nonce regardless of role — the wire format in
        // PROTOCOL.md is what this test pins down.
        let schedule = makeSchedule()
        let proof = schedule.obsProof(serverNonce: serverNonce, clientNonce: clientNonce)
        XCTAssertEqual(proof.count, 32)
        XCTAssertEqual(
            Hex.encode(proof),
            "8c3d1432fe3c2fd61fd742aba61f6fd96a915738aa29d7823e7790f5b1d60fb4"
        )
    }

    func testProofsDifferPerRoleAndPerNonceOrder() {
        let schedule = makeSchedule()
        let phone = schedule.phoneProof(clientNonce: clientNonce, serverNonce: serverNonce)
        let obs = schedule.obsProof(serverNonce: serverNonce, clientNonce: clientNonce)
        XCTAssertNotEqual(phone, obs)

        // Feeding the nonces in the wrong order must not reproduce the proof.
        let phoneSwapped = schedule.phoneProof(clientNonce: serverNonce, serverNonce: clientNonce)
        XCTAssertNotEqual(phone, phoneSwapped)
        let obsSwapped = schedule.obsProof(serverNonce: clientNonce, clientNonce: serverNonce)
        XCTAssertNotEqual(obs, obsSwapped)
    }

    func testProofsAreKeyedByTranscript() {
        let schedule = makeSchedule()
        let other = makeSchedule(challenge: Data(#"{"type":"challenge","nonce":"bbbb"}"#.utf8))
        XCTAssertEqual(schedule.transcript, makeSchedule().transcript)
        XCTAssertNotEqual(schedule.transcript, other.transcript)
        XCTAssertNotEqual(
            schedule.phoneProof(clientNonce: clientNonce, serverNonce: serverNonce),
            other.phoneProof(clientNonce: clientNonce, serverNonce: serverNonce)
        )
    }

    func testMasterChangesAllKeys() {
        let schedule = makeSchedule()
        let other = makeSchedule(master: patternedBytes(count: 32, seed: 9))
        XCTAssertNotEqual(schedule.kAuth, other.kAuth)
        XCTAssertNotEqual(schedule.kCtrlEnc, other.kCtrlEnc)
        XCTAssertNotEqual(schedule.kMediaMac, other.kMediaMac)
        // The transcript does not depend on the master secret.
        XCTAssertEqual(schedule.transcript, other.transcript)
    }
}
