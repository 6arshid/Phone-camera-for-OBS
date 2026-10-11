import CoreMedia
import CoreVideo
import Foundation
import VideoToolbox

/// Real-time H.264 encoder on VTCompressionSession (PROTOCOL.md §4.1).
/// Emits Annex-B access units; keyframes carry SPS/PPS ahead of the first slice.
final class H264Encoder {
    /// Invoked on an internal VideoToolbox queue with one Annex-B access unit.
    var onAccessUnit: ((_ annexB: Data, _ isKeyframe: Bool, _ timestampNs: Int64) -> Void)?

    private var session: VTCompressionSession?
    private let queue = DispatchQueue(label: "com.obsphonecamera.encoder")
    private var pendingTimestamps: [Int64] = []
    private(set) var width = 0
    private(set) var height = 0
    private(set) var fps = 30
    private(set) var bitrateKbps = 4000
    private(set) var gopSeconds = 2.0

    private enum Profile: CaseIterable {
        case high, main, baseline

        var level: CMVideoCodecProfileLevel {
            switch self {
            case .high: return kVTProfileLevel_H264_High_AutoLevel
            case .main: return kVTProfileLevel_H264_Main_AutoLevel
            case .baseline: return kVTProfileLevel_H264_Baseline_AutoLevel
            }
        }
    }

    var isRunning: Bool { session != nil }

    func start(width: Int, height: Int, fps: Int, bitrateKbps: Int, gopSeconds: Double) {
        queue.sync { [self] in
            stopLocked()
            self.width = width
            self.height = height
            self.fps = max(1, fps)
            self.bitrateKbps = bitrateKbps > 0 ? bitrateKbps : Self.defaultBitrate(width: width, height: height, fps: max(1, fps))
            self.gopSeconds = max(0.5, gopSeconds)
            // Prefer High profile, fall back Main → Baseline for older devices.
            for profile in Profile.allCases {
                if createSession(profile: profile) { break }
            }
        }
    }

    func stop() {
        queue.sync { stopLocked() }
    }

    private func stopLocked() {
        if let session {
            VTCompressionSessionCompleteFrames(session, untilPresentationTimeStamp: .invalid)
            VTCompressionSessionInvalidate(session)
        }
        session = nil
        pendingTimestamps.removeAll()
    }

    func setBitrate(_ kbps: Int) {
        queue.async { [self] in
            guard let session, kbps > 0 else { return }
            bitrateKbps = kbps
            VTSessionSetProperty(
                session,
                key: kVTCompressionPropertyKey_AverageBitRate,
                value: NSNumber(value: kbps * 1000)
            )
        }
    }

    static func defaultBitrate(width: Int, height: Int, fps: Int) -> Int {
        // ~0.07 bits per pixel per frame, clamped to sane bounds.
        let raw = Double(width * height * fps) * 0.07 / 1000.0
        return Int(min(max(raw, 500), 50_000))
    }

    private func createSession(profile: Profile) -> Bool {
        var compressionSession: VTCompressionSession?
        let status = VTCompressionSessionCreate(
            allocator: nil,
            width: Int32(width),
            height: Int32(height),
            codecType: kCMVideoCodecType_H264,
            encoderSpecification: nil,
            imageBufferAttributes: nil,
            compressedDataAllocator: nil,
            outputCallback: encodedFrameCallback,
            refcon: Unmanaged.passUnretained(self).toOpaque(),
            compressionSessionOut: &compressionSession
        )
        guard status == noErr, let compressionSession else { return false }

        let properties: [(CFString, Any)] = [
            (kVTCompressionPropertyKey_RealTime, kCFBooleanTrue as Any),
            (kVTCompressionPropertyKey_AllowFrameReordering, kCFBooleanFalse as Any),
            (kVTCompressionPropertyKey_AverageBitRate, NSNumber(value: bitrateKbps * 1000)),
            (kVTCompressionPropertyKey_MaxKeyFrameIntervalDuration, NSNumber(value: gopSeconds)),
            (kVTCompressionPropertyKey_ExpectedFrameRate, NSNumber(value: fps)),
            (kVTCompressionPropertyKey_ProfileLevel, profile.level),
        ]
        for (key, value) in properties {
            let propertyStatus = VTSessionSetProperty(compressionSession, key: key, value: value as CFTypeRef)
            if propertyStatus == kVTPropertyKeyNotSupportedErr || propertyStatus == kVTParameterErr {
                // ProfileLevel unsupported on this device — try the next profile.
                if key == kVTCompressionPropertyKey_ProfileLevel {
                    VTCompressionSessionInvalidate(compressionSession)
                    return false
                }
            }
        }
        VTCompressionSessionPrepareToEncodeFrames(compressionSession)
        session = compressionSession
        return true
    }

