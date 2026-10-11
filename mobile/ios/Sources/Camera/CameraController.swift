import AVFoundation
import Foundation
import UIKit

/// AVCaptureSession pipeline: device input, format/fps selection, torch,
/// focus, exposure, white balance, orientation, and video sample delivery.
final class CameraController: NSObject {
    enum FocusMode: String {
        case auto, continuous, manual
    }

    enum ExposureMode: String {
        case auto, manual
    }

    enum WhiteBalanceMode: String {
        case auto, manual
    }

    enum OrientationMode: String {
        case auto, portrait, landscape
    }

    static let backID = "back"
    static let frontID = "front"

    let session = AVCaptureSession()
    var videoSampleHandler: ((CMSampleBuffer) -> Void)?

    private let sessionQueue = DispatchQueue(label: "com.obsphonecamera.session")
    private let outputQueue = DispatchQueue(label: "com.obsphonecamera.video.output")
    private let sampleProxy = VideoSampleProxy()
    private var videoInput: AVCaptureDeviceInput?
    private var videoOutput: AVCaptureVideoDataOutput?
    private var device: AVCaptureDevice?
    private(set) var position: AVCaptureDevice.Position = .back
    private var orientationMode: OrientationMode = .auto
    private var lastDeviceOrientation: UIDeviceOrientation = .portrait
    private var observers: [NSObjectProtocol] = []
    private var isConfigured = false

    var currentCameraID: String {
        position == .front ? Self.frontID : Self.backID
    }

    override init() {
        super.init()
        sampleProxy.handler = { [weak self] buffer in
            self?.videoSampleHandler?(buffer)
        }
    }

    deinit {
        stop()
        observers.forEach(NotificationCenter.default.removeObserver(_:))
    }

    // MARK: - Device discovery

    static func device(for position: AVCaptureDevice.Position) -> AVCaptureDevice? {
        let deviceTypes: [AVCaptureDevice.DeviceType] = [
            .builtInWideAngleCamera,
            .builtInTripleCamera,
            .builtInDualWideCamera,
            .builtInDualCamera,
        ]
        let discovery = AVCaptureDeviceDiscoverySession(
            deviceTypes: deviceTypes,
            mediaType: .video,
            position: position
        )
        return discovery.devices.first
    }

    static func availableResolutions(fps: Int) -> [(width: Int, height: Int)] {
        guard let device = device(for: .back) ?? device(for: .front) else { return [] }
        var seen = Set<String>()
        var result: [(Int, Int)] = []
        for format in device.formats where format.mediaType == .video {
            let dimensions = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
            let fpsSupported = format.videoSupportedFrameRateRanges.contains {
                Double(fps) >= $0.minFrameRate && Double(fps) <= $0.maxFrameRate
            }
            guard fpsSupported else { continue }
            let key = "\(dimensions.width)x\(dimensions.height)"
            if !seen.contains(key) {
                seen.insert(key)
                result.append((Int(dimensions.width), Int(dimensions.height)))
            }
        }
        return result.sorted { $0.0 * $0.1 < $1.0 * $1.1 }
    }

    // MARK: - Configuration

    /// Selects the camera at `position` and (re)builds the video input.
    func selectCamera(id: String) {
        sessionQueue.async { [self] in
            let target: AVCaptureDevice.Position = id == Self.frontID ? .front : .back
            guard let device = Self.device(for: target) else { return }
            position = target
            replaceInput(with: device)
        }
    }

    func toggleCamera() {
        sessionQueue.async { [self] in
            let target: AVCaptureDevice.Position = position == .back ? .front : .back
            guard let device = Self.device(for: target) else { return }
            position = target
            replaceInput(with: device)
        }
    }

    private func replaceInput(with device: AVCaptureDevice) {
        do {
            let input = try AVCaptureDeviceInput(device: device)
            session.beginConfiguration()
            if let existing = videoInput {
                session.removeInput(existing)
            }
            if session.canAddInput(input) {
                session.addInput(input)
                videoInput = input
                self.device = device
                session.commitConfiguration()
                configureOutputIfNeeded()
                updateTorchAvailability()
                return
            }
            session.commitConfiguration()
        } catch {
            session.commitConfiguration()
        }
    }

