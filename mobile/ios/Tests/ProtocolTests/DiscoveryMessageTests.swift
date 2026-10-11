import Foundation
import XCTest

@testable import PhoneCamera

/// Discovery datagrams (PROTOCOL.md §2) — unencrypted JSON on UDP 47109.
final class ServerAnnouncementTests: XCTestCase {
    private func decode(_ json: String) -> ServerAnnouncement? {
        ServerAnnouncement.decode(Data(json.utf8))
    }

    func testDecodeDocumentedExample() throws {
        let announcement = try XCTUnwrap(
            decode(
                #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"DESKTOP-NAME","tcp":47100,"udp":47109}"#
            )
        )
        XCTAssertEqual(announcement.id, "00112233445566778899aabbccddeeff")
        XCTAssertEqual(announcement.name, "DESKTOP-NAME")
        XCTAssertEqual(announcement.tcpPort, 47100)
        XCTAssertEqual(announcement.udpPort, 47109)
    }

    func testDecodeMediaPortAnnouncement() throws {
        let announcement = try XCTUnwrap(
            decode(
                #"{"t":"obs","v":1,"id":"ffeeddccbbaa99887766554433221100","name":"Studio PC","tcp":47100,"udp":47101}"#
            )
        )
        XCTAssertEqual(announcement.tcpPort, 47100)
        XCTAssertEqual(announcement.udpPort, 47101)
    }

    func testDecodeAcceptsMissingUdpPort() throws {
        // The C++ reference (src/core/discovery/discovery.cpp) rejects obs
        // announcements without a valid udp field; the iOS decoder tolerates
        // it because only the control port is needed to connect.
        let announcement = try XCTUnwrap(
            decode(#"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":47100}"#)
        )
        XCTAssertNil(announcement.udpPort)
    }

    func testDecodeTruncatesLongName() throws {
        let longName = String(repeating: "x", count: 100)
        let announcement = try XCTUnwrap(
            decode(
                #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"\#(longName)","tcp":47100,"udp":47101}"#
            )
        )
        XCTAssertEqual(announcement.name.count, 64)
    }

    func testRejectsInvalidDatagrams() {
        // A receiver MUST ignore datagrams that fail JSON parsing, have an
        // unknown t, or v != 1 (§2).
        let invalid = [
            "not json",
            "",
            #"{"v":1,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":47100}"#, // no t
            #"{"t":"phone","v":1,"id":"00112233445566778899aabbccddeeff","name":"X","os":"ios","proto":"ocp/1"}"#,
            #"{"t":"obs","v":2,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":47100}"#, // wrong version
            #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":0}"#, // bad port
            #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":70000}"#, // bad port
            #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","tcp":47100}"#, // no name
            #"{"t":"obs","v":1,"id":"too-short","name":"X","tcp":47100}"#, // id not 32 chars
            #"{"t":"obs","v":1,"id":"00112233445566778899aabbccddeeff","name":"X","tcp":"47100"}"#, // port not numeric
        ]
        for json in invalid {
            XCTAssertNil(decode(json), "expected rejection for \(json)")
        }
    }
}

final class ClientAnnouncementTests: XCTestCase {
    func testEncodeMatchesDocumentedShape() throws {
        let announcement = ClientAnnouncement(
            id: "00112233445566778899aabbccddeeff",
            name: "iPhone"
        )
        let data = try announcement.encoded()
        let object = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(object["t"] as? String, "phone")
        XCTAssertEqual(object["v"] as? Int, 1)
        XCTAssertEqual(object["id"] as? String, "00112233445566778899aabbccddeeff")
        XCTAssertEqual(object["name"] as? String, "iPhone")
        XCTAssertEqual(object["os"] as? String, "ios")
        XCTAssertEqual(object["proto"] as? String, "ocp/1")
        XCTAssertEqual(object.count, 6)
    }

    func testEncodingIsDeterministic() throws {
        let announcement = ClientAnnouncement(
            id: "00112233445566778899aabbccddeeff",
            name: "iPhone"
        )
        // OCPJSON sorts keys, so repeated encodings are byte-identical.
        XCTAssertEqual(try announcement.encoded(), try announcement.encoded())
    }

    func testDecodesDocumentedExample() throws {
        let json = #"{"t":"phone","v":1,"id":"ffeeddccbbaa99887766554433221100","name":"Pixel 7","os":"android","proto":"ocp/1"}"#
        let announcement = try JSONDecoder().decode(ClientAnnouncement.self, from: Data(json.utf8))
        XCTAssertEqual(announcement.t, "phone")
        XCTAssertEqual(announcement.v, 1)
        XCTAssertEqual(announcement.id, "ffeeddccbbaa99887766554433221100")
        XCTAssertEqual(announcement.name, "Pixel 7")
        XCTAssertEqual(announcement.os, "android")
        XCTAssertEqual(announcement.proto, "ocp/1")
    }
}
