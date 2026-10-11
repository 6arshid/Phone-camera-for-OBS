package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.crypto.Hkdf
import com.obsphonecamera.app.crypto.HmacSha256

data class SessionKeys(
    val kAuth: ByteArray,
    val kCtrlEnc: ByteArray,
    val kCtrlMac: ByteArray,
    val kMediaEnc: ByteArray,
    val kMediaMac: ByteArray,
) {
    fun mediaKeys() = MediaKeys(kMediaEnc, kMediaMac)
    fun controlCipher() = ControlCipher(kCtrlEnc, kCtrlMac)
}

/**
 * Handshake key schedule and proofs (PROTOCOL.md §3.2).
 * Nonces are used as raw 16-byte values in transcripts and proofs.
 */
object KeySchedule {

    const val KDF_HKDF = "hkdf-sha256"
    const val KDF_PBKDF2 = "pbkdf2-sha256"
    const val PBKDF2_MIN_ITER = 100000

    fun transcript(helloPayload: ByteArray, challengePayload: ByteArray): ByteArray =
        java.security.MessageDigest.getInstance("SHA-256").digest(helloPayload + challengePayload)

    fun keys(master: ByteArray, clientNonce: ByteArray, serverNonce: ByteArray): SessionKeys {
        val salt = clientNonce + serverNonce
        val kAuth = Hkdf.derive(salt, master, "ocp1-auth".toByteArray(Charsets.US_ASCII), 32)
        val kCtrl = Hkdf.derive(salt, master, "ocp1-ctrl".toByteArray(Charsets.US_ASCII), 64)
        val kMedia = Hkdf.derive(salt, master, "ocp1-media".toByteArray(Charsets.US_ASCII), 64)
        return SessionKeys(
            kAuth = kAuth,
            kCtrlEnc = kCtrl.copyOfRange(0, 32),
            kCtrlMac = kCtrl.copyOfRange(32, 64),
            kMediaEnc = kMedia.copyOfRange(0, 32),
            kMediaMac = kMedia.copyOfRange(32, 64),
        )
    }

    /** proof = HMAC-SHA256(k_auth, "phone" || client_nonce || server_nonce || transcript) */
    fun clientProof(kAuth: ByteArray, clientNonce: ByteArray, serverNonce: ByteArray, transcript: ByteArray): ByteArray =
        HmacSha256.mac(kAuth, "phone".toByteArray(Charsets.US_ASCII), clientNonce, serverNonce, transcript)

    /** proof = HMAC-SHA256(k_auth, "obs" || server_nonce || client_nonce || transcript) */
    fun serverProof(kAuth: ByteArray, serverNonce: ByteArray, clientNonce: ByteArray, transcript: ByteArray): ByteArray =
        HmacSha256.mac(kAuth, "obs".toByteArray(Charsets.US_ASCII), serverNonce, clientNonce, transcript)

    fun transcriptFrom(helloJson: String, challengeJson: String): ByteArray =
        transcript(helloJson.toByteArray(Charsets.UTF_8), challengeJson.toByteArray(Charsets.UTF_8))
}
