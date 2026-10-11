package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

class MediaPacketTest {

    private fun testKeys() = MediaKeys(
        encKey = ByteArray(32) { i -> (i * 7 + 3).toByte() },
        macKey = ByteArray(32) { i -> (i xor 0x5a).toByte() },
    )

    private fun bytes(size: Int, seed: Int): ByteArray =
        ByteArray(size) { i -> (seed + i * 31).toByte() }

    @Test
    fun constantsMatchSpec() {
        assertEquals(32, MediaPacket.HEADER_SIZE)
        assertEquals(16, MediaPacket.TAG_SIZE)
        assertEquals(1200, MediaPacket.MAX_DATAGRAM)
        assertEquals(1152, MediaPacket.MAX_CIPHERTEXT)
        assertEquals(0, MediaPacket.STREAM_VIDEO)
        assertEquals(1, MediaPacket.STREAM_AUDIO)
        assertEquals(0x01, MediaPacket.FLAG_KEYFRAME)
    }

    @Test
    fun goldenDatagramMatchesIndependentImplementation() {
        val keys = testKeys()
        val payload = bytes(100, 9)
        val sealed = MediaPacket.seal(
            keys = keys,
            sessionTag = Hex.decode("1122334455667788"),
            stream = MediaPacket.STREAM_VIDEO,
            keyframe = true,
            frameId = 42,
            fragIndex = 0,
            fragCount = 1,
            timestampNs = 123456789L,
            plaintext = payload,
        )

        // Produced by a JDK (SunJCE) implementation of PROTOCOL.md §4:
        // nonce = session_tag[0..4) || stream || u32 frame_id || u16 frag_idx || 0x00 (big endian),
        // tag = HMAC-SHA256(k_media_mac, header[0..31] || ciphertext)[0..16].
        val expected = Hex.decode(
            "4f43504d0100010011223344556677880000002a0000000100000000075bcd15" +
                "cdf2d0a7afc062ed4eb75b7bd332e8a2fa87c60195e443d685ed610f971e8524" +
                "acc211301e2f1a157ca15558d73ec8e74ce47562d8681b695dbdfddb1d4821204" +
                "be52be63cc037a31f9a1223353c5b97814a77c259661c4b19f620f530957dfee" +
                "b7fa55c7a29e6da6fdf16f284bed844ab3a8bc8",
        )
        assertContentEquals(expected, sealed)
    }

    @Test
    fun headerLayoutMatchesSpec() {
        val sealed = MediaPacket.seal(
            keys = testKeys(),
            sessionTag = Hex.decode("aabbccddeeff0011"),
            stream = MediaPacket.STREAM_AUDIO,
            keyframe = false,
            frameId = 0x01020304,
            fragIndex = 2,
            fragCount = 5,
            timestampNs = 0x0102030405060708L,
            plaintext = bytes(32, 1),
        )

        assertContentEquals("OCPM".toByteArray(Charsets.US_ASCII), sealed.copyOfRange(0, 4))
        assertEquals(1, sealed[4].toInt())
        assertEquals(MediaPacket.STREAM_AUDIO, sealed[5].toInt() and 0xff)
        assertEquals(0, sealed[6].toInt() and 0xff)
        assertEquals(0, sealed[7].toInt())
        assertContentEquals(Hex.decode("aabbccddeeff0011"), sealed.copyOfRange(8, 16))
        assertContentEquals(Hex.decode("01020304"), sealed.copyOfRange(16, 20))
        assertContentEquals(Hex.decode("0002"), sealed.copyOfRange(20, 22))
        assertContentEquals(Hex.decode("0005"), sealed.copyOfRange(22, 24))
        assertContentEquals(Hex.decode("0102030405060708"), sealed.copyOfRange(24, 32))
    }

    @Test
    fun roundTrip() {
        val keys = testKeys()
        val payload = bytes(100, 9)
        val sealed = MediaPacket.seal(
            keys = keys,
            sessionTag = Hex.decode("1122334455667788"),
            stream = MediaPacket.STREAM_VIDEO,
            keyframe = true,
            frameId = 42,
            fragIndex = 0,
            fragCount = 1,
            timestampNs = 123456789L,
            plaintext = payload,
        )
        assertEquals(MediaPacket.HEADER_SIZE + payload.size + MediaPacket.TAG_SIZE, sealed.size)
        assertTrue(sealed.size <= MediaPacket.MAX_DATAGRAM)

        val frame = MediaPacket.open(keys, sealed)
        assertEquals(MediaPacket.STREAM_VIDEO, frame.stream)
        assertTrue(frame.keyframe)
        assertEquals(42, frame.frameId)
        assertEquals(0, frame.fragIndex)
        assertEquals(1, frame.fragCount)
        assertEquals(123456789L, frame.timestampNs)
        assertContentEquals(payload, frame.payload)
    }

