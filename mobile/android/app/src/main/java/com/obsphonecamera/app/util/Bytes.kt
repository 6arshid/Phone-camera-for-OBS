package com.obsphonecamera.app.util

object Bytes {

    fun constantTimeEquals(a: ByteArray, b: ByteArray): Boolean {
        if (a.size != b.size) return false
        var diff = 0
        for (i in a.indices) {
            diff = diff or (a[i].toInt() xor b[i].toInt())
        }
        return diff == 0
    }

    fun copyOfRange(b: ByteArray, from: Int, to: Int): ByteArray = b.copyOfRange(from, to)

    fun randomBytes(n: Int): ByteArray {
        val b = ByteArray(n)
        java.security.SecureRandom().nextBytes(b)
        return b
    }
}
