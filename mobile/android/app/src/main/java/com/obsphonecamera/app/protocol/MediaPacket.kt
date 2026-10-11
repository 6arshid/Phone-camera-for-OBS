package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.crypto.ChaCha20
import com.obsphonecamera.app.crypto.HmacSha256
import com.obsphonecamera.app.util.Bytes

class MediaKeys(encKey: ByteArray, macKey: ByteArray) {
    val encKey: ByteArray = encKey.copyOf()
    val macKey: ByteArray = macKey.copyOf()
}

class MediaFrame(
    val stream: Int,
    val keyframe: Boolean,
    val frameId: Int,
    val fragIndex: Int,
    val fragCount: Int,
    val timestampNs: Long,
    val payload: ByteArray,
)

/**
 * Media channel packets (PROTOCOL.md §4). One datagram = one fragment.
 */
object MediaPacket {

    const val HEADER_SIZE = 32
    const val TAG_SIZE = 16
    const val MAX_DATAGRAM = 1200
    const val MAX_CIPHERTEXT = MAX_DATAGRAM - HEADER_SIZE - TAG_SIZE
    const val STREAM_VIDEO = 0
    const val STREAM_AUDIO = 1
    const val FLAG_KEYFRAME = 0x01

    private val MAGIC = byteArrayOf('O'.code.toByte(), 'C'.code.toByte(), 'P'.code.toByte(), 'M'.code.toByte())

    fun fragmentCount(payloadSize: Int): Int = maxOf(1, (payloadSize + MAX_CIPHERTEXT - 1) / MAX_CIPHERTEXT)

    fun fragment(payload: ByteArray): List<ByteArray> {
        if (payload.isEmpty()) return listOf(ByteArray(0))
        val out = ArrayList<ByteArray>()
        var offset = 0
        while (offset < payload.size) {
            val end = minOf(offset + MAX_CIPHERTEXT, payload.size)
            out.add(payload.copyOfRange(offset, end))
            offset = end
        }
        return out
    }

    fun seal(
        keys: MediaKeys,
        sessionTag: ByteArray,
        stream: Int,
        keyframe: Boolean,
        frameId: Int,
        fragIndex: Int,
        fragCount: Int,
        timestampNs: Long,
        plaintext: ByteArray,
    ): ByteArray {
        require(sessionTag.size == 8) { "session tag must be 8 bytes" }
        require(plaintext.size <= MAX_CIPHERTEXT) { "fragment too large: ${plaintext.size}" }
        val header = buildHeader(stream, keyframe, sessionTag, frameId, fragIndex, fragCount, timestampNs)
        val nonce = nonceFor(sessionTag, stream, frameId, fragIndex)
        val ciphertext = ChaCha20.crypt(keys.encKey, nonce, plaintext)
        val tag = HmacSha256.mac(keys.macKey, header + ciphertext).copyOfRange(0, TAG_SIZE)
        return header + ciphertext + tag
    }

    fun open(keys: MediaKeys, datagram: ByteArray): MediaFrame {
        if (datagram.size < HEADER_SIZE + TAG_SIZE) throw ProtocolException("media datagram too short")
        for (i in 0 until 4) {
            if (datagram[i] != MAGIC[i]) throw ProtocolException("bad media magic")
        }
        if (datagram[4].toInt() != 1) throw ProtocolException("bad media version")
        val stream = datagram[5].toInt() and 0xff
        val flags = datagram[6].toInt() and 0xff
        val sessionTag = datagram.copyOfRange(8, 16)
        val frameId = be32(datagram, 16)
        val fragIndex = be16(datagram, 20)
        val fragCount = be16(datagram, 22)
        val timestampNs = be64(datagram, 24)
        if (fragCount < 1) throw ProtocolException("bad fragment count")
        if (fragIndex >= fragCount) throw ProtocolException("fragment index out of range")
        val ciphertextEnd = datagram.size - TAG_SIZE
        val ciphertext = datagram.copyOfRange(HEADER_SIZE, ciphertextEnd)
        val tag = datagram.copyOfRange(ciphertextEnd, datagram.size)
        val expected = HmacSha256.mac(keys.macKey, datagram.copyOfRange(0, HEADER_SIZE) + ciphertext)
            .copyOfRange(0, TAG_SIZE)
        if (!Bytes.constantTimeEquals(expected, tag)) {
            throw ProtocolException("media packet authentication failed")
        }
        val nonce = nonceFor(sessionTag, stream, frameId, fragIndex)
        val payload = ChaCha20.crypt(keys.encKey, nonce, ciphertext)
        return MediaFrame(
            stream = stream,
            keyframe = (flags and FLAG_KEYFRAME) != 0,
            frameId = frameId,
            fragIndex = fragIndex,
            fragCount = fragCount,
            timestampNs = timestampNs,
            payload = payload,
        )
    }

    private fun buildHeader(
        stream: Int,
        keyframe: Boolean,
        sessionTag: ByteArray,
        frameId: Int,
        fragIndex: Int,
        fragCount: Int,
        timestampNs: Long,
    ): ByteArray {
        val h = ByteArray(HEADER_SIZE)
        System.arraycopy(MAGIC, 0, h, 0, 4)
        h[4] = 1
        h[5] = stream.toByte()
        h[6] = if (keyframe) FLAG_KEYFRAME.toByte() else 0
        h[7] = 0
        System.arraycopy(sessionTag, 0, h, 8, 8)
        h[16] = (frameId ushr 24).toByte()
        h[17] = (frameId ushr 16).toByte()
        h[18] = (frameId ushr 8).toByte()
        h[19] = frameId.toByte()
        h[20] = (fragIndex ushr 8).toByte()
        h[21] = fragIndex.toByte()
        h[22] = (fragCount ushr 8).toByte()
        h[23] = fragCount.toByte()
        for (i in 0 until 8) {
            h[24 + i] = (timestampNs ushr (56 - 8 * i)).toByte()
        }
        return h
    }

    /** Nonce = session_tag[0..4) || u8 stream || u32 frame_id || u16 frag_idx || 0x00 (big endian fields). */
    private fun nonceFor(sessionTag: ByteArray, stream: Int, frameId: Int, fragIndex: Int): ByteArray {
        val n = ByteArray(12)
        System.arraycopy(sessionTag, 0, n, 0, 4)
        n[4] = stream.toByte()
        n[5] = (frameId ushr 24).toByte()
        n[6] = (frameId ushr 16).toByte()
        n[7] = (frameId ushr 8).toByte()
        n[8] = frameId.toByte()
        n[9] = (fragIndex ushr 8).toByte()
        n[10] = fragIndex.toByte()
        n[11] = 0
        return n
    }

    private fun be32(b: ByteArray, i: Int): Int =
        ((b[i].toInt() and 0xff) shl 24) or
            ((b[i + 1].toInt() and 0xff) shl 16) or
            ((b[i + 2].toInt() and 0xff) shl 8) or
            (b[i + 3].toInt() and 0xff)

    private fun be16(b: ByteArray, i: Int): Int =
        ((b[i].toInt() and 0xff) shl 8) or (b[i + 1].toInt() and 0xff)

    private fun be64(b: ByteArray, i: Int): Long {
        var v = 0L
        for (j in 0 until 8) {
            v = (v shl 8) or (b[i + j].toLong() and 0xff)
        }
        return v
    }
}