    @Test
    fun bitFlipInCiphertextRejected() {
        val keys = testKeys()
        val sealed = MediaPacket.seal(
            keys, Hex.decode("1122334455667788"),
            MediaPacket.STREAM_VIDEO, false, 7, 0, 1, 0L, bytes(64, 1),
        )
        sealed[MediaPacket.HEADER_SIZE + 5] = (sealed[MediaPacket.HEADER_SIZE + 5].toInt() xor 0x01).toByte()
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, sealed) }
    }

    @Test
    fun bitFlipInHeaderRejected() {
        val keys = testKeys()
        val sealed = MediaPacket.seal(
            keys, Hex.decode("1122334455667788"),
            MediaPacket.STREAM_VIDEO, false, 7, 0, 1, 0L, bytes(64, 1),
        )
        sealed[19] = (sealed[19].toInt() xor 0x01).toByte()
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, sealed) }
    }

    @Test
    fun wrongKeyRejected() {
        val sealed = MediaPacket.seal(
            testKeys(), Hex.decode("1122334455667788"),
            MediaPacket.STREAM_VIDEO, false, 3, 0, 1, 0L, bytes(32, 4),
        )
        val wrong = MediaKeys(ByteArray(32), ByteArray(32))
        assertFailsWith<ProtocolException> { MediaPacket.open(wrong, sealed) }
    }

    @Test
    fun fragmentCountMath() {
        assertEquals(1, MediaPacket.fragmentCount(0))
        assertEquals(1, MediaPacket.fragmentCount(1))
        assertEquals(1, MediaPacket.fragmentCount(1152))
        assertEquals(2, MediaPacket.fragmentCount(1153))
        assertEquals(2, MediaPacket.fragmentCount(2304))
        assertEquals(3, MediaPacket.fragmentCount(2305))
        assertEquals(35, MediaPacket.fragmentCount(40000))
    }

    @Test
    fun fragmentSplitsAtMaxCiphertext() {
        val empty = MediaPacket.fragment(ByteArray(0))
        assertEquals(1, empty.size)
        assertEquals(0, empty[0].size)

        val parts = MediaPacket.fragment(ByteArray(2305))
        assertEquals(3, parts.size)
        assertEquals(1152, parts[0].size)
        assertEquals(1152, parts[1].size)
        assertEquals(1, parts[2].size)
    }

    @Test
    fun fragmentAndReassembleLargeFrame() {
        val keys = testKeys()
        val payload = bytes(40000, 11)
        val parts = MediaPacket.fragment(payload)
        assertTrue(parts.size > 10)

        val sessionTag = Hex.decode("aabbccdd00112233")
        val reassembled = parts.mapIndexed { idx, part ->
            val sealed = MediaPacket.seal(
                keys = keys,
                sessionTag = sessionTag,
                stream = MediaPacket.STREAM_VIDEO,
                keyframe = true,
                frameId = 5,
                fragIndex = idx,
                fragCount = parts.size,
                timestampNs = 999L,
                plaintext = part,
            )
            assertTrue(sealed.size <= MediaPacket.MAX_DATAGRAM)
            MediaPacket.open(keys, sealed)
        }
        reassembled.forEachIndexed { idx, frame ->
            assertEquals(idx, frame.fragIndex)
            assertEquals(parts.size, frame.fragCount)
            assertTrue(frame.keyframe)
            assertEquals(999L, frame.timestampNs)
        }
        assertContentEquals(payload, reassembled.flatMap { it.payload.toList() }.toByteArray())
    }

    @Test
    fun singleAudioFrameIsOnePacket() {
        val keys = testKeys()
        val pcm = bytes(960, 3)
        assertEquals(1, MediaPacket.fragmentCount(pcm.size))

        val sealed = MediaPacket.seal(
            keys, Hex.decode("0102030405060708"),
            MediaPacket.STREAM_AUDIO, false, 1, 0, 1, 10_000_000L, pcm,
        )
        assertTrue(sealed.size <= MediaPacket.MAX_DATAGRAM)
        val frame = MediaPacket.open(keys, sealed)
        assertEquals(MediaPacket.STREAM_AUDIO, frame.stream)
        assertContentEquals(pcm, frame.payload)
    }

    @Test
    fun noncesDifferPerFragment() {
        val keys = testKeys()
        val payload = bytes(1152, 5)
        val sessionTag = Hex.decode("1122334455667788")
        val a = MediaPacket.seal(keys, sessionTag, MediaPacket.STREAM_VIDEO, false, 9, 0, 2, 1L, payload)
        val b = MediaPacket.seal(keys, sessionTag, MediaPacket.STREAM_VIDEO, false, 9, 1, 2, 1L, payload)
        val ctA = a.copyOfRange(MediaPacket.HEADER_SIZE, a.size - MediaPacket.TAG_SIZE)
        val ctB = b.copyOfRange(MediaPacket.HEADER_SIZE, b.size - MediaPacket.TAG_SIZE)
        assertTrue(!ctA.contentEquals(ctB))
    }

    @Test
    fun openRejectsMalformedDatagrams() {
        val keys = testKeys()
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, ByteArray(47)) }

        val short = MediaPacket.seal(keys, Hex.decode("1122334455667788"), 0, false, 1, 0, 1, 0, ByteArray(16))
        short[0] = 'X'.code.toByte()
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, short) }

        val badVersion = MediaPacket.seal(keys, Hex.decode("1122334455667788"), 0, false, 1, 0, 1, 0, ByteArray(16))
        badVersion[4] = 2
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, badVersion) }

        val zeroFrags = MediaPacket.seal(keys, Hex.decode("1122334455667788"), 0, false, 1, 0, 1, 0, ByteArray(16))
        zeroFrags[22] = 0
        zeroFrags[23] = 0
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, zeroFrags) }

        val idxOutOfRange = MediaPacket.seal(keys, Hex.decode("1122334455667788"), 0, false, 1, 2, 2, 0, ByteArray(16))
        assertFailsWith<ProtocolException> { MediaPacket.open(keys, idxOutOfRange) }
    }

    @Test
    fun sealRejectsOversizedFragmentAndBadTag() {
        assertFailsWith<IllegalArgumentException> {
            MediaPacket.seal(
                testKeys(), ByteArray(8), 0, false, 1, 0, 1, 0, ByteArray(1153),
            )
        }
        assertFailsWith<IllegalArgumentException> {
            MediaPacket.seal(
                testKeys(), ByteArray(7), 0, false, 1, 0, 1, 0, ByteArray(16),
            )
        }
    }
}
