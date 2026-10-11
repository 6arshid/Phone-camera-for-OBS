package com.obsphonecamera.app.protocol

import com.obsphonecamera.app.util.Hex
import java.net.URLDecoder

data class PairingInfo(
    val host: String,
    val port: Int,
    val secretHex: String,
    val name: String,
)

/**
 * QR pairing payload (PROTOCOL.md §6.1):
 * ocp1://pair?h=<ip>&p=<tcp-port>&s=<64-hex>&n=<name>
 */
object PairingUri {

    const val SCHEME = "ocp1"
    const val AUTHORITY_PAIR = "pair"

    fun parse(text: String): PairingInfo? {
        val trimmed = text.trim()
        if (!trimmed.startsWith("$SCHEME://$AUTHORITY_PAIR?")) return null
        val query = trimmed.substringAfter('?')
        var host = ""
        var port = -1
        var secret = ""
        var name = ""
        for (part in query.split('&')) {
            if (part.isEmpty()) continue
            val eq = part.indexOf('=')
            if (eq <= 0) return null
            val key = part.substring(0, eq)
            val value = URLDecoder.decode(part.substring(eq + 1), "UTF-8")
            when (key) {
                "h" -> host = value
                "p" -> port = value.toIntOrNull() ?: return null
                "s" -> secret = value.lowercase()
                "n" -> name = value
            }
        }
        if (host.isEmpty()) return null
        if (port !in 1..65535) return null
        if (!Hex.isValidHex(secret, 64)) return null
        return PairingInfo(host = host, port = port, secretHex = secret, name = name)
    }
}
