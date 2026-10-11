package com.obsphonecamera.app.protocol

class ProtocolException(message: String) : Exception(message)

/**
 * OCP/1 control frame header (PROTOCOL.md §3). Header is 27 bytes:
 * magic "OCP1" | u32 BE payload length | u8 flags | 2 reserved | 16-byte tag.
 */
class ControlFrame(
    val flags: Int,
    val tag: ByteArray,
    val payload: ByteArray,
) {
    val encrypted: Boolean get() = (flags and FLAG_ENCRYPTED) != 0

    companion object {
        const val HEADER_SIZE = 27
        const val MIN_PAYLOAD = 1
        const val MAX_PAYLOAD = 65536
        const val FLAG_ENCRYPTED = 0x01

        private val MAGIC = byteArrayOf('O'.code.toByte(), 'C'.code.toByte(), 'P'.code.toByte(), '1'.code.toByte())

        fun pack(flags: Int, tag: ByteArray, payload: ByteArray): ByteArray {
            require(payload.size in MIN_PAYLOAD..MAX_PAYLOAD) { "payload length out of range: ${payload.size}" }
            require(tag.size == 16) { "tag must be 16 bytes" }
            val out = ByteArray(HEADER_SIZE + payload.size)
            System.arraycopy(MAGIC, 0, out, 0, 4)
            out[4] = (payload.size ushr 24).toByte()
            out[5] = (payload.size ushr 16).toByte()
            out[6] = (payload.size ushr 8).toByte()
            out[7] = payload.size.toByte()
            out[8] = flags.toByte()
            out[9] = 0
            out[10] = 0
            System.arraycopy(tag, 0, out, 11, 16)
            System.arraycopy(payload, 0, out, HEADER_SIZE, payload.size)
            return out
        }

        fun parse(buf: ByteArray): ControlFrame {
            if (buf.size < HEADER_SIZE) throw ProtocolException("frame too short: ${buf.size}")
            for (i in 0 until 4) {
                if (buf[i] != MAGIC[i]) throw ProtocolException("bad magic")
            }
            val len = ((buf[4].toInt() and 0xff) shl 24) or
                ((buf[5].toInt() and 0xff) shl 16) or
                ((buf[6].toInt() and 0xff) shl 8) or
                (buf[7].toInt() and 0xff)
            if (len < MIN_PAYLOAD || len > MAX_PAYLOAD) throw ProtocolException("bad payload length: $len")
            if (buf.size != HEADER_SIZE + len) throw ProtocolException("truncated frame")
            val flags = buf[8].toInt() and 0xff
            val tag = buf.copyOfRange(11, 27)
            val payload = buf.copyOfRange(HEADER_SIZE, HEADER_SIZE + len)
            return ControlFrame(flags, tag, payload)
        }
    }
}
