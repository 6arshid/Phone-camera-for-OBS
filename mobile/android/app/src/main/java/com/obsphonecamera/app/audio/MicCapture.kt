package com.obsphonecamera.app.audio

import android.annotation.SuppressLint
import android.media.AudioFormat
import android.media.AudioRecord
import android.media.MediaRecorder
import android.os.Process

/**
 * 48 kHz mono PCM16 microphone capture in 10 ms frames (480 samples, 960 bytes).
 */
class MicCapture(
    private val onFrame: (data: ByteArray, timestampNs: Long) -> Unit,
) {

    @Volatile
    private var running = false

    private var thread: Thread? = null
    private var record: AudioRecord? = null

    @SuppressLint("MissingPermission")
    fun start(sampleRate: Int = 48000): Boolean {
        if (running) return true
        val minBuf = AudioRecord.getMinBufferSize(
            sampleRate,
            AudioFormat.CHANNEL_IN_MONO,
            AudioFormat.ENCODING_PCM_16BIT,
        )
        if (minBuf <= 0) return false
        val bufSize = maxOf(minBuf, FRAME_BYTES * 4)
        val rec = try {
            AudioRecord(
                MediaRecorder.AudioSource.MIC,
                sampleRate,
                AudioFormat.CHANNEL_IN_MONO,
                AudioFormat.ENCODING_PCM_16BIT,
                bufSize,
            )
        } catch (_: Exception) {
            return false
        }
        if (rec.state != AudioRecord.STATE_INITIALIZED) {
            rec.release()
            return false
        }
        record = rec
        running = true
        rec.startRecording()
        thread = Thread({
            Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO)
            val buf = ByteArray(FRAME_BYTES)
            while (running) {
                val n = rec.read(buf, 0, FRAME_BYTES)
                if (n == FRAME_BYTES) {
                    try {
                        onFrame(buf.copyOf(), System.nanoTime())
                    } catch (_: Exception) {
                    }
                } else if (n < 0) {
                    break
                }
            }
        }, "ocp-mic").also { it.start() }
        return true
    }

    fun stop() {
        running = false
        try {
            thread?.join(1000)
        } catch (_: Exception) {
        }
        thread = null
        try {
            record?.stop()
        } catch (_: Exception) {
        }
        try {
            record?.release()
        } catch (_: Exception) {
        }
        record = null
    }

    companion object {
        const val SAMPLE_RATE = 48000
        const val FRAME_SAMPLES = 480
        const val FRAME_BYTES = FRAME_SAMPLES * 2
    }
}
