package com.obsphonecamera.app.net

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.net.Uri
import android.os.Build
import android.os.BatteryManager
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import com.obsphonecamera.app.audio.MicCapture
import com.obsphonecamera.app.camera.AvatarSettings
import com.obsphonecamera.app.camera.CameraEntry
import com.obsphonecamera.app.camera.GlPipeline
import com.obsphonecamera.app.camera.StreamController
import com.obsphonecamera.app.protocol.AuthOk
import com.obsphonecamera.app.protocol.AvatarAssembler
import com.obsphonecamera.app.protocol.AvatarConfig
import com.obsphonecamera.app.protocol.CameraControlConfig
import com.obsphonecamera.app.protocol.ControlCipher
import com.obsphonecamera.app.protocol.HintMessage
import com.obsphonecamera.app.protocol.KeySchedule
import com.obsphonecamera.app.protocol.PairingInfo
import com.obsphonecamera.app.protocol.ProtocolException
import com.obsphonecamera.app.protocol.SessionKeys
import com.obsphonecamera.app.protocol.StreamConfig
import com.obsphonecamera.app.util.AvatarStore
import com.obsphonecamera.app.util.Hex
import com.obsphonecamera.app.util.Prefs
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.Executors
import java.util.concurrent.CountDownLatch
import java.util.concurrent.ScheduledFuture
import java.util.concurrent.TimeUnit

enum class SessionState {
    IDLE, CONNECTING, HANDSHAKE, CONNECTED, STREAMING, RECONNECTING, ERROR
}

data class StatsSnapshot(
    val fps: Float,
    val bitrateKbps: Int,
    val targetBitrateKbps: Int,
    val frames: Long,
    val batteryPct: Int,
    val thermal: String,
    val rttMs: Int,
    val camera: String,
    val streaming: Boolean,
)

/**
 * Drives the OCP/1 client session: handshake, config, stats, media, reconnect.
 */
