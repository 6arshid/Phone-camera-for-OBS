package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertFalse
import kotlin.test.assertTrue

class ControlFrameTest {

    private val payload = """{"type":"hello","proto":"ocp/1"}""".toByteArray(Charsets.UTF_8)

    @Test
    fun constantsMatchSpec() {
        assertEquals(27, ControlFrame.HEADER_SIZE)
        assertEquals(1, ControlFrame.MIN_PAYLOAD)
        assertEquals(65536, ControlFrame.MAX_PAYLOAD)
        assertEquals(0x01, ControlFrame.FLAG_ENCRYPTED)
    }

    @Test
    fun packLayoutMatchesSpec() {
        val tag = ByteArray(16) { (it + 1).toByte() }
        val buf = ControlFrame.pack(0, tag, payload)

        assertEquals(ControlFrame.HEADER_SIZE + payload.size, buf.size)
        assertContentEquals("OCP1".toByteArray(Charsets.US_ASCII), buf.copyOfRange(0, 4))
        assertEquals(payload.size, ((buf[4].toInt() and 0xff) shl 24 or
            ((buf[5].toInt() and 0xff) shl 16) or
            ((buf[6].toInt() and 0xff) shl 8) or
            (buf[7].toInt() and 0xff)))
        assertEquals(0, buf[8].toInt())
        assertEquals(0, buf[9].toInt())
        assertEquals(0, buf[10].toInt())
        assertContentEquals(tag, buf.copyOfRange(11, 27))
        assertContentEquals(payload, buf.copyOfRange(27, buf.size))
    }

    @Test
    fun packParseRoundTrip() {
        val tag = ByteArray(16) { 7 }
        val frame = ControlFrame.parse(ControlFrame.pack(ControlFrame.FLAG_ENCRYPTED, tag, payload))

        assertTrue(frame.encrypted)
        assertContentEquals(tag, frame.tag)
        assertContentEquals(payload, frame.payload)
    }

    @Test
    fun plaintextFrameNotEncrypted() {
        val frame = ControlFrame.parse(ControlFrame.pack(0, ByteArray(16), payload))
        assertFalse(frame.encrypted)
    }

    @Test
    fun packRejectsOutOfRangePayload() {
        assertFailsWith<IllegalArgumentException> { ControlFrame.pack(0, ByteArray(16), ByteArray(0)) }
        assertFailsWith<IllegalArgumentException> { ControlFrame.pack(0, ByteArray(16), ByteArray(65537)) }
    }

    @Test
    fun packRejectsWrongTagSize() {
        assertFailsWith<IllegalArgumentException> { ControlFrame.pack(0, ByteArray(15), payload) }
    }

    @Test
    fun parseRejectsShortBuffer() {
        assertFailsWith<ProtocolException> { ControlFrame.parse(ByteArray(26)) }
    }

    @Test
    fun parseRejectsBadMagic() {
        val buf = ControlFrame.pack(0, ByteArray(16), payload)
        buf[0] = 'X'.code.toByte()
        assertFailsWith<ProtocolException> { ControlFrame.parse(buf) }
    }

    @Test
    fun parseRejectsZeroPayloadLength() {
        val buf = ControlFrame.pack(0, ByteArray(16), payload)
        for (i in 4..7) buf[i] = 0
        assertFailsWith<ProtocolException> { ControlFrame.parse(buf) }
    }

    @Test
    fun parseRejectsOversizedPayloadLength() {
        val buf = ControlFrame.pack(0, ByteArray(16), payload)
        buf[4] = 1
        buf[5] = 1
        assertFailsWith<ProtocolException> { ControlFrame.parse(buf) }
    }

    @Test
    fun parseRejectsTruncatedFrame() {
        val buf = ControlFrame.pack(0, ByteArray(16), payload)
        assertFailsWith<ProtocolException> { ControlFrame.parse(buf.copyOf(buf.size - 1)) }
        assertFailsWith<ProtocolException> { ControlFrame.parse(buf + 0.toByte()) }
    }

    @Test
    fun payloadLengthIsBigEndian() {
        val big = ByteArray(300) { 1 }
        val buf = ControlFrame.pack(0, ByteArray(16), big)
        assertContentEquals(Hex.decode("0000012c"), buf.copyOfRange(4, 8))
    }
}
