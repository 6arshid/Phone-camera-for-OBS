package com.obsphonecamera.app.camera

import kotlin.math.abs
import kotlin.math.atan2
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Face in camera texture space (0..1, y down); eyes = lx, ly, rx, ry when the HAL reports them. */
data class FaceSample(
    val left: Float,
    val top: Float,
    val right: Float,
    val bottom: Float,
    val eyes: FloatArray? = null,
)

data class AvatarSettings(
    val mode: String = "animate",
    val cols: Int = 1,
    val rows: Int = 1,
    val fps: Int = 12,
    val scale: Int = 150,
    val offsetY: Int = -10,
    val flip: Boolean = false,
)

/**
 * Places sprite boxes over tracked faces in output space and picks sheet
 * frames from movement. No ML: it only smooths Camera2 face rectangles.
 * Pure Kotlin so it can be tested on the JVM.
 *
 * Output "units" are output-image heights, y up, so rotation is isotropic:
 * x = quadX * aspect, y = quadY, where quad space is the drawn quad (0..1).
 */
internal class AvatarTracker {
    var settings = AvatarSettings()
    /** Sprite cell width / height in pixels. */
    var frameAspect = 1f

    /** Per sprite: center x, center y, half width, half height (units). */
    val boxes = FloatArray(MAX_FACES * 4)
    /** Per sprite: cos(roll), sin(roll), frame index, flip (0/1). */
    val params = FloatArray(MAX_FACES * 4)
    var count = 0
        private set

    private class Track(var cx: Float, var cy: Float, var w: Float, var h: Float, var angle: Float) {
        var prevX = cx
        var prevY = cy
    }

    private var tracks = ArrayList<Track>()
    private var lastNs = 0L
    private var lastSeenNs = 0L
    private var phase = 0f
    private var speed = 0f
    private var facingLeft = false

    fun reset() {
        tracks.clear()
        count = 0
        lastNs = 0L
        phase = 0f
        speed = 0f
        facingLeft = false
    }