class SessionManager(
    context: Context,
    private val prefs: Prefs,
    private val listener: Listener,
) {

    interface Listener {
        fun onStateChanged(state: SessionState, detail: String)
        fun onStats(stats: StatsSnapshot)
        fun onStreamActive(active: Boolean)
        fun onVideoConfig(width: Int, height: Int, fps: Int, bitrateKbps: Int, gopS: Int)
        fun onUserMicMuteChanged(muted: Boolean)
        fun onPairingStored(ip: String, port: Int, name: String)
    }

    private val appContext = context.applicationContext
    private val mainHandler = Handler(appContext.mainLooper)
    private val executor = Executors.newSingleThreadExecutor { r -> Thread(r, "ocp-session") }
    private val timerScheduler = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "ocp-timers") }

    private var control: ControlClient? = null
    private var connectionGeneration = 0L
    private var mediaSender: MediaSender? = null
    private var stream: StreamController? = null
    private var mic: MicCapture? = null

    @Volatile
    var state: SessionState = SessionState.IDLE
        private set

    @Volatile
    var lastError: String? = null
        private set

    private var host = ""
    private var port = 47100
    private var serverNameHint = ""

    private var kdf = KeySchedule.KDF_HKDF
    private var pbkdf2Iter = 0
    private var pendingCode: String? = null
    private var pendingSecretHex: String? = null
    private var masterHex = ""

    private var clientNonceHex = ""
    private var serverNonceHex = ""
    private var helloJson = ""
    private var challengeJson = ""
    private var sessionKeys: SessionKeys? = null
    private var sessionTag = ByteArray(8)
    private var mediaPort = 47101

    private var config: StreamConfig? = null
    private var streamStartNs = 0L

    private var statsTask: ScheduledFuture<*>? = null
    private var reconnectTask: ScheduledFuture<*>? = null
    private var reconnectAttempt = 0
    private var userDisconnect = false

    private var lastVideoFrames = 0L
    private var lastVideoBytes = 0L
    private var lastStatsMs = 0L
    private var totalFrames = 0L

    private var configuredTargetKbps = 4000
    private var workingKbps = 4000
    private var lastBitrateChangeMs = 0L
    private var lowLossSeconds = 0
    private var lastHintRttMs = -1

    private var localMicMuted = false

    private val avatarStore = AvatarStore(appContext, prefs)
    private val avatarAssembler = AvatarAssembler()
    private var obsAvatar: Pair<String, Bitmap>? = null
    private var localAvatar: Bitmap? = null

    private var previewSurface: Surface? = null
    private var previewWidth = 0
    private var previewHeight = 0
    private var deviceDegrees = 0

    val isStreaming: Boolean get() = state == SessionState.STREAMING

    // ---------------------------------------------------------------- connect

    fun connectQr(info: PairingInfo) {
        executor.execute {
            pendingSecretHex = info.secretHex
            pendingCode = null
            serverNameHint = info.name.ifEmpty { info.host }
            startConnection(info.host, info.port)
        }
    }

    fun connectManual(host: String, port: Int, pairingCode: String?) {
        executor.execute {
            val code = pairingCode?.trim()?.takeIf { it.isNotEmpty() }
            if (code != null) {
                pendingCode = code
                pendingSecretHex = null
            } else {
                pendingCode = null
                pendingSecretHex = prefs.pairedSecretHex.takeIf {
                    it.length == 64 && prefs.pairedServerIp == host
                }
            }
            serverNameHint = host
            startConnection(host, port)
        }
    }

    fun connectStored() {
        executor.execute {
            if (!prefs.hasPairing()) {
                updateState(SessionState.ERROR, "no stored pairing")
                return@execute
            }
            pendingSecretHex = prefs.pairedSecretHex
            pendingCode = null
            startConnection(prefs.pairedServerIp, prefs.pairedServerPort)
        }
    }

    fun disconnect(sendBye: Boolean) {
        executor.execute {
            userDisconnect = true
            reconnectAttempt = 0
            if (sendBye) {
                try {
                    control?.sendEncrypted(JSONObject().put("type", "bye").put("reason", "user_stop").toString().toByteArray())
                } catch (_: Exception) {
                }
            }
            teardownSession()
            updateState(SessionState.IDLE, "disconnected")
        }
    }

    fun forgetPairing() {
        executor.execute { prefs.clearPairing() }
    }

    // ------------------------------------------------------------- UI actions

    fun setPreviewSurface(surface: Surface?, width: Int, height: Int) {
        executor.execute {
            previewSurface = surface
            previewWidth = width
            previewHeight = height
            stream?.setPreviewSurface(surface, width, height)
        }
    }

    fun setDeviceDegrees(degrees: Int) {
        executor.execute {
            deviceDegrees = degrees
            stream?.setDeviceDegrees(degrees)
        }
    }

    fun userSwitchCamera() {
        executor.execute {
            val s = stream ?: return@execute
            val target = if (s.currentCameraFront()) "back" else "front"
            val cfg = config ?: return@execute
            val newVideo = cfg.video.copy(camera = target)
            config = cfg.copy(video = newVideo)
            if (state == SessionState.STREAMING) {
                s.start(
                    StreamController.PipelineConfig(
                        camera = s.resolveCameraId(target),
                        outWidth = newVideo.width,
                        outHeight = newVideo.height,
                        fps = newVideo.fps,
                        bitrateKbps = workingKbps,
                        gopS = newVideo.gopS,
                        orientation = newVideo.orientation,
                    ),
                )
                s.requestKeyFrame()
            }
        }
    }

    fun userSetResolution(width: Int, height: Int) {
        executor.execute {
            val cfg = config ?: return@execute
            config = cfg.copy(video = cfg.video.copy(width = width, height = height))
            prefs.lastResolution = "${width}x$height"
            reconfigureVideoIfStreaming()
        }
    }

    fun userSetFps(fps: Int) {
        executor.execute {
            val cfg = config ?: return@execute
            config = cfg.copy(video = cfg.video.copy(fps = fps))
            prefs.lastFps = fps
            reconfigureVideoIfStreaming()
        }
    }

    fun userSetFacePixelation(enabled: Boolean, strength: Int) {
        executor.execute {
            prefs.facePixelation = enabled
            prefs.facePixelationStrength = strength
            config?.let { stream?.applyControls(effectiveCameraControls(it.camera)) }
        }
    }

    fun userSetAvatar(enabled: Boolean, settings: AvatarSettings) {
        executor.execute {
            prefs.avatarEnabled = enabled
            prefs.avatarSettings = settings
            applyAvatar()
        }
    }

    /** Imports a picked image; [done] runs on the main thread with the result. */
    fun userImportAvatar(uri: Uri, done: (Boolean) -> Unit) {
        executor.execute {
            val ok = try { avatarStore.importLocal(uri) } catch (_: Exception) { false }
            if (ok) {
                localAvatar = null
                applyAvatar()
            }
            mainHandler.post { done(ok) }
        }
    }

    val hasLocalAvatar: Boolean get() = avatarStore.hasLocal

    /** OBS's avatar wins once its image has arrived; otherwise the phone's own, if enabled. */
    private fun applyAvatar() {
        val obs = config?.avatar
        val spec = obsAvatarSpec(obs) ?: if (prefs.avatarEnabled) {
            val bmp = localAvatar ?: avatarStore.loadLocal()?.also { localAvatar = it }
            bmp?.let { GlPipeline.AvatarSpec(it, prefs.avatarSettings) }
        } else null
        stream?.setAvatar(spec)
    }

    private fun obsAvatarSpec(obs: AvatarConfig?): GlPipeline.AvatarSpec? {
        if (obs == null || !obs.hasImage) return null
        val cached = obsAvatar
        val bmp = if (cached != null && cached.first == obs.image) cached.second
            else avatarStore.loadObs(obs.image)?.also { obsAvatar = obs.image to it }
        bmp ?: return null
        return GlPipeline.AvatarSpec(
            bmp,
            AvatarSettings(obs.mode, obs.cols, obs.rows, obs.fps, obs.scale, obs.offsetY, obs.flip),
        )
    }

    private fun onAvatarChunk(text: String) {
        val (hash, bytes) = avatarAssembler.accept(text) ?: return
        try {
            avatarStore.saveObs(hash, bytes)
        } catch (_: Exception) {
            return
        }
        obsAvatar = null
        applyAvatar()
    }

    private fun effectiveCameraControls(camera: CameraControlConfig): CameraControlConfig {
        return camera.withLocalFacePixelation(prefs.facePixelation, prefs.facePixelationStrength)
    }

    fun userSetTorch(on: Boolean) {
        executor.execute {
            val cfg = config ?: return@execute
            val camCfg = cfg.camera.copy(torch = on)
            config = cfg.copy(camera = camCfg)
            stream?.applyControls(effectiveCameraControls(camCfg))
        }
    }

    fun userSetFocus(mode: String, distance: Float) {
        executor.execute {
            val cfg = config ?: return@execute
            val camCfg = cfg.camera.copy(focusMode = mode, focusDistance = distance)
            config = cfg.copy(camera = camCfg)
            stream?.applyControls(effectiveCameraControls(camCfg))
        }
    }

    fun userSetExposure(bias: Float) {
        executor.execute {
            val cfg = config ?: return@execute
            val camCfg = cfg.camera.copy(exposureMode = "auto", exposureBias = bias)
            config = cfg.copy(camera = camCfg)
            stream?.applyControls(effectiveCameraControls(camCfg))
        }
    }

    fun userSetWb(mode: String, kelvin: Int) {
        executor.execute {
            val cfg = config ?: return@execute
            val camCfg = cfg.camera.copy(wbMode = mode, wbKelvin = kelvin)
            config = cfg.copy(camera = camCfg)
            stream?.applyControls(effectiveCameraControls(camCfg))
        }
    }

    fun userSetMicMuted(muted: Boolean) {
        executor.execute {
            localMicMuted = muted
            applyMicState()
            listener.onUserMicMuteChanged(muted)
        }
    }

    val cameras: List<CameraEntry>
        get() = stream?.listCameras() ?: emptyList()

    val currentConfig: StreamConfig?
        get() = config

    // ----------------------------------------------------------- connection

    private fun startConnection(newHost: String, newPort: Int) {
        teardownSession()
        if (pendingSecretHex == null && pendingCode == null) {
            userDisconnect = true
            updateState(SessionState.ERROR, appContext.getString(com.obsphonecamera.app.R.string.pairing_required))
            return
        }
        userDisconnect = false
        host = newHost
        port = newPort
        lastError = null
        if (pendingCode != null) {
            kdf = KeySchedule.KDF_PBKDF2
            pbkdf2Iter = KeySchedule.PBKDF2_MIN_ITER
        } else {
            kdf = KeySchedule.KDF_HKDF
            pbkdf2Iter = 0
        }
        updateState(SessionState.CONNECTING, "$host:$port")
        val client = ControlClient(host, port, controlListener(connectionGeneration))
        control = client
        try {
            client.connect()
        } catch (e: Exception) {
            lastError = e.message ?: "connect failed"
            updateState(SessionState.ERROR, lastError ?: "")
            scheduleReconnect()
            return
        }
        sendHello()
    }

    private fun sendHello() {
        val saltHex = Hex.encode(com.obsphonecamera.app.util.Bytes.randomBytes(16))
        clientNonceHex = Hex.encode(com.obsphonecamera.app.util.Bytes.randomBytes(16))
        if (prefs.deviceIdHex.isEmpty()) {
            prefs.deviceIdHex = DiscoveryService.randomId()
        }
        val hello = JSONObject()
            .put("type", "hello")
            .put("proto", "ocp/1")
            .put("kdf", kdf)
            .put("iter", pbkdf2Iter)
            .put("salt", saltHex)
            .put("nonce", clientNonceHex)
            .put(
                "device",
                JSONObject()
                    .put("id", prefs.deviceIdHex)
                    .put("name", prefs.deviceName.take(64))
                    .put("os", "android")
                    .put("app", APP_VERSION),
            )
        helloJson = hello.toString()
        try {
            control?.sendPlaintext(helloJson.toByteArray(Charsets.UTF_8))
        } catch (e: Exception) {
            lastError = e.message ?: "send hello failed"
            updateState(SessionState.ERROR, lastError ?: "")
        }
    }

    private fun controlListener(generation: Long) = object : ControlClient.Listener {
        override fun onPlaintextFrame(payload: ByteArray) {
            if (executor.isShutdown) return
            val processed = CountDownLatch(1)
            executor.execute {
                try {
                    if (generation == connectionGeneration) handlePlaintext(payload)
                } finally {
                    processed.countDown()
                }
            }
            // auth_ok must install keys before the reader consumes encrypted config.
            processed.await()
        }

        override fun onEncryptedFrame(payload: ByteArray) {
            if (executor.isShutdown) return
            executor.execute { if (generation == connectionGeneration) handleEncrypted(payload) }
        }

        override fun onDisconnected(reason: String?) {
            if (executor.isShutdown) return
            executor.execute {
                if (generation != connectionGeneration) return@execute
                val wasStreaming = state == SessionState.STREAMING
                teardownSession()
                if (wasStreaming) {
                    listener.onStreamActive(false)
                }
                if (!userDisconnect) {
                    lastError = reason
                    updateState(SessionState.ERROR, reason ?: "connection lost")
                    scheduleReconnect()
                }
            }
        }
    }

    private fun handlePlaintext(payload: ByteArray) {
        val text = payload.toString(Charsets.UTF_8)
        val type = try {
            JSONObject(text).optString("type")
        } catch (_: Exception) {
            return
        }
        when (type) {
            "challenge" -> onChallenge(text)
            "auth_ok" -> onAuthOk(text)
            "error" -> onErrorFrame(text)
            "bye" -> {
                control?.close()
            }
            else -> Unit
        }
    }

    private fun handleEncrypted(payload: ByteArray) {
        val text = payload.toString(Charsets.UTF_8)
        val type = try {
            JSONObject(text).optString("type")
        } catch (_: Exception) {
            return
        }
        when (type) {
            "config" -> onConfig(text)
            "ping" -> onPing(text)
            "hint" -> onHint(text)
            "avatar_chunk" -> onAvatarChunk(text)
            "error" -> onErrorFrame(text)
            "bye" -> control?.close()
            else -> Unit
        }
    }

    private fun onChallenge(challengeText: String) {
        updateState(SessionState.HANDSHAKE, "challenge")
        challengeJson = challengeText
        val o = JSONObject(challengeText)
        val serverNonce = o.optString("nonce")
        if (!Hex.isValidHex(serverNonce, 32)) {
            sendErrorAndClose("protocol_error", "bad server nonce")
            return
        }
        serverNonceHex = serverNonce
        val master = try {
            resolveMaster()
        } catch (_: Exception) {
            null
        }
        if (master == null) {
            sendErrorAndClose("pairing_failed", "no pairing secret available")
            return
        }
        masterHex = Hex.encode(master)
        val cNonce = Hex.decode(clientNonceHex)
        val sNonce = Hex.decode(serverNonceHex)
        val transcript = KeySchedule.transcript(
            helloJson.toByteArray(Charsets.UTF_8),
            challengeJson.toByteArray(Charsets.UTF_8),
        )
        val keys = KeySchedule.keys(master, cNonce, sNonce)
        sessionKeys = keys
        val proof = KeySchedule.clientProof(keys.kAuth, cNonce, sNonce, transcript)
        val auth = JSONObject()
            .put("type", "auth")
            .put("nonce", clientNonceHex)
            .put("proof", Hex.encode(proof))
        try {
            control?.sendPlaintext(auth.toString().toByteArray(Charsets.UTF_8))
        } catch (e: Exception) {
            lastError = e.message
            updateState(SessionState.ERROR, "auth send failed")
        }
    }

    private fun resolveMaster(): ByteArray? {
        pendingSecretHex?.let { return Hex.decode(it) }
        pendingCode?.let { code ->
            val salt = Hex.decode(JSONObject(helloJson).optString("salt"))
            return com.obsphonecamera.app.crypto.Pbkdf2.derive(
                code.toByteArray(Charsets.US_ASCII),
                salt,
                pbkdf2Iter,
                32,
            )
        }
        return null
    }

    private fun onAuthOk(text: String) {
        val ok = try {
            AuthOk.parse(text)
        } catch (e: ProtocolException) {
            sendErrorAndClose("protocol_error", e.message ?: "bad auth_ok")
            return
        }
        val keys = sessionKeys
        if (keys == null) {
            sendErrorAndClose("protocol_error", "auth_ok before challenge")
            return
        }
        val expected = KeySchedule.serverProof(
            keys.kAuth,
            Hex.decode(serverNonceHex),
            Hex.decode(clientNonceHex),
            KeySchedule.transcript(
                helloJson.toByteArray(Charsets.UTF_8),
                challengeJson.toByteArray(Charsets.UTF_8),
            ),
        )
        if (!com.obsphonecamera.app.util.Bytes.constantTimeEquals(expected, Hex.decode(ok.proofHex))) {
            sendErrorAndClose("auth_failed", "server proof mismatch")
            return
        }
        sessionTag = Hex.decode(ok.sessionHex).copyOfRange(0, 8)
        mediaPort = ok.mediaPort
        if (ok.paired) {
            prefs.pairedServerIp = host
            prefs.pairedServerPort = port
            prefs.pairedServerName = serverNameHint.ifEmpty { host }
            prefs.pairedSecretHex = masterHex
            mainHandler.post { listener.onPairingStored(host, port, prefs.pairedServerName) }
        }
        reconnectAttempt = 0
        reconnectTask?.cancel(false)
        reconnectTask = null
        val cipher = keys.controlCipher()
        control?.enableEncryption(cipher)
        try {
            mediaSender = MediaSender(host, mediaPort, keys.mediaKeys(), sessionTag)
        } catch (e: Exception) {
            lastError = e.message
            updateState(SessionState.ERROR, "media socket failed")
            return
        }
        if (stream == null) {
            stream = StreamController(appContext, { data, keyframe, tsNs ->
                val start = streamStartNs
                if (start != 0L) {
                    mediaSender?.sendVideo(data, keyframe, tsNs - start)
                }
            }, { msg ->
                lastError = msg
                updateState(SessionState.ERROR, msg)
            })
        }
        stream?.setPreviewSurface(previewSurface, previewWidth, previewHeight)
        stream?.setDeviceDegrees(deviceDegrees)
        sendReady()
        updateState(SessionState.CONNECTED, "session established")
    }

    private fun sendReady() {
        val videoCaps = JSONObject()
            .put("codec", JSONArray().put("h264"))
            .put(
                "resolutions",
                JSONArray()
                    .put(JSONArray().put(3840).put(2160))
                    .put(JSONArray().put(1920).put(1080))
                    .put(JSONArray().put(1280).put(720))
                    .put(JSONArray().put(640).put(360)),
            )
            .put("fps", JSONArray().put(60).put(30).put(24))
            .put("aspect_ratios", JSONArray().put("4:3").put("16:9"))
            .put("max_bitrate_kbps", 50000)
        val audioCaps = JSONObject()
            .put("codecs", JSONArray().put("pcm_s16le"))
            .put("sample_rates", JSONArray().put(48000))
            .put("channels", JSONArray().put(1))
        val camerasArr = JSONArray()
        val features = JSONObject().put("torch", true).put("manual_focus", true)
            .put("exposure", true).put("white_balance", true).put("avatar", true)
        stream?.listCameras()?.forEach { entry ->
            camerasArr.put(JSONObject().put("id", if (entry.facingFront) "front" else "back").put("label", entry.label))
            features.put("torch", features.optBoolean("torch") && entry.supportsTorch)
            features.put("manual_focus", features.optBoolean("manual_focus") && entry.supportsManualFocus)
            features.put("exposure", features.optBoolean("exposure") && entry.supportsExposureBias)
            features.put("white_balance", features.optBoolean("white_balance") && entry.supportsManualWb)
        }
        val ready = JSONObject()
            .put("type", "ready")
            .put(
                "caps",
                JSONObject()
                    .put("video", videoCaps)
                    .put("audio", audioCaps)
                    .put("cameras", camerasArr)
                    .put("features", features)
                    // Lets OBS skip re-sending an image the phone already cached.
                    .put("avatar_have", avatarStore.obsHash),
            )
        sendEncrypted(ready)
    }

    private fun onConfig(text: String) {
        val cfg = try {
            StreamConfig.parse(text)
        } catch (_: Exception) {
            return
        }
        val prev = config
        config = cfg
        configuredTargetKbps = if (cfg.video.bitrateKbps > 0) cfg.video.bitrateKbps else DEFAULT_BITRATE_Kbps
        if (prev == null || prev.video.bitrateKbps != cfg.video.bitrateKbps) {
            workingKbps = configuredTargetKbps
        }
        mainHandler.post {
            listener.onVideoConfig(
                cfg.video.width,
                cfg.video.height,
                cfg.video.fps,
                configuredTargetKbps,
                cfg.video.gopS,
            )
        }
        if (cfg.active) {
            startPipelineIfNeeded(cfg)
        } else {
            stopPipeline()
        }
        stream?.applyControls(effectiveCameraControls(cfg.camera))
        applyAvatar()
        applyMicState()
    }

    private fun reconfigureVideoIfStreaming() {
        val cfg = config ?: return
        if (state == SessionState.STREAMING) {
            startPipeline(cfg)
            stream?.requestKeyFrame()
        }
    }

    private fun startPipelineIfNeeded(cfg: StreamConfig) {
        if (state == SessionState.STREAMING && stream?.isRunning == true) {
            return
        }
        startPipeline(cfg)
    }

    private fun startPipeline(cfg: StreamConfig) {
        val s = stream ?: return
        streamStartNs = System.nanoTime()
        s.applyControls(effectiveCameraControls(cfg.camera))
        s.start(
            StreamController.PipelineConfig(
                camera = s.resolveCameraId(cfg.video.camera),
                outWidth = cfg.video.width,
                outHeight = cfg.video.height,
                fps = cfg.video.fps,
                bitrateKbps = workingKbps,
                gopS = cfg.video.gopS,
                orientation = cfg.video.orientation,
            ),
        )
        if (!s.isRunning) {
            updateState(SessionState.ERROR, lastError ?: "Camera pipeline could not start")
            return
        }
        mediaSender?.resetFrameIds()
        lastVideoFrames = 0
        lastVideoBytes = 0
        totalFrames = 0
        lastStatsMs = System.currentTimeMillis()
        s.requestKeyFrame()
        mic?.stop()
        mic = null
        applyMicState()
        updateState(SessionState.STREAMING, "streaming")
        mainHandler.post { listener.onStreamActive(true) }
        startStats()
    }

    private fun stopPipeline() {
        if (state == SessionState.STREAMING) {
            stopStats()
            stream?.stop()
            mic?.stop()
            streamStartNs = 0
            updateState(SessionState.CONNECTED, "stream stopped")
            mainHandler.post { listener.onStreamActive(false) }
        }
    }

    private fun applyMicState() {
        val cfg = config ?: run {
            mic?.stop()
            return
        }
        val want = cfg.active && cfg.audio.enabled && !localMicMuted && state == SessionState.STREAMING
        if (want) {
            if (mic == null) {
                val sender = mediaSender ?: return
                val startNs = streamStartNs
                mic = MicCapture { data, tsNs ->
                    sender.sendAudio(data, if (startNs != 0L) tsNs - startNs else 0L)
                }
            }
            val started = mic?.start(cfg.audio.sampleRate) == true
            if (!started) {
                mic?.stop()
                mic = null
            }
        } else {
            mic?.stop()
            mic = null
        }
    }

    private fun onPing(text: String) {
        val t = try {
            JSONObject(text).optLong("t", 0L)
        } catch (_: Exception) {
            0L
        }
        sendEncrypted(JSONObject().put("type", "pong").put("t", t))
    }

    private fun onHint(text: String) {
        val hint = try {
            HintMessage.parse(text)
        } catch (_: Exception) {
            return
        }
        lastHintRttMs = hint.rttMs
        adaptBitrate(hint.lossPct)
    }

    private fun adaptBitrate(lossPct: Float) {
        if (state != SessionState.STREAMING) return
        val now = System.currentTimeMillis()
        if (now - lastBitrateChangeMs < BITRATE_MIN_INTERVAL_MS) return
        if (lossPct > 5f) {
            lowLossSeconds = 0
            val next = (workingKbps * 0.8).toInt().coerceAtLeast(MIN_BITRATE_Kbps)
            if (next < workingKbps) {
                workingKbps = next
                lastBitrateChangeMs = now
                stream?.setBitrateKbps(next)
            }
        } else if (lossPct < 1f) {
            lowLossSeconds++
            if (lowLossSeconds >= 10 && workingKbps < configuredTargetKbps) {
                lowLossSeconds = 0
                workingKbps = (workingKbps * 1.1).toInt().coerceAtMost(configuredTargetKbps)
                lastBitrateChangeMs = now
                stream?.setBitrateKbps(workingKbps)
            }
        } else {
            lowLossSeconds = 0
        }
    }

    private fun onErrorFrame(text: String) {
        val o = try {
            JSONObject(text)
        } catch (_: Exception) {
            return
        }
        val code = o.optString("code", "protocol_error")
        val msg = o.optString("msg", code)
        lastError = "$code: $msg"
        updateState(SessionState.ERROR, lastError ?: "")
        control?.close()
    }

    private fun sendErrorAndClose(code: String, msg: String) {
        try {
            val json = JSONObject().put("type", "error").put("code", code).put("msg", msg)
            control?.sendPlaintext(json.toString().toByteArray(Charsets.UTF_8))
        } catch (_: Exception) {
        }
        lastError = "$code: $msg"
        updateState(SessionState.ERROR, lastError ?: "")
        control?.close()
    }

    private fun sendEncrypted(json: JSONObject) {
        try {
            control?.sendEncrypted(json.toString().toByteArray(Charsets.UTF_8))
        } catch (_: Exception) {
        }
    }

    // ------------------------------------------------------------- stats/abr

    private fun startStats() {
        statsTask?.cancel(false)
        statsTask = timerScheduler.scheduleAtFixedRate({
            executor.execute { sendStatsTick() }
        }, 1000, 1000, TimeUnit.MILLISECONDS)
    }

    private fun stopStats() {
        statsTask?.cancel(false)
        statsTask = null
    }

    private fun sendStatsTick() {
        if (state != SessionState.STREAMING) return
        val sender = mediaSender ?: return
        val nowMs = System.currentTimeMillis()
        val elapsed = (nowMs - lastStatsMs).coerceAtLeast(1)
        val frames = sender.videoFramesSent.get()
        val bytes = sender.videoBytesSent.get()
        val fps = (frames - lastVideoFrames) * 1000f / elapsed
        val bitrate = ((bytes - lastVideoBytes) * 8 * 1000) / elapsed / 1000
        lastVideoFrames = frames
        lastVideoBytes = bytes
        totalFrames = frames
        lastStatsMs = nowMs
        val tsRel = if (streamStartNs != 0L) System.nanoTime() - streamStartNs else 0L
        val battery = batteryPct()
        val thermal = thermalStatus()
        val cameraName = if (stream?.currentCameraFront() == true) "front" else "back"
        val statsJson = JSONObject()
            .put("type", "stats")
            .put("fps", Math.round(fps * 10) / 10.0)
            .put("bitrate_kbps", bitrate.toInt())
            .put("frames", totalFrames)
            .put("battery", battery)
            .put("thermal", thermal)
            .put("camera", cameraName)
            .put("ts_rel_ns", tsRel)
        sendEncrypted(statsJson)
        mainHandler.post {
            listener.onStats(
                StatsSnapshot(
                    fps = fps,
                    bitrateKbps = bitrate.toInt(),
                    targetBitrateKbps = workingKbps,
                    frames = totalFrames,
                    batteryPct = battery,
                    thermal = thermal,
                    rttMs = lastHintRttMs,
                    camera = cameraName,
                    streaming = true,
                ),
            )
        }
    }

    private fun batteryPct(): Int {
        return try {
            val intent = appContext.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))
            val level = intent?.getIntExtra(BatteryManager.EXTRA_LEVEL, -1) ?: -1
            val scale = intent?.getIntExtra(BatteryManager.EXTRA_SCALE, -1) ?: -1
            if (level >= 0 && scale > 0) (level * 100 / scale) else -1
        } catch (_: Exception) {
            -1
        }
    }

    private fun thermalStatus(): String {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return "unknown"
        return try {
            val pm = appContext.getSystemService(Context.POWER_SERVICE) as android.os.PowerManager
            when (pm.currentThermalStatus) {
                android.os.PowerManager.THERMAL_STATUS_NONE -> "nominal"
                android.os.PowerManager.THERMAL_STATUS_LIGHT -> "light"
                android.os.PowerManager.THERMAL_STATUS_MODERATE -> "moderate"
                android.os.PowerManager.THERMAL_STATUS_SEVERE -> "severe"
                android.os.PowerManager.THERMAL_STATUS_CRITICAL -> "critical"
                android.os.PowerManager.THERMAL_STATUS_EMERGENCY -> "emergency"
                android.os.PowerManager.THERMAL_STATUS_SHUTDOWN -> "shutdown"
                else -> "unknown"
            }
        } catch (_: Exception) {
            "unknown"
        }
    }

    // -------------------------------------------------------------- teardown

    private fun teardownSession() {
        connectionGeneration++
        stopStats()
        mic?.stop()
        mic = null
        stream?.stop()
        mediaSender?.close()
        mediaSender = null
        control?.close()
        control = null
        sessionKeys = null
        config = null
        avatarAssembler.reset()
        streamStartNs = 0
    }

    private fun updateState(newState: SessionState, detail: String) {
        state = newState
        mainHandler.post { listener.onStateChanged(newState, detail) }
    }

    private fun scheduleReconnect() {
        if (userDisconnect || !prefs.autoReconnect) return
        if (pendingSecretHex == null && pendingCode == null && !prefs.hasPairing()) return
        reconnectTask?.cancel(false)
        val delay = minOf(1000L shl reconnectAttempt.coerceAtMost(5), 30000L)
        reconnectAttempt++
        reconnectTask = timerScheduler.schedule({
            executor.execute {
                if (state == SessionState.IDLE || state == SessionState.ERROR || state == SessionState.RECONNECTING) {
                    updateState(SessionState.RECONNECTING, "reconnect attempt $reconnectAttempt")
                    if (pendingSecretHex == null && pendingCode == null) {
                        pendingSecretHex = prefs.pairedSecretHex.takeIf { it.length == 64 }
                    }
                    startConnection(host, port)
                }
            }
        }, delay, TimeUnit.MILLISECONDS)
    }

    fun close() {
        executor.execute {
            userDisconnect = true
            teardownSession()
            stream?.close()
            stream = null
        }
        executor.shutdown()
        timerScheduler.shutdownNow()
    }

    companion object {
        private const val APP_VERSION = "1.0.2"
        private const val DEFAULT_BITRATE_Kbps = 4000
        private const val MIN_BITRATE_Kbps = 500
        private const val BITRATE_MIN_INTERVAL_MS = 2000L
    }
}
