import Foundation
import XCTest

@testable import PhoneCamera

/// Message serialization and dispatch (PROTOCOL.md §3.2).
/// Payloads are reused from tests/test_messages.cpp where applicable.
final class MessageSerializationTests: XCTestCase {
    private func decodePayload(_ json: String) throws -> OCPMessage {
        try OCPMessage.decode(payload: Data(json.utf8))
    }

    // MARK: - hello

    func testHelloEncodingMatchesDocumentedShape() throws {
        // Mirrors tests/test_messages.cpp:messages_hello_round_trip.
        let hello = HelloMessage(
            proto: "ocp/1",
            kdf: "hkdf-sha256",
            iter: 0,
            salt: String(repeating: "a", count: 32),
            nonce: String(repeating: "b", count: 32),
            device: DeviceInfo(
                id: "00112233445566778899aabbccddeeff",
                name: "Pixel 7",
                os: "ios",
                app: "1.0.0"
            )
        )
        let data = try OCPJSON.encode(hello)
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(object["type"] as? String, "hello")
        XCTAssertEqual(object["proto"] as? String, "ocp/1")
        XCTAssertEqual(object["kdf"] as? String, "hkdf-sha256")
        XCTAssertEqual(object["iter"] as? Int, 0)
        XCTAssertEqual(object["salt"] as? String, String(repeating: "a", count: 32))
        XCTAssertEqual(object["nonce"] as? String, String(repeating: "b", count: 32))

        let device = try XCTUnwrap(object["device"] as? [String: Any])
        XCTAssertEqual(device["id"] as? String, "00112233445566778899aabbccddeeff")
        XCTAssertEqual(device["name"] as? String, "Pixel 7")
        XCTAssertEqual(device["os"] as? String, "ios")
        XCTAssertEqual(device["app"] as? String, "1.0.0")

        XCTAssertEqual(try JSONDecoder().decode(HelloMessage.self, from: data), hello)
    }

    func testHelloDecodingDocumentedExample() throws {
        let json = #"{"type":"hello","proto":"ocp/1","kdf":"pbkdf2-sha256","iter":100000,"salt":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","nonce":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","device":{"id":"00112233445566778899aabbccddeeff","name":"Pixel 7","os":"android","app":"1.0.0"}}"#
        let message = try decodePayload(json)
        guard case .hello(let hello) = message else {
            return XCTFail("expected hello, got \(message)")
        }
        XCTAssertEqual(hello.proto, "ocp/1")
        XCTAssertEqual(hello.kdf, "pbkdf2-sha256")
        XCTAssertEqual(hello.iter, 100_000)
        XCTAssertEqual(hello.salt, String(repeating: "a", count: 32))
        XCTAssertEqual(hello.device.id, "00112233445566778899aabbccddeeff")
        XCTAssertEqual(hello.device.os, "android")
    }

    // MARK: - challenge / auth / auth_ok

    func testChallengeRoundTrip() throws {
        let json = #"{"type":"challenge","nonce":"cccccccccccccccccccccccccccccccc","kdf":"hkdf-sha256","iter":0}"#
        let message = try decodePayload(json)
        guard case .challenge(let challenge) = message else {
            return XCTFail("expected challenge, got \(message)")
        }
        XCTAssertEqual(challenge.nonce, String(repeating: "c", count: 32))
        XCTAssertEqual(challenge.kdf, "hkdf-sha256")
        XCTAssertEqual(challenge.iter, 0)

        XCTAssertEqual(try JSONDecoder().decode(ChallengeMessage.self, from: Data(json.utf8)), challenge)
    }

    func testAuthRoundTrip() throws {
        let proof = String(repeating: "ab", count: 32)
        let json = #"{"type":"auth","nonce":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","proof":"\#(proof)"}"#
        let message = try decodePayload(json)
        guard case .auth(let auth) = message else {
            return XCTFail("expected auth, got \(message)")
        }
        XCTAssertEqual(auth.nonce, String(repeating: "b", count: 32))
        XCTAssertEqual(auth.proof, proof)
        XCTAssertEqual(auth.proof.count, 64)
    }

    func testAuthOkDecoding() throws {
        // Payload from tests/test_messages.cpp:messages_ready_authok_bye.
        let json = #"{"type":"auth_ok","session":"aabb","media_port":47101,"proof":"ccdd","paired":true}"#
        let message = try decodePayload(json)
        guard case .authOK(let authOK) = message else {
            return XCTFail("expected auth_ok, got \(message)")
        }
        XCTAssertEqual(authOK.type, "auth_ok")
        XCTAssertEqual(authOK.session, "aabb")
        XCTAssertEqual(authOK.mediaPort, 47101)
        XCTAssertEqual(authOK.proof, "ccdd")
        XCTAssertTrue(authOK.paired)
    }

