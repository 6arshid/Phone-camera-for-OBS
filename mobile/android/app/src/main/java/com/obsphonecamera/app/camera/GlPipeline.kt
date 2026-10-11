package com.obsphonecamera.app.camera

import android.graphics.Bitmap
import android.graphics.SurfaceTexture
import android.graphics.RectF
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.GLUtils
import android.opengl.Matrix
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import java.util.concurrent.atomic.AtomicBoolean

/**
 * GLES2 pipeline: camera frames arrive on an OES SurfaceTexture and are drawn
 * (with rotation/mirror) into the encoder input surface and the preview surface.
 */
class GlPipeline {

    private var eglDisplay: EGLDisplay = EGL14.EGL_NO_DISPLAY
    private var eglContext: EGLContext = EGL14.EGL_NO_CONTEXT
    private var eglConfig: EGLConfig? = null

    private var encoderSurface: EGLSurface = EGL14.EGL_NO_SURFACE
    private var previewESurface: EGLSurface = EGL14.EGL_NO_SURFACE

    private var oesTextureId = -1
    private var surfaceTexture: SurfaceTexture? = null
    private var cameraSurface: Surface? = null

    private var program = 0
    private var positionLoc = -1
    private var texCoordLoc = -1
    private var mvpLoc = -1
    private var stLoc = -1
    private var textureLoc = -1
    private var faceCountLoc = -1
    private var faceRectsLoc = -1
    private var pixelStepLoc = -1
    private var faceEnabled = false
    private var faceStrength = 50
    private var faces = emptyList<RectF>()
    private var facesUpdatedNs = 0L
    private var avatarFaces = emptyList<FaceSample>()
    private var avatar: AvatarSpec? = null
    private var avatarUploadPending = false
    private var avatarTexId = -1
    private val avatarTracker = AvatarTracker()
    private var spriteLoc = -1
    private var avatarCountLoc = -1
    private var avatarBoxLoc = -1
    private var avatarParamLoc = -1
    private var aspectLoc = -1
    private var gridLoc = -1
    private var insetLoc = -1

    @Synchronized
    fun setFacePixelation(enabled: Boolean, strength: Int) {
        if (enabled != faceEnabled) { faces = emptyList(); facesUpdatedNs = 0L }
        faceEnabled = enabled
        faceStrength = strength.coerceIn(1, 100)
    }

    @Synchronized
    fun updateFaces(samples: List<FaceSample>) {
        val rects = samples.map { f ->
            val r = RectF(f.left, f.top, f.right, f.bottom)
            // Include forehead/chin and allow for metadata latency during movement.
            r.inset(-r.width() * 0.30f, -r.height() * 0.40f)
            RectF(r.left.coerceIn(0f, 1f), r.top.coerceIn(0f, 1f), r.right.coerceIn(0f, 1f), r.bottom.coerceIn(0f, 1f))
        }.filter { it.width() > 0f && it.height() > 0f }
        // Beyond the shader's face limit, obscure the full frame.
        faces = if (rects.size > 8) emptyList() else rects
        avatarFaces = samples
        facesUpdatedNs = System.nanoTime()
    }

    /** Image or character sheet to draw over faces; null disables. */
    class AvatarSpec(val bitmap: Bitmap, val settings: AvatarSettings)

    @Synchronized
    fun setAvatar(spec: AvatarSpec?) {
        if (spec?.bitmap !== avatar?.bitmap) avatarUploadPending = true
        if (spec == null) avatarTracker.reset()
        avatar = spec
        spec?.let {
            avatarTracker.settings = it.settings
            val cellW = it.bitmap.width.toFloat() / it.settings.cols.coerceIn(1, 16)
            val cellH = it.bitmap.height.toFloat() / it.settings.rows.coerceIn(1, 16)
            avatarTracker.frameAspect = if (cellH > 0f) cellW / cellH else 1f
        }
    }

    private val frameLock = Object()
    private val frameAvailable = AtomicBoolean(false)

    private val stMatrix = FloatArray(16)
    private val mvpMatrix = FloatArray(16)
    private val correctedStMatrix = FloatArray(16)
    private var sensorOrientation = 0
    private var cameraFront = false

