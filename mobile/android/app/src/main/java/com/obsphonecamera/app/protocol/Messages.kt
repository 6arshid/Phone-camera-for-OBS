package com.obsphonecamera.app.protocol

import org.json.JSONObject

data class DiscoveryAnnouncement(
    val id: String,
    val name: String,
    val tcpPort: Int,
    val udpPort: Int,
) {
    companion object {
        fun parse(json: String): DiscoveryAnnouncement? {
            return try {
                val o = JSONObject(json)
                if (o.optString("t") != "obs") return null
                if (o.optInt("v", -1) != 1) return null
                val id = o.optString("id")
                if (id.length != 32) return null
                val tcp = o.optInt("tcp", -1)
                if (tcp !in 1..65535) return null
                DiscoveryAnnouncement(
                    id = id,
                    name = o.optString("name").take(64),
                    tcpPort = tcp,
                    udpPort = o.optInt("udp", -1).coerceIn(1, 65535),
                )
            } catch (_: Exception) {
                null
            }
        }
    }
}

data class VideoConfig(
    val codec: String,
    val width: Int,
    val height: Int,
    val fps: Int,
    val bitrateKbps: Int,
    val gopS: Int,
    val camera: String,
    val orientation: String,
)

data class CameraControlConfig(
    val focusMode: String,
    val focusDistance: Float,
    val exposureMode: String,
    val exposureBias: Float,
    val wbMode: String,
    val wbKelvin: Int,
    val torch: Boolean,
    val facePixelation: Boolean = false,
    val facePixelationStrength: Int = 50,
) {
    fun withLocalFacePixelation(enabled: Boolean, strength: Int): CameraControlConfig = copy(
        facePixelation = enabled || facePixelation,
        facePixelationStrength = when {
            enabled && facePixelation -> maxOf(strength, facePixelationStrength)
            enabled -> strength
            else -> facePixelationStrength
        }.coerceIn(1, 100),
    )
}

data class AudioConfig(
    val enabled: Boolean,
    val sampleRate: Int,
    val channels: Int,
)

data class StreamConfig(
    val video: VideoConfig,
    val camera: CameraControlConfig,
    val audio: AudioConfig,
    val active: Boolean,
) {
    companion object {
        fun parse(json: String): StreamConfig {
            val o = JSONObject(json)
            val v = o.optJSONObject("video") ?: JSONObject()
            val c = o.optJSONObject("camera") ?: JSONObject()
            val a = o.optJSONObject("audio") ?: JSONObject()
            return StreamConfig(
                video = VideoConfig(
                    codec = v.optString("codec", "h264"),
                    width = v.optInt("width", 1920).let { if (it == 0) 1280 else it.coerceIn(160, 3840) and -2 },
                    height = v.optInt("height", 1080).let { if (it == 0) 720 else it.coerceIn(120, 2160) and -2 },
                    fps = v.optInt("fps", 0).let { if (it <= 0) 30 else it.coerceIn(1, 120) },
                    bitrateKbps = v.optInt("bitrate_kbps", 0).coerceIn(0, 200000),
                    gopS = v.optInt("gop_s", 2).coerceIn(1, 30),
                    camera = v.optString("camera", "back"),
                    orientation = v.optString("orientation", "auto"),
                ),
                camera = CameraControlConfig(
                    focusMode = c.optString("focus_mode", "auto"),
                    focusDistance = c.optDouble("focus_distance", 0.0).toFloat().coerceIn(0f, 1f),
                    exposureMode = c.optString("exposure_mode", "auto"),
                    exposureBias = c.optDouble("exposure_bias", 0.0).toFloat().coerceIn(-2f, 2f),
                    wbMode = c.optString("wb_mode", "auto"),
                    wbKelvin = c.optInt("wb_kelvin", 5500).coerceIn(2500, 10000),
                    torch = c.optBoolean("torch", false),
                    facePixelation = c.optBoolean("face_pixelation", false),
                    facePixelationStrength = c.optInt("face_pixelation_strength", 50).coerceIn(1, 100),
                ),
                audio = AudioConfig(
                    enabled = a.optBoolean("enabled", false),
                    sampleRate = a.optInt("sample_rate", 48000),
                    channels = a.optInt("channels", 1),
                ),
                active = o.optJSONObject("stream")?.optBoolean("active", false) ?: false,
            )
        }
    }
}

data class HintMessage(
    val lossPct: Float,
    val rttMs: Int,
    val queueMs: Int,
) {
    companion object {
        fun parse(json: String): HintMessage {
            val o = JSONObject(json)
            return HintMessage(
                lossPct = o.optDouble("loss_pct", 0.0).toFloat(),
                rttMs = o.optInt("rtt_ms", -1),
                queueMs = o.optInt("queue_ms", -1),
            )
        }
    }
}

data class AuthOk(
    val sessionHex: String,
    val mediaPort: Int,
    val proofHex: String,
    val paired: Boolean,
) {
    companion object {
        fun parse(json: String): AuthOk {
            val o = JSONObject(json)
            val session = o.optString("session")
            if (!com.obsphonecamera.app.util.Hex.isValidHex(session, 32)) {
                throw ProtocolException("bad session id in auth_ok")
            }
            val port = o.optInt("media_port", -1)
            if (port !in 1..65535) throw ProtocolException("bad media_port in auth_ok")
            val proof = o.optString("proof")
            if (!com.obsphonecamera.app.util.Hex.isValidHex(proof, 64)) {
                throw ProtocolException("bad proof in auth_ok")
            }
            return AuthOk(
                sessionHex = session,
                mediaPort = port,
                proofHex = proof,
                paired = o.optBoolean("paired", false),
            )
        }
    }
}