    /**
     * @param st column-major texture matrix mapping quad coords to texture coords
     * @param aspect output image width / height
     */
    fun update(faces: List<FaceSample>, st: FloatArray, aspect: Float, nowNs: Long) {
        val dt = if (lastNs == 0L) 0f else ((nowNs - lastNs) / 1e9f).coerceIn(0f, 0.25f)
        lastNs = nowNs
        val det = st[0] * st[5] - st[4] * st[1]
        if (abs(det) < 1e-6f || aspect <= 0f) {
            count = 0
            return
        }
        // Inverse of the 2D affine part: texture -> quad, then quad -> units.
        fun ux(tx: Float, ty: Float) = (st[5] * (tx - st[12]) - st[4] * (ty - st[13])) / det * aspect
        fun uy(tx: Float, ty: Float) = (-st[1] * (tx - st[12]) + st[0] * (ty - st[13])) / det

        val observed = faces
            .sortedByDescending { (it.right - it.left) * (it.bottom - it.top) }
            .take(MAX_FACES)
            .map { f ->
                val xs = floatArrayOf(ux(f.left, f.top), ux(f.right, f.top), ux(f.left, f.bottom), ux(f.right, f.bottom))
                val ys = floatArrayOf(uy(f.left, f.top), uy(f.right, f.top), uy(f.left, f.bottom), uy(f.right, f.bottom))
                var angle = 0f
                f.eyes?.let { e ->
                    var dx = ux(e[2], e[3]) - ux(e[0], e[1])
                    var dy = uy(e[2], e[3]) - uy(e[0], e[1])
                    // Mirroring may swap the eyes; roll only needs the eye line.
                    if (dx < 0f) { dx = -dx; dy = -dy }
                    if (hypot(dx, dy) > 1e-4f) angle = atan2(dy, dx).coerceIn(-MAX_ROLL, MAX_ROLL)
                }
                Track(
                    (xs.min() + xs.max()) / 2f, (ys.min() + ys.max()) / 2f,
                    xs.max() - xs.min(), ys.max() - ys.min(), angle,
                )
            }
            .filter { it.w > 0f && it.h > 0f }

        if (observed.isEmpty()) {
            if (nowNs - lastSeenNs > HOLD_NS) tracks.clear()
            for (t in tracks) { t.prevX = t.cx; t.prevY = t.cy }
        } else {
            lastSeenNs = nowNs
            val unused = tracks.toMutableList()
            val next = ArrayList<Track>(observed.size)
            for (o in observed) {
                val match = unused.minByOrNull { hypot(it.cx - o.cx, it.cy - o.cy) }
                    ?.takeIf { hypot(it.cx - o.cx, it.cy - o.cy) < max(it.h, o.h) * 1.5f }
                if (match == null) {
                    next += o
                } else {
                    unused.remove(match)
                    match.prevX = match.cx
                    match.prevY = match.cy
                    match.cx += (o.cx - match.cx) * POS_ALPHA
                    match.cy += (o.cy - match.cy) * POS_ALPHA
                    match.w += (o.w - match.w) * SIZE_ALPHA
                    match.h += (o.h - match.h) * SIZE_ALPHA
                    match.angle += (o.angle - match.angle) * ANGLE_ALPHA
                    next += match
                }
            }
            tracks = next
        }

        val s = settings
        val primary = tracks.maxByOrNull { it.h }
        if (primary != null && dt > 0f) {
            val vx = (primary.cx - primary.prevX) / dt / primary.h
            val vy = (primary.cy - primary.prevY) / dt / primary.h
            speed += (hypot(vx, vy) - speed) * min(1f, dt * SPEED_RESPONSE)
            if (s.flip && abs(vx) > FLIP_SPEED) facingLeft = vx < 0f
        } else if (primary == null) {
            speed = 0f
        }
        if (!s.flip) facingLeft = false

        val cols = s.cols.coerceIn(1, 16)
        val rows = s.rows.coerceIn(1, 16)
        val frames = cols * rows
        val animFrame = if (s.mode != "position" && frames > 1 && speed > MOVE_SPEED) {
            phase = (phase + dt * s.fps.coerceIn(1, 30)) % frames
            floor(phase)
        } else {
            phase = 0f
            0f
        }

        tracks.forEachIndexed { i, t ->
            val halfH = t.h * s.scale / 100f / 2f
            val halfW = halfH * frameAspect
            // Offset along the face's own up direction; negative offsetY moves up.
            val shift = -s.offsetY / 100f * t.h
            val c = cos(t.angle)
            val sn = sin(t.angle)
            boxes[i * 4] = t.cx - sn * shift
            boxes[i * 4 + 1] = t.cy + c * shift
            boxes[i * 4 + 2] = halfW
            boxes[i * 4 + 3] = halfH
            val frame = if (s.mode == "position") {
                val col = floor(t.cx / aspect * cols).toInt().coerceIn(0, cols - 1)
                val row = floor((1f - t.cy) * rows).toInt().coerceIn(0, rows - 1)
                (row * cols + col).toFloat()
            } else animFrame
            params[i * 4] = c
            params[i * 4 + 1] = sn
            params[i * 4 + 2] = frame
            params[i * 4 + 3] = if (facingLeft) 1f else 0f
        }
        count = tracks.size
    }

    companion object {
        const val MAX_FACES = 8
        private const val HOLD_NS = 700_000_000L
        private const val POS_ALPHA = 0.6f
        private const val SIZE_ALPHA = 0.25f
        private const val ANGLE_ALPHA = 0.4f
        private const val SPEED_RESPONSE = 8f
        /** Face heights per second. */
        private const val MOVE_SPEED = 0.35f
        private const val FLIP_SPEED = 0.4f
        private const val MAX_ROLL = 1.05f
    }
}
