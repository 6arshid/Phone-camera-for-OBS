import AVFoundation
import Combine
import Foundation
import UIKit

/// Coordinates the OCP/1 session state machine:
/// idle → discovering/connected → streaming (PROTOCOL.md §3.2).
@MainActor
final class AppModel: ObservableObject {
    enum Phase: Equatable {
        case idle
        case connecting
        case handshaking
        case connected
        case streaming
    }

    struct DiscoveredServer: Identifiable, Equatable {
        let id: String
        var name: String
        var host: String
        var port: UInt16
        var lastSeen: Date
    }

    // MARK: - Published state

    @Published private(set) var phase: Phase = .idle
    @Published private(set) var servers: [DiscoveredServer] = []
    @Published private(set) var lastErrorMessage: String?
    @Published private(set) var connectedServerName = ""
    @Published private(set) var liveFPS: Double = 0
    @Published private(set) var liveBitrateKbps: Int = 0
    @Published private(set) var liveRTTMs: Double = 0
    @Published private(set) var totalFramesSent: Int = 0
    @Published private(set) var batteryLevel: Float = -1
    @Published private(set) var thermal: ProcessInfo.ThermalState = .nominal
    @Published private(set) var isDiscovering = false
    @Published private(set) var isBusyPairing = false
    @Published var lastConnectionError: String?

    let settings: SettingsStore
    let camera = CameraController()
    let encoder = H264Encoder()
    let audioCapture = AudioCapture()

    // MARK: - Private collaborators

    private let discovery = DiscoveryService()
    private var control: ControlClient?
    private var media: MediaSender?
    private var pump: MediaStreamPump?
    private let streamClock = StreamClock()
    private var statsTask: Task<Void, Never>?
    private var reconnectTask: Task<Void, Never>?
    private var serverExpiryTask: Task<Void, Never>?
    private var reconnectAttempt = 0
    private var userInitiatedDisconnect = false

    private struct HandshakeContext {
        var host: String
        var port: UInt16
        var master: Data
        var kdf: String
        var iter: Int
        var salt: Data
        var clientNonce: Data
        var helloPayload: Data
        var serverName: String
        var serverID: String?
        var keychainAccounts: [String]
        var schedule: KeySchedule?
    }

    private var handshake: HandshakeContext?
    private var currentVideo: VideoConfig
    private var currentCamera: CameraConfig
    private var currentAudio: AudioConfig
    private var streamActive = false
    private var encoderConfigured = false
    private var lastAdaptTime: Date = .distantPast
    private var lowLossSeconds = 0
    private var encoderTargetKbps = 0
    private var rttMs: Double = 0

    // MARK: - Init

