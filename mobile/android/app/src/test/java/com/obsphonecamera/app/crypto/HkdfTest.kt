package com.obsphonecamera.app.crypto

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class HkdfTest {

    @Test
    fun rfc5869Case1() {
        val ikm = Hex.decode("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b")
        val salt = Hex.decode("000102030405060708090a0b0c")
        val info = Hex.decode("f0f1f2f3f4f5f6f7f8f9")
        val okm = Hkdf.derive(salt, ikm, info, 42)
        assertEquals(
            "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865",
            Hex.encode(okm),
        )
    }

    @Test
    fun extractIsHmacOfIkm() {
        val salt = ByteArray(16) { 0x5a }
        val ikm = ByteArray(32) { it.toByte() }
        assertContentEquals(HmacSha256.mac(salt, ikm), Hkdf.extract(salt, ikm))
    }

    @Test
    fun expandHonorsRequestedLengthAndPrefixes() {
        val prk = ByteArray(32) { (it * 3).toByte() }
        val info = "ocp1-ctrl".toByteArray(Charsets.US_ASCII)
        val out32 = Hkdf.expand(prk, info, 32)
        val out64 = Hkdf.expand(prk, info, 64)
        assertEquals(32, out32.size)
        assertEquals(64, out64.size)
        assertContentEquals(out64.copyOfRange(0, 32), out32)
    }
}
