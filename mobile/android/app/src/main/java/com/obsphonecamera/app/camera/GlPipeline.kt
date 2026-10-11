package com.obsphonecamera.app.camera

import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES20
import android.opengl.Matrix
import android.os.Handler
import android.os.HandlerThread
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

    private val frameLock = Object()
    private val frameAvailable = AtomicBoolean(false)

    private val stMatrix = FloatArray(16)
    private val mvpMatrix = FloatArray(16)
    private val tmpMatrix = FloatArray(16)

    @Volatile
    var rotationDegrees = 0
        private set

    @Volatile
    var mirror = false

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

        Matrix.setIdentityM(stMatrix, 0)
        updateMvp()
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

    fun updateTextureBufferSize(w: Int, h: Int) {
        surfaceTexture?.setDefaultBufferSize(w, h)
    }

    fun setRotation(degrees: Int) {
        rotationDegrees = ((degrees % 360) + 360) % 360
        updateMvp()
    }

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
        val st = surfaceTexture ?: return false
        synchronized(frameLock) {
            if (!frameAvailable.get()) {
                frameLock.wait(timeoutMs)
            }
        }
        if (!frameAvailable.compareAndSet(true, false)) {
            return false
        }
        val ts = System.nanoTime()
        try {
            st.updateTexImage()
            st.getTransformMatrix(stMatrix)
        } catch (_: Exception) {
            return false
        }
        makeCurrent(encoderSurface)
        GLES20.glViewport(0, 0, encoderWidth, encoderHeight)
        draw()
        EGLExt.eglPresentationTimeANDROID(eglDisplay, encoderSurface, ts)
        EGL14.eglSwapBuffers(eglDisplay, encoderSurface)

        if (previewESurface != EGL14.EGL_NO_SURFACE && previewWidth > 0 && previewHeight > 0) {
            makeCurrent(previewESurface)
            applyPreviewViewport()
            draw()
            EGL14.eglSwapBuffers(eglDisplay, previewESurface)
        }
        return true
    }

    fun release() {
        val display = eglDisplay
        if (display == EGL14.EGL_NO_DISPLAY) return
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
        if (mirror) {
            Matrix.setIdentityM(tmpMatrix, 0)
            Matrix.setScaleM(tmpMatrix, 0, -1f, 1f, 1f)
            Matrix.multiplyMM(mvpMatrix, 0, mvpMatrix, 0, tmpMatrix, 0)
        }
        Matrix.rotateM(mvpMatrix, 0, rotationDegrees.toFloat(), 0f, 0f, 1f)
    }

    private fun draw() {
        GLES20.glClearColor(0f, 0f, 0f, 1f)
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT)
        GLES20.glUseProgram(program)
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0)
        GLES20.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, oesTextureId)
        GLES20.glUniform1i(textureLoc, 0)
        GLES20.glUniformMatrix4fv(mvpLoc, 1, false, mvpMatrix, 0)
        GLES20.glUniformMatrix4fv(stLoc, 1, false, stMatrix, 0)
        GLES20.glEnableVertexAttribArray(positionLoc)
        GLES20.glVertexAttribPointer(positionLoc, 2, GLES20.GL_FLOAT, false, 0, positionBuffer)
        GLES20.glEnableVertexAttribArray(texCoordLoc)
        GLES20.glVertexAttribPointer(texCoordLoc, 2, GLES20.GL_FLOAT, false, 0, texCoordBuffer)
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4)
        GLES20.glDisableVertexAttribArray(positionLoc)
        GLES20.glDisableVertexAttribArray(texCoordLoc)
    }

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
        return if (status[0] != 0) p else 0
    }

    private fun compileShader(type: Int, source: String): Int {
        val shader = GLES20.glCreateShader(type)
        GLES20.glShaderSource(shader, source)
        GLES20.glCompileShader(shader)
        val status = IntArray(1)
        GLES20.glGetCompileStatus(shader, status, 0)
        if (status[0] == 0) {
            GLES20.glDeleteShader(shader)
            return 0
        }
        return shader
    }

    companion object {
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
            void main() {
                gl_Position = uMVP * a_position;
                v_texCoord = (uST * vec4(a_texCoord, 0.0, 1.0)).xy;
            }
        """

        private const val FRAGMENT_SHADER = """
            #extension GL_OES_EGL_image_external : require
            precision mediump float;
            varying vec2 v_texCoord;
            uniform samplerExternalOES s_texture;
            void main() {
                gl_FragColor = texture2D(s_texture, v_texCoord);
            }
        """
    }
}
