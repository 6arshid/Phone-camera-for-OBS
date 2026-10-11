import Foundation

struct DeviceInfo: Codable, Equatable {
    var id: String
    var name: String
    var os: String
    var app: String
}

struct HelloMessage: Codable, Equatable {
    static let messageType = "hello"
    var type = messageType
    var proto: String
    var kdf: String
    var iter: Int
    var salt: String
    var nonce: String
    var device: DeviceInfo

    enum CodingKeys: String, CodingKey {
        case type, proto, kdf, iter, salt, nonce, device
    }
}

struct ChallengeMessage: Codable, Equatable {
    static let messageType = "challenge"
    var type = messageType
    var nonce: String
    var kdf: String
    var iter: Int

    enum CodingKeys: String, CodingKey {
        case type, nonce, kdf, iter
    }
}

struct AuthMessage: Codable, Equatable {
    static let messageType = "auth"
    var type = messageType
    var nonce: String
    var proof: String

    enum CodingKeys: String, CodingKey {
        case type, nonce, proof
    }
}

struct AuthOKMessage: Codable, Equatable {
    static let messageType = "auth_ok"
    var type: String
    var session: String
    var mediaPort: Int
    var proof: String
    var paired: Bool

    enum CodingKeys: String, CodingKey {
        case type, session, proof, paired
        case mediaPort = "media_port"
    }
}

struct VideoCapabilities: Codable, Equatable {
    var codec: [String]
    var resolutions: [[Int]]
    var fps: [Int]
    var aspectRatios: [String]
    var maxBitrateKbps: Int

    enum CodingKeys: String, CodingKey {
        case codec, resolutions, fps
        case aspectRatios = "aspect_ratios"
        case maxBitrateKbps = "max_bitrate_kbps"
    }
}

struct AudioCapabilities: Codable, Equatable {
    var codecs: [String]
    var sampleRates: [Int]
    var channels: [Int]

    enum CodingKeys: String, CodingKey {
        case codecs, channels
        case sampleRates = "sample_rates"
    }
}

struct CameraCapability: Codable, Equatable {
    var id: String
    var label: String
}

struct FeatureCapabilities: Codable, Equatable {
    var torch: Bool
    var manualFocus: Bool
    var exposure: Bool
    var whiteBalance: Bool

    enum CodingKeys: String, CodingKey {
        case torch, exposure
        case manualFocus = "manual_focus"
        case whiteBalance = "white_balance"
    }
}

struct Capabilities: Codable, Equatable {
    var video: VideoCapabilities
    var audio: AudioCapabilities
    var cameras: [CameraCapability]
    var features: FeatureCapabilities
}

struct ReadyMessage: Codable, Equatable {
    static let messageType = "ready"
    var type = messageType
    var caps: Capabilities

    enum CodingKeys: String, CodingKey {
        case type, caps
    }
}

struct VideoConfig: Codable, Equatable {
    var codec: String
    var width: Int
    var height: Int
    var fps: Int
    var bitrateKbps: Int
    var gopSeconds: Double
    var camera: String
    var orientation: String

    enum CodingKeys: String, CodingKey {
        case codec, width, height, fps, camera, orientation
        case bitrateKbps = "bitrate_kbps"
        case gopSeconds = "gop_s"
    }
}

struct CameraConfig: Codable, Equatable {
    var focusMode: String
    var focusDistance: Double
    var exposureMode: String
    var exposureBias: Double
    var wbMode: String
    var wbKelvin: Int
    var torch: Bool

    enum CodingKeys: String, CodingKey {
        case focusMode = "focus_mode"
        case focusDistance = "focus_distance"
        case exposureMode = "exposure_mode"
        case exposureBias = "exposure_bias"
        case wbMode = "wb_mode"
        case wbKelvin = "wb_kelvin"
        case torch
    }
}

struct AudioConfig: Codable, Equatable {
    var enabled: Bool
    var sampleRate: Int
    var channels: Int

    enum CodingKeys: String, CodingKey {
        case enabled, channels
        case sampleRate = "sample_rate"
    }
}

struct StreamConfig: Codable, Equatable {
    var active: Bool
}

struct ConfigMessage: Codable, Equatable {
    static let messageType = "config"
    var type = messageType
    var video: VideoConfig?
    var camera: CameraConfig?
    var audio: AudioConfig?
    var stream: StreamConfig?

