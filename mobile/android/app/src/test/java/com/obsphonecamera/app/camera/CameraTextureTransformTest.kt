package com.obsphonecamera.app.camera

import org.junit.Assert.assertArrayEquals
import org.junit.Test

class CameraTextureTransformTest {
    private val flip = floatArrayOf(1f,0f,0f,0f, 0f,-1f,0f,0f, 0f,0f,1f,0f, 0f,1f,0f,1f)
    private val back90 = floatArrayOf(0f,-1f,0f,0f, -1f,0f,0f,0f, 0f,0f,1f,0f, 1f,1f,0f,1f)
    private val front270 = floatArrayOf(0f,-1f,0f,0f, 1f,0f,0f,0f, 0f,0f,1f,0f, 0f,1f,0f,1f)

    @Test fun portraitDoesNotApplySensorRotationTwice() {
        assertArrayEquals(back90, CameraTextureTransform.correct(back90, 90, false, 90, false), 0.00001f)
        assertArrayEquals(front270, CameraTextureTransform.correct(front270, 270, true, 270, true), 0.00001f)
    }

    @Test fun landscapeRemovesProducerRotationAndMirror() {
        assertArrayEquals(flip, CameraTextureTransform.correct(back90, 90, false, 0, false), 0.00001f)
        assertArrayEquals(flip, CameraTextureTransform.correct(front270, 270, true, 0, false), 0.00001f)
    }

    @Test fun producerCropIsPreserved() {
        val cropped = back90.copyOf().apply {
            this[1] = -0.8f; this[4] = -0.6f; this[12] = 0.8f; this[13] = 0.9f
        }
        assertArrayEquals(cropped, CameraTextureTransform.correct(cropped, 90, false, 90, false), 0.00001f)
    }
}