    @Volatile
    var rotationDegrees = 0
        private set

    @Volatile
    var mirror = false

    private var cameraWidth = 0
    private var cameraHeight = 0
    private var encoderWidth = 0
    private var encoderHeight = 0
    private var previewWidth = 0
    private var previewHeight = 0

    fun init(encoderInput: Surface, encW: Int, encH: Int) {
        encoderWidth = encW
        encoderHeight = encH
        val display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
        if (display == EGL14.EGL_NO_DISPLAY) throw IllegalStateException("eglGetDisplay failed")
        val version = IntArray(2)
        if (!EGL14.eglInitialize(display, version, 0, version, 1)) {
            throw IllegalStateException("eglInitialize failed")
        }
        eglDisplay = display
        val attribs = intArrayOf(
            EGL14.EGL_RENDERABLE_TYPE, EGL14.EGL_OPENGL_ES2_BIT,
            EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT or EGL14.EGL_PBUFFER_BIT,
            EGL14.EGL_RED_SIZE, 8,
            EGL14.EGL_GREEN_SIZE, 8,
            EGL14.EGL_BLUE_SIZE, 8,
            EGL14.EGL_ALPHA_SIZE, 8,
            EGL_RECORDABLE, 1,
            EGL14.EGL_NONE,
        )
        val configs = arrayOfNulls<EGLConfig>(1)
        val numConfigs = IntArray(1)
        if (!EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, numConfigs, 0) || numConfigs[0] == 0) {
            throw IllegalStateException("eglChooseConfig failed")
        }
        val config = configs[0]!!
        eglConfig = config
        val ctxAttrs = intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 2, EGL14.EGL_NONE)
        val context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, ctxAttrs, 0)
        if (context == null || context == EGL14.EGL_NO_CONTEXT) throw IllegalStateException("eglCreateContext failed")
        eglContext = context

        val surfAttrs = intArrayOf(EGL14.EGL_NONE)
        encoderSurface = EGL14.eglCreateWindowSurface(display, config, encoderInput, surfAttrs, 0)
        if (encoderSurface == EGL14.EGL_NO_SURFACE) throw IllegalStateException("encoder egl surface failed")
        makeCurrent(encoderSurface)

        oesTextureId = createOesTexture()
        val st = SurfaceTexture(oesTextureId)
        surfaceTexture = st
        st.setDefaultBufferSize(encW, encH)
        cameraSurface = Surface(st)

        program = createProgram()
        if (program == 0) throw IllegalStateException("GL program failed")
        positionLoc = GLES20.glGetAttribLocation(program, "a_position")
        texCoordLoc = GLES20.glGetAttribLocation(program, "a_texCoord")
        mvpLoc = GLES20.glGetUniformLocation(program, "uMVP")
        stLoc = GLES20.glGetUniformLocation(program, "uST")
        textureLoc = GLES20.glGetUniformLocation(program, "s_texture")
        faceCountLoc = GLES20.glGetUniformLocation(program, "uFaceCount")
        faceRectsLoc = GLES20.glGetUniformLocation(program, "uFaces[0]")
        pixelStepLoc = GLES20.glGetUniformLocation(program, "uPixelStep")
        spriteLoc = GLES20.glGetUniformLocation(program, "uSprite")
        avatarCountLoc = GLES20.glGetUniformLocation(program, "uAvatarCount")
        avatarBoxLoc = GLES20.glGetUniformLocation(program, "uAvatarBox[0]")
        avatarParamLoc = GLES20.glGetUniformLocation(program, "uAvatarParam[0]")
        aspectLoc = GLES20.glGetUniformLocation(program, "uAspect")
        gridLoc = GLES20.glGetUniformLocation(program, "uGrid")
        insetLoc = GLES20.glGetUniformLocation(program, "uCellInset")

        Matrix.setIdentityM(stMatrix, 0)
        updateMvp()
        detachCurrent()
    }

    fun getCameraSurface(): Surface = cameraSurface ?: throw IllegalStateException("pipeline not initialized")

    fun setOnFrameAvailableHandler(handler: Handler) {
        surfaceTexture?.setOnFrameAvailableListener({
            synchronized(frameLock) {
                frameAvailable.set(true)
                frameLock.notifyAll()
            }
        }, handler)
    }

    @Synchronized
    fun updateTextureBufferSize(w: Int, h: Int) {
        cameraWidth = w
        cameraHeight = h
        surfaceTexture?.setDefaultBufferSize(w, h)
    }

    @Synchronized
    fun setCameraTransform(sensorDegrees: Int, front: Boolean) {
        sensorOrientation = sensorDegrees
        cameraFront = front
    }

    @Synchronized
    fun setRotation(degrees: Int) {
        rotationDegrees = ((degrees % 360) + 360) % 360
        updateMvp()
    }

    @Synchronized
    fun setPreviewSurface(surface: Surface?, width: Int, height: Int) {
        previewWidth = width
        previewHeight = height
        val display = eglDisplay
        if (display == EGL14.EGL_NO_DISPLAY) return
        if (previewESurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
            EGL14.eglDestroySurface(display, previewESurface)
            previewESurface = EGL14.EGL_NO_SURFACE
        }
        if (surface != null && surface.isValid) {
            val attrs = intArrayOf(EGL14.EGL_NONE)
            previewESurface = EGL14.eglCreateWindowSurface(display, eglConfig, surface, attrs, 0)
        }
    }

    /**
     * Waits (briefly) for a camera frame, then draws it into the encoder
     * surface (with presentation timestamp) and the preview surface.
     * Returns true when a frame was drawn to the encoder.
     */
    fun drawFrame(timeoutMs: Long): Boolean {
        // Wait without holding the pipeline lock: setters (rotation, faces,
        // avatar) run on other threads and the camera only opens after them.
        synchronized(frameLock) {
            if (!frameAvailable.get()) {
                frameLock.wait(timeoutMs)
            }
        }
        if (!frameAvailable.compareAndSet(true, false)) {
            return false
        }
        return synchronized(this) { drawLocked() }
    }

    private fun drawLocked(): Boolean {
        val st = surfaceTexture ?: return false
        val ts = System.nanoTime()
        makeCurrent(encoderSurface)
        try {
            st.updateTexImage()
            st.getTransformMatrix(stMatrix)
        updateMvp()
        prepareAvatar(ts)
        GLES20.glViewport(0, 0, encoderWidth, encoderHeight)
        draw()
        EGLExt.eglPresentationTimeANDROID(eglDisplay, encoderSurface, ts)
        check(EGL14.eglSwapBuffers(eglDisplay, encoderSurface)) { "Encoder buffer swap failed" }

        if (previewESurface != EGL14.EGL_NO_SURFACE && previewWidth > 0 && previewHeight > 0) {
            makeCurrent(previewESurface)
            applyPreviewViewport()
            draw()
            EGL14.eglSwapBuffers(eglDisplay, previewESurface)
        }
        return true
        } finally {
            detachCurrent()
        }
    }

    private fun detachCurrent() {
        EGL14.eglMakeCurrent(eglDisplay, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
    }

    @Synchronized
    fun release() {
        val display = eglDisplay
        if (display == EGL14.EGL_NO_DISPLAY) return
        makeCurrent(encoderSurface)
        if (program != 0) { GLES20.glDeleteProgram(program); program = 0 }
        if (oesTextureId != -1) { GLES20.glDeleteTextures(1, intArrayOf(oesTextureId), 0); oesTextureId = -1 }
        if (avatarTexId != -1) { GLES20.glDeleteTextures(1, intArrayOf(avatarTexId), 0); avatarTexId = -1 }
        try {
            EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
        } catch (_: Exception) {
        }
        cameraSurface?.release()
        cameraSurface = null
        surfaceTexture?.release()
        surfaceTexture = null
        if (encoderSurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglDestroySurface(display, encoderSurface)
            encoderSurface = EGL14.EGL_NO_SURFACE
        }
        if (previewESurface != EGL14.EGL_NO_SURFACE) {
            EGL14.eglDestroySurface(display, previewESurface)
            previewESurface = EGL14.EGL_NO_SURFACE
        }
        if (program != 0) {
            GLES20.glDeleteProgram(program)
            program = 0
        }
        if (oesTextureId != -1) {
            GLES20.glDeleteTextures(1, intArrayOf(oesTextureId), 0)
            oesTextureId = -1
        }
        if (eglContext != EGL14.EGL_NO_CONTEXT) {
            EGL14.eglDestroyContext(display, eglContext)
            eglContext = EGL14.EGL_NO_CONTEXT
        }
        EGL14.eglTerminate(display)
        eglDisplay = EGL14.EGL_NO_DISPLAY
    }

    private fun makeCurrent(surface: EGLSurface) {
        if (!EGL14.eglMakeCurrent(eglDisplay, surface, surface, eglContext)) {
            throw IllegalStateException("eglMakeCurrent failed")
        }
    }

    private fun applyPreviewViewport() {
        val streamAspect = encoderWidth.toFloat() / encoderHeight.toFloat()
        val viewAspect = previewWidth.toFloat() / previewHeight.toFloat()
        var vpW = previewWidth
        var vpH = previewHeight
        if (viewAspect > streamAspect) {
            vpW = (previewHeight * streamAspect).toInt()
        } else {
            vpH = (previewWidth / streamAspect).toInt()
        }
        val x = (previewWidth - vpW) / 2
        val valY = (previewHeight - vpH) / 2
        GLES20.glViewport(x, valY, vpW, vpH)
    }

    private fun updateMvp() {
        Matrix.setIdentityM(mvpMatrix, 0)
        if (cameraWidth > 0 && cameraHeight > 0) {
            val swap = rotationDegrees % 180 != 0
            val imageAspect = if (swap) cameraHeight.toFloat() / cameraWidth else cameraWidth.toFloat() / cameraHeight
            val outputAspect = encoderWidth.toFloat() / encoderHeight
            if (imageAspect > outputAspect) {
                Matrix.scaleM(mvpMatrix, 0, 1f, outputAspect / imageAspect, 1f)
            } else {
                Matrix.scaleM(mvpMatrix, 0, imageAspect / outputAspect, 1f, 1f)
            }
        }
        // Camera2 already includes sensor rotation and front mirroring in uST.
        // Remove that orientation before applying the requested one, preserving
        // the producer's crop and GL vertical flip.
        val corrected = CameraTextureTransform.correct(
            stMatrix, sensorOrientation, cameraFront, rotationDegrees, mirror,
        )
        corrected.copyInto(correctedStMatrix)
    }

    /** Uploads a new sprite texture if needed and lays out sprites for this frame. */
    private fun prepareAvatar(nowNs: Long) {
        if (avatarUploadPending) {
            avatarUploadPending = false
            if (avatarTexId != -1) { GLES20.glDeleteTextures(1, intArrayOf(avatarTexId), 0); avatarTexId = -1 }
            val bmp = avatar?.bitmap
            if (bmp != null && !bmp.isRecycled) {
                val tex = IntArray(1)
                GLES20.glGenTextures(1, tex, 0)
                GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, tex[0])
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
                GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
                GLUtils.texImage2D(GLES20.GL_TEXTURE_2D, 0, bmp, 0)
                avatarTexId = tex[0]
            }
        }
        if (avatar == null || avatarTexId == -1 || cameraWidth <= 0 || cameraHeight <= 0) return
        val fresh = if (nowNs - facesUpdatedNs < 250_000_000L) avatarFaces else emptyList()
        avatarTracker.update(fresh, correctedStMatrix, outputAspect(), nowNs)
    }

    /** Width / height of the rotated camera image the quad shows. */
    private fun outputAspect(): Float =
        if (rotationDegrees % 180 != 0) cameraHeight.toFloat() / cameraWidth else cameraWidth.toFloat() / cameraHeight

    private fun draw() {
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES20.glUniform1i(textureLoc, 0)
        // Samplers of different types must never share a unit, or every draw
        // fails with GL_INVALID_OPERATION (black output) - keep uSprite on unit 1.
        GLES20.glUniform1i(spriteLoc, 1)
        val freshFaces = if (System.nanoTime() - facesUpdatedNs < 250_000_000L) faces else emptyList()
        val rects = FloatArray(32)
        freshFaces.forEachIndexed { i, r ->
            rects[i * 4] = r.left; rects[i * 4 + 1] = r.top
            rects[i * 4 + 2] = r.right; rects[i * 4 + 3] = r.bottom
        }
        // -1 disables; 0 means no tracking and pixelates the entire image.
        GLES20.glUniform1i(faceCountLoc, if (faceEnabled) freshFaces.size else -1)
        GLES20.glUniform4fv(faceRectsLoc, 8, rects, 0)
        val block = (8f + faceStrength * 0.88f) * minOf(cameraWidth, cameraHeight).coerceAtLeast(1) / 720f
        GLES20.glUniform2f(pixelStepLoc, block / cameraWidth.coerceAtLeast(1), block / cameraHeight.coerceAtLeast(1))
        val spec = avatar
        val sprites = if (spec != null && avatarTexId != -1) avatarTracker.count else 0
        GLES20.glUniform1i(avatarCountLoc, sprites)
        if (spec != null && sprites > 0) {
            GLES20.glActiveTexture(GLES20.GL_TEXTURE1)
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, avatarTexId)
            GLES20.glUniform4fv(avatarBoxLoc, AvatarTracker.MAX_FACES, avatarTracker.boxes, 0)
            GLES20.glUniform4fv(avatarParamLoc, AvatarTracker.MAX_FACES, avatarTracker.params, 0)
            GLES20.glUniform1f(aspectLoc, outputAspect())
            val cols = spec.settings.cols.coerceIn(1, 16)
            val rows = spec.settings.rows.coerceIn(1, 16)
            GLES20.glUniform2f(gridLoc, cols.toFloat(), rows.toFloat())
            // Half a texel inside each cell so linear filtering never bleeds into neighbours.
            GLES20.glUniform2f(insetLoc, 0.5f * cols / spec.bitmap.width, 0.5f * rows / spec.bitmap.height)
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        }
        GLES20.glUniformMatrix4fv(mvpLoc, 1, false, mvpMatrix, 0)
        GLES20.glUniformMatrix4fv(stLoc, 1, false, correctedStMatrix, 0)
        GLES20.glEnableVertexAttribArray(positionLoc)
        GLES20.glVertexAttribPointer(positionLoc, 2, GLES20.GL_FLOAT, false, 0, positionBuffer)
        GLES20.glEnableVertexAttribArray(texCoordLoc)
        GLES20.glVertexAttribPointer(texCoordLoc, 2, GLES20.GL_FLOAT, false, 0, texCoordBuffer)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(positionLoc)
        GLES20.glDisableVertexAttribArray(texCoordLoc)
        if (!drawErrorLogged) {
            val err = GLES20.glGetError()
            if (err != GLES20.GL_NO_ERROR) {
                drawErrorLogged = true
                Log.e(TAG, "draw failed: GL error 0x${Integer.toHexString(err)}")
            }
        }
    }

    private var drawErrorLogged = false

    private fun createOesTexture(): Int {
        val tex = IntArray(1)
        GLES20.glGenTextures(1, tex, 0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, tex[0])
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MIN_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_MAG_FILTER, GLES20.GL_LINEAR)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_S, GLES20.GL_CLAMP_TO_EDGE)
        GLES20.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES20.GL_TEXTURE_WRAP_T, GLES20.GL_CLAMP_TO_EDGE)
        return tex[0]
    }

    private fun createProgram(): Int {
        val vs = compileShader(GLES20.GL_VERTEX_SHADER, VERTEX_SHADER)
        val fs = compileShader(GLES20.GL_FRAGMENT_SHADER, FRAGMENT_SHADER)
        if (vs == 0 || fs == 0) return 0
        val p = GLES20.glCreateProgram()
        GLES20.glAttachShader(p, vs)
        GLES20.glAttachShader(p, fs)
        GLES20.glLinkProgram(p)
        GLES20.glDeleteShader(vs)
        GLES20.glDeleteShader(fs)
        val status = IntArray(1)
        GLES20.glGetProgramiv(p, GLES20.GL_LINK_STATUS, status, 0)
        if (status[0] == 0) {
            Log.e(TAG, "program link failed: ${GLES20.glGetProgramInfoLog(p)}")
            GLES20.glDeleteProgram(p)
            return 0
        }
        return p
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source)
        GLES20.glCompileShader(shader)
        val status = IntArray(1)
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, status, 0)
        if (status[0] == 0) {
            Log.e(TAG, "shader compile failed: ${GLES20.glGetShaderInfoLog(shader)}")
            GLES20.glDeleteShader(shader)
            return 0
        }
        return shader
    }

    companion object {
        private const val TAG = "OcpGl"
        private const val EGL_RECORDABLE = 0x3142

        private val positionBuffer = java.nio.ByteBuffer.allocateDirect(4 * 8)
            .order(java.nio.ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                put(
                    floatArrayOf(
                        -1f, -1f,
                        1f, -1f,
                        -1f, 1f,
                        1f, 1f,
                    ),
                )
                position(0)
            }

        private val texCoordBuffer = java.nio.ByteBuffer.allocateDirect(4 * 8)
            .order(java.nio.ByteOrder.nativeOrder())
            .asFloatBuffer()
            .apply {
                put(
                    floatArrayOf(
                        0f, 0f,
                        1f, 0f,
                        0f, 1f,
                        1f, 1f,
                    ),
                )
                position(0)
            }

        private const val VERTEX_SHADER = """
            attribute vec4 a_position;
            attribute vec2 a_texCoord;
            uniform mat4 uMVP;
            uniform mat4 uST;
            varying vec2 v_texCoord;
            varying vec2 v_quad;
            void main() {
                gl_Position = uMVP * a_position;
                v_texCoord = (uST * vec4(a_texCoord, 0.0, 1.0)).xy;
                v_quad = a_texCoord;
            }
        """

        private const val FRAGMENT_SHADER = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            varying vec2 v_quad;
            uniform samplerExternalOES s_texture;
            uniform int uFaceCount;
            uniform vec4 uFaces[8];
            uniform vec2 uPixelStep;
            uniform sampler2D uSprite;
            uniform int uAvatarCount;
            uniform vec4 uAvatarBox[8];
            uniform vec4 uAvatarParam[8];
            uniform float uAspect;
            uniform vec2 uGrid;
            uniform vec2 uCellInset;
            void main() {
                bool obscure = uFaceCount == 0;
                for (int i = 0; i < 8; i++) {
                    if (i < uFaceCount) {
                        vec4 r = uFaces[i];
                        if (v_texCoord.x >= r.x && v_texCoord.x <= r.z &&
                            v_texCoord.y >= r.y && v_texCoord.y <= r.w) obscure = true;
                    }
                }
                vec2 uv = v_texCoord;
                if (obscure) uv = clamp((floor(uv / uPixelStep) + 0.5) * uPixelStep, 0.0, 1.0);
                vec3 color = texture2D(s_texture, uv).rgb;
                // Sprites: boxes in output units (image heights, y up), rotated by head roll.
                vec2 p = vec2(v_quad.x * uAspect, v_quad.y);
                for (int i = 0; i < 8; i++) {
                    if (i < uAvatarCount) {
                        vec4 box = uAvatarBox[i];
                        vec4 prm = uAvatarParam[i];
                        vec2 d = p - box.xy;
                        vec2 l = vec2(prm.x * d.x + prm.y * d.y, -prm.y * d.x + prm.x * d.y) / box.zw;
                        if (abs(l.x) <= 1.0 && abs(l.y) <= 1.0) {
                            float u = l.x * 0.5 + 0.5;
                            if (prm.w > 0.5) u = 1.0 - u;
                            vec2 cell = clamp(vec2(u, 0.5 - l.y * 0.5), uCellInset, 1.0 - uCellInset);
                            float row = floor((prm.z + 0.5) / uGrid.x);
                            float col = prm.z - row * uGrid.x;
                            vec4 s = texture2D(uSprite, (vec2(col, row) + cell) / uGrid);
                            // Android uploads bitmaps with premultiplied alpha.
                            color = s.rgb + color * (1.0 - s.a);
                        }
                    }
                }
                gl_FragColor = vec4(color, 1.0);
            }
        """
    }
}
