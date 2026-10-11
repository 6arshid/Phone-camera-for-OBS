package com.obsphonecamera.app.net

import com.obsphonecamera.app.protocol.DiscoveryAnnouncement
import com.obsphonecamera.app.util.Bytes
import com.obsphonecamera.app.util.Hex
import org.json.JSONObject
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import java.net.InetSocketAddress
import java.net.MulticastSocket

/**
 * UDP discovery (PROTOCOL.md §2). Listens on 47109 for OBS announcements and
 * broadcasts a phone announcement every 2 s while enabled.
 */
class DiscoveryService(
    private val deviceIdHex: String,
    private val deviceName: String,
    private val listener: Listener,
) {

    interface Listener {
        fun onServerAnnouncement(announcement: DiscoveryAnnouncement, from: InetAddress)
    }

    @Volatile
    private var running = false
    @Volatile
    private var announcing = true

    private var thread: Thread? = null
    private var socket: MulticastSocket? = null

    private var lastAnnounceMs = 0L

    fun start() {
        if (running) return
        running = true
        thread = Thread({ run() }, "ocp-discovery").also { it.start() }
    }

    fun setAnnouncing(enabled: Boolean) {
        announcing = enabled
    }

    fun close() {
        running = false
        socket?.close()
        thread?.join(1000)
        thread = null
    }

    private fun run() {
        val sock = try {
            MulticastSocket(null).apply {
                reuseAddress = true
                broadcast = true
                bind(InetSocketAddress(PORT))
            }
        } catch (_: Exception) {
            return
        }
        socket = sock
        joinMulticast(sock)
        val buf = ByteArray(2048)
        while (running) {
            val now = System.currentTimeMillis()
            if (announcing && now - lastAnnounceMs >= ANNOUNCE_INTERVAL_MS) {
                lastAnnounceMs = now
                sendAnnouncement(sock)
            }
            val packet = DatagramPacket(buf, buf.size)
            try {
                sock.soTimeout = 500
                sock.receive(packet)
            } catch (_: Exception) {
                continue
            }
            val text = String(buf, 0, packet.length, Charsets.UTF_8)
            val announcement = DiscoveryAnnouncement.parse(text) ?: continue
            if (announcement.id == deviceIdHex) continue
            try {
                listener.onServerAnnouncement(announcement, packet.address)
            } catch (_: Exception) {
            }
        }
        sock.close()
    }

    private fun joinMulticast(sock: MulticastSocket) {
        try {
            val group = InetAddress.getByName(MULTICAST_GROUP)
            sock.joinGroup(group)
        } catch (_: Exception) {
        }
    }

    private fun sendAnnouncement(sock: DatagramSocket) {
        val json = JSONObject()
            .put("t", "phone")
            .put("v", 1)
            .put("id", deviceIdHex)
            .put("name", deviceName.take(64))
            .put("os", "android")
            .put("proto", "ocp/1")
            .toString()
        val data = json.toByteArray(Charsets.UTF_8)
        val targets = listOf("255.255.255.255", MULTICAST_GROUP)
        for (target in targets) {
            try {
                val addr = InetAddress.getByName(target)
                sock.send(DatagramPacket(data, data.size, addr, PORT))
            } catch (_: Exception) {
            }
        }
    }

    companion object {
        const val PORT = 47109
        const val MULTICAST_GROUP = "239.255.74.79"
        private const val ANNOUNCE_INTERVAL_MS = 2000L

        fun randomId(): String = Hex.encode(Bytes.randomBytes(16))
    }
}
