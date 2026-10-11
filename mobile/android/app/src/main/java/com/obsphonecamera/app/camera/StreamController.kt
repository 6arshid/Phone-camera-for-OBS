package com.obsphonecamera.app.camera

import android.content.Context
import android.os.Handler
import android.os.HandlerThread
import android.util.Size
import android.view.Surface
import com.obsphonecamera.app.protocol.CameraControlConfig

/**
 * Orchestrates Camera2 -> GL (SurfaceTexture) -> MediaCodec encoder.
 * Camera can be switched without tearing down the encoder/session.
 */
class StreamController(
    context: Context,
    private val onAccessUnit: (data: ByteArray, keyframe: Boolean, tsNs: Long) -> Unit,
    private val onError: (String) -> Unit,
) {

    data class PipelineConfig(
        val camera: String,
        val outWidth: Int,
        val outHeight: Int,
        val fps: Int,
        val bitrateKbps: Int,
        val gopS: Int,
        val orientation: String,
    )

    private val appContext = context.applicationContext
    private val camera = CameraController(appContext, onError)

    private var gl: GlPipeline? = null
    private var encoder: VideoEncoder? = null

    @Volatile
    private var drawing = false
    private var drawThread: Thread? = null
    private var frameThread: HandlerThread? = null
    private var frameHandler: Handler? = null

    private var previewSurface: Surface? = null
    private var previewWidth = 0
    private var previewHeight = 0

    @Volatile
    private var deviceDegrees = 0

    @Volatile
    private var running = false

    private var runningConfig: PipelineConfig? = null
    private var controls = CameraControlConfig(
        focusMode = "auto",
        focusDistance = 0f,
        exposureMode = "auto",
        exposureBias = 0f,
        wbMode = "auto",
        wbKelvin = 5500,
        torch = false,
    )

    val isRunning: Boolean get() = running

    fun listCameras(): List<CameraEntry> = camera.listCameras()

    fun currentCameraFront(): Boolean {
        val id = camera.currentCameraId ?: return false
        return camera.isFront(id)
    }

    fun resolveCameraId(requested: String): String {
        val cameras = camera.listCameras()
        if (cameras.isEmpty()) return ""
        cameras.firstOrNull { it.id == requested }?.let { return it.id }
        val entry = when (requested) {
            "front" -> cameras.firstOrNull { it.facingFront }
            "toggle" -> {
                val curFront = camera.currentCameraId?.let { camera.isFront(it) }
                cameras.firstOrNull { it.facingFront != curFront }
            }
            else -> cameras.firstOrNull { !it.facingFront }
        } ?: cameras.first()
        return entry.id
    }

    fun setPreviewSurface(surface: Surface?, width: Int, height: Int) {
        previewSurface = surface
        previewWidth = width
        previewHeight = height
        gl?.setPreviewSurface(surface, width, height)
    }

    fun setDeviceDegrees(degrees: Int) {
        deviceDegrees = degrees
        val cfg = runningConfig ?: return
        applyRotation(cfg)
    }

    fun applyControls(cfg: CameraControlConfig) {
        controls = cfg
        camera.applyControls(cfg)
    }

    fun requestKeyFrame() {
        encoder?.requestKeyFrame()
    }

    fun setBitrateKbps(kbps: Int) {
        encoder?.setBitrate(kbps * 1000)
    }

    @Synchronized
    fun start(config: PipelineConfig) {
        val previous = runningConfig
        val canReuseEncoder = previous != null && encoder != null && gl != null &&
            previous.outWidth == config.outWidth &&
            previous.outHeight == config.outHeight &&
            previous.fps == config.fps &&
            previous.bitrateKbps == config.bitrateKbps &&
            previous.gopS == config.gopS

        if (!canReuseEncoder) {
            stopPipeline()
        }

        val cameras = camera.listCameras()
        if (cameras.isEmpty()) {
            onError("no cameras available")
            return
        }
        val entry = cameras.firstOrNull { it.id == resolveCameraId(config.camera) }
            ?: cameras.first()

        val rotation = rotationFor(entry, config.orientation)
        val sizes = camera.supportedSizes(entry.id)
        val outSize = CameraController.evenSize(config.outWidth, config.outHeight)
        val cameraSize = pickCameraSize(sizes, outSize, rotation)

        if (!canReuseEncoder) {
            val enc = VideoEncoder(
                width = outSize.width,
                height = outSize.height,
                fps = config.fps,
                bitrateBps = config.bitrateKbps * 1000,
                iFrameIntervalS = config.gopS,
                onAccessUnit = onAccessUnit,
            )
            val inputSurface = try {
                enc.start()
            } catch (e: Exception) {
                onError("encoder start failed: ${e.message}")
                return
            }
            encoder = enc
            val pipeline = GlPipeline()
            try {
                pipeline.init(inputSurface, outSize.width, outSize.height)
            } catch (e: Exception) {
                onError("GL init failed: ${e.message}")
                enc.stop()
                return
            }
            gl = pipeline
            val ft = HandlerThread("ocp-frame").also { it.start() }
            frameThread = ft
            frameHandler = Handler(ft.looper)
            pipeline.setOnFrameAvailableHandler(frameHandler!!)
            pipeline.setPreviewSurface(previewSurface, previewWidth, previewHeight)
            drawing = true
            val fps = config.fps.coerceIn(1, 120)
            drawThread = Thread({ drawLoop(fps) }, "ocp-gl-draw").also { it.start() }
        }

        val pipeline = gl ?: return
        pipeline.setCameraTransform(entry.sensorOrientation, entry.facingFront)
        pipeline.setRotation(rotation)
        pipeline.mirror = entry.facingFront
        pipeline.updateTextureBufferSize(cameraSize.width, cameraSize.height)
        runningConfig = config
        running = true

        camera.open(entry.id, pipeline.getCameraSurface(), cameraSize, config.fps) {
            camera.applyControls(controls)
        }
    }

    @Synchronized
    fun stop() {
        stopPipeline()
        runningConfig = null
        running = false
    }

    private fun stopPipeline() {
        running = false
        camera.closeCamera()
        drawing = false
        drawThread?.join(1000)
        drawThread = null
        frameThread?.quitSafely()
        try {
            frameThread?.join(500)
        } catch (_: Exception) {
        }
        frameThread = null
        frameHandler = null
        encoder?.stop()
        encoder = null
        gl?.release()
        gl = null
    }

    private fun drawLoop(fps: Int) {
        val intervalNs = 1_000_000_000L / fps
        val pipeline = gl ?: return
        while (drawing) {
            val start = System.nanoTime()
            val drew = try {
                pipeline.drawFrame(500)
            } catch (e: Exception) {
                drawing = false
                onError("GL draw failed: ${e.message}")
                return
            }
            if (drew) {
                val elapsed = System.nanoTime() - start
                val sleepNs = intervalNs - elapsed
                if (sleepNs > 0) {
                    try {
                        Thread.sleep(sleepNs / 1_000_000L, (sleepNs % 1_000_000L).toInt())
                    } catch (_: InterruptedException) {
                        return
                    }
                }
            }
        }
    }

    private fun applyRotation(cfg: PipelineConfig) {
        val entry = camera.listCameras().firstOrNull { it.id == camera.currentCameraId } ?: return
        gl?.setRotation(rotationFor(entry, cfg.orientation))
        gl?.mirror = entry.facingFront
    }

    private fun rotationFor(entry: CameraEntry, orientation: String): Int {
        val device = when (orientation) {
            "portrait" -> 0
            "landscape" -> 90
            else -> deviceDegrees
        }
        return CameraController.sensorRelativeRotation(entry.sensorOrientation, device, entry.facingFront)
    }

    private fun pickCameraSize(sizes: List<Size>, outSize: Size, rotation: Int): Size {
        val swap = (rotation % 180) != 0
        val desiredW = if (swap) outSize.height else outSize.width
        val desiredH = if (swap) outSize.width else outSize.height
        return CameraController.chooseSize(sizes, desiredW, desiredH)
    }

    fun close() {
        stop()
        camera.closeCamera()
    }
}