    /// Picks the smallest format with area ≥ requested area, clamps fps into
    /// the format's supported ranges, and returns the actually selected size.
    @discardableResult
    func applyFormat(width: Int, height: Int, fps: Int) -> (width: Int, height: Int)? {
        sessionQueue.sync { () -> (Int, Int)? in
            guard let device else { return nil }
            let requestedArea = max(1, width * height)
            var best: AVCaptureDevice.Format?
            var bestArea = Int.max
            var largest: AVCaptureDevice.Format?
            var largestArea = -1
            for format in device.formats where format.mediaType == .video {
                let dimensions = CMVideoFormatDescriptionGetDimensions(format.formatDescription)
                let area = Int(dimensions.width) * Int(dimensions.height)
                if area >= requestedArea && area < bestArea {
                    best = format
                    bestArea = area
                }
                if area > largestArea {
                    largest = format
                    largestArea = area
                }
            }
            let chosen = best ?? largest
            guard let chosen else { return nil }
            let dimensions = CMVideoFormatDescriptionGetDimensions(chosen.formatDescription)

            var clampedFPS = Double(fps)
            let ranges = chosen.videoSupportedFrameRateRanges
            if let range = ranges.first(where: { Double(fps) >= $0.minFrameRate && Double(fps) <= $0.maxFrameRate }) {
                clampedFPS = Double(fps)
                _ = range
            } else if let first = ranges.first {
                clampedFPS = min(max(Double(fps), first.minFrameRate), first.maxFrameRate)
            }

            do {
                try device.lockForConfiguration()
                if session.sessionPreset != .inputPriority {
                    session.beginConfiguration()
                    session.sessionPreset = .inputPriority
                    session.commitConfiguration()
                }
                device.activeFormat = chosen
                let duration = CMTime(value: 1, timescale: CMTimeScale(max(1, Int32(clampedFPS.rounded()))))
                device.activeVideoMinFrameDuration = duration
                device.activeVideoMaxFrameDuration = duration
                device.unlockForConfiguration()
            } catch {
                device.unlockForConfiguration()
                return nil
            }
            return (Int(dimensions.width), Int(dimensions.height))
        }
    }

    private func configureOutputIfNeeded() {
        session.beginConfiguration()
        let output = videoOutput ?? {
            let output = AVCaptureVideoDataOutput()
            output.videoSettings = [
                kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarFullRange,
            ]
            output.alwaysDiscardsLateVideoFrames = true
            output.setSampleBufferDelegate(sampleProxy, queue: outputQueue)
            return output
        }()
        if videoOutput == nil, session.canAddOutput(output) {
            session.addOutput(output)
            videoOutput = output
            isConfigured = true
        }
        session.commitConfiguration()
        applyOrientation()
    }

    func start() {
        sessionQueue.async { [self] in
            guard !session.isRunning else { return }
            session.startRunning()
        }
    }

    func stop() {
        sessionQueue.async { [self] in
            guard session.isRunning else { return }
            session.stopRunning()
        }
    }

    // MARK: - Orientation

    func setOrientationMode(_ mode: OrientationMode) {
        orientationMode = mode
        beginOrientationNotifications()
        applyOrientation()
    }

    private func beginOrientationNotifications() {
        guard observers.isEmpty else { return }
        UIDevice.current.beginGeneratingDeviceOrientationNotifications()
        let center = NotificationCenter.default
        observers.append(center.addObserver(
            forName: UIDevice.orientationDidChangeNotification,
            object: nil,
            queue: .main
        ) { [weak self] _ in
            guard let self else { return }
            let orientation = UIDevice.current.orientation
            if orientation.isValidInterfaceOrientation {
                self.lastDeviceOrientation = orientation
            }
            self.applyOrientation()
        })
    }

    private func applyOrientation() {
        guard let connection = videoOutput?.connection(with: .video), connection.isVideoOrientationSupported else { return }
        let target: AVCaptureVideoOrientation
        switch orientationMode {
        case .portrait:
            target = .portrait
        case .landscape:
            target = .landscapeRight
        case .auto:
            // UIDevice and AVCapture orientations are mirrored for landscape.
            switch lastDeviceOrientation {
            case .portrait:
                target = .portrait
            case .portraitUpsideDown:
                target = .portraitUpsideDown
            case .landscapeLeft:
                target = .landscapeRight
            case .landscapeRight:
                target = .landscapeLeft
            default:
                target = .portrait
            }
        }
        if connection.videoOrientation != target {
            connection.videoOrientation = target
        }
    }

