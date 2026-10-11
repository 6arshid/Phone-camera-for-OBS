package com.obsphonecamera.app.util

object Hex {

    private val DIGITS = "0123456789abcdef"

    fun encode(bytes: ByteArray): String {
        val sb = StringBuilder(bytes.size * 2)
        for (b in bytes) {
            val v = b.toInt() and 0xff
            sb.append(DIGITS[v ushr 4]).append(DIGITS[v and 0x0f])
        }
        return sb.toString()
    }

    fun decode(s: String): ByteArray {
        val clean = s.trim()
        require(clean.length % 2 == 0) { "hex string must have even length" }
        val out = ByteArray(clean.length / 2)
        for (i in out.indices) {
            val hi = digit(clean[i * 2])
            val lo = digit(clean[i * 2 + 1])
            out[i] = ((hi shl 4) or lo).toByte()
        }
        return out
    }

    fun isValidHex(s: String, length: Int): Boolean {
        if (s.length != length) return false
        for (c in s) {
            val d = digit(c)
            if (d < 0) return false
        }
        return true
    }

    private fun digit(c: Char): Int = when (c) {
        in '0'..'9' -> c - '0'
        in 'a'..'f' -> c - 'a' + 10
        in 'A'..'F' -> c - 'A' + 10
        else -> -1
    }
}
