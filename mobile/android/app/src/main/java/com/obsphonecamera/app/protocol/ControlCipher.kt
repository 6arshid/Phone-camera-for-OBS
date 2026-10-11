package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.crypto.ChaCha20
import com.obsphonecamera.app.crypto.HmacSha256
import com.obsphonecamera.app.util.Bytes

/**
 * Encrypt-then-MAC for control frames (PROTOCOL.md §3.1).
 * Nonce = u64 LE frame counter || 4 zero bytes. Counters are per direction.
 */
class ControlCipher(encKey: ByteArray, macKey: ByteArray) {

    private val encKey = encKey.copyOf()
    private val macKey = macKey.copyOf()

    private var sendCounter = 0L
    private var lastReceived = -1L

    @Synchronized
    fun seal(plaintext: ByteArray): ByteArray {
        val counter = sendCounter
        sendCounter += 1
        val nonce = nonceFor(counter)
        val ciphertext = ChaCha20.crypt(encKey, nonce, plaintext)
        val zeroTag = ByteArray(16)
        val header = ControlFrame.pack(ControlFrame.FLAG_ENCRYPTED, zeroTag, ciphertext)
        val macInput = header.copyOfRange(0, ControlFrame.HEADER_SIZE) + ciphertext
        val tag = HmacSha256.mac(macKey, macInput).copyOfRange(0, 16)
        return ControlFrame.pack(ControlFrame.FLAG_ENCRYPTED, tag, ciphertext)
    }

    @Synchronized
    fun open(frame: ControlFrame): ByteArray {
        val counter = lastReceived + 1
        val nonce = nonceFor(counter)
        val zeroTag = ByteArray(16)
        val header = ControlFrame.pack(ControlFrame.FLAG_ENCRYPTED, zeroTag, frame.payload)
        val macInput = header.copyOfRange(0, ControlFrame.HEADER_SIZE) + frame.payload
        val expected = HmacSha256.mac(macKey, macInput).copyOfRange(0, 16)
        if (!Bytes.constantTimeEquals(expected, frame.tag)) {
            throw ProtocolException("control frame authentication failed")
        }
        lastReceived = counter
        return ChaCha20.crypt(encKey, nonce, frame.payload)
    }

    private fun nonceFor(counter: Long): ByteArray {
        val n = ByteArray(12)
        for (i in 0 until 8) {
            n[i] = (counter ushr (8 * i)).toByte()
        }
        return n
    }
}
