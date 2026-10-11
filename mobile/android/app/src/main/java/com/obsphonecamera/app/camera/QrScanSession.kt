package com.obsphonecamera.app.camera

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.hardware.camera2.CameraAccessException
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.media.ImageReader
import android.os.Handler
import android.os.HandlerThread
import android.util.Size
import android.view.Surface
import kotlin.math.abs

/**
 * Camera session for QR pairing: preview surface + YUV ImageReader analyzed
 * by [QrAnalyzer].
 */
class QrScanSession(
    context: Context,
    private val onQrText: (String) -> Unit,
    private val onError: (String) -> Unit,
) {

    private val cameraManager =
        context.applicationContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager

    private var cameraDevice: CameraDevice? = null
    private var session: CameraCaptureSession? = null
    private var thread: HandlerThread? = null
    private var analyzer: QrAnalyzer? = null
    private var imageReader: ImageReader? = null

    @Volatile
    private var running = false

    @SuppressLint("MissingPermission")
    fun start(previewSurface: Surface) {
        stop()
        running = true
        val t = HandlerThread("ocp-qr").also { it.start() }
        thread = t
        val handler = Handler(t.looper)
        val cameraId = try {
            cameraManager.cameraIdList.firstOrNull { id ->
                cameraManager.getCameraCharacteristics(id)
                    .get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
            } ?: cameraManager.cameraIdList.firstOrNull()
        } catch (_: Exception) {
            null
        }
        if (cameraId == null) {
            onError("no camera available")
            return
        }
        val scanSize = try {
            val map = cameraManager.getCameraCharacteristics(cameraId)
                .get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
            val sizes = map?.getOutputSizes(ImageFormat.YUV_420_888)?.toList() ?: emptyList()
            chooseScanSize(sizes)
        } catch (_: Exception) {
            Size(1280, 720)
        }
        val reader = ImageReader.newInstance(scanSize.width, scanSize.height, ImageFormat.YUV_420_888, 2)
        imageReader = reader
        val qr = QrAnalyzer(handler, onQrText)
        analyzer = qr
        qr.attach(reader)
        try {
            cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                override fun onOpened(camera: CameraDevice) {
                    if (!running) {
                        camera.close()
                        return
                    }
                    cameraDevice = camera
                    createSession(camera, previewSurface, reader, handler)
                }

                override fun onDisconnected(camera: CameraDevice) {
                    camera.close()
                    cameraDevice = null
                    if (running) onError("camera disconnected")
                }

                override fun onError(camera: CameraDevice, error: Int) {
                    camera.close()
                    cameraDevice = null
                    if (running) onError("camera error $error")
                }
            }, handler)
        } catch (e: Exception) {
            onError(e.message ?: "camera open failed")
        }
    }

    private fun createSession(
        camera: CameraDevice,
        previewSurface: Surface,
        reader: ImageReader,
        handler: Handler,
    ) {
        try {
            camera.createCaptureSession(
                listOf(previewSurface, reader.surface),
                object : CameraCaptureSession.StateCallback() {
                    override fun onConfigured(s: CameraCaptureSession) {
                        if (cameraDevice == null || !running) {
                            s.close()
                            return
                        }
                        session = s
                        try {
                            val builder = camera.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW)
                            builder.addTarget(previewSurface)
                            builder.addTarget(reader.surface)
                            builder.set(CaptureRequest.CONTROL_MODE, CaptureRequest.CONTROL_MODE_AUTO)
                            builder.set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                            s.setRepeatingRequest(builder.build(), null, handler)
                        } catch (e: Exception) {
                            onError(e.message ?: "scan session failed")
                        }
                    }

                    override fun onConfigureFailed(s: CameraCaptureSession) {
                        onError("QR scan session configuration failed")
                    }
                },
                handler,
            )
        } catch (e: Exception) {
            onError(e.message ?: "createCaptureSession failed")
        }
    }

    fun stop() {
        running = false
        analyzer?.stop()
        analyzer = null
        try {
            session?.close()
        } catch (_: Exception) {
        }
        session = null
        try {
            cameraDevice?.close()
        } catch (_: Exception) {
        }
        cameraDevice = null
        try {
            imageReader?.close()
        } catch (_: Exception) {
        }
        imageReader = null
        thread?.quitSafely()
        try {
            thread?.join(500)
        } catch (_: Exception) {
        }
        thread = null
    }

    companion object {
        private fun chooseScanSize(sizes: List<Size>): Size {
            if (sizes.isEmpty()) return Size(1280, 720)
            val targetArea = 1280L * 720L
            return sizes.minByOrNull { abs(it.width.toLong() * it.height - targetArea) } ?: sizes.first()
        }
    }
}