    func testAuthOkRoundTrip() throws {
        let original = AuthOKMessage(
            type: "auth_ok",
            session: String(repeating: "0f", count: 16),
            mediaPort: 47101,
            proof: String(repeating: "ee", count: 32),
            paired: false
        )
        let data = try OCPJSON.encode(original)
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(object["media_port"] as? Int, 47101) // snake_case key
        XCTAssertEqual(object["type"] as? String, "auth_ok")

        let payload = try XCTUnwrap(String(data: data, encoding: .utf8))
        let roundTripped = try decodePayload(payload)
        guard case .authOK(let decoded) = roundTripped else {
            return XCTFail("expected auth_ok")
        }
        XCTAssertEqual(decoded, original)
    }

    // MARK: - ready

    func testReadyDecodingFullDocumentedExample() throws {
        let json = #"{"type":"ready","caps":{"video":{"codec":["h264"],"resolutions":[[3840,2160],[1920,1080],[1280,720],[640,360]],"fps":[60,30,24],"aspect_ratios":["4:3","16:9"],"max_bitrate_kbps":50000},"audio":{"codecs":["pcm_s16le"],"sample_rates":[48000],"channels":[1]},"cameras":[{"id":"back","label":"Back"},{"id":"front","label":"Front"}],"features":{"torch":true,"manual_focus":true,"exposure":true,"white_balance":true}}}"#
        let message = try decodePayload(json)
        guard case .ready(let ready) = message else {
            return XCTFail("expected ready, got \(message)")
        }
        XCTAssertEqual(ready.caps.video.codec, ["h264"])
        XCTAssertEqual(ready.caps.video.resolutions.first, [3840, 2160])
        XCTAssertEqual(ready.caps.video.resolutions.count, 4)
        XCTAssertEqual(ready.caps.video.fps, [60, 30, 24])
        XCTAssertEqual(ready.caps.video.aspectRatios, ["4:3", "16:9"])
        XCTAssertEqual(ready.caps.video.maxBitrateKbps, 50_000)
        XCTAssertEqual(ready.caps.audio.codecs, ["pcm_s16le"])
        XCTAssertEqual(ready.caps.audio.sampleRates, [48_000])
        XCTAssertEqual(ready.caps.audio.channels, [1])
        XCTAssertEqual(ready.caps.cameras.map(\.id), ["back", "front"])
        XCTAssertTrue(ready.caps.features.torch)
        XCTAssertTrue(ready.caps.features.manualFocus)
        XCTAssertTrue(ready.caps.features.exposure)
        XCTAssertTrue(ready.caps.features.whiteBalance)
    }

