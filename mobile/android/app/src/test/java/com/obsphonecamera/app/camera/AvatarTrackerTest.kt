package com.obsphonecamera.app.camera

import kotlin.math.abs
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

class AvatarTrackerTest {

    // Quad coords == texture coords; quad y is up, so a face "lower" in texture y is higher on screen.
    private val identity = floatArrayOf(1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f, 0f, 0f, 0f, 0f, 1f)
    private val ms = 1_000_000L

    private fun face(cx: Float, cy: Float, size: Float = 0.2f) =
        FaceSample(cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2)

    private fun near(expected: Float, actual: Float, eps: Float = 1e-3f) =
        assertTrue(abs(expected - actual) < eps, "expected $expected but was $actual")

    @Test
    fun boxSitsOnFaceWithScaleAndOffset() {
        val t = AvatarTracker()
        t.settings = AvatarSettings(scale = 200, offsetY = -10)
        t.frameAspect = 0.5f
        t.update(listOf(face(0.5f, 0.5f)), identity, 16f / 9f, 1_000 * ms)
        assertEquals(1, t.count)
        near(0.5f * 16f / 9f, t.boxes[0])
        near(0.5f + 0.1f * 0.2f, t.boxes[1]) // moved up by 10 % of face height
        near(0.2f, t.boxes[3]) // half of 200 % of 0.2
        near(0.1f, t.boxes[2]) // half height * cell aspect
        near(1f, t.params[0]) // no roll without eye landmarks
    }

    @Test
    fun rollFollowsEyeLineEvenWhenMirrored() {
        val t = AvatarTracker()
        val tilted = FaceSample(0.4f, 0.4f, 0.6f, 0.6f, floatArrayOf(0.45f, 0.45f, 0.55f, 0.55f))
        t.update(listOf(tilted), identity, 1f, 1_000 * ms)
        near(Math.toRadians(45.0).toFloat(), kotlin.math.atan2(t.params[1], t.params[0]), 1e-2f)
        val mirrored = AvatarTracker()
        val swapped = tilted.copy(eyes = floatArrayOf(0.55f, 0.55f, 0.45f, 0.45f))
        mirrored.update(listOf(swapped), identity, 1f, 1_000 * ms)
        near(t.params[1], mirrored.params[1])
    }

    @Test
    fun animateModeCyclesWhileMovingAndRestsWhenStill() {
        val t = AvatarTracker()
        t.settings = AvatarSettings(mode = "animate", cols = 4, rows = 1, fps = 10)
        var now = 1_000 * ms
        var x = 0.2f
        val seen = HashSet<Int>()
        repeat(40) {
            t.update(listOf(face(x, 0.5f)), identity, 1f, now)
            seen += t.params[2].toInt()
            x += 0.01f
            now += 33 * ms
        }
        assertTrue(seen.size >= 3, "frames seen while moving: $seen")
        repeat(40) {
            t.update(listOf(face(x, 0.5f)), identity, 1f, now)
            now += 33 * ms
        }
        assertEquals(0f, t.params[2])
    }

    @Test
    fun flipFacesMovementDirection() {
        val t = AvatarTracker()
        t.settings = AvatarSettings(flip = true)
        var now = 1_000 * ms
        var x = 0.8f
        repeat(20) {
            t.update(listOf(face(x, 0.5f)), identity, 1f, now)
            x -= 0.015f
            now += 33 * ms
        }
        assertEquals(1f, t.params[3])
        repeat(20) {
            t.update(listOf(face(x, 0.5f)), identity, 1f, now)
            x += 0.015f
            now += 33 * ms
        }
        assertEquals(0f, t.params[3])
    }

    @Test
    fun positionModePicksCellFromScreenPosition() {
        val t = AvatarTracker()
        t.settings = AvatarSettings(mode = "position", cols = 3, rows = 3)
        // Texture y 0.15 maps to quad y 0.15, the bottom row on screen.
        t.update(listOf(face(0.15f, 0.15f, 0.1f)), identity, 1f, 1_000 * ms)
        assertEquals(6f, t.params[2]) // row 2, col 0
        val u = AvatarTracker()
        u.settings = t.settings
        u.update(listOf(face(0.85f, 0.85f, 0.1f)), identity, 1f, 1_000 * ms)
        assertEquals(2f, u.params[2]) // row 0, col 2
    }

    @Test
    fun spriteHoldsBrieflyThenDisappearsWhenFaceIsLost() {
        val t = AvatarTracker()
        t.update(listOf(face(0.5f, 0.5f)), identity, 1f, 1_000 * ms)
        t.update(emptyList(), identity, 1f, 1_300 * ms)
        assertEquals(1, t.count)
        t.update(emptyList(), identity, 1f, 2_000 * ms)
        assertEquals(0, t.count)
    }

    @Test
    fun rotatedTextureMatrixMapsFaceIntoOutputSpace() {
        // 90° rotation: tex = (1 - qy, qx), i.e. qx = ty, qy = 1 - tx.
        val rot = floatArrayOf(0f, 1f, 0f, 0f, -1f, 0f, 0f, 0f, 0f, 0f, 1f, 0f, 1f, 0f, 0f, 1f)
        val t = AvatarTracker()
        t.settings = AvatarSettings(offsetY = 0)
        t.update(listOf(FaceSample(0.1f, 0.6f, 0.3f, 0.8f)), rot, 1f, 1_000 * ms)
        near(0.7f, t.boxes[0])
        near(0.8f, t.boxes[1])
    }
}
