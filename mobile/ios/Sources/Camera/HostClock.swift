import CoreMedia
import Foundation

/// Shared monotonic host-clock helpers for media timestamps (PROTOCOL.md §5):
/// timestamps are nanoseconds since stream start on the client's monotonic clock.
enum HostClock {
    static func nowNs() -> Int64 {
        let time = CMClockGetTime(CMClockGetHostTimeClock())
        return ns(from: time)
    }

    static func ns(from time: CMTime) -> Int64 {
        guard time.timescale != 0 else { return 0 }
        return time.value * 1_000_000_000 / Int64(time.timescale)
    }

    /// Nanoseconds of `time` relative to a host-clock base captured earlier.
    static func relativeNs(_ time: CMTime, since base: CMTime) -> Int64 {
        let delta = CMTimeSubtract(time, base)
        return delta.value * 1_000_000_000 / Int64(max(delta.timescale, 1))
    }
}
