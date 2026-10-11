import Combine
import Foundation
import Security

/// User preferences and identity persisted in UserDefaults. The paired-server
/// secret itself lives in the Keychain (see KeychainStore).
@MainActor
final class SettingsStore: ObservableObject {
    private static let defaults = UserDefaults.standard

    @Published var width: Int { didSet { Self.defaults.set(width, forKey: "video.width") } }
    @Published var height: Int { didSet { Self.defaults.set(height, forKey: "video.height") } }
    @Published var fps: Int { didSet { Self.defaults.set(fps, forKey: "video.fps") } }
    @Published var bitrateKbps: Int { didSet { Self.defaults.set(bitrateKbps, forKey: "video.bitrate_kbps") } }
    @Published var cameraID: String { didSet { Self.defaults.set(cameraID, forKey: "video.camera") } }
    @Published var orientationMode: String { didSet { Self.defaults.set(orientationMode, forKey: "video.orientation") } }
    @Published var torch: Bool { didSet { Self.defaults.set(torch, forKey: "camera.torch") } }
    @Published var focusMode: String { didSet { Self.defaults.set(focusMode, forKey: "camera.focus_mode") } }
    @Published var focusDistance: Double { didSet { Self.defaults.set(focusDistance, forKey: "camera.focus_distance") } }
    @Published var exposureMode: String { didSet { Self.defaults.set(exposureMode, forKey: "camera.exposure_mode") } }
    @Published var exposureBias: Double { didSet { Self.defaults.set(exposureBias, forKey: "camera.exposure_bias") } }
    @Published var wbMode: String { didSet { Self.defaults.set(wbMode, forKey: "camera.wb_mode") } }
    @Published var wbKelvin: Int { didSet { Self.defaults.set(wbKelvin, forKey: "camera.wb_kelvin") } }
    @Published var audioEnabled: Bool { didSet { Self.defaults.set(audioEnabled, forKey: "audio.enabled") } }
    @Published var lastHost: String { didSet { Self.defaults.set(lastHost, forKey: "server.host") } }
    @Published var lastPort: Int { didSet { Self.defaults.set(lastPort, forKey: "server.port") } }

    /// Stable random 128-bit device id (lowercase hex) used in `hello.device.id`.
    let deviceId: String

    /// Stable random 128-bit id used in discovery announcements.
    let discoveryId: String

    init() {
        width = Self.defaults.object(forKey: "video.width") as? Int ?? 1920
        height = Self.defaults.object(forKey: "video.height") as? Int ?? 1080
        fps = Self.defaults.object(forKey: "video.fps") as? Int ?? 30
        bitrateKbps = Self.defaults.object(forKey: "video.bitrate_kbps") as? Int ?? 4000
        cameraID = Self.defaults.string(forKey: "video.camera") ?? CameraController.backID
        orientationMode = Self.defaults.string(forKey: "video.orientation") ?? "auto"
        torch = Self.defaults.bool(forKey: "camera.torch")
        focusMode = Self.defaults.string(forKey: "camera.focus_mode") ?? "auto"
        focusDistance = Self.defaults.double(forKey: "camera.focus_distance")
        exposureMode = Self.defaults.string(forKey: "camera.exposure_mode") ?? "auto"
        exposureBias = Self.defaults.double(forKey: "camera.exposure_bias")
        wbMode = Self.defaults.string(forKey: "camera.wb_mode") ?? "auto"
        wbKelvin = Self.defaults.object(forKey: "camera.wb_kelvin") as? Int ?? 5500
        audioEnabled = Self.defaults.bool(forKey: "audio.enabled")
        lastHost = Self.defaults.string(forKey: "server.host") ?? ""
        lastPort = Self.defaults.object(forKey: "server.port") as? Int ?? Int(OCP.defaultControlPort)
        deviceId = Self.stableHexID(key: "identity.device_id")
        discoveryId = Self.stableHexID(key: "identity.discovery_id")
    }

    private static func stableHexID(key: String) -> String {
        if let existing = defaults.string(forKey: key), existing.utf8.count == 32 {
            return existing
        }
        var bytes = [UInt8](repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, bytes.count, &bytes)
        let id = Hex.encode(Data(bytes))
        defaults.set(id, forKey: key)
        return id
    }
}
