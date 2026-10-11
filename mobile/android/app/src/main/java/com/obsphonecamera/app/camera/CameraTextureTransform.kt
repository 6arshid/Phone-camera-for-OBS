package com.obsphonecamera.app.camera

import kotlin.math.cos
import kotlin.math.sin

/** Column-major texture matrices; pure Kotlin so orientation can be tested on the JVM. */
internal object CameraTextureTransform {
    private fun orientation(rotation: Int, mirror: Boolean): FloatArray {
        val radians = Math.toRadians(rotation.toDouble())
        val c = cos(radians).toFloat()
        val s = sin(radians).toFloat()
        val m = if (mirror) -1f else 1f
        // GL vertical flip * rotation * horizontal mirror, about (0.5, 0.5).
        val a = c * m
        val b = -s * m
        val d = -s
        val e = -c
        return floatArrayOf(a, b, 0f, 0f, d, e, 0f, 0f,
            0f, 0f, 1f, 0f, (1f - a - d) / 2, (1f - b - e) / 2, 0f, 1f)
    }

    private fun multiply(a: FloatArray, b: FloatArray): FloatArray =
        FloatArray(16) { i ->
            val row = i % 4
            val col = i / 4
            (0..3).sumOf { k -> (a[k * 4 + row] * b[col * 4 + k]).toDouble() }.toFloat()
        }

    fun correct(input: FloatArray, sensor: Int, front: Boolean,
                rotation: Int, mirror: Boolean): FloatArray {
        val predicted = orientation(sensor, front)
        // Invert the orthogonal 2D part, with translation about the texture center.
        val inverse = floatArrayOf(
            predicted[0], predicted[4], 0f, 0f,
            predicted[1], predicted[5], 0f, 0f,
            0f, 0f, 1f, 0f,
            (1f - predicted[0] - predicted[1]) / 2,
            (1f - predicted[4] - predicted[5]) / 2, 0f, 1f,
        )
        return multiply(multiply(input, inverse), orientation(rotation, mirror))
    }
}
