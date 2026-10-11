package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class ControlCipherTest {

    private fun testKeys(seed: Int = 0): Pair<ByteArray, ByteArray> {
        val enc = ByteArray(32) { i -> (i * 3 + 1 + seed * 17).toByte() }
        val mac = ByteArray(32) { i -> (255 - i - seed * 13).toByte() }
        return enc to mac
    }

    @Test
    fun goldenEncryptedFrameMatchesIndependentImplementation() {
        val (enc, mac) = testKeys()
        val cipher = ControlCipher(enc, mac)
        val sealed = cipher.seal("""{"type":"ready"}""".toByteArray(Charsets.UTF_8))

        // Produced by a JDK (SunJCE) implementation of PROTOCOL.md §3.1:
        // ChaCha20 counter 0, nonce = u64 LE counter || 0x00*4,
        // tag = HMAC-SHA256(mac, header[0..26] with zeroed tag || ciphertext)[0..16].
        val expected = Hex.decode(
            "4f4350310000001001000055f301a0ad98d0333d4436991daa843ead5bb7cb4750fc96b2eab6f8cabbb8e8",
        )
        assertContentEquals(expected, sealed)
    }

    @Test
    fun sealedFrameHasEncryptedFlagAndTruncatedTag() {
        val (enc, mac) = testKeys()
        val sealed = ControlCipher(enc, mac).seal("ping".toByteArray())
        val frame = ControlFrame.parse(sealed)

        assertTrue(frame.encrypted)
        assertEquals(16, frame.tag.size)
        assertEquals(4, frame.payload.size)
    }

    @Test
    fun roundTripThroughMatchingPeer() {
        val (enc, mac) = testKeys()
        val sender = ControlCipher(enc.copyOf(), mac.copyOf())
        val receiver = ControlCipher(enc.copyOf(), mac.copyOf())

        val messages = listOf(
            """{"type":"ready"}""",
            """{"type":"config"}""",
            "x".repeat(1000),
            """{"type":"ping","t":1}""",
        )
        val opened = messages.map { m ->
            val frame = ControlFrame.parse(sender.seal(m.toByteArray(Charsets.UTF_8)))
            receiver.open(frame).toString(Charsets.UTF_8)
        }
        assertEquals(messages, opened)
    }

    @Test
    fun tamperedCiphertextRejected() {
        val (enc, mac) = testKeys()
        val sender = ControlCipher(enc.copyOf(), mac.copyOf())
        val receiver = ControlCipher(enc.copyOf(), mac.copyOf())

        val sealed = sender.seal("""{"type":"auth_ok"}""".toByteArray())
        sealed[ControlFrame.HEADER_SIZE + 2] = (sealed[ControlFrame.HEADER_SIZE + 2].toInt() xor 0x40).toByte()
        assertFailsWith<ProtocolException> { receiver.open(ControlFrame.parse(sealed)) }
    }

    @Test
    fun tamperedTagRejected() {
        val (enc, mac) = testKeys()
        val sender = ControlCipher(enc.copyOf(), mac.copyOf())
        val receiver = ControlCipher(enc.copyOf(), mac.copyOf())

        val sealed = sender.seal("""{"type":"bye"}""".toByteArray())
        sealed[ControlFrame.HEADER_SIZE - 1] = (sealed[ControlFrame.HEADER_SIZE - 1].toInt() xor 0x01).toByte()
        assertFailsWith<ProtocolException> { receiver.open(ControlFrame.parse(sealed)) }
    }

    @Test
    fun wrongKeyRejected() {
        val (encA, macA) = testKeys(0)
        val (encB, macB) = testKeys(1)
        val sender = ControlCipher(encA, macA)
        val receiver = ControlCipher(encB, macB)

        val sealed = sender.seal("""{"secret":true}""".toByteArray())
        assertFailsWith<ProtocolException> { receiver.open(ControlFrame.parse(sealed)) }
    }

    @Test
    fun countersArePerDirectionAndStrictlySequential() {
        val (enc, mac) = testKeys()
        val sender = ControlCipher(enc.copyOf(), mac.copyOf())
        val receiver = ControlCipher(enc.copyOf(), mac.copyOf())

        val m0 = ControlFrame.parse(sender.seal("zero".toByteArray()))
        val m1 = ControlFrame.parse(sender.seal("one".toByteArray()))

        // A fresh receiver starts at counter 0: opening the second frame first must fail.
        assertFailsWith<ProtocolException> {
            ControlCipher(enc.copyOf(), mac.copyOf()).open(m1)
        }

        assertContentEquals("zero".toByteArray(), receiver.open(m0))
        assertContentEquals("one".toByteArray(), receiver.open(m1))
    }

    @Test
    fun failedAuthenticationDoesNotAdvanceReceiveCounter() {
        val (enc, mac) = testKeys()
        val sender = ControlCipher(enc.copyOf(), mac.copyOf())
        val receiver = ControlCipher(enc.copyOf(), mac.copyOf())

        val bad = sender.seal("bad".toByteArray())
        bad[ControlFrame.HEADER_SIZE] = (bad[ControlFrame.HEADER_SIZE].toInt() xor 0xff).toByte()
        assertFailsWith<ProtocolException> { receiver.open(ControlFrame.parse(bad)) }

        // A genuine frame for counter 0 (fresh sender) still opens afterwards.
        val good = ControlFrame.parse(ControlCipher(enc.copyOf(), mac.copyOf()).seal("good".toByteArray()))
        assertContentEquals("good".toByteArray(), receiver.open(good))
    }
}
