import Foundation

enum OCP {
    static let controlMagic = Data([0x4F, 0x43, 0x50, 0x31]) // "OCP1"
    static let mediaMagic = Data([0x4F, 0x43, 0x50, 0x4D])  // "OCPM"
    static let protocolVersion = 1
    static let protoIdentifier = "ocp/1"

    static let controlHeaderSize = 27
    static let controlTagSize = 16
    static let controlMaxPayload = 65536

    static let mediaHeaderSize = 32
    static let mediaTagSize = 16
    static let mediaMaxDatagram = 1200
    static let mediaMaxCiphertext = mediaMaxDatagram - mediaHeaderSize - mediaTagSize // 1152

    static let defaultControlPort: UInt16 = 47100
    static let defaultMediaPort: UInt16 = 47101
    static let discoveryPort: UInt16 = 47109
    static let discoveryMulticastGroup = "239.255.74.79"

    static let streamVideo: UInt8 = 0
    static let streamAudio: UInt8 = 1
    static let mediaFlagKeyframe: UInt8 = 0x01

    static let kdfHKDF = "hkdf-sha256"
    static let kdfPBKDF2 = "pbkdf2-sha256"
    static let pbkdf2MinIterations = 100_000
}

enum OCPError: Error, Equatable {
    case badMagic
    case badFrameLength(Int)
    case truncated
    case tagMismatch
    case counterReuse
    case badVersion
    case badDatagram
    case oversizedDatagram
    case badPairingURL(String)
    case badHex(String)
    case protocolViolation(String)

    var localizedDescriptionKey: String {
        switch self {
        case .badMagic, .badFrameLength, .truncated, .badVersion, .badDatagram, .protocolViolation:
            return "error.internal"
        case .tagMismatch, .counterReuse:
            return "error.authFailed"
        case .oversizedDatagram:
            return "error.internal"
        case .badPairingURL, .badHex:
            return "error.badQR"
        }
    }
}
