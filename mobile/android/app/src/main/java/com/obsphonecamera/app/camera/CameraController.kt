package com.obsphonecamera.app.camera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.SurfaceTexture
import android.graphics.RectF
import android.hardware.camera2.CaptureResult
import android.hardware.camera2.TotalCaptureResult
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.StreamConfigurationMap
import android.os.Handler
import android.os.HandlerThread
import android.util.Range
import android.util.Rational
import android.util.Size
import android.view.Surface
import com.obsphonecamera.app.protocol.CameraControlConfig
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

data class CameraEntry(
    val id: String,
    val facingFront: Boolean,
    val label: String,
    val sensorOrientation: Int,
    val supportsTorch: Boolean,
    val supportsManualFocus: Boolean,
    val supportsExposureBias: Boolean,
    val supportsManualWb: Boolean,
    val maxBitrateKbps: Int,
)

/**
 * Camera2 wrapper: opens a camera into a GL SurfaceTexture target, applies
 * focus/exposure/white-balance/torch controls with capability checks.
 */
class CameraController(
    context: Context,
    private val errorHandler: (String) -> Unit,
) {

    private val cameraManager =
        context.applicationContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    private var cameraDevice: CameraDevice? = null
    private var session: CameraCaptureSession? = null
    private var sessionThread: HandlerThread? = null
    private var sessionHandler: Handler? = null
    private var requestBuilder: CaptureRequest.Builder? = null

    var currentCameraId: String? = null
        private set

    var onFaces: ((List<RectF>) -> Unit)? = null
    private var outputSize = Size(1, 1)
    private var pendingFacePixelation = false
    private val captureCallback = object : CameraCaptureSession.CaptureCallback() {
        override fun onCaptureCompleted(session: CameraCaptureSession, request: CaptureRequest, result: TotalCaptureResult) {
            val id = currentCameraId ?: return
            val chars = cameraManager.getCameraCharacteristics(id)
            val crop = result.get(CaptureResult.SCALER_CROP_REGION)
                ?: chars.get(CameraCharacteristics.SENSOR_INFO_ACTIVE_ARRAY_SIZE) ?: return
            // Camera2 further center-crops to the output stream's aspect ratio.
            val region = RectF(crop)
            val aspect = outputSize.width.toFloat() / outputSize.height
            if (region.width() / region.height() > aspect) {
                val inset = (region.width() - region.height() * aspect) / 2f
                region.inset(inset, 0f)
            } else {
                val inset = (region.height() - region.width() / aspect) / 2f
                region.inset(0f, inset)
            }
            val faces = result.get(CaptureResult.STATISTICS_FACES).orEmpty().map { face ->
                val r = RectF(face.bounds)
                // Include forehead/chin and allow for metadata latency during movement.
                r.inset(-r.width() * 0.30f, -r.height() * 0.40f)
                RectF(
                    ((r.left - region.left) / region.width()).coerceIn(0f, 1f),
                    ((r.top - region.top) / region.height()).coerceIn(0f, 1f),
                    ((r.right - region.left) / region.width()).coerceIn(0f, 1f),
                    ((r.bottom - region.top) / region.height()).coerceIn(0f, 1f),
                )
            }.filter { it.width() > 0f && it.height() > 0f }
            onFaces?.invoke(faces)
        }
    }

    private var pendingTorch = false
    private var pendingFocusMode = "auto"
    private var pendingFocusDistance = 0f
    private var pendingExposureMode = "auto"
    private var pendingExposureBias = 0f
    private var pendingWbMode = "auto"
    private var pendingWbKelvin = 5500

    fun listCameras(): List<CameraEntry> {
        return try {
            cameraManager.cameraIdList.mapNotNull { id ->
                val c = cameraManager.getCameraCharacteristics(id)
                val facing = c.get(CameraCharacteristics.LENS_FACING) ?: return@mapNotNull null
                val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                if (map?.getOutputSizes(SurfaceTexture::class.java).isNullOrEmpty()) return@mapNotNull null
                CameraEntry(
                    id = id,
                    facingFront = facing == CameraCharacteristics.LENS_FACING_FRONT,
                    label = if (facing == CameraCharacteristics.LENS_FACING_FRONT) "Front" else "Back",
                    sensorOrientation = c.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0,
                    supportsTorch = c.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true,
                    supportsManualFocus = supportsManualFocus(c),
                    supportsExposureBias = supportsExposureBias(c),
                    supportsManualWb = supportsManualWb(c),
                    maxBitrateKbps = 50000,
                )
            }
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun preferredCamera(front: Boolean): CameraEntry? {
        val cameras = listCameras()
        return cameras.firstOrNull { it.facingFront == front } ?: cameras.firstOrNull()
    }

    fun supportedSizes(cameraId: String): List<Size> {
        return try {
            val c = cameraManager.getCameraCharacteristics(cameraId)
            val map = c.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            map?.getOutputSizes(SurfaceTexture::class.java)?.toList() ?: emptyList()
        } catch (_: Exception) {
            emptyList()
        }
    }

    fun isFront(cameraId: String): Boolean = try {
        cameraManager.getCameraCharacteristics(cameraId)
            .get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_FRONT
    } catch (_: Exception) {
        false
    }

    @SuppressLint("MissingPermission")
    fun open(cameraId: String, target: Surface, size: Size, fps: Int, onOpened: () -> Unit) {
        closeCamera()
        outputSize = size
        val thread = HandlerThread("ocp-camera").also { it.start() }
        sessionThread = thread
        sessionHandler = Handler(thread.looper)
        try {
            cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    cameraDevice = camera
                    currentCameraId = cameraId
                    createSession(camera, target, size, fps, onOpened)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    camera.close()
                    cameraDevice = null
                    errorHandler("camera disconnected")
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    camera.close()
                    cameraDevice = null
                    errorHandler("camera error $error")
                }
            }, sessionHandler)
        } catch (e: Exception) {
            errorHandler(e.message ?: "camera open failed")
        }
    }

    private fun createSession(
        camera: CameraDevice,
        target: Surface,
        size: Size,
        fps: Int,
        onOpened: () -> Unit,
    ) {
        try {
            camera.createCaptureSession(
                listOf(target),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(s: CameraCaptureSession) {
                        if (cameraDevice == null) {
                            s.close()
                            return
                        }
                        session = s
                        try {
                            val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                            builder.addTarget(target)
                            builder.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO)
                            requestBuilder = builder
                            applyAllLocked(builder)
                            s.setRepeatingRequest(builder.build(), captureCallback, sessionHandler)
                            onOpened()
                        } catch (e: Exception) {
                            errorHandler(e.message ?: "session start failed")
                        }
                    }

                    override fun onConfigureFailed(s: CameraCaptureSession) {
                        errorHandler("camera session configuration failed")
                    }
                },
                sessionHandler,
            )
        } catch (e: Exception) {
            errorHandler(e.message ?: "createCaptureSession failed")
        }
    }

    fun setTorch(on: Boolean) {
        pendingTorch = on
        reapply()
    }

    fun applyControls(cfg: CameraControlConfig) {
        pendingFocusMode = cfg.focusMode
        pendingFocusDistance = cfg.focusDistance
        pendingExposureMode = cfg.exposureMode
        pendingExposureBias = cfg.exposureBias
        pendingWbMode = cfg.wbMode
        pendingWbKelvin = cfg.wbKelvin
        pendingTorch = cfg.torch
        pendingFacePixelation = cfg.facePixelation
        reapply()
    }

    fun closeCamera() {
        onFaces?.invoke(emptyList())
        try {
            session?.close()
        } catch (_: Exception) {
        }
        session = null
        requestBuilder = null
        try {
            cameraDevice?.close()
        } catch (_: Exception) {
        }
        cameraDevice = null
        currentCameraId = null
        sessionThread?.quitSafely()
        try {
            sessionThread?.join(500)
        } catch (_: Exception) {
        }
        sessionThread = null
        sessionHandler = null
    }

    private fun reapply() {
        val builder = requestBuilder ?: return
        val s = session ?: return
        try {
            applyAllLocked(builder)
            s.setRepeatingRequest(builder.build(), captureCallback, sessionHandler)
        } catch (_: Exception) {
        }
    }

    private fun applyAllLocked(builder: CaptureRequest.Builder) {
        val id = currentCameraId ?: return
        val chars = try {
            cameraManager.getCameraCharacteristics(id)
        } catch (_: Exception) {
            return
        }

        val modes = chars.get(CameraCharacteristics.STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES) ?: intArrayOf()
        val mode = if (!pendingFacePixelation) CaptureRequest.STATISTICS_FACE_DETECT_MODE_OFF
            else if (modes.contains(CaptureRequest.STATISTICS_FACE_DETECT_MODE_SIMPLE)) CaptureRequest.STATISTICS_FACE_DETECT_MODE_SIMPLE
            else if (modes.contains(CaptureRequest.STATISTICS_FACE_DETECT_MODE_FULL)) CaptureRequest.STATISTICS_FACE_DETECT_MODE_FULL
            else CaptureRequest.STATISTICS_FACE_DETECT_MODE_OFF
        builder.set(CaptureRequest.STATISTICS_FACE_DETECT_MODE, mode)
        applyFocus(builder, chars)
        applyExposure(builder, chars)
        applyWhiteBalance(builder, chars)
        applyTorch(builder, chars)
    }

    private fun applyFocus(builder: CaptureRequest.Builder, chars: CameraCharacteristics) {
        val maxD = chars.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f
        val manualOk = supportsManualFocus(chars)
        when (pendingFocusMode) {
            "manual" -> {
                if (manualOk) {
                    builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_OFF)
                    val diopters = (1f - pendingFocusDistance.coerceIn(0f, 1f)) * maxD
                    builder.set(CaptureRequest.LENS_FOCUS_DISTANCE, diopters)
                    return
                }
            }
            "continuous" -> {
                builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_VIDEO)
                return
            }
            else -> {
                builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO)
                return
            }
        }
        builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_AUTO)
    }

    private fun applyExposure(builder: CaptureRequest.Builder, chars: CameraCharacteristics) {
        val range = chars.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE)
        val step = chars.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_STEP)
        if (pendingExposureMode == "manual" && supportsManualSensor(chars)) {
            builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_OFF)
            val exposureRange = chars.get(CameraCharacteristics.SENSOR_INFO_EXPOSURE_TIME_RANGE)
            val sensitivityRange = chars.get(CameraCharacteristics.SENSOR_INFO_SENSITIVITY_RANGE)
            if (exposureRange != null && sensitivityRange != null) {
                val baseNs = 1_000_000_000L / 60
                val scaled = (baseNs * Math.pow(2.0, pendingExposureBias.toDouble())).toLong()
                builder.set(
                    CaptureRequest.SENSOR_EXPOSURE_TIME,
                    scaled.coerceIn(exposureRange.lower, exposureRange.upper),
                )
                builder.set(
                    CaptureRequest.SENSOR_SENSITIVITY,
                    sensitivityRange.lower.coerceAtLeast(100),
                )
            }
            return
        }
        builder.set(CaptureRequest.CONTROL_AE_MODE, CaptureRequest.CONTROL_AE_MODE_ON)
        if (range != null && step != null && step.toFloat() > 0f) {
            val steps = (pendingExposureBias / step.toFloat()).roundToInt()
            builder.set(CaptureRequest.CONTROL_AE_EXPOSURE_COMPENSATION, steps.coerceIn(range.lower, range.upper))
        }
    }

    private fun applyWhiteBalance(builder: CaptureRequest.Builder, chars: CameraCharacteristics) {
        val modes = chars.get(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)
        if (pendingWbMode == "manual" && supportsManualWb(chars)) {
            builder.set(CaptureRequest.CONTROL_AWB_MODE, CaptureRequest.CONTROL_AWB_MODE_OFF)
            builder.set(CaptureRequest.COLOR_CORRECTION_MODE, CaptureRequest.COLOR_CORRECTION_MODE_TRANSFORM_MATRIX)
            val gains = kelvinToRgbGains(pendingWbKelvin)
            builder.set(CaptureRequest.COLOR_CORRECTION_GAINS,
                android.hardware.camera2.params.RggbChannelVector(gains[0], gains[1], gains[1], gains[2]))
            return
        }
        if (modes != null && modes.contains(CaptureRequest.CONTROL_AWB_MODE_AUTO)) {
            builder.set(CaptureRequest.CONTROL_AWB_MODE, CaptureRequest.CONTROL_AWB_MODE_AUTO)
        }
    }

    private fun applyTorch(builder: CaptureRequest.Builder, chars: CameraCharacteristics) {
        val flashAvailable = chars.get(CameraCharacteristics.FLASH_INFO_AVAILABLE) == true
        if (flashAvailable) {
            builder.set(
                CaptureRequest.FLASH_MODE,
                if (pendingTorch) CaptureRequest.FLASH_MODE_TORCH else CaptureRequest.FLASH_MODE_OFF,
            )
        }
    }

    companion object {

        fun supportsManualFocus(chars: CameraCharacteristics): Boolean {
            val distance = chars.get(CameraCharacteristics.LENS_INFO_MINIMUM_FOCUS_DISTANCE) ?: 0f
            val modes = chars.get(CameraCharacteristics.CONTROL_AF_AVAILABLE_MODES)
            return distance > 0f && modes?.contains(CaptureRequest.CONTROL_AF_MODE_OFF) == true
        }

        fun supportsExposureBias(chars: CameraCharacteristics): Boolean {
            val range = chars.get(CameraCharacteristics.CONTROL_AE_COMPENSATION_RANGE)
            return range != null && (range.upper > 0 || range.lower < 0)
        }

        fun supportsManualSensor(chars: CameraCharacteristics): Boolean {
            val caps = chars.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
            return caps?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_SENSOR) == true
        }

        fun supportsManualWb(chars: CameraCharacteristics): Boolean {
            val caps = chars.get(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES)
            val modes = chars.get(CameraCharacteristics.CONTROL_AWB_AVAILABLE_MODES)
            return caps?.contains(CameraCharacteristics.REQUEST_AVAILABLE_CAPABILITIES_MANUAL_POST_PROCESSING) == true &&
                modes?.contains(CaptureRequest.CONTROL_AWB_MODE_OFF) == true
        }

        /** Choose the camera output size closest to (>= preferred) the requested oriented size. */
        fun chooseSize(sizes: List<Size>, desiredW: Int, desiredH: Int): Size {
            if (sizes.isEmpty()) return Size(max(2, desiredW), max(2, desiredH))
            val desiredAspect = desiredW.toDouble() / desiredH.toDouble()
            val desiredArea = desiredW.toLong() * desiredH.toLong()
            val sorted = sizes.sortedWith(
                compareBy<Size> { s ->
                    if (s.width.toLong() * s.height >= desiredArea) 0 else 1
                }.thenBy { s ->
                    val aspect = s.width.toDouble() / s.height.toDouble()
                    abs(aspect - desiredAspect)
                }.thenBy { s ->
                    abs(s.width.toLong() * s.height - desiredArea)
                },
            )
            return sorted.first()
        }

        fun chooseFpsRange(chars: CameraCharacteristics, fps: Int): Range<Int> {
            val ranges = chars.get(CameraCharacteristics.CONTROL_AE_AVAILABLE_TARGET_FPS_RANGES)
                ?: return Range(fps, fps)
            val containing = ranges.filter { it.lower <= fps && it.upper >= fps }
            if (containing.isNotEmpty()) {
                return containing.maxByOrNull { it.upper - it.lower }!!
            }
            return ranges.minByOrNull { abs(it.upper - fps) }!!
        }

        /** Approximate Planckian-locus RGB gains (R/G, 1, B/G) for a color temperature. */
        fun kelvinToRgbGains(kelvin: Int): FloatArray {
            val t = kelvin.coerceIn(1000, 12000) / 100.0
            val r: Double
            val g: Double
            val b: Double
            if (t <= 66) {
                r = 255.0
                g = 99.4708025861 * log10(t) - 161.1195681661
                b = if (t <= 19) 0.0 else 138.5177312231 * log10(t - 10) - 305.0447927307
            } else {
                r = 329.698727446 * Math.pow(t - 60, -0.1332047592)
                g = 288.1221695283 * Math.pow(t - 60, -0.0755148492)
                b = 255.0
            }
            val rf = r.coerceIn(0.0, 255.0).toFloat()
            val gf = g.coerceIn(0.0, 255.0).toFloat().coerceAtLeast(1f)
            val bf = b.coerceIn(0.0, 255.0).toFloat()
            return floatArrayOf(rf / gf, 1f, bf / gf)
        }

        fun evenSize(w: Int, h: Int): Size = Size(max(2, w and -2), max(2, h and -2))

        fun sensorRelativeRotation(sensorOrientation: Int, deviceDegrees: Int, front: Boolean): Int {
            return if (front) {
                (sensorOrientation + deviceDegrees) % 360
            } else {
                (sensorOrientation - deviceDegrees + 360) % 360
            }
        }

        fun computeStreamConfig(
            sizes: List<Size>,
            chars: CameraCharacteristics,
            outW: Int,
            outH: Int,
            fps: Int,
            rotation: Int,
        ): Triple<Size, Size, Range<Int>> {
            val swap = (rotation % 180) != 0
            val desiredCamW = if (swap) outH else outW
            val desiredCamH = if (swap) outW else outH
            val cameraSize = chooseSize(sizes, desiredCamW, desiredCamH)
            val encoderSize = evenSize(outW, outH)
            val fpsRange = chooseFpsRange(chars, fps)
            return Triple(cameraSize, encoderSize, fpsRange)
        }
    }
}
