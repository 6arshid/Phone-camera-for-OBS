package com.obsphonecamera.app.crypto

/**
 * ChaCha20 stream cipher (RFC 8439). Pure Kotlin implementation of the
 * 256-bit key / 96-bit nonce variant.
 */
object ChaCha20 {

    private const val SIGMA0 = 0x61707865
    private const val SIGMA1 = 0x3320646e
    private const val SIGMA2 = 0x79622d32
    private const val SIGMA3 = 0x6b206574

    fun crypt(key: ByteArray, nonce: ByteArray, input: ByteArray, initialCounter: Int = 0): ByteArray {
        require(key.size == 32) { "key must be 32 bytes" }
        require(nonce.size == 12) { "nonce must be 12 bytes" }
        val out = ByteArray(input.size)
        val block = ByteArray(64)
        var offset = 0
        var counter = initialCounter
        while (offset < input.size) {
            block(key, counter, nonce, block)
            val n = minOf(64, input.size - offset)
            for (i in 0 until n) {
                out[offset + i] = (input[offset + i].toInt() xor block[i].toInt()).toByte()
            }
            offset += n
            counter = counter + 1
        }
        return out
    }

    fun block(key: ByteArray, counter: Int, nonce: ByteArray, out64: ByteArray) {
        val s = IntArray(16)
        s[0] = SIGMA0
        s[1] = SIGMA1
        s[2] = SIGMA2
        s[3] = SIGMA3
        for (i in 0 until 8) {
            s[4 + i] = le32(key, i * 4)
        }
        s[12] = counter
        for (i in 0 until 3) {
            s[13 + i] = le32(nonce, i * 4)
        }
        val w = s.copyOf()
        for (i in 0 until 10) {
            qr(w, 0, 4, 8, 12)
            qr(w, 1, 5, 9, 13)
            qr(w, 2, 6, 10, 14)
            qr(w, 3, 7, 11, 15)
            qr(w, 0, 5, 10, 15)
            qr(w, 1, 6, 11, 12)
            qr(w, 2, 7, 8, 13)
            qr(w, 3, 4, 9, 14)
        }
        for (i in 0 until 16) {
            putLe32(out64, i * 4, w[i] + s[i])
        }
    }

    private inline fun qr(x: IntArray, a: Int, b: Int, c: Int, d: Int) {
        x[a] = x[a] + x[b]; x[d] = rotl(x[d] xor x[a], 16)
        x[c] = x[c] + x[d]; x[b] = rotl(x[b] xor x[c], 12)
        x[a] = x[a] + x[b]; x[d] = rotl(x[d] xor x[a], 8)
        x[c] = x[c] + x[d]; x[b] = rotl(x[b] xor x[c], 7)
    }

    private fun rotl(v: Int, n: Int): Int = (v shl n) or (v ushr (32 - n))

    private fun le32(b: ByteArray, i: Int): Int =
        (b[i].toInt() and 0xff) or
            ((b[i + 1].toInt() and 0xff) shl 8) or
            ((b[i + 2].toInt() and 0xff) shl 16) or
            ((b[i + 3].toInt() and 0xff) shl 24)

    private fun putLe32(b: ByteArray, i: Int, v: Int) {
        b[i] = v.toByte()
        b[i + 1] = (v ushr 8).toByte()
        b[i + 2] = (v ushr 16).toByte()
        b[i + 3] = (v ushr 24).toByte()
    }
}