    // MARK: - Torch

    private(set) var isTorchAvailable = false

    private func updateTorchAvailability() {
        isTorchAvailable = device?.hasTorch ?? false
    }

    func setTorch(_ on: Bool) {
        sessionQueue.async { [self] in
            guard let device, device.hasTorch else { return }
            do {
                try device.lockForConfiguration()
                if on {
                    try device.setTorchModeOn(level: 1.0)
                } else {
                    device.torchMode = .off
                }
                device.unlockForConfiguration()
            } catch {
                device.unlockForConfiguration()
            }
        }
    }

    // MARK: - Focus / exposure / white balance

    func setFocus(mode: FocusMode, distance: Double) {
        sessionQueue.async { [self] in
            guard let device else { return }
            do {
                try device.lockForConfiguration()
                switch mode {
                case .auto where device.isFocusModeSupported(.autoFocus):
                    device.focusMode = .autoFocus
                case .continuous where device.isFocusModeSupported(.continuousAutoFocus):
                    device.focusMode = .continuousAutoFocus
                case .manual where device.isFocusModeSupported(.manualFocus):
                    device.lensFocusMode = .manualFocus
                    device.lensPosition = Float(min(max(distance, 0), 1))
                case .auto, .continuous, .manual:
                    break
                }
                device.unlockForConfiguration()
            } catch {
                device.unlockForConfiguration()
            }
        }
    }

    func setExposure(mode: ExposureMode, bias: Double) {
        sessionQueue.async { [self] in
            guard let device else { return }
            let clampedBias = Float(min(max(bias, -2.0), 2.0))
            do {
                try device.lockForConfiguration()
                if device.isExposureTargetBiasSupported {
                    device.setExposureTargetBias(clampedBias, completionHandler: nil)
                }
                switch mode {
                case .auto:
                    if device.isExposureModeSupported(.continuousAutoExposure) {
                        device.exposureMode = .continuousAutoExposure
                    }
                case .manual:
                    // OCP/1 carries only a bias for manual exposure; apply the
                    // bias and then lock exposure at the resulting point.
                    if device.isExposureModeSupported(.locked) {
                        device.exposureMode = .locked
                    }
                }
                device.unlockForConfiguration()
            } catch {
                device.unlockForConfiguration()
            }
        }
    }

    func setWhiteBalance(mode: WhiteBalanceMode, kelvin: Int) {
        sessionQueue.async { [self] in
            guard let device, device.isWhiteBalanceModeSupported(.locked) else { return }
            do {
                try device.lockForConfiguration()
                switch mode {
                case .auto:
                    if device.isWhiteBalanceModeSupported(.continuousAutoWhiteBalance) {
                        device.whiteBalanceMode = .continuousAutoWhiteBalance
                    }
                case .manual:
                    let temperature = Float(min(max(kelvin, 2500), 10000))
                    let tempTint = AVWhiteBalanceTemperatureAndTintValues(temperature: temperature, tint: 0)
                    let gains = device.getWhiteBalanceGains(for: tempTint)
                    let clamped = AVCaptureDevice.WhiteBalanceGains(
                        redGain: min(max(gains.redGain, 1.0), 8.0),
                        greenGain: min(max(gains.greenGain, 1.0), 8.0),
                        blueGain: min(max(gains.blueGain, 1.0), 8.0)
                    )
                    try device.setWhiteBalanceGains(clamped)
                    device.whiteBalanceMode = .locked
                }
                device.unlockForConfiguration()
            } catch {
                device.unlockForConfiguration()
            }
        }
    }
}

/// Thin delegate forwarding captured video frames to a closure.
final class VideoSampleProxy: NSObject, AVCaptureVideoDataOutputSampleBufferDelegate {
    var handler: ((CMSampleBuffer) -> Void)?

    func captureOutput(
        _ output: AVCaptureOutput,
        didOutput sampleBuffer: CMSampleBuffer,
        from connection: AVCaptureConnection
    ) {
        handler?(sampleBuffer)
    }
}
