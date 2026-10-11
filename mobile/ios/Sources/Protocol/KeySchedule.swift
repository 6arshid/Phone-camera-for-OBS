import CryptoKit
import Foundation

/// OCP/1 key schedule (PROTOCOL.md §3.2):
/// transcript = SHA256(hello || challenge)
/// salt_kdf   = client_nonce || server_nonce
/// k_auth/k_ctrl/k_media via HKDF-SHA256 with info "ocp1-auth"/"ocp1-ctrl"/"ocp1-media".
struct KeySchedule {
    let transcript: Data
    let kAuth: Data
    let kCtrlEnc: Data
    let kCtrlMac: Data
    let kMediaEnc: Data
    let kMediaMac: Data

    init(master: Data, clientNonce: Data, serverNonce: Data, helloPayload: Data, challengePayload: Data) {
        var transcriptInput = helloPayload
        transcriptInput.append(challengePayload)
        transcript = Data(SHA256.hash(data: transcriptInput))

        let saltKDF = clientNonce + serverNonce
        kAuth = HKDF.derive(ikm: master, salt: saltKDF, info: "ocp1-auth", length: 32)
        let ctrl = HKDF.derive(ikm: master, salt: saltKDF, info: "ocp1-ctrl", length: 64)
        kCtrlEnc = Data(ctrl.prefix(32))
        kCtrlMac = Data(ctrl.suffix(32))
        let media = HKDF.derive(ikm: master, salt: saltKDF, info: "ocp1-media", length: 64)
        kMediaEnc = Data(media.prefix(32))
        kMediaMac = Data(media.suffix(32))
    }

    /// proof = HMAC-SHA256(k_auth, "phone" || client_nonce || server_nonce || transcript)
    func phoneProof(clientNonce: Data, serverNonce: Data) -> Data {
        var message = Data("phone".utf8)
        message.append(clientNonce)
        message.append(serverNonce)
        message.append(transcript)
        return Data(HMAC<SHA256>.authenticationCode(for: message, using: SymmetricKey(data: kAuth)))
    }

    /// proof = HMAC-SHA256(k_auth, "obs" || server_nonce || client_nonce || transcript)
    func obsProof(serverNonce: Data, clientNonce: Data) -> Data {
        var message = Data("obs".utf8)
        message.append(serverNonce)
        message.append(clientNonce)
        message.append(transcript)
        return Data(HMAC<SHA256>.authenticationCode(for: message, using: SymmetricKey(data: kAuth)))
    }
}
