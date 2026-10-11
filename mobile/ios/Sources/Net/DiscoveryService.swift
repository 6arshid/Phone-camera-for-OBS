import Darwin
import Foundation

/// UDP discovery on port 47109 (PROTOCOL.md §2).
///
/// Implemented with BSD sockets rather than NWConnection because receiving
/// both broadcast and multicast datagrams on one port requires
/// SO_REUSEADDR/SO_REUSEPORT plus IP_ADD_MEMBERSHIP, which Network.framework
/// does not expose portably on iOS.
final class DiscoveryService {
    var onServerAnnouncement: ((ServerAnnouncement, String) -> Void)?

    private let queue = DispatchQueue(label: "com.obsphonecamera.discovery")
    private var receiveSocket: Int32 = -1
    private var sendSocket: Int32 = -1
    private var receiveSource: DispatchSourceRead?
    private var announceTimer: DispatchSourceTimer?
    private var announceData = Data()
    private var running = false

    var isRunning: Bool {
        queue.sync { running }
    }

    /// Begins listening and announcing `announcement` every 2 seconds.
    func start(announcement: ClientAnnouncement) {
        queue.async { [self] in
            guard !running else { return }
            announceData = (try? announcement.encoded()) ?? Data()
            if openReceiveSocket() {
                startReceiving()
                startAnnouncing()
                running = true
            }
        }
    }

    func stop() {
        queue.async { [self] in
            guard running else { return }
            running = false
            receiveSource?.cancel()
            receiveSource = nil
            announceTimer?.cancel()
            announceTimer = nil
            if receiveSocket >= 0 { close(receiveSocket); receiveSocket = -1 }
            if sendSocket >= 0 { close(sendSocket); sendSocket = -1 }
        }
    }

    /// Sends one announcement immediately (no-op until start() succeeded).
    func announce() {
        queue.async { [self] in
            guard running else { return }
            sendAnnouncement()
        }
    }

    // MARK: - Sockets

    private func openReceiveSocket() -> Bool {
        let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { return false }
        defer { if fd >= 0 && receiveSocket < 0 { close(fd) } }

        var reuse: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, socklen_t(MemoryLayout<Int32>.size))
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, socklen_t(MemoryLayout<Int32>.size))

        var bindAddress = sockaddr_in()
        bindAddress.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        bindAddress.sin_family = sa_family_t(AF_INET)
        bindAddress.sin_port = OCP.discoveryPort.bigEndian
        bindAddress.sin_addr = in_addr(s_addr: in_addr_t(0)) // INADDR_ANY
        let bindResult = withUnsafePointer(to: &bindAddress) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                bind(fd, $0, socklen_t(MemoryLayout<sockaddr_in>.size))
            }
        }
        guard bindResult == 0 else {
            close(fd)
            return false
        }

        // Join the OCP multicast group so server announcements sent to
        // 239.255.74.79 are received as well as broadcast/unicast ones.
        var membership = ip_mreq()
        membership.imr_multiaddr = in_addr()
        _ = OCP.discoveryMulticastGroup.withCString { inet_pton(AF_INET, $0, &membership.imr_multiaddr) }
        membership.imr_interface = in_addr(s_addr: in_addr_t(0))
        _ = withUnsafePointer(to: &membership) {
            $0.withMemoryRebound(to: ip_mreq.self, capacity: 1) {
                setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, $0, socklen_t(MemoryLayout<ip_mreq>.size))
            }
        }

        receiveSocket = fd
        let fileFlags = fcntl(fd, F_GETFL, 0)
        _ = fcntl(fd, F_SETFL, fileFlags | O_NONBLOCK)
        return true
    }

    private func startReceiving() {
        guard receiveSocket >= 0 else { return }
        let source = DispatchSource.makeReadSource(fileDescriptor: receiveSocket, queue: queue)
        source.setEventHandler { [weak self] in
            self?.drainReceiveSocket()
        }
        source.resume()
        receiveSource = source
    }

    private func drainReceiveSocket() {
        var buffer = [UInt8](repeating: 0, count: 2048)
        while receiveSocket >= 0 {
            var sourceAddress = sockaddr_in()
            var sourceLength = socklen_t(MemoryLayout<sockaddr_in>.size)
            let count = withUnsafeMutablePointer(to: &sourceAddress) { addressPointer in
                addressPointer.withMemoryRebound(to: sockaddr.self, capacity: 1) { sockaddrPointer in
                    recvfrom(receiveSocket, &buffer, buffer.count, 0, sockaddrPointer, &sourceLength)
                }
            }
            guard count > 0 else { break }
            if let announcement = ServerAnnouncement.decode(Data(buffer[0..<count])) {
                let ip = Self.ipv4String(from: sourceAddress)
                onServerAnnouncement?(announcement, ip)
            }
        }
    }

    private func startAnnouncing() {
        openSendSocket()
        let timer = DispatchSource.makeTimerSource(queue: queue)
        timer.schedule(deadline: .now(), repeating: 2.0)
        timer.setEventHandler { [weak self] in
            self?.sendAnnouncement()
        }
        timer.resume()
        announceTimer = timer
    }

    private func openSendSocket() {
        let fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)
        guard fd >= 0 else { return }
        var broadcast: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &broadcast, socklen_t(MemoryLayout<Int32>.size))
        var ttl: Int32 = 1
        setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, socklen_t(MemoryLayout<Int32>.size))
        sendSocket = fd
    }

    private func sendAnnouncement() {
        guard sendSocket >= 0, !announceData.isEmpty else { return }
        var broadcastAddress = sockaddr_in()
        broadcastAddress.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        broadcastAddress.sin_family = sa_family_t(AF_INET)
        broadcastAddress.sin_port = OCP.discoveryPort.bigEndian
        _ = "255.255.255.255".withCString { inet_pton(AF_INET, $0, &broadcastAddress.sin_addr) }
        sendto(sendSocket, (announceData as NSData).bytes, announceData.count, 0,
               withUnsafePointer(to: &broadcastAddress) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { $0 } },
               socklen_t(MemoryLayout<sockaddr_in>.size))

        var multicastAddress = sockaddr_in()
        multicastAddress.sin_len = UInt8(MemoryLayout<sockaddr_in>.size)
        multicastAddress.sin_family = sa_family_t(AF_INET)
        multicastAddress.sin_port = OCP.discoveryPort.bigEndian
        _ = OCP.discoveryMulticastGroup.withCString { inet_pton(AF_INET, $0, &multicastAddress.sin_addr) }
        sendto(sendSocket, (announceData as NSData).bytes, announceData.count, 0,
               withUnsafePointer(to: &multicastAddress) { $0.withMemoryRebound(to: sockaddr.self, capacity: 1) { $0 } },
               socklen_t(MemoryLayout<sockaddr_in>.size))
    }

    private static func ipv4String(from address: sockaddr_in) -> String {
        var address = address
        var buffer = [CChar](repeating: 0, count: Int(INET_ADDRSTRLEN))
        if let cString = withUnsafePointer(to: &address.sin_addr)({
            inet_ntop(AF_INET, $0, &buffer, socklen_t(INET_ADDRSTRLEN))
        }) {
            return String(cString: cString)
        }
        return ""
    }
}
