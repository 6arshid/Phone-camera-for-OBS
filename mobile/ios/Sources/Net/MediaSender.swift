import Foundation
import Network

/// UDP media sender (PROTOCOL.md §4). Fire-and-forget datagrams over one
/// NWConnection; loss is expected and handled by the server-side reassembler.
final class MediaSender {
    private let connection: NWConnection
    private let queue = DispatchQueue(label: "com.obsphonecamera.media")
    private var ready = false
    private var cancelled = false

    init(host: String, port: UInt16) {
        let endpoint = NWEndpoint.hostPort(
            host: NWEndpoint.Host(host),
            port: NWEndpoint.Port(rawValue: port) ?? .init(integerLiteral: OCP.defaultMediaPort)
        )
        connection = NWConnection(to: endpoint, using: .udp)
        connection.stateUpdateHandler = { [weak self] state in
            guard let self else { return }
            self.queue.async {
                switch state {
                case .ready:
                    self.ready = true
                case .failed, .cancelled:
                    self.ready = false
                    self.cancelled = true
                default:
                    break
                }
            }
        }
        connection.start(queue: queue)
    }

    func send(_ datagram: Data) {
        queue.async { [self] in
            guard ready, !cancelled, datagram.count <= OCP.mediaMaxDatagram else { return }
            connection.send(content: datagram, completion: .contentProcessed { _ in })
        }
    }

    func cancel() {
        queue.async { [self] in
            cancelled = true
            ready = false
            connection.cancel()
        }
    }

    deinit {
        connection.cancel()
    }
}
