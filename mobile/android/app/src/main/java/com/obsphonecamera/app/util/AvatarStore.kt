package com.obsphonecamera.app.util

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import java.io.ByteArrayOutputStream
import java.io.File

/**
 * Stores the avatar images in app-private storage: the last image received
 * from OBS (keyed by SHA-256) and the image picked on the phone.
 */
class AvatarStore(context: Context, private val prefs: Prefs) {
    private val dir = context.applicationContext.filesDir
    private val resolver = context.applicationContext.contentResolver
    private val obsFile = File(dir, "avatar_obs.img")
    private val localFile = File(dir, "avatar_local.img")

    /** Hash of the cached OBS image, or "" when none is stored. */
    val obsHash: String
        get() = if (obsFile.isFile) prefs.avatarObsHash else ""

    val hasLocal: Boolean get() = localFile.isFile

    fun saveObs(hash: String, bytes: ByteArray) {
        writeAtomically(obsFile, bytes)
        prefs.avatarObsHash = hash
    }

    fun loadObs(hash: String): Bitmap? = if (hash.isNotEmpty() && hash == obsHash) decode(obsFile) else null

    fun loadLocal(): Bitmap? = if (hasLocal) decode(localFile) else null

    /** Copies a picked image into app storage; returns false if it is not a decodable image. */
    fun importLocal(uri: Uri): Boolean {
        val bytes = resolver.openInputStream(uri)?.use { input ->
            val out = ByteArrayOutputStream()
            val buf = ByteArray(64 * 1024)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                out.write(buf, 0, n)
                if (out.size() > MAX_LOCAL_BYTES) return false
            }
            out.toByteArray()
        } ?: return false
        val probe = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, probe)
        if (probe.outWidth <= 0 || probe.outHeight <= 0) return false
        writeAtomically(localFile, bytes)
        return true
    }

    private fun writeAtomically(target: File, bytes: ByteArray) {
        val tmp = File(target.parentFile, target.name + ".tmp")
        tmp.writeBytes(bytes)
        if (!tmp.renameTo(target)) {
            target.delete()
            tmp.renameTo(target)
        }
    }

    companion object {
        private const val MAX_LOCAL_BYTES = 16 * 1024 * 1024
        private const val MAX_TEXTURE = 2048

        /** Decodes as ARGB_8888 (required for GL upload), downsampled to fit a GPU texture. */
        fun decode(file: File): Bitmap? = try {
            val probe = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            BitmapFactory.decodeFile(file.path, probe)
            var sample = 1
            while (maxOf(probe.outWidth, probe.outHeight) / sample > MAX_TEXTURE) sample *= 2
            val opts = BitmapFactory.Options().apply {
                inSampleSize = sample
                inPreferredConfig = Bitmap.Config.ARGB_8888
            }
            BitmapFactory.decodeFile(file.path, opts)?.let { bmp ->
                if (bmp.config == Bitmap.Config.ARGB_8888) bmp
                else bmp.copy(Bitmap.Config.ARGB_8888, false).also { bmp.recycle() }
            }
        } catch (_: Exception) {
            null
        } catch (_: OutOfMemoryError) {
            null
        }
    }
}
