package com.obsphonecamera.app.crypto

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals

class HmacSha256Test {

    @Test
    fun rfc4231Case1() {
        val key = ByteArray(20) { 0x0b }
        val data = "Hi There".toByteArray(Charsets.US_ASCII)
        assertEquals(
            "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7",
            Hex.encode(HmacSha256.mac(key, data)),
        )
    }

    @Test
    fun rfc4231Case2() {
        assertEquals(
            "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
            Hex.encode(
                HmacSha256.mac(
                    "Jefe".toByteArray(Charsets.US_ASCII),
                    "what do ya want for nothing?".toByteArray(Charsets.US_ASCII),
                ),
            ),
        )
    }

    @Test
    fun rfc4231Case6LargerThanBlockSizeKey() {
        val key = ByteArray(131) { 0xaa.toByte() }
        val data = "Test Using Larger Than Block-Size Key - Hash Key First".toByteArray(Charsets.US_ASCII)
        assertEquals(
            "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54",
            Hex.encode(HmacSha256.mac(key, data)),
        )
    }

    @Test
    fun multiPartEqualsSinglePart() {
        val key = ByteArray(32) { (it + 1).toByte() }
        val a = "hello ".toByteArray()
        val b = "world".toByteArray()
        assertContentEquals(
            HmacSha256.mac(key, a + b),
            HmacSha256.mac(key, a, b),
        )
    }
}