    init(settings: SettingsStore) {
        self.settings = settings
        currentVideo = VideoConfig(
            codec: "h264",
            width: settings.width,
            height: settings.height,
            fps: settings.fps,
            bitrateKbps: settings.bitrateKbps,
            gopSeconds: 2.0,
            camera: settings.cameraID,
            orientation: settings.orientationMode
        )
        currentCamera = CameraConfig(
            focusMode: settings.focusMode,
            focusDistance: settings.focusDistance,
            exposureMode: settings.exposureMode,
            exposureBias: settings.exposureBias,
            wbMode: settings.wbMode,
            wbKelvin: settings.wbKelvin,
            torch: settings.torch
        )
        currentAudio = AudioConfig(enabled: settings.audioEnabled, sampleRate: 48_000, channels: 1)

        UIDevice.current.isBatteryMonitoringEnabled = true
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(batteryStateChanged),
            name: UIDevice.batteryLevelDidChangeNotification,
            object: nil
        )
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(thermalStateChanged),
            name: ProcessInfo.thermalStateDidChangeNotification,
            object: nil
        )
        batteryLevel = UIDevice.current.batteryLevel
        thermal = ProcessInfo.processInfo.thermalState

        setupDiscovery()
        setupPipeline()
        startDiscovery()
    }

    deinit {
        statsTask?.cancel()
        reconnectTask?.cancel()
        serverExpiryTask?.cancel()
    }

    @objc private func batteryStateChanged() {
        batteryLevel = UIDevice.current.batteryLevel
    }

    @objc private func thermalStateChanged() {
        thermal = ProcessInfo.processInfo.thermalState
    }

    // MARK: - Discovery

    private func setupDiscovery() {
        discovery.onServerAnnouncement = { [weak self] announcement, host in
            DispatchQueue.main.async {
                self?.ingest(announcement: announcement, host: host)
            }
        }
    }

    func startDiscovery() {
        guard !isDiscovering else { return }
        isDiscovering = true
        let announcement = ClientAnnouncement(
            id: settings.discoveryId,
            name: String(UIDevice.current.name.prefix(64))
        )
        discovery.start(announcement: announcement)
        startServerExpiry()
    }

    func stopDiscovery() {
        isDiscovering = false
        discovery.stop()
    }

    private func ingest(announcement: ServerAnnouncement, host: String) {
        guard phase == .idle || phase == .connecting || phase == .handshaking else { return }
        if let index = servers.firstIndex(where: { $0.id == announcement.id }) {
            servers[index].name = announcement.name
            servers[index].host = host
            servers[index].port = UInt16(announcement.tcpPort)
            servers[index].lastSeen = Date()
        } else {
            servers.append(DiscoveredServer(
                id: announcement.id,
                name: announcement.name,
                host: host,
                port: UInt16(announcement.tcpPort),
                lastSeen: Date()
            ))
        }
    }

    private func startServerExpiry() {
        serverExpiryTask?.cancel()
        serverExpiryTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 2_000_000_000)
                guard let self, !Task.isCancelled else { return }
                let cutoff = Date().addingTimeInterval(-6)
                self.servers.removeAll { $0.lastSeen < cutoff }
            }
        }
    }

    // MARK: - Pipeline wiring

    private func setupPipeline() {
        camera.videoSampleHandler = { [weak self] sampleBuffer in
            self?.encoder.encode(sampleBuffer: sampleBuffer, timestampNs: 0)
        }
        encoder.onAccessUnit = { [weak self] annexB, isKeyframe, timestampNs in
            self?.pump?.sendVideo(annexB: annexB, isKeyframe: isKeyframe, timestampNs: timestampNs)
        }
        audioCapture.onPCMFrame = { [weak self] pcm, timestampNs in
            self?.pump?.sendAudio(pcm: pcm, timestampNs: timestampNs)
        }
    }

    // MARK: - Connecting

    func connect(to server: DiscoveredServer) {
        let accounts = keychainAccounts(host: server.host, port: server.port, serverID: server.id)
        guard let master = accounts.lazy.compactMap({ KeychainStore.load(account: $0) }).first else {
            lastConnectionError = String(localized: "error.pairingFailed")
            return
        }
        beginConnect(
            host: server.host,
            port: server.port,
            master: master,
            kdf: OCP.kdfHKDF,
            iter: 0,
            serverName: server.name,
            serverID: server.id,
            keychainAccounts: accounts
        )
    }

    func connectViaPairing(_ info: PairingInfo, serverID: String? = nil) {
        beginConnect(
            host: info.host,
            port: info.port,
            master: info.secret,
            kdf: OCP.kdfHKDF,
            iter: 0,
            serverName: info.serverName.isEmpty ? info.host : info.serverName,
            serverID: serverID,
            keychainAccounts: keychainAccounts(host: info.host, port: info.port, serverID: serverID)
        )
    }

    func connectViaPairingCode(host: String, port: UInt16, code: String, serverName: String = "", serverID: String? = nil) {
        let digits = code.filter { $0.isASCII && $0.isNumber }
        guard digits.count == 6 else {
            lastConnectionError = String(localized: "error.codeFormat")
            return
        }
        var salt = Data(repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, salt.count, &salt)
        isBusyPairing = true
        phase = .connecting
        lastConnectionError = nil
        Task { [weak self] in
            let master = await Task.detached(priority: .userInitiated) {
                PBKDF2.derive(password: Data(digits.utf8), salt: salt, iterations: OCP.pbkdf2MinIterations, keyLength: 32)
            }.value
            guard let self else { return }
            self.isBusyPairing = false
            self.beginConnect(
                host: host,
                port: port,
                master: master,
                kdf: OCP.kdfPBKDF2,
                iter: OCP.pbkdf2MinIterations,
                saltOverride: salt,
                serverName: serverName,
                serverID: serverID,
                keychainAccounts: self.keychainAccounts(host: host, port: port, serverID: serverID)
            )
        }
    }

    private func keychainAccounts(host: String, port: UInt16, serverID: String?) -> [String] {
        var accounts = ["\(host):\(port)"]
        if let serverID {
            accounts.insert("\(serverID)@\(host):\(port)", at: 0)
        }
        return accounts
    }

    private func beginConnect(
        host: String,
        port: UInt16,
        master: Data,
        kdf: String,
        iter: Int,
        saltOverride: Data? = nil,
        serverName: String,
        serverID: String?,
        keychainAccounts: [String]
    ) {
        cancelReconnect()
        userInitiatedDisconnect = false
        reconnectAttempt = 0
        lastConnectionError = nil
        phase = .connecting
        connectedServerName = serverName.isEmpty ? host : serverName

        var salt = Data(repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, salt.count, &salt)
        if let saltOverride { salt = saltOverride }
        var clientNonce = Data(repeating: 0, count: 16)
        _ = SecRandomCopyBytes(kSecRandomDefault, clientNonce.count, &clientNonce)

        var context = HandshakeContext(
            host: host,
            port: port,
            master: master,
            kdf: kdf,
            iter: iter,
            salt: salt,
            clientNonce: clientNonce,
            helloPayload: Data(),
            serverName: connectedServerName,
            serverID: serverID,
            keychainAccounts: keychainAccounts,
            schedule: nil
        )

        let hello = HelloMessage(
            proto: OCP.protoIdentifier,
            kdf: kdf,
            iter: iter,
            salt: Hex.encode(salt),
            nonce: Hex.encode(clientNonce),
            device: DeviceInfo(
                id: settings.deviceId,
                name: String(UIDevice.current.name.prefix(64)),
                os: "ios",
                app: Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.0.0"
            )
        )
        do {
            context.helloPayload = try OCPJSON.encode(hello)
        } catch {
            fail(with: String(localized: "error.internal"))
            return
        }
        handshake = context

        let client = ControlClient()
        client.onOpen = { [weak self] in
            Task { @MainActor in
                guard let self else { return }
                self.control?.send(payload: context.helloPayload)
            }
        }
        client.onFrame = { [weak self] frame in
            Task { @MainActor in
                self?.handle(frame: frame)
            }
        }
        client.onClose = { [weak self] error in
            Task { @MainActor in
                self?.handleClose(error: error)
            }
        }
        control = client
        client.connect(host: host, port: port)
    }

    func disconnect() {
        userInitiatedDisconnect = true
        cancelReconnect()
        stopStreamProduction()
        if streamActive {
            streamActive = false
        }
        sendBye()
        control?.disconnect()
        control = nil
        media?.cancel()
        media = nil
        pump = nil
        handshake = nil
        phase = .idle
        UIApplication.shared.isIdleTimerDisabled = false
        startDiscovery()
    }

    // MARK: - Handshake / control frames

    private func handle(frame: ControlFrame) {
        let message: OCPMessage
        do {
            message = try OCPMessage.decode(payload: frame.payload)
        } catch {
            fail(with: String(localized: "error.internal"), close: true)
            return
        }
        switch message {
        case .challenge(let challenge):
            handleChallenge(challenge, rawPayload: frame.payload)
        case .authOK(let authOK):
            handleAuthOK(authOK)
        case .config(let config):
            apply(config: config)
        case .ping(let ping):
            control?.send(payload: (try? OCPJSON.encode(PongMessage(t: ping.t))) ?? Data())
        case .pong:
            break
        case .hint(let hint):
            handleHint(hint)
        case .error(let error):
            switch error.code {
            case "pairing_failed":
                lastConnectionError = String(localized: "error.pairingFailed")
            case "auth_failed":
                lastConnectionError = String(localized: "error.authFailed")
            default:
                lastConnectionError = error.msg ?? String(localized: "status.error")
            }
            control?.disconnect()
        case .bye:
            control?.disconnect()
        case .hello, .auth, .ready, .stats, .unknown:
            break
        }
    }

    private func handleChallenge(_ challenge: ChallengeMessage, rawPayload: Data) {
        guard var context = handshake else { return }
        guard challenge.kdf == context.kdf, challenge.iter == context.iter else {
            sendErrorAndClose(code: "protocol_error")
            return
        }
        guard let serverNonce = Hex.decode(challenge.nonce), serverNonce.count == 16 else {
            sendErrorAndClose(code: "protocol_error")
            return
        }
        let schedule = KeySchedule(
            master: context.master,
            clientNonce: context.clientNonce,
            serverNonce: serverNonce,
            helloPayload: context.helloPayload,
            challengePayload: rawPayload
        )
        context.schedule = schedule
        handshake = context
        let proof = Hex.encode(schedule.phoneProof(clientNonce: context.clientNonce, serverNonce: serverNonce))
        let auth = AuthMessage(nonce: Hex.encode(context.clientNonce), proof: proof)
        control?.send(payload: (try? OCPJSON.encode(auth)) ?? Data())
        phase = .handshaking
    }

    private func handleAuthOK(_ authOK: AuthOKMessage) {
        guard var context = handshake, let schedule = context.schedule,
              let serverNonceData = schedule.transcript.isEmpty ? nil : nil else {
            fail(with: String(localized: "error.authFailed"), close: true)
            return
        }
        _ = serverNonceData
        // Verify the server proof with k_auth over "obs" || server || client || transcript.
        // The server nonce is not re-sent; recompute it from the handshake context.
        guard let serverNonce = handshakeServerNonce() else {
            fail(with: String(localized: "error.authFailed"), close: true)
            return
        }
        let expected = Hex.encode(schedule.obsProof(serverNonce: serverNonce, clientNonce: context.clientNonce))
        guard expected == authOK.proof else {
            sendErrorAndClose(code: "auth_failed")
            return
        }
        guard let session = Hex.decode(authOK.session), session.count == 16 else {
            sendErrorAndClose(code: "protocol_error")
            return
        }
        guard authOK.mediaPort > 0, authOK.mediaPort <= 65535,
              let port = UInt16(exactly: authOK.mediaPort) else {
            sendErrorAndClose(code: "protocol_error")
            return
        }

        // Persist the master for auto-reconnect once pairing succeeded.
        for account in context.keychainAccounts {
            try? KeychainStore.save(secret: context.master, account: account)
        }

        let sendCipher = ControlCipher(encKey: schedule.kCtrlEnc, macKey: schedule.kCtrlMac)
        let receiveCipher = ControlCipher(encKey: schedule.kCtrlEnc, macKey: schedule.kCtrlMac)
        installCiphers(send: sendCipher, receive: receiveCipher)

        let sessionTag = Data(session.prefix(8))
        let mediaSender = MediaSender(host: context.host, port: port)
        media = mediaSender
        pump = MediaStreamPump(
            sender: mediaSender,
            crypto: MediaStreamPump.Crypto(
                encKey: schedule.kMediaEnc,
                macKey: schedule.kMediaMac,
                sessionTag: sessionTag
            )
        )

        handshake = context
        lastConnectionError = nil
        phase = .connected
        stopDiscovery()
        reconnectAttempt = 0

        control?.send(payload: (try? OCPJSON.encode(ReadyMessage(caps: buildCapabilities()))) ?? Data())
    }

    private func handshakeServerNonce() -> Data? {
        // The server nonce arrived in the challenge frame; keep a copy during
        // the challenge handshake for the auth_ok proof check.
        storedServerNonce
    }

    private var storedServerNonce: Data?

    private func installCiphers(send: ControlCipher, receive: ControlCipher) {
        // ControlClient owns one cipher for both directions (the phone's send
        // and receive counters are independent instances per PROTOCOL.md §3.1,
        // so wrap both in one adapter).
        let combined = CombinedControlCipher(send: send, receive: receive)
        control?.installCipher(combined)
    }

    private func sendErrorAndClose(code: String) {
        let error = ErrorMessage(code: code, msg: nil)
        control?.send(payload: (try? OCPJSON.encode(error)) ?? Data())
        control?.disconnect()
    }

    private func sendBye() {
        guard phase == .connected || phase == .streaming else { return }
        let bye = ByeMessage(reason: "client_disconnect")
        control?.send(payload: (try? OCPJSON.encode(bye)) ?? Data())
    }

    private func handleClose(error: Error?) {
        if streamActive {
            stopStreamProduction()
            streamActive = false
        }
        media?.cancel()
        media = nil
        pump = nil
        UIApplication.shared.isIdleTimerDisabled = false
        if userInitiatedDisconnect {
            phase = .idle
            startDiscovery()
            return
        }
        if let error {
            lastConnectionError = String(format: String(localized: "error.network"), "\(error.localizedDescription)")
        } else {
            lastConnectionError = String(localized: "status.error")
        }
        phase = .idle
        startDiscovery()
        scheduleReconnect()
    }

    // MARK: - Reconnect with exponential backoff

    private func scheduleReconnect() {
        guard handshake != nil else { return }
        reconnectAttempt += 1
        let delay = min(0.5 * pow(2.0, Double(min(reconnectAttempt - 1, 5))), 15.0)
        reconnectTask?.cancel()
        reconnectTask = Task { [weak self] in
            try? await Task.sleep(nanoseconds: UInt64(delay * 1_000_000_000))
            guard let self, !Task.isCancelled else { return }
            guard let context = self.handshake else { return }
            self.beginConnect(
                host: context.host,
                port: context.port,
                master: context.master,
                kdf: context.kdf,
                iter: context.iter,
                serverName: context.serverName,
                serverID: context.serverID,
                keychainAccounts: context.keychainAccounts
            )
        }
    }

    private func cancelReconnect() {
        reconnectTask?.cancel()
        reconnectTask = nil
    }

    // MARK: - Configuration application

    private func apply(config: ConfigMessage) {
        var videoChanged = false
        if let video = config.video {
            videoChanged = video != currentVideo
            currentVideo = video
            settings.width = video.width
            settings.height = video.height
            settings.fps = video.fps
            settings.bitrateKbps = video.bitrateKbps
            settings.orientationMode = video.orientation
            if video.camera == CameraController.backID || video.camera == CameraController.frontID {
                settings.cameraID = video.camera
                camera.selectCamera(id: video.camera)
            } else if video.camera == "toggle" {
                camera.toggleCamera()
                settings.cameraID = camera.currentCameraID
            }
            camera.setOrientationMode(CameraController.OrientationMode(rawValue: video.orientation) ?? .auto)
        }
        if let cameraConfig = config.camera {
            currentCamera = cameraConfig
            settings.torch = cameraConfig.torch
            settings.focusMode = cameraConfig.focusMode
            settings.focusDistance = cameraConfig.focusDistance
            settings.exposureMode = cameraConfig.exposureMode
            settings.exposureBias = cameraConfig.exposureBias
            settings.wbMode = cameraConfig.wbMode
            settings.wbKelvin = cameraConfig.wbKelvin
            camera.setFocus(
                mode: CameraController.FocusMode(rawValue: cameraConfig.focusMode) ?? .auto,
                distance: cameraConfig.focusDistance
            )
            camera.setExposure(
                mode: CameraController.ExposureMode(rawValue: cameraConfig.exposureMode) ?? .auto,
                bias: cameraConfig.exposureBias
            )
            camera.setWhiteBalance(
                mode: CameraController.WhiteBalanceMode(rawValue: cameraConfig.wbMode) ?? .auto,
                kelvin: cameraConfig.wbKelvin
            )
            camera.setTorch(cameraConfig.torch)
        }
        if let audio = config.audio {
            currentAudio = audio
            settings.audioEnabled = audio.enabled
            updateAudioAttachment()
        }
        if let stream = config.stream {
            if stream.active != streamActive {
                streamActive = stream.active
                if stream.active {
                    startStreamProduction()
                } else {
                    stopStreamProduction()
                }
            } else if stream.active && videoChanged {
                reconfigureEncoder()
            }
        }
        encoderTargetKbps = currentVideo.bitrateKbps > 0
            ? currentVideo.bitrateKbps
            : H264Encoder.defaultBitrate(width: currentVideo.width, height: currentVideo.height, fps: currentVideo.fps)
    }

    // MARK: - Stream production

    private func startStreamProduction() {
        streamClock.start()
        audioCapture.reset(t0HostNs: streamClock.t0HostNs)
        camera.start()
        reconfigureEncoder()
        updateAudioAttachment()
        phase = .streaming
        UIApplication.shared.isIdleTimerDisabled = true
        startStatsLoop()
    }

    private func stopStreamProduction() {
        statsTask?.cancel()
        statsTask = nil
        encoder.stop()
        encoderConfigured = false
        audioCapture.detach(from: camera.session)
        streamClock.stop()
        phase = (control != nil) ? .connected : .idle
        UIApplication.shared.isIdleTimerDisabled = false
    }

    private func reconfigureEncoder() {
        camera.selectCamera(id: settings.cameraID)
        camera.setOrientationMode(CameraController.OrientationMode(rawValue: currentVideo.orientation) ?? .auto)
        guard let actual = camera.applyFormat(
            width: currentVideo.width,
            height: currentVideo.height,
            fps: currentVideo.fps
        ) else { return }
        let bitrate = currentVideo.bitrateKbps > 0
            ? currentVideo.bitrateKbps
            : H264Encoder.defaultBitrate(width: actual.width, height: actual.height, fps: currentVideo.fps)
        encoderTargetKbps = bitrate
        encoder.start(
            width: actual.width,
            height: actual.height,
            fps: currentVideo.fps,
            bitrateKbps: bitrate,
            gopSeconds: currentVideo.gopSeconds
        )
        encoderConfigured = true
    }

    private func updateAudioAttachment() {
        if streamActive && currentAudio.enabled {
            audioCapture.attach(to: camera.session)
        } else {
            audioCapture.detach(from: camera.session)
        }
    }

    private func startStatsLoop() {
        statsTask?.cancel()
        statsTask = Task { [weak self] in
            while !Task.isCancelled {
                try? await Task.sleep(nanoseconds: 1_000_000_000)
                guard let self, !Task.isCancelled else { return }
                self.sendStatsTick()
            }
        }
    }

    private func sendStatsTick() {
        guard phase == .streaming else { return }
        let window = pump?.takeWindowCounters() ?? (frames: 0, bytes: 0)
        let frames = pump?.totalFrames ?? 0
        totalFramesSent = frames
        liveFPS = Double(window.frames)
        liveBitrateKbps = window.bytes * 8 / 1000
        batteryLevel = UIDevice.current.batteryLevel
        thermal = ProcessInfo.processInfo.thermalState
        let battery = batteryLevel < 0 ? 0 : Int((batteryLevel * 100).rounded())
        let stats = StatsMessage(
            fps: (Double(window.frames) * 10).rounded() / 10,
            bitrateKbps: liveBitrateKbps,
            frames: frames,
            battery: battery,
            thermal: Self.thermalString(thermal),
            camera: camera.currentCameraID,
            tsRelNs: streamClock.elapsedNs()
        )
        control?.send(payload: (try? OCPJSON.encode(stats)) ?? Data())
    }

    static func thermalString(_ state: ProcessInfo.ThermalState) -> String {
        switch state {
        case .nominal: return "nominal"
        case .fair: return "fair"
        case .serious: return "serious"
        case .critical: return "critical"
        @unknown default: return "nominal"
        }
    }

    // MARK: - Adaptive bitrate (PROTOCOL.md §3.2 hint rules)

    private func handleHint(_ hint: HintMessage) {
        liveRTTMs = hint.rttMs
        rttMs = hint.rttMs
        guard streamActive, encoderConfigured else { return }
        let now = Date()
        guard now.timeIntervalSince(lastAdaptTime) >= 2 else { return }
        if hint.lossPct > 5 {
            lowLossSeconds = 0
            let reduced = max(Int(Double(encoderTargetKbps) * 0.8), 200)
            encoderTargetKbps = reduced
            encoder.setBitrate(reduced)
            lastAdaptTime = now
        } else if hint.lossPct < 1 {
            lowLossSeconds += 1
            if lowLossSeconds >= 10 {
                lowLossSeconds = 0
                let configured = currentVideo.bitrateKbps > 0
                    ? currentVideo.bitrateKbps
                    : H264Encoder.defaultBitrate(width: currentVideo.width, height: currentVideo.height, fps: currentVideo.fps)
                let increased = min(Int(Double(encoderTargetKbps) * 1.1), configured)
                if increased > encoderTargetKbps {
                    encoderTargetKbps = increased
                    encoder.setBitrate(increased)
                    lastAdaptTime = now
                }
            }
        } else {
            lowLossSeconds = 0
        }
    }

    // MARK: - Capabilities

    private func buildCapabilities() -> Capabilities {
        var cameras: [CameraCapability] = []
        if CameraController.device(for: .back) != nil {
            cameras.append(CameraCapability(id: CameraController.backID, label: String(localized: "control.back")))
        }
        if CameraController.device(for: .front) != nil {
            cameras.append(CameraCapability(id: CameraController.frontID, label: String(localized: "control.front")))
        }
        let resolutions: [[Int]] = [[3840, 2160], [1920, 1080], [1280, 720], [640, 360]]
        return Capabilities(
            video: VideoCapabilities(
                codec: ["h264"],
                resolutions: resolutions,
                fps: [60, 30, 24],
                aspectRatios: ["4:3", "16:9"],
                maxBitrateKbps: 50_000
            ),
            audio: AudioCapabilities(codecs: ["pcm_s16le"], sampleRates: [48_000], channels: [1]),
            cameras: cameras,
            features: FeatureCapabilities(torch: true, manualFocus: true, exposure: true, whiteBalance: true)
        )
    }

    // MARK: - Local controls

    func toggleTorch() {
        settings.torch.toggle()
        camera.setTorch(settings.torch)
        currentCamera.torch = settings.torch
    }

    func switchCamera() {
        camera.toggleCamera()
        settings.cameraID = camera.currentCameraID
        currentVideo.camera = camera.currentCameraID
    }

    func setLocalResolution(width: Int, height: Int) {
        settings.width = width
        settings.height = height
        currentVideo.width = width
        currentVideo.height = height
        if streamActive { reconfigureEncoder() }
    }

    func setLocalFPS(_ fps: Int) {
        settings.fps = fps
        currentVideo.fps = fps
        if streamActive { reconfigureEncoder() }
    }

    func setLocalAudioEnabled(_ enabled: Bool) {
        settings.audioEnabled = enabled
        currentAudio.enabled = enabled
        updateAudioAttachment()
    }

    func setLocalFocus(mode: String, distance: Double? = nil) {
        settings.focusMode = mode
        currentCamera.focusMode = mode
        if let distance {
            settings.focusDistance = distance
            currentCamera.focusDistance = distance
        }
        camera.setFocus(mode: CameraController.FocusMode(rawValue: mode) ?? .auto, distance: settings.focusDistance)
    }

    func setLocalExposure(mode: String, bias: Double? = nil) {
        settings.exposureMode = mode
        currentCamera.exposureMode = mode
        if let bias {
            settings.exposureBias = bias
            currentCamera.exposureBias = bias
        }
        camera.setExposure(mode: CameraController.ExposureMode(rawValue: mode) ?? .auto, bias: settings.exposureBias)
    }

    func setLocalWhiteBalance(mode: String, kelvin: Int? = nil) {
        settings.wbMode = mode
        currentCamera.wbMode = mode
        if let kelvin {
            settings.wbKelvin = kelvin
            currentCamera.wbKelvin = kelvin
        }
        camera.setWhiteBalance(mode: CameraController.WhiteBalanceMode(rawValue: mode) ?? .auto, kelvin: settings.wbKelvin)
    }

    func forgetPaired(host: String, port: UInt16, serverID: String?) {
        for account in keychainAccounts(host: host, port: port, serverID: serverID) {
            KeychainStore.delete(account: account)
        }
    }

    private func fail(with message: String, close: Bool = true) {
        lastConnectionError = message
        if close {
            control?.disconnect()
        }
    }
}

