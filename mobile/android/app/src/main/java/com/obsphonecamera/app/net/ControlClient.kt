package com.obsphonecamera.app.net

import com.obsphonecamera.app.protocol.ControlCipher
import com.obsphonecamera.app.protocol.ControlFrame
import com.obsphonecamera.app.protocol.ProtocolException
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.EOFException
import java.io.IOException
import java.net.InetSocketAddress
import java.net.Socket

/**
 * TCP control channel (PROTOCOL.md §3). One reader thread; writes are
 * synchronized. Encryption is enabled after auth_ok via [enableEncryption].
 */
class ControlClient(
    private val host: String,
    private val port: Int,
    private val listener: Listener,
) {

    interface Listener {
        fun onPlaintextFrame(payload: ByteArray)
        fun onEncryptedFrame(payload: ByteArray)
        fun onDisconnected(reason: String?)
    }

    private var socket: Socket? = null
    private var input: BufferedInputStream? = null
    private var output: BufferedOutputStream? = null
    private var readerThread: Thread? = null

    @Volatile
    private var closed = false

    @Volatile
    private var cipher: ControlCipher? = null

    val isConnected: Boolean
        get() = socket?.isConnected == true && !closed

    fun connect(timeoutMs: Int = 8000) {
        val s = Socket()
        s.tcpNoDelay = true
        s.connect(InetSocketAddress(host, port), timeoutMs)
        socket = s
        input = BufferedInputStream(s.getInputStream(), 64 * 1024)
        output = BufferedOutputStream(s.getOutputStream(), 64 * 1024)
        closed = false
        readerThread = Thread({ readLoop() }, "ocp-control-read").also { it.start() }
    }

    fun enableEncryption(c: ControlCipher) {
        cipher = c
    }

    @Throws(IOException::class)
    fun sendPlaintext(payload: ByteArray) {
        writeFrame(ControlFrame.pack(0, ByteArray(16), payload))
    }

    @Throws(IOException::class)
    fun sendEncrypted(payload: ByteArray) {
        val c = cipher ?: throw IOException("encryption not enabled")
        writeFrame(c.seal(payload))
    }

    fun close() {
        closeWithReason(null)
    }

    private fun closeWithReason(reason: String?) {
        if (closed) return
        closed = true
        try {
            socket?.close()
        } catch (_: Exception) {
        }
        listener.onDisconnected(reason)
    }

    private fun writeFrame(frame: ByteArray) {
        val out = synchronized(this) {
            output ?: throw IOException("not connected")
        }
        synchronized(out) {
            out.write(frame)
            out.flush()
        }
    }

    private fun readLoop() {
        try {
            val inp = input ?: return
            val headerBuf = ByteArray(ControlFrame.HEADER_SIZE)
            while (!closed) {
                readFully(inp, headerBuf, 0, ControlFrame.HEADER_SIZE)
                val length = ((headerBuf[4].toInt() and 0xff) shl 24) or
                    ((headerBuf[5].toInt() and 0xff) shl 16) or
                    ((headerBuf[6].toInt() and 0xff) shl 8) or
                    (headerBuf[7].toInt() and 0xff)
                if (length < ControlFrame.MIN_PAYLOAD || length > ControlFrame.MAX_PAYLOAD) {
                    throw ProtocolException("bad payload length: $length")
                }
                val full = ByteArray(ControlFrame.HEADER_SIZE + length)
                System.arraycopy(headerBuf, 0, full, 0, ControlFrame.HEADER_SIZE)
                readFully(inp, full, ControlFrame.HEADER_SIZE, length)
                val frame = ControlFrame.parse(full)
                if (frame.encrypted) {
                    val c = cipher ?: throw ProtocolException("encrypted frame before keys")
                    val plain = c.open(frame)
                    listener.onEncryptedFrame(plain)
                } else {
                    listener.onPlaintextFrame(frame.payload)
                }
            }
        } catch (_: EOFException) {
            closeWithReason("connection closed by peer")
        } catch (e: IOException) {
            closeWithReason(e.message ?: "connection error")
        } catch (_: ProtocolException) {
            closeWithReason("protocol error")
        }
    }

    private fun readFully(inp: BufferedInputStream, buf: ByteArray, off: Int, len: Int) {
        var read = 0
        while (read < len) {
            val n = inp.read(buf, off + read, len - read)
            if (n < 0) throw EOFException("end of stream")
            read += n
        }
    }
}
