package com.obsphonecamera.app.crypto

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertNotEquals
import kotlin.test.assertTrue

class ChaCha20Test {

    @Test
    fun rfc8439Section242Vector() {
        val key = Hex.decode("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f")
        val plain = ("Ladies and Gentlemen of the class of '99: If I could offer you only one tip " +
            "for the future, sunscreen would be it.").toByteArray(Charsets.US_ASCII)
        val expected = Hex.decode(
            "6e2e359a2568f98041ba0728dd0d6981e97e7aec1d4360c20a27afccfd9fae0b" +
                "f91b65c5524733ab8f593dabcd62b3571639d624e65152ab8f530c359f0861d8" +
                "07ca0dbf500d6a6156a38e088a22b65e52bc514d16ccf806818ce91ab77937365" +
                "af90bbf74a35be6b40b8eedf2785e42874d",
        )
        val nonce = Hex.decode("000000000000004a00000000")

        val cipher = ChaCha20.crypt(key, nonce, plain, initialCounter = 1)
        assertContentEquals(expected, cipher)

        val back = ChaCha20.crypt(key, nonce, cipher, initialCounter = 1)
        assertContentEquals(plain, back)
    }

    @Test
    fun multiBlockRoundTripIncrementsCounter() {
        val key = ByteArray(32) { it.toByte() }
        val nonce = ByteArray(12) { (it * 3).toByte() }
        val plain = ByteArray(200) { (it * 7 + 1).toByte() }

        val cipher = ChaCha20.crypt(key, nonce, plain)
        val back = ChaCha20.crypt(key, nonce, cipher)
        assertContentEquals(plain, back)

        val firstBlock = cipher.copyOfRange(0, 64)
        val secondBlock = cipher.copyOfRange(64, 128)
        assertTrue(!firstBlock.contentEquals(secondBlock))
    }

    @Test
    fun differentNonceDifferentKeystream() {
        val key = ByteArray(32) { 5 }
        val plain = ByteArray(64) { 0x42 }
        val a = ChaCha20.crypt(key, ByteArray(12), plain)
        val b = ChaCha20.crypt(key, ByteArray(12) { 1 }, plain)
        assertNotEquals(Hex.encode(a), Hex.encode(b))
    }

    @Test
    fun emptyInputProducesEmptyOutput() {
        val out = ChaCha20.crypt(ByteArray(32), ByteArray(12), ByteArray(0))
        assertTrue(out.isEmpty())
    }
}
