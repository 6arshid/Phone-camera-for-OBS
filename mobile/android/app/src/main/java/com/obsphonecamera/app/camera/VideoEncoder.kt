package com.obsphonecamera.app.camera

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaFormat
import android.os.Bundle
import android.view.Surface
import java.nio.ByteBuffer

/**
 * H.264 encoder fed from a GL input surface. Emits Annex-B access units;
 * key frames are guaranteed to carry SPS/PPS.
 */
class VideoEncoder(
    private val width: Int,
    private val height: Int,
    private val fps: Int,
    bitrateBps: Int,
    iFrameIntervalS: Int,
    private val onAccessUnit: (data: ByteArray, keyframe: Boolean, tsNs: Long) -> Unit,
) {

    private val codec: MediaCodec
    private var drainThread: Thread? = null

    @Volatile
    private var running = false

    @Volatile
    private var csd: ByteArray? = null

    val inputWidth: Int get() = width
    val inputHeight: Int get() = height

    init {
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(
                MediaFormat.KEY_COLOR_FORMAT,
                MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface,
            )
            setInteger(MediaFormat.KEY_BIT_RATE, bitrateBps.coerceAtLeast(100_000))
            setInteger(MediaFormat.KEY_FRAME_RATE, fps)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, iFrameIntervalS.coerceIn(1, 30))
        }
        codec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
    }

    fun start(): Surface {
        val surface = codec.createInputSurface()
        codec.start()
        running = true
        drainThread = Thread({ drainLoop() }, "ocp-encoder-drain").also { it.start() }
        return surface
    }

    fun requestKeyFrame() {
        try {
            val params = Bundle()
            params.putInt(MediaCodec.PARAMETER_KEY_REQUEST_SYNC_FRAME, 0)
            codec.setParameters(params)
        } catch (_: Exception) {
        }
    }

    fun setBitrate(bps: Int) {
        try {
            val params = Bundle()
            params.putInt(MediaCodec.PARAMETER_KEY_VIDEO_BITRATE, bps.coerceAtLeast(100_000))
            codec.setParameters(params)
        } catch (_: Exception) {
        }
    }

    fun stop() {
        if (!running) return
        running = false
        try {
            codec.signalEndOfInputStream()
        } catch (_: Exception) {
        }
        try {
            drainThread?.join(2000)
        } catch (_: Exception) {
        }
        try {
            codec.stop()
        } catch (_: Exception) {
        }
        try {
            codec.release()
        } catch (_: Exception) {
        }
    }

    private fun drainLoop() {
        val info = MediaCodec.BufferInfo()
        var idleTicks = 0
        while (true) {
            val index = try {
                codec.dequeueOutputBuffer(info, 10_000)
            } catch (_: Exception) {
                return
            }
            when (index) {
                MediaCodec.INFO_TRY_AGAIN_LATER -> {
                    if (!running) {
                        idleTicks++
                        if (idleTicks > 20) return
                    }
                }
                MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> Unit
                MediaCodec.INFO_OUTPUT_BUFFERS_CHANGED -> Unit
                else -> {
                    if (index < 0) continue
                    idleTicks = 0
                    val buffer = try {
                        codec.getOutputBuffer(index)
                    } catch (_: Exception) {
                        null
                    }
                    if (buffer != null && info.size > 0 &&
                        (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0
                    ) {
                        val data = readBuffer(buffer, info)
                        val keyframe = (info.flags and MediaCodec.BUFFER_FLAG_KEY_FRAME) != 0
                        val out = if (keyframe) prependCsdIfNeeded(data) else data
                        try {
                            onAccessUnit(out, keyframe, info.presentationTimeUs * 1000L)
                        } catch (_: Exception) {
                        }
                    } else if (buffer != null && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) != 0) {
                        csd = readBuffer(buffer, info)
                    }
                    val eos = (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0
                    try {
                        codec.releaseOutputBuffer(index, false)
                    } catch (_: Exception) {
                    }
                    if (eos) return
                }
            }
        }
    }

    private fun readBuffer(buffer: ByteBuffer, info: MediaCodec.BufferInfo): ByteArray {
        val data = ByteArray(info.size)
        buffer.position(info.offset)
        buffer.get(data)
        return data
    }

    private fun prependCsdIfNeeded(data: ByteArray): ByteArray {
        if (hasStartCodeSps(data)) return data
        val config = csd ?: return data
        return config + data
    }

    private fun hasStartCodeSps(data: ByteArray): Boolean {
        var i = 0
        while (i + 4 < data.size) {
            if (data[i].toInt() == 0 && data[i + 1].toInt() == 0) {
                val sc = if (data[i + 2].toInt() == 0 && data[i + 3].toInt() == 1) 4 else if (data[i + 2].toInt() == 1) 3 else 0
                if (sc > 0) {
                    val nalType = data[i + sc].toInt() and 0x1f
                    if (nalType == 7) return true
                    i += sc + 1
                    continue
                }
            }
            i++
        }
        return false
    }
}