    func testReadyRejectsPartialCapabilities() {
        // The C++ reference tolerates partial caps objects; the Swift Codable
        // model requires every field (see report: potential strictness
        // divergence from tests/test_messages.cpp:messages_ready_authok_bye).
        XCTAssertThrowsError(
            try decodePayload(#"{"type":"ready","caps":{"video":{"codec":["h264"],"fps":[30,60]}}}"#)
        )
    }

    // MARK: - config

    func testConfigDecodingFullDocumentedExample() throws {
        let json = #"{"type":"config","video":{"codec":"h264","width":1920,"height":1080,"fps":30,"bitrate_kbps":4000,"gop_s":2,"camera":"back","orientation":"auto"},"camera":{"focus_mode":"auto","focus_distance":0.5,"exposure_mode":"auto","exposure_bias":0.0,"wb_mode":"auto","wb_kelvin":5500,"torch":false},"audio":{"enabled":false,"sample_rate":48000,"channels":1},"stream":{"active":true}}"#
        let message = try decodePayload(json)
        guard case .config(let config) = message else {
            return XCTFail("expected config, got \(message)")
        }

        let video = try XCTUnwrap(config.video)
        XCTAssertEqual(video.codec, "h264")
        XCTAssertEqual(video.width, 1920)
        XCTAssertEqual(video.height, 1080)
        XCTAssertEqual(video.fps, 30)
        XCTAssertEqual(video.bitrateKbps, 4000)
        XCTAssertEqual(video.gopSeconds, 2.0)
        XCTAssertEqual(video.camera, "back")
        XCTAssertEqual(video.orientation, "auto")

        let camera = try XCTUnwrap(config.camera)
        XCTAssertEqual(camera.focusMode, "auto")
        XCTAssertEqual(camera.focusDistance, 0.5)
        XCTAssertEqual(camera.exposureMode, "auto")
        XCTAssertEqual(camera.exposureBias, 0.0)
        XCTAssertEqual(camera.wbMode, "auto")
        XCTAssertEqual(camera.wbKelvin, 5500)
        XCTAssertFalse(camera.torch)

        let audio = try XCTUnwrap(config.audio)
        XCTAssertFalse(audio.enabled)
        XCTAssertEqual(audio.sampleRate, 48_000)
        XCTAssertEqual(audio.channels, 1)

        XCTAssertTrue(try XCTUnwrap(config.stream).active)
    }

    func testConfigRoundTrip() throws {
        let config = ConfigMessage(
            video: VideoConfig(
                codec: "h264",
                width: 1280,
                height: 720,
                fps: 60,
                bitrateKbps: 6000,
                gopSeconds: 1.5,
                camera: "toggle",
                orientation: "portrait"
            ),
            camera: CameraConfig(
                focusMode: "manual",
                focusDistance: 0.7,
                exposureMode: "manual",
                exposureBias: -1.5,
                wbMode: "manual",
                wbKelvin: 4300,
                torch: true
            ),
            audio: AudioConfig(enabled: true, sampleRate: 48_000, channels: 1),
            stream: StreamConfig(active: false)
        )
        let data = try OCPJSON.encode(config)
        XCTAssertEqual(try JSONDecoder().decode(ConfigMessage.self, from: data), config)

        let payload = try XCTUnwrap(String(data: data, encoding: .utf8))
        let message = try decodePayload(payload)
        guard case .config(let decoded) = message else {
            return XCTFail("expected config")
        }
        XCTAssertEqual(decoded.video?.camera, "toggle")
        XCTAssertEqual(decoded.video?.orientation, "portrait")
        XCTAssertEqual(decoded.camera?.focusMode, "manual")
        XCTAssertEqual(decoded.camera?.torch, true)
        XCTAssertEqual(decoded.audio?.enabled, true)
        XCTAssertEqual(decoded.stream?.active, false)
    }

    func testConfigRejectsPartialVideoSection() {
        // tests/test_messages.cpp:messages_partial_config expects the C++
        // reference to accept {"video":{"camera":"front"}} with defaults; the
        // Swift Codable model requires the full object (see report).
        XCTAssertThrowsError(try decodePayload(#"{"type":"config","video":{"camera":"front"}}"#))
    }

    func testConfigAllowsMissingSections() throws {
        // Sections themselves are optional: an audio-only config decodes.
        let message = try decodePayload(#"{"type":"config","stream":{"active":false}}"#)
        guard case .config(let config) = message else {
            return XCTFail("expected config, got \(message)")
        }
        XCTAssertNil(config.video)
        XCTAssertNil(config.camera)
        XCTAssertNil(config.audio)
        XCTAssertEqual(config.stream?.active, false)
    }

    // MARK: - stats / hint / ping / pong / error / bye

    func testStatsDecoding() throws {
        let json = #"{"type":"stats","fps":29.8,"bitrate_kbps":3980,"frames":100,"battery":72,"thermal":"nominal","camera":"back","ts_rel_ns":5000000}"#
        let message = try decodePayload(json)
        guard case .stats(let stats) = message else {
            return XCTFail("expected stats, got \(message)")
        }
        XCTAssertEqual(stats.fps, 29.8, accuracy: 0.0001)
        XCTAssertEqual(stats.bitrateKbps, 3980)
        XCTAssertEqual(stats.frames, 100)
        XCTAssertEqual(stats.battery, 72)
        XCTAssertEqual(stats.thermal, "nominal")
        XCTAssertEqual(stats.camera, "back")
        XCTAssertEqual(stats.tsRelNs, 5_000_000)
    }

    func testStatsRoundTrip() throws {
        let stats = StatsMessage(
            fps: 30.0,
            bitrateKbps: 4000,
            frames: 10,
            battery: 50,
            thermal: "fair",
            camera: "front",
            tsRelNs: 1_000_000_000
        )
        let data = try OCPJSON.encode(stats)
        XCTAssertEqual(try JSONDecoder().decode(StatsMessage.self, from: data), stats)
    }

    func testHintDecoding() throws {
        let json = #"{"type":"hint","loss_pct":2.5,"rtt_ms":12,"queue_ms":30}"#
        let message = try decodePayload(json)
        guard case .hint(let hint) = message else {
            return XCTFail("expected hint, got \(message)")
        }
        XCTAssertEqual(hint.lossPct, 2.5, accuracy: 0.0001)
        XCTAssertEqual(hint.rttMs, 12, accuracy: 0.0001)
        XCTAssertEqual(hint.queueMs, 30, accuracy: 0.0001)
    }

    func testPingPongRoundTrip() throws {
        let ping = try decodePayload(#"{"type":"ping","t":1700000000000}"#)
        guard case .ping(let decoded) = ping else {
            return XCTFail("expected ping, got \(ping)")
        }
        XCTAssertEqual(decoded.t, 1_700_000_000_000)

        let data = try OCPJSON.encode(PongMessage(t: 12_345))
        let encoded = try XCTUnwrap(String(data: data, encoding: .utf8))
        XCTAssertEqual(encoded, #"{"t":12345,"type":"pong"}"#)
        let message = try decodePayload(encoded)
        guard case .pong(let pong) = message else {
            return XCTFail("expected pong")
        }
        XCTAssertEqual(pong.t, 12_345)
    }

    func testErrorDecoding() throws {
        let json = #"{"type":"error","code":"pairing_failed","msg":"Pairing code rejected"}"#
        let message = try decodePayload(json)
        guard case .error(let error) = message else {
            return XCTFail("expected error, got \(message)")
        }
        XCTAssertEqual(error.code, "pairing_failed")
        XCTAssertEqual(error.msg, "Pairing code rejected")
    }

    func testErrorEncodingOmitsNilMessage() throws {
        let data = try OCPJSON.encode(ErrorMessage(code: "busy", msg: nil))
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])
        XCTAssertEqual(object["type"] as? String, "error")
        XCTAssertEqual(object["code"] as? String, "busy")
        XCTAssertNil(object["msg"])
    }

    func testByeRoundTrip() throws {
        let message = try decodePayload(#"{"type":"bye","reason":"shutdown"}"#)
        guard case .bye(let bye) = message else {
            return XCTFail("expected bye, got \(message)")
        }
        XCTAssertEqual(bye.reason, "shutdown")

        let data = try OCPJSON.encode(ByeMessage(reason: "done"))
        XCTAssertEqual(String(data: data, encoding: .utf8), #"{"reason":"done","type":"bye"}"#)
    }
}

/// Dispatch on the `type` field (Messages.swift:OCPMessage).
final class OCPMessageDispatchTests: XCTestCase {
    private func decodePayload(_ json: String) throws -> OCPMessage {
        try OCPMessage.decode(payload: Data(json.utf8))
    }

    func testRejectsMissingType() {
        XCTAssertThrowsError(try decodePayload(#"{"nofield":1}"#)) { error in
            XCTAssertEqual(error as? OCPError, .protocolViolation("missing type"))
        }
    }

    func testRejectsInvalidJson() {
        XCTAssertThrowsError(try decodePayload("not json"))
        XCTAssertThrowsError(try decodePayload(""))
        XCTAssertThrowsError(try decodePayload("[1,2,3]"))
    }

    func testRejectsIncompleteHello() {
        // tests/test_messages.cpp:messages_rejects_bad_payloads — a hello with
        // proto ocp/2 (and no other fields) must fail to parse.
        XCTAssertThrowsError(try decodePayload(#"{"type":"hello","proto":"ocp/2"}"#))
    }

    func testUnknownTypeIsSurfacedNotFatal() throws {
        // NOTE: the C++ reference parseMessage() reports an error for unknown
        // types; the iOS client surfaces them as .unknown and AppModel ignores
        // them (forward compatibility with additive message types).
        let message = try decodePayload(#"{"type":"mystery"}"#)
        guard case .unknown(let type) = message else {
            return XCTFail("expected unknown, got \(message)")
        }
        XCTAssertEqual(type, "mystery")
    }

    func testDecodesEveryDocumentedType() throws {
        let payloads: [String] = [
            #"{"type":"hello","proto":"ocp/1","kdf":"hkdf-sha256","iter":0,"salt":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","nonce":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","device":{"id":"00112233445566778899aabbccddeeff","name":"X","os":"ios","app":"1.0.0"}}"#,
            #"{"type":"challenge","nonce":"cccccccccccccccccccccccccccccccc","kdf":"hkdf-sha256","iter":0}"#,
            #"{"type":"auth","nonce":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","proof":"dd"}"#,
            #"{"type":"auth_ok","session":"aa","media_port":47101,"proof":"bb","paired":false}"#,
            #"{"type":"ready","caps":{"video":{"codec":["h264"],"resolutions":[[1920,1080]],"fps":[30],"aspect_ratios":["16:9"],"max_bitrate_kbps":8000},"audio":{"codecs":["pcm_s16le"],"sample_rates":[48000],"channels":[1]},"cameras":[],"features":{"torch":false,"manual_focus":false,"exposure":false,"white_balance":false}}}"#,
            #"{"type":"config","stream":{"active":true}}"#,
            #"{"type":"stats","fps":30,"bitrate_kbps":1,"frames":2,"battery":3,"thermal":"nominal","camera":"back","ts_rel_ns":4}"#,
            #"{"type":"ping","t":1}"#,
            #"{"type":"pong","t":1}"#,
            #"{"type":"hint","loss_pct":0,"rtt_ms":0,"queue_ms":0}"#,
            #"{"type":"error","code":"busy"}"#,
            #"{"type":"bye","reason":"done"}"#,
        ]

        for payload in payloads {
            XCTAssertNoThrow(try decodePayload(payload), "failed for \(payload)")
        }
    }
}
