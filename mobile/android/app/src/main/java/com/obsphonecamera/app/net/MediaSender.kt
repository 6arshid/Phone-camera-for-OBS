package com.obsphonecamera.app.net

import com.obsphonecamera.app.protocol.MediaKeys
import com.obsphonecamera.app.protocol.MediaPacket
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.util.concurrent.atomic.AtomicLong

/**
 * Sends sealed media packets (PROTOCOL.md §4) over UDP to the server.
 */
class MediaSender(
    host: String,
    port: Int,
    private val keys: MediaKeys,
    sessionTag: ByteArray,
) {

    private val address: InetAddress = InetAddress.getByName(host)
    private val port = port
    private val sessionTag = sessionTag.copyOf()
    private val socket = DatagramSocket()

    private val videoFrameId = AtomicLong(0)
    private val audioFrameId = AtomicLong(0)

    @Volatile
    var videoBytesSent = AtomicLong(0)
    @Volatile
    var audioBytesSent = AtomicLong(0)
    @Volatile
    var videoFramesSent = AtomicLong(0)
    @Volatile
    var audioFramesSent = AtomicLong(0)

    @Volatile
    private var closed = false

    fun sendVideo(payload: ByteArray, keyframe: Boolean, timestampNs: Long) {
        if (closed) return
        val frameId = videoFrameId.incrementAndGet().toInt()
        sendFragments(MediaPacket.STREAM_VIDEO, keyframe, frameId, payload, timestampNs)
        videoFramesSent.incrementAndGet()
        videoBytesSent.addAndGet(payload.size.toLong())
    }

    fun sendAudio(payload: ByteArray, timestampNs: Long) {
        if (closed) return
        val frameId = audioFrameId.incrementAndGet().toInt()
        sendFragments(MediaPacket.STREAM_AUDIO, false, frameId, payload, timestampNs)
        audioFramesSent.incrementAndGet()
        audioBytesSent.addAndGet(payload.size.toLong())
    }

    fun resetFrameIds() {
        videoFrameId.set(0)
        audioFrameId.set(0)
    }

    fun close() {
        closed = true
        socket.close()
    }

    private fun sendFragments(stream: Int, keyframe: Boolean, frameId: Int, payload: ByteArray, timestampNs: Long) {
        val fragments = MediaPacket.fragment(payload)
        val count = fragments.size
        for ((idx, frag) in fragments.withIndex()) {
            val datagram = MediaPacket.seal(
                keys = keys,
                sessionTag = sessionTag,
                stream = stream,
                keyframe = keyframe,
                frameId = frameId,
                fragIndex = idx,
                fragCount = count,
                timestampNs = timestampNs,
                plaintext = frag,
            )
            try {
                socket.send(DatagramPacket(datagram, datagram.size, address, port))
            } catch (_: Exception) {
                return
            }
        }
    }
}
