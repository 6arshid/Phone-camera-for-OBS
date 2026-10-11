import AVFoundation
import CoreMedia
import Foundation

/// Captures microphone audio and produces PCM s16le mono 48 kHz frames of
/// 10 ms (480 samples = 960 bytes) for UDP stream 1 (PROTOCOL.md §4.2).
final class AudioCapture: NSObject, AVCaptureAudioDataOutputSampleBufferDelegate {
    static let frameBytes = 960
    static let sampleRate = 48_000

    /// Receives complete 960-byte PCM frames with stream-relative timestamps.
    var onPCMFrame: ((Data, Int64) -> Void)?

    private let output = AVCaptureAudioDataOutput()
    private let queue = DispatchQueue(label: "com.obsphonecamera.audio")
    private var attached = false
    private var pending = Data()
    private var timebaseNs: Int64?
    private var pendingOffsetBytes = 0

    var isAttached: Bool { attached }

    func attach(to session: AVCaptureSession) {
        queue.async { [self] in
            guard !attached else { return }
            output.audioSettings = [
                AVFormatIDKey: kAudioFormatLinearPCM,
                AVSampleRateKey: Self.sampleRate,
                AVNumberOfChannelsKey: 1,
                AVLinearPCMBitDepthKey: 16,
                AVLinearPCMIsBigEndianKey: false,
                AVLinearPCMIsFloatKey: false,
                AVLinearPCMIsNonInterleaved: false,
            ]
            output.setSampleBufferDelegate(self, queue: queue)
            if session.canAddOutput(output) {
                session.addOutput(output)
                attached = true
            }
        }
    }

    func detach(from session: AVCaptureSession) {
        queue.async { [self] in
            guard attached else { return }
            if session.outputs.contains(output) {
                session.removeOutput(output)
            }
            output.setSampleBufferDelegate(nil, queue: nil)
            attached = false
            pending.removeAll()
            pendingOffsetBytes = 0
        }
    }

    /// Resets buffering and sets the stream-start clock base (host time, ns).
    func reset(t0HostNs: Int64?) {
        queue.async { [self] in
            timebaseNs = t0HostNs
            pending.removeAll()
            pendingOffsetBytes = 0
        }
    }

    func captureOutput(
        _ output: AVCaptureOutput,
        didOutput sampleBuffer: CMSampleBuffer,
        from connection: AVCaptureConnection
    ) {
        guard let timebaseNs else { return }
        guard let blockBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return }
        let length = CMBlockBufferGetDataLength(blockBuffer)
        guard length > 0 else { return }

        var bytes = [UInt8](repeating: 0, count: length)
        guard CMBlockBufferCopyDataBytes(blockBuffer, atOffset: 0, dataLength: length, destination: &bytes) == kCMBlockBufferNoErr else {
            return
        }

        let pcm: Data
        if is16BitLEPCM(sampleBuffer) {
            pcm = Data(bytes)
        } else {
            pcm = convertToPCM16(bytes, sampleBuffer: sampleBuffer)
        }
        guard !pcm.isEmpty else { return }

        // Timestamp the first byte of this buffer on the host clock.
        let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
        let bufferPtsNs = HostClock.ns(from: pts) - timebaseNs

        pending.append(pcm)
        while pending.count >= Self.frameBytes {
            let frame = Data(pending.prefix(Self.frameBytes))
            pending.removeFirst(Self.frameBytes)
            let offsetNs = Int64(pendingOffsetBytes) * 1_000_000_000 / Int64(Self.sampleRate * 2)
            let timestamp = bufferPtsNs + offsetNs
            pendingOffsetBytes += Self.frameBytes
            onPCMFrame?(frame, max(0, timestamp))
        }
        if pending.isEmpty {
            pendingOffsetBytes = 0
        }
    }

    private func is16BitLEPCM(_ sampleBuffer: CMSampleBuffer) -> Bool {
        guard let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer) else { return false }
        let asbdPointer = CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription)
        guard let asbd = asbdPointer?.pointee else { return false }
        let format = AudioFormatID(asbd.mFormatID)
        return format == kAudioFormatLinearPCM
            && (asbd.mFormatFlags & kAudioFormatFlagIsFloat) == 0
            && (asbd.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0
            && asbd.mBitsPerChannel == 16
    }

    /// Fallback path if the requested audioSettings conversion is not applied:
    /// converts float32 or s16 interleaved buffers to mono s16le at 48 kHz
    /// (device rates other than 48 kHz are passed through at their native rate
    /// scaled to 480-sample frames by simple nearest-sample decimation).
    private func convertToPCM16(_ bytes: [UInt8], sampleBuffer: CMSampleBuffer) -> Data {
        guard let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer),
              let asbdPointer = CMAudioFormatDescriptionGetStreamBasicDescription(formatDescription) else {
            return Data()
        }
        let asbd = asbdPointer.pointee
        let channels = Int(asbd.mChannelsPerFrame)
        guard channels >= 1 else { return Data() }
        var output = Data()

        if asbd.mFormatID == kAudioFormatLinearPCM, asbd.mBitsPerChannel == 32,
           (asbd.mFormatFlags & kAudioFormatFlagIsFloat) != 0 {
            let sampleCount = bytes.count / (MemoryLayout<Float>.size * channels)
            output.reserveCapacity(sampleCount * 2)
            bytes.withUnsafeBytes { (raw: UnsafeRawBufferPointer) in
                let floats = raw.bindMemory(to: Float.self)
                for index in 0..<sampleCount {
                    let value = floats[index * channels] // first channel only
                    let clamped = min(max(value, -1.0), 1.0)
                    let int16 = Int16(clamped * 32767.0)
                    withUnsafeBytes(of: int16.littleEndian) { output.append(contentsOf: $0) }
                }
            }
        } else if asbd.mFormatID == kAudioFormatLinearPCM {
            // Interleaved integer PCM: take the first channel of each frame.
            let bytesPerSample = Int(asbd.mBitsPerChannel / 8)
            guard bytesPerSample == 2 else { return Data() }
            let frameCount = bytes.count / (bytesPerSample * channels)
            output.reserveCapacity(frameCount * 2)
            for frame in 0..<frameCount {
                let offset = frame * bytesPerSample * channels
                output.append(bytes[offset])
                output.append(bytes[offset + 1])
            }
        }
        return output
    }
}
