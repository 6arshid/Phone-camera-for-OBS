package com.obsphonecamera.app.crypto

/**
 * HKDF-SHA256 (RFC 5869).
 */
object Hkdf {

    fun extract(salt: ByteArray, ikm: ByteArray): ByteArray =
        HmacSha256.mac(salt, ikm)

    fun expand(prk: ByteArray, info: ByteArray, length: Int): ByteArray {
        var t = ByteArray(0)
        var out = ByteArray(0)
        var counter = 1
        while (out.size < length) {
            t = HmacSha256.mac(prk, t, info, byteArrayOf(counter.toByte()))
            out += t
            counter++
        }
        return out.copyOf(length)
    }

    fun derive(salt: ByteArray, ikm: ByteArray, info: ByteArray, length: Int): ByteArray =
        expand(extract(salt, ikm), info, length)
}