    enum CodingKeys: String, CodingKey {
        case type, video, camera, audio, stream
    }
}

struct StatsMessage: Codable, Equatable {
    static let messageType = "stats"
    var type = messageType
    var fps: Double
    var bitrateKbps: Int
    var frames: Int
    var battery: Int
    var thermal: String
    var camera: String
    var tsRelNs: Int64

    enum CodingKeys: String, CodingKey {
        case type, fps, frames, battery, thermal, camera
        case bitrateKbps = "bitrate_kbps"
        case tsRelNs = "ts_rel_ns"
    }
}

struct PingMessage: Codable, Equatable {
    static let messageType = "ping"
    var type = messageType
    var t: Int64

    enum CodingKeys: String, CodingKey {
        case type, t
    }
}

struct PongMessage: Codable, Equatable {
    static let messageType = "pong"
    var type = messageType
    var t: Int64

    enum CodingKeys: String, CodingKey {
        case type, t
    }
}

struct HintMessage: Codable, Equatable {
    static let messageType = "hint"
    var type = messageType
    var lossPct: Double
    var rttMs: Double
    var queueMs: Double

    enum CodingKeys: String, CodingKey {
        case type
        case lossPct = "loss_pct"
        case rttMs = "rtt_ms"
        case queueMs = "queue_ms"
    }
}

struct ErrorMessage: Codable, Equatable {
    static let messageType = "error"
    var type = messageType
    var code: String
    var msg: String?

    enum CodingKeys: String, CodingKey {
        case type, code, msg
    }
}

struct ByeMessage: Codable, Equatable {
    static let messageType = "bye"
    var type = messageType
    var reason: String

    enum CodingKeys: String, CodingKey {
        case type, reason
    }
}

/// Dispatches an incoming raw JSON payload on the `type` field.
enum OCPMessage {
    case hello(HelloMessage)
    case challenge(ChallengeMessage)
    case auth(AuthMessage)
    case authOK(AuthOKMessage)
    case ready(ReadyMessage)
    case config(ConfigMessage)
    case stats(StatsMessage)
    case ping(PingMessage)
    case pong(PongMessage)
    case hint(HintMessage)
    case error(ErrorMessage)
    case bye(ByeMessage)
    case unknown(String)

    static func decode(payload: Data) throws -> OCPMessage {
        let decoder = JSONDecoder()
        let object = try JSONSerialization.jsonObject(with: payload)
        guard let dict = object as? [String: Any], let type = dict["type"] as? String else {
            throw OCPError.protocolViolation("missing type")
        }
        switch type {
        case HelloMessage.messageType:
            return .hello(try decoder.decode(HelloMessage.self, from: payload))
        case ChallengeMessage.messageType:
            return .challenge(try decoder.decode(ChallengeMessage.self, from: payload))
        case AuthMessage.messageType:
            return .auth(try decoder.decode(AuthMessage.self, from: payload))
        case AuthOKMessage.messageType:
            return .authOK(try decoder.decode(AuthOKMessage.self, from: payload))
        case ReadyMessage.messageType:
            return .ready(try decoder.decode(ReadyMessage.self, from: payload))
        case ConfigMessage.messageType:
            return .config(try decoder.decode(ConfigMessage.self, from: payload))
        case StatsMessage.messageType:
            return .stats(try decoder.decode(StatsMessage.self, from: payload))
        case PingMessage.messageType:
            return .ping(try decoder.decode(PingMessage.self, from: payload))
        case PongMessage.messageType:
            return .pong(try decoder.decode(PongMessage.self, from: payload))
        case HintMessage.messageType:
            return .hint(try decoder.decode(HintMessage.self, from: payload))
        case ErrorMessage.messageType:
            return .error(try decoder.decode(ErrorMessage.self, from: payload))
        case ByeMessage.messageType:
            return .bye(try decoder.decode(ByeMessage.self, from: payload))
        default:
            return .unknown(type)
        }
    }
}

enum OCPJSON {
    static let encoder: JSONEncoder = {
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.sortedKeys]
        return encoder
    }()

    static func encode<T: Encodable>(_ value: T) throws -> Data {
        try encoder.encode(value)
    }
}
