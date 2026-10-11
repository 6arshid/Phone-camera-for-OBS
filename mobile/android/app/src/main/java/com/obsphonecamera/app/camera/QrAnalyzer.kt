package com.obsphonecamera.app.camera

import android.media.Image
import android.media.ImageReader
import android.os.Handler
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.NotFoundException
import com.google.zxing.BarcodeFormat
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.common.HybridBinarizer
import java.util.EnumMap

/**
 * Decodes QR codes from YUV_420_888 frames off an ImageReader using ZXing.
 */
class QrAnalyzer(
    handler: Handler,
    private val onQrText: (String) -> Unit,
) : ImageReader.OnImageAvailableListener {

    private val reader = MultiFormatReader().apply {
        val hints = EnumMap<DecodeHintType, Any>(DecodeHintType::class.java)
        hints[DecodeHintType.TRY_HARDER] = true
        hints[DecodeHintType.POSSIBLE_FORMATS] = listOf(BarcodeFormat.QR_CODE)
        setHints(hints)
    }

    @Volatile
    private var lastDecodeMs = 0L

    @Volatile
    private var stopped = false

    private val background = Handler(handler.looper)

    fun attach(imageReader: ImageReader) {
        imageReader.setOnImageAvailableListener(this, background)
    }

    fun stop() {
        stopped = true
    }

    override fun onImageAvailable(reader: ImageReader) {
        if (stopped) return
        val now = System.currentTimeMillis()
        if (now - lastDecodeMs < MIN_INTERVAL_MS) {
            reader.acquireLatestImage()?.close()
            return
        }
        lastDecodeMs = now
        val image = reader.acquireLatestImage() ?: return
        try {
            val text = decode(image) ?: return
            if (!stopped) {
                background.post { onQrText(text) }
            }
        } finally {
            image.close()
        }
    }

    private fun decode(image: Image): String? {
        val plane = image.planes[0]
        val width = image.width
        val height = image.height
        val buffer = plane.buffer
        val rowStride = plane.rowStride
        val pixelStride = plane.pixelStride
        val data: ByteArray
        if (rowStride == width && pixelStride == 1) {
            data = ByteArray(width * height)
            buffer.rewind()
            buffer.get(data, 0, minOf(buffer.remaining(), data.size))
        } else {
            data = ByteArray(width * height)
            var pos = 0
            buffer.rewind()
            for (row in 0 until height) {
                val rowStart = row * rowStride
                for (col in 0 until width) {
                    data[pos++] = buffer.get(rowStart + col * pixelStride)
                }
            }
        }
        val source = PlanarYUVLuminanceSource(data, width, height, 0, 0, width, height, false)
        val bitmap = BinaryBitmap(HybridBinarizer(source))
        return try {
            val result = reader.decodeWithState(bitmap)
            result.text
        } catch (_: NotFoundException) {
            null
        } catch (_: Exception) {
            null
        } finally {
            reader.reset()
        }
    }

    companion object {
        private const val MIN_INTERVAL_MS = 150L
    }
}
