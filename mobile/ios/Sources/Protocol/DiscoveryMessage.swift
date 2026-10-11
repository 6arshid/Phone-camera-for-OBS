import Foundation

/// Discovery datagrams (PROTOCOL.md §2) — unencrypted JSON on UDP 47109.
struct ServerAnnouncement: Equatable {
    var id: String
    var name: String
    var tcpPort: Int
    var udpPort: Int?

    static func decode(_ data: Data) -> ServerAnnouncement? {
        guard
            let object = try? JSONSerialization.jsonObject(with: data),
            let dict = object as? [String: Any],
            dict["t"] as? String == "obs",
            dict["v"] as? Int == OCP.protocolVersion,
            let id = dict["id"] as? String,
            id.utf8.count == 32,
            let name = dict["name"] as? String,
            let tcp = dict["tcp"] as? Int,
            tcp > 0, tcp <= 65535
        else { return nil }
        let udp = dict["udp"] as? Int
        return ServerAnnouncement(id: id, name: String(name.prefix(64)), tcpPort: tcp, udpPort: udp)
    }
}

struct ClientAnnouncement: Codable, Equatable {
    var t = "phone"
    var v = OCP.protocolVersion
    var id: String
    var name: String
    var os = "ios"
    var proto = OCP.protoIdentifier

    enum CodingKeys: String, CodingKey {
        case t, v, id, name, os, proto
    }

    func encoded() throws -> Data {
        try OCPJSON.encode(self)
    }
}