// MARK: - Stream clock

/// Monotonic stream-start clock shared across capture queues.
final class StreamClock {
    private let lock = NSLock()
    private var t0: Int64 = 0
    private var running = false

    var t0HostNs: Int64 {
        lock.lock()
        defer { lock.unlock() }
        return t0
    }

    func start() {
        lock.lock()
        t0 = HostClock.nowNs()
        running = true
        lock.unlock()
    }

    func stop() {
        lock.lock()
        running = false
        lock.unlock()
    }

    func elapsedNs() -> Int64 {
        lock.lock()
        defer { lock.unlock() }
        guard running else { return 0 }
        return HostClock.nowNs() - t0
    }
}

// MARK: - Media pump

/// Seals and sends media frames off the main thread; owns per-stream frame
/// counters (global per stream, starting at 1 — PROTOCOL.md §4) and the
/// byte/frame counters consumed by the stats loop.
final class MediaStreamPump {
    struct Crypto {
        let encKey: Data
        let macKey: Data
        let sessionTag: Data
    }

    private let sender: MediaSender
    private let crypto: Crypto
    private let lock = NSLock()
    private var videoFrameID: UInt32 = 0
    private var audioFrameID: UInt32 = 0
    private var windowFrames = 0
    private var windowBytes = 0
    private var totalFrames = 0

