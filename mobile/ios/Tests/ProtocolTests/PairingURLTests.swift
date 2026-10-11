import Foundation
import XCTest

@testable import PhoneCamera

/// Pairing URL parsing (PROTOCOL.md §6.1):
/// ocp1://pair?h=<ip>&p=<tcp-port>&s=<64-hex secret>&n=<server name>
final class PairingURLTests: XCTestCase {
    /// hex32(7) from tests/test_pairing.cpp: bytes 7, 8, … 38.
    private let secretHex = "0708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f20212223242526"

    func testParseRoundTrip() throws {
        // Mirrors tests/test_pairing.cpp:pairing_url_round_trip.
        let url = "ocp1://pair?h=192.168.1.42&p=47100&s=\(secretHex)&n=Studio%20PC"
        let info = try PairingInfo.parse(url)
        XCTAssertEqual(info.host, "192.168.1.42")
        XCTAssertEqual(info.port, 47100)
        XCTAssertEqual(info.secret.count, 32)
        XCTAssertEqual(Hex.encode(info.secret), secretHex)
        XCTAssertEqual(info.serverName, "Studio PC")
    }

    func testParseWithoutName() throws {
        let url = "ocp1://pair?h=10.0.0.7&p=47100&s=\(secretHex)"
        let info = try PairingInfo.parse(url)
        XCTAssertEqual(info.host, "10.0.0.7")
        XCTAssertEqual(info.port, 47100)
        XCTAssertEqual(info.serverName, "")
    }

    func testParseAcceptsUppercaseSecret() throws {
        let upper = String(repeating: "AA", count: 32)
        let info = try PairingInfo.parse("ocp1://pair?h=192.168.1.42&p=47100&s=\(upper)")
        XCTAssertEqual(info.secret, Data(repeating: 0xAA, count: 32))
    }

    func testParseAcceptsMaxPort() throws {
        let info = try PairingInfo.parse("ocp1://pair?h=192.168.1.42&p=65535&s=\(secretHex)")
        XCTAssertEqual(info.port, 65535)
    }

    func testRejectsWrongScheme() {
        XCTAssertThrowsError(try PairingInfo.parse("http://example.com")) { error in
            XCTAssertEqual(error as? OCPError, .badPairingURL("http://example.com"))
        }
        XCTAssertThrowsError(try PairingInfo.parse("ocp2://pair?h=1.2.3.4&p=1&s=\(secretHex)")) { error in
            XCTAssertEqual(error as? OCPError, .badPairingURL("ocp2://pair?h=1.2.3.4&p=1&s=\(secretHex)"))
        }
    }

    func testRejectsWrongHost() {
        let input = "ocp1://other?h=1.2.3.4&p=47100&s=\(secretHex)"
        XCTAssertThrowsError(try PairingInfo.parse(input)) { error in
            XCTAssertEqual(error as? OCPError, .badPairingURL(input))
        }
    }

    func testRejectsMissingQueryItems() {
        // Mirrors tests/test_pairing.cpp:pairing_url_round_trip rejections.
        for input in [
            "ocp1://pair?h=1.2.3.4",
            "ocp1://pair?h=&p=0&s=&n=",
            "ocp1://pair",
            "ocp1://pair?p=47100&s=\(secretHex)",
            "ocp1://pair?h=1.2.3.4&p=47100",
        ] {
            XCTAssertThrowsError(try PairingInfo.parse(input), "expected rejection for \(input)") { error in
                XCTAssertEqual(error as? OCPError, .badPairingURL(input))
            }
        }
    }

    func testRejectsBadHostValue() {
        for host in ["example.com", "999.1.1.1", "1.2.3", "::1", ""] {
            let input = "ocp1://pair?h=\(host)&p=47100&s=\(secretHex)"
            XCTAssertThrowsError(try PairingInfo.parse(input), "expected rejection for host \(host)") { error in
                XCTAssertEqual(error as? OCPError, .badPairingURL(input))
            }
        }
    }

    func testRejectsBadPort() {
        for port in ["0", "70000", "abc", "-1"] {
            let input = "ocp1://pair?h=192.168.1.42&p=\(port)&s=\(secretHex)"
            XCTAssertThrowsError(try PairingInfo.parse(input), "expected rejection for port \(port)") { error in
                XCTAssertEqual(error as? OCPError, .badPairingURL(input))
            }
        }
    }

    func testRejectsBadSecret() {
        // 64 hex chars required; short or non-hex values must fail.
        let short = String(repeating: "ab", count: 31) // 62 chars
        let nonHex = String(repeating: "zz", count: 32)
        for secret in [short, nonHex, ""] {
            let input = "ocp1://pair?h=192.168.1.42&p=47100&s=\(secret)"
            XCTAssertThrowsError(try PairingInfo.parse(input), "expected rejection for secret \(secret)") { error in
                XCTAssertEqual(error as? OCPError, .badPairingURL(input))
            }
        }
    }

    func testIPv4Validation() {
        XCTAssertTrue(PairingInfo.isIPv4("192.168.1.42"))
        XCTAssertTrue(PairingInfo.isIPv4("10.0.0.1"))
        XCTAssertTrue(PairingInfo.isIPv4("255.255.255.255"))
        XCTAssertFalse(PairingInfo.isIPv4("example.com"))
        XCTAssertFalse(PairingInfo.isIPv4("999.999.999.999"))
        XCTAssertFalse(PairingInfo.isIPv4("1.2.3"))
        XCTAssertFalse(PairingInfo.isIPv4("::1"))
        XCTAssertFalse(PairingInfo.isIPv4(""))
    }
}
