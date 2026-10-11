import Foundation
import Network

/// TCP control channel client (PROTOCOL.md §3) built on NWConnection.
/// All mutable state is confined to an internal serial queue.
final class ControlClient {
    var onOpen: (() -> Void)?
    var onFrame: ((ControlFrame) -> Void)?
    var onClose: ((Error?) -> Void)?

    private let queue = DispatchQueue(label: "com.obsphonecamera.control")
    private var connection: NWConnection?
    private var decoder = ControlFrameStreamDecoder()
    private var cipher: ControlCipher?
    private var closed = false

    func connect(host: String, port: UInt16) {
        queue.async { [self] in
            guard connection == nil, !closed else { return }
            guard let nwPort = NWEndpoint.Port(rawValue: port) else {
                onClose?(OCPError.protocolViolation("invalid port"))
                return
            }
            let endpoint = NWEndpoint.hostPort(host: NWEndpoint.Host(host), port: nwPort)
            let parameters = NWParameters.tcp
            parameters.serviceClass = .responsiveData
            let connection = NWConnection(to: endpoint, using: parameters)
            self.connection = connection
            connection.stateUpdateHandler = { [weak self] state in
                guard let self else { return }
                self.queue.async {
                    switch state {
                    case .ready:
                        self.onOpen?()
                        self.receiveLoop()
                    case .failed(let error):
                        self.teardown(error: error)
                    case .cancelled:
                        self.teardown(error: nil)
                    default:
                        // `.waiting` during transient network conditions is
                        // tolerated; the app model owns the reconnect policy.
                        break
                    }
                }
            }
            connection.start(queue: queue)
        }
    }

    /// Sends one JSON payload; seals it when the cipher is installed.
    func send(payload: Data) {
        queue.async { [self] in
            guard let connection, !closed else { return }
            let frame: ControlFrame
            do {
                if let cipher {
                    frame = try cipher.seal(payload: payload)
                } else {
                    frame = ControlFrame(flags: 0, tag: Data(repeating: 0, count: OCP.controlTagSize), payload: payload)
                }
                let data = try ControlFrame.encode(frame)
                connection.send(content: data, completion: .contentProcessed { [weak self] error in
                    if let error {
                        self?.queue.async { self?.teardown(error: error) }
                    }
                })
            } catch {
                teardown(error: error)
            }
        }
    }

    /// Activates control-frame encryption (after `auth_ok`, PROTOCOL.md §3.1).
    func installCipher(_ cipher: ControlCipher) {
        queue.async { [self] in
            self.cipher = cipher
        }
    }

    func disconnect() {
        queue.async { [self] in
            teardown(error: nil)
        }
    }

    private func receiveLoop() {
        guard let connection, !closed else { return }
        connection.receive(minimumIncompleteLength: 1, maximumLength: 64 * 1024) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            self.queue.async {
                if let data {
                    self.decoder.append(data)
                    self.drainFrames()
                }
                if let error {
                    self.teardown(error: error)
                    return
                }
                if isComplete {
                    self.teardown(error: nil)
                    return
                }
                self.receiveLoop()
            }
        }
    }

    private func drainFrames() {
        do {
            while let frame = try decoder.nextFrame() {
                if frame.isEncrypted {
                    guard let cipher else {
                        teardown(error: OCPError.protocolViolation("encrypted frame before keys"))
                        return
                    }
                    let plaintext = try cipher.open(frame)
                    onFrame?(ControlFrame(flags: 0, tag: Data(repeating: 0, count: OCP.controlTagSize), payload: plaintext))
                } else {
                    onFrame?(frame)
                }
            }
        } catch {
            teardown(error: error)
        }
    }

    private func teardown(error: Error?) {
        guard !closed else { return }
        closed = true
        connection?.cancel()
        connection = nil
        onClose?(error)
    }
}