    /// Encodes one captured frame; `timestampNs` is its stream-relative
    /// timestamp and is delivered unchanged with the encoded access unit.
    func encode(sampleBuffer: CMSampleBuffer, timestampNs: Int64) {
        queue.async { [self] in
            guard let session,
                  let pixelBuffer = CMSampleBufferGetImageBuffer(sampleBuffer) else { return }
            pendingTimestamps.append(timestampNs)
            let pts = CMSampleBufferGetPresentationTimeStamp(sampleBuffer)
            let status = VTCompressionSessionEncodeFrame(
                session,
                imageBuffer: pixelBuffer,
                presentationTimeStamp: pts,
                duration: .invalid,
                frameProperties: nil,
                infoFlagsOut: nil
            )
            if status != noErr {
                pendingTimestamps.removeLast()
            }
        }
    }

    fileprivate func handleEncoded(
        status: OSStatus,
        sampleBuffer: CMSampleBuffer?
    ) {
        let timestamp = pendingTimestamps.isEmpty ? 0 : pendingTimestamps.removeFirst()
        guard status == noErr, let sampleBuffer else { return }

        let attachments = CMSampleBufferGetSampleAttachmentsArray(sampleBuffer, createIfNecessary: false) as? [[CFString: Any]]
        let isKeyframe = !(attachments?.first?[kVTEncodeFrameAttachmentKey_DependsOnOthers] as? Bool ?? true)

        guard let formatDescription = CMSampleBufferGetFormatDescription(sampleBuffer) else { return }
        var nalHeaderLength: Int32 = 0
        var parameterSetCount = 0
        var spsPointer: UnsafePointer<UInt8>?
        var spsSize = 0
        let spsStatus = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription,
            0,
            parameterSetPointer: &spsPointer,
            parameterSetSizeOut: &spsSize,
            parameterSetCountOut: &parameterSetCount,
            nalUnitHeaderLengthOut: &nalHeaderLength
        )
        guard spsStatus == noErr, let spsPointer, parameterSetCount >= 2 else { return }
        var ppsPointer: UnsafePointer<UInt8>?
        var ppsSize = 0
        let ppsStatus = CMVideoFormatDescriptionGetH264ParameterSetAtIndex(
            formatDescription,
            1,
            parameterSetPointer: &ppsPointer,
            parameterSetSizeOut: &ppsSize,
            parameterSetCountOut: nil,
            nalUnitHeaderLengthOut: nil
        )
        guard ppsStatus == noErr, let ppsPointer else { return }

        guard let dataBuffer = CMSampleBufferGetDataBuffer(sampleBuffer) else { return }
        let avccLength = CMBlockBufferGetDataLength(dataBuffer)
        guard avccLength > 0, nalHeaderLength == 4 else { return }
        var avcc = [UInt8](repeating: 0, count: avccLength)
        let copyStatus = CMBlockBufferCopyDataBytes(dataBuffer, atOffset: 0, dataLength: avccLength, destination: &avcc)
        guard copyStatus == kCMBlockBufferNoErr else { return }

        var annexB = Data(capacity: avccLength + spsSize + ppsSize + 32)
        if isKeyframe {
            Self.appendStartCode(&annexB)
            annexB.append(spsPointer, count: spsSize)
            Self.appendStartCode(&annexB)
            annexB.append(ppsPointer, count: ppsSize)
        }
        var offset = 0
        while offset + 4 <= avccLength {
            let length = Int(avcc[offset]) << 24 | Int(avcc[offset + 1]) << 16 | Int(avcc[offset + 2]) << 8 | Int(avcc[offset + 3])
            offset += 4
            guard length > 0, offset + length <= avccLength else { return }
            Self.appendStartCode(&annexB)
            annexB.append(contentsOf: avcc[offset..<(offset + length)])
            offset += length
        }
        guard !annexB.isEmpty else { return }
        onAccessUnit?(annexB, isKeyframe, timestamp)
    }

    private static func appendStartCode(_ data: inout Data) {
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x01])
    }
}

private let encodedFrameCallback: VTCompressionOutputCallback = { refcon, status, _, sampleBuffer in
    guard let refcon else { return }
    let encoder = Unmanaged<H264Encoder>.fromOpaque(refcon).takeUnretainedValue()
    encoder.handleEncoded(status: status, sampleBuffer: sampleBuffer)
}
