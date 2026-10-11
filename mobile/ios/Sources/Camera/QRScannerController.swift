import AVFoundation
import Foundation
import UIKit

/// Dedicated capture session for scanning the OBS pairing QR code (§6.1).
final class QRScannerController: NSObject, AVCaptureMetadataOutputObjectsDelegate {
    var onCode: ((String) -> Void)?

    let session = AVCaptureSession()
    private let sessionQueue = DispatchQueue(label: "com.obsphonecamera.qr.session")
    private let metadataQueue = DispatchQueue(label: "com.obsphonecamera.qr.metadata")
    private var preview: AVCaptureVideoPreviewLayer?
    private(set) var isRunning = false
    private var lastEmitted = ""
    private var lastEmissionTime: Date = .distantPast

    func attachPreview(to view: UIView) -> AVCaptureVideoPreviewLayer {
        if let existing = preview {
            existing.frame = view.bounds
            view.layer.insertSublayer(existing, at: 0)
            return existing
        }
        let layer = AVCaptureVideoPreviewLayer(session: session)
        layer.videoGravity = .resizeAspectFill
        layer.frame = view.bounds
        view.layer.insertSublayer(layer, at: 0)
        preview = layer
        return layer
    }

    func layoutPreview() {
        preview?.frame = preview?.superlayer?.bounds ?? .zero
    }

    func start() {
        sessionQueue.async { [self] in
            guard !isRunning else { return }
            session.beginConfiguration()
            if session.inputs.isEmpty,
               let device = AVCaptureDevice.default(.builtInWideAngleCamera, for: .video, position: .back) {
                do {
                    let input = try AVCaptureDeviceInput(device: device)
                    if session.canAddInput(input) {
                        session.addInput(input)
                    }
                } catch {
                    session.commitConfiguration()
                    return
                }
            }
            if session.outputs.isEmpty {
                let metadataOutput = AVCaptureMetadataOutput()
                if session.canAddOutput(metadataOutput) {
                    session.addOutput(metadataOutput)
                    metadataOutput.setMetadataObjectsDelegate(self, queue: metadataQueue)
                    metadataOutput.availableMetadataObjectTypes = [.qr]
                }
            }
            session.commitConfiguration()
            session.startRunning()
            isRunning = session.isRunning
        }
    }

    func stop() {
        sessionQueue.async { [self] in
            guard isRunning else { return }
            session.stopRunning()
            isRunning = false
        }
    }

    func metadataOutput(
        _ output: AVCaptureMetadataOutput,
        didOutput metadataObjects: [AVMetadataObject],
        from connection: AVCaptureConnection
    ) {
        guard
            let object = metadataObjects.first as? AVMetadataMachineReadableCodeObject,
            object.type == .qr,
            let value = object.stringValue
        else { return }
        // Debounce the same code while the sheet stays open.
        let now = Date()
        if value != lastEmitted || now.timeIntervalSince(lastEmissionTime) > 2 {
            lastEmitted = value
            lastEmissionTime = now
            DispatchQueue.main.async { [weak self] in
                self?.onCode?(value)
            }
        }
    }
}
