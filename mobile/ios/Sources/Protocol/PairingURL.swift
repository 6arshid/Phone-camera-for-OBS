import Foundation

/// Pairing URL (PROTOCOL.md §6.1):
/// ocp1://pair?h=<ip>&p=<tcp-port>&s=<64-hex secret>&n=<server name>
struct PairingInfo: Equatable {
    var host: String
    var port: UInt16
    var secret: Data
    var serverName: String

    static func parse(_ string: String) throws -> PairingInfo {
        guard let components = URLComponents(string: string) else {
            throw OCPError.badPairingURL(string)
        }
        guard components.scheme?.lowercased() == "ocp1", components.host?.lowercased() == "pair" else {
            throw OCPError.badPairingURL(string)
        }
        guard let items = components.queryItems else {
            throw OCPError.badPairingURL(string)
        }
        var host: String?
        var port: UInt16?
        var secret: Data?
        var name = ""
        for item in items {
            switch item.name {
            case "h":
                host = item.value
            case "p":
                if let value = item.value, let parsed = UInt16(value), parsed > 0 {
                    port = parsed
                }
            case "s":
                if let value = item.value, value.utf8.count == 64, let decoded = Hex.decode(value) {
                    secret = decoded
                }
            case "n":
                name = item.value ?? ""
            default:
                continue
            }
        }
        guard let host, let port, let secret else {
            throw OCPError.badPairingURL(string)
        }
        guard isIPv4(host) else {
            throw OCPError.badPairingURL(string)
        }
        return PairingInfo(host: host, port: port, secret: secret, serverName: name)
    }

    static func isIPv4(_ host: String) -> Bool {
        var address = in_addr()
        return host.withCString { inet_pton(AF_INET, $0, &address) } == 1
    }
}
