package com.obsphonecamera.app.util

import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import com.obsphonecamera.app.camera.AvatarSettings

class Prefs(context: Context) {

    private val sp: SharedPreferences =
        context.applicationContext.getSharedPreferences("ocp_settings", Context.MODE_PRIVATE)

    var deviceIdHex: String
        get() = sp.getString(KEY_DEVICE_ID, null) ?: ""
        set(value) = sp.edit().putString(KEY_DEVICE_ID, value).apply()

    var deviceName: String
        get() = sp.getString(KEY_DEVICE_NAME, null) ?: Build.MODEL ?: "Android"
        set(value) = sp.edit().putString(KEY_DEVICE_NAME, value).apply()

    var pairedServerIp: String
        get() = sp.getString(KEY_PAIR_IP, "") ?: ""
        set(value) = sp.edit().putString(KEY_PAIR_IP, value).apply()

    var pairedServerPort: Int
        get() = sp.getInt(KEY_PAIR_PORT, 47100)
        set(value) = sp.edit().putInt(KEY_PAIR_PORT, value).apply()

    var pairedServerName: String
        get() = sp.getString(KEY_PAIR_NAME, "") ?: ""
        set(value) = sp.edit().putString(KEY_PAIR_NAME, value).apply()

    var pairedServerId: String
        get() = sp.getString(KEY_PAIR_ID, "") ?: ""
        set(value) = sp.edit().putString(KEY_PAIR_ID, value).apply()

    var pairedSecretHex: String
        get() = sp.getString(KEY_PAIR_SECRET, "") ?: ""
        set(value) = sp.edit().putString(KEY_PAIR_SECRET, value).apply()

    var keepScreenOn: Boolean
        get() = sp.getBoolean(KEY_KEEP_SCREEN, true)
        set(value) = sp.edit().putBoolean(KEY_KEEP_SCREEN, value).apply()

    var autoReconnect: Boolean
        get() = sp.getBoolean(KEY_AUTO_RECONNECT, true)
        set(value) = sp.edit().putBoolean(KEY_AUTO_RECONNECT, value).apply()

    var lastResolution: String
        get() = sp.getString(KEY_LAST_RES, "1920x1080") ?: "1920x1080"
        set(value) = sp.edit().putString(KEY_LAST_RES, value).apply()

    var facePixelation: Boolean
        get() = sp.getBoolean("face_pixelation", false)
        set(value) = sp.edit().putBoolean("face_pixelation", value).apply()

    var facePixelationStrength: Int
        get() = sp.getInt("face_pixelation_strength", 50).coerceIn(1, 100)
        set(value) = sp.edit().putInt("face_pixelation_strength", value.coerceIn(1, 100)).apply()

    var avatarEnabled: Boolean
        get() = sp.getBoolean("avatar_enabled", false)
        set(value) = sp.edit().putBoolean("avatar_enabled", value).apply()

    var avatarObsHash: String
        get() = sp.getString("avatar_obs_hash", "") ?: ""
        set(value) = sp.edit().putString("avatar_obs_hash", value).apply()

    /** Local (phone-picked) avatar settings. */
    var avatarSettings: AvatarSettings
        get() = AvatarSettings(
            mode = if (sp.getString("avatar_mode", "animate") == "position") "position" else "animate",
            cols = sp.getInt("avatar_cols", 1).coerceIn(1, 16),
            rows = sp.getInt("avatar_rows", 1).coerceIn(1, 16),
            fps = sp.getInt("avatar_fps", 12).coerceIn(1, 30),
            scale = sp.getInt("avatar_scale", 150).coerceIn(50, 300),
            offsetY = sp.getInt("avatar_offset_y", -10).coerceIn(-100, 100),
            flip = sp.getBoolean("avatar_flip", false),
        )
        set(value) = sp.edit()
            .putString("avatar_mode", value.mode)
            .putInt("avatar_cols", value.cols)
            .putInt("avatar_rows", value.rows)
            .putInt("avatar_fps", value.fps)
            .putInt("avatar_scale", value.scale)
            .putInt("avatar_offset_y", value.offsetY)
            .putBoolean("avatar_flip", value.flip)
            .apply()

    var lastFps: Int
        get() = sp.getInt(KEY_LAST_FPS, 30)
        set(value) = sp.edit().putInt(KEY_LAST_FPS, value).apply()

    fun clearPairing() {
        sp.edit()
            .remove(KEY_PAIR_IP)
            .remove(KEY_PAIR_PORT)
            .remove(KEY_PAIR_NAME)
            .remove(KEY_PAIR_ID)
            .remove(KEY_PAIR_SECRET)
            .apply()
    }

    fun hasPairing(): Boolean = pairedSecretHex.length == 64 && pairedServerIp.isNotEmpty()

    companion object {
        private const val KEY_DEVICE_ID = "device_id"
        private const val KEY_DEVICE_NAME = "device_name"
        private const val KEY_PAIR_IP = "pair_ip"
        private const val KEY_PAIR_PORT = "pair_port"
        private const val KEY_PAIR_NAME = "pair_name"
        private const val KEY_PAIR_ID = "pair_id"
        private const val KEY_PAIR_SECRET = "pair_secret"
        private const val KEY_KEEP_SCREEN = "keep_screen_on"
        private const val KEY_AUTO_RECONNECT = "auto_reconnect"
        private const val KEY_LAST_RES = "last_resolution"
        private const val KEY_LAST_FPS = "last_fps"
    }
}