    init(sender: MediaSender, crypto: Crypto) {
        self.sender = sender
        self.crypto = crypto
    }

    var totalFramesSent: Int {
        lock.lock()
        defer { lock.unlock() }
        return totalFrames
    }

    var totalFrames: Int { totalFramesSent }

    func sendVideo(annexB: Data, isKeyframe: Bool, timestampNs: Int64) {
        lock.lock()
        videoFrameID &+= 1
        let frameID = videoFrameID
        lock.unlock()
        send(payload: annexB, stream: OCP.streamVideo, isKeyframe: isKeyframe, frameID: frameID, timestampNs: timestampNs)
    }

    func sendAudio(pcm: Data, timestampNs: Int64) {
        lock.lock()
        audioFrameID &+= 1
        let frameID = audioFrameID
        lock.unlock()
        send(payload: pcm, stream: OCP.streamAudio, isKeyframe: false, frameID: frameID, timestampNs: timestampNs)
    }

    private func send(payload: Data, stream: UInt8, isKeyframe: Bool, frameID: UInt32, timestampNs: Int64) {
        do {
            let datagrams = try MediaPacket.sealFrame(
                encKey: crypto.encKey,
                macKey: crypto.macKey,
                sessionTag: crypto.sessionTag,
                stream: stream,
                isKeyframe: isKeyframe,
                frameID: frameID,
                timestamp: UInt64(max(0, timestampNs)),
                payload: payload
            )
            lock.lock()
            windowFrames += 1
            windowBytes += datagrams.reduce(0) { $0 + $1.count }
            totalFrames += 1
            lock.unlock()
            for datagram in datagrams {
                sender.send(datagram)
            }
        } catch {
            // Drop frames that fail to seal (oversized/corrupt); the stats
            // loop and server-side loss metrics will surface the problem.
        }
    }

    /// Returns frames/bytes sent since the previous call (1 s stats window).
    func takeWindowCounters() -> (frames: Int, bytes: Int) {
        lock.lock()
        defer { lock.unlock() }
        let result = (frames: windowFrames, bytes: windowBytes)
        windowFrames = 0
        windowBytes = 0
        return result
    }
}

/// Pairs a send-cipher and a receive-cipher under the ControlCipher interface
/// so ControlClient can keep a single installed cipher object.
final class CombinedControlCipher {
    private let send: ControlCipher
    private let receive: ControlCipher

    init(send: ControlCipher, receive: ControlCipher) {
        self.send = send
        self.receive = receive
    }

    func seal(payload: Data) throws -> ControlFrame {
        try send.seal(payload: payload)
    }

    func open(_ frame: ControlFrame) throws -> Data {
        try receive.open(frame)
    }
}
