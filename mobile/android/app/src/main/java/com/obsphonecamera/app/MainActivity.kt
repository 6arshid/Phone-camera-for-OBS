package com.obsphonecamera.app

import android.Manifest
import android.app.Activity
import android.content.pm.PackageManager
import android.os.Bundle
import android.view.OrientationEventListener
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.*
import com.obsphonecamera.app.camera.QrScanSession
import com.obsphonecamera.app.net.*
import com.obsphonecamera.app.protocol.PairingUri
import com.obsphonecamera.app.util.Prefs

class MainActivity : Activity(), SessionManager.Listener {
    private lateinit var session: SessionManager
    private lateinit var prefs: Prefs
    private lateinit var status: TextView
    private lateinit var stats: TextView
    private lateinit var preview: SurfaceView
    private var scanner: QrScanSession? = null
    private var scanRequested = false
    private var resumed = false
    private lateinit var orientationListener: OrientationEventListener
    private var lastDeviceDegrees = -1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        prefs = Prefs(this)
        session = SessionManager(this, prefs, this)
        orientationListener = object : OrientationEventListener(this) {
            override fun onOrientationChanged(orientation: Int) {
                if (orientation == ORIENTATION_UNKNOWN) return
                // Sensor angles run opposite to display rotation. Keep a dead
                // zone near quadrant boundaries to prevent orientation jitter.
                val degrees = ((360 - orientation + 45) / 90 * 90) % 360
                val distance = if (lastDeviceDegrees < 0) 180 else
                    kotlin.math.abs(((360 - orientation - lastDeviceDegrees + 540) % 360) - 180)
                if (degrees != lastDeviceDegrees && distance >= 55) {
                    lastDeviceDegrees = degrees
                    session.setDeviceDegrees(degrees)
                }
            }
        }
        val layout = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(24, 24, 24, 24)
            setBackgroundColor(0xff101b2b.toInt())
        }
        fun label(value: String) = TextView(this).apply {
            text = value
            setTextColor(0xfff1f5f9.toInt())
            setPadding(0, 8, 0, 8)
            layout.addView(this)
        }
        fun button(textId: Int, action: () -> Unit) {
            layout.addView(Button(this).apply { setText(textId); setOnClickListener { action() } })
        }
        label(getString(R.string.home_title) + "  1.0.4").textSize = 21f
        status = label(getString(R.string.status_idle))
        label(getString(R.string.scan_hint))
        button(R.string.btn_scan_qr) {
            scanRequested = true
            stopScanner()
            session.disconnect(true)
            if (!cameraAllowed()) requestPermissions(arrayOf(Manifest.permission.CAMERA), 1)
        }
        if (prefs.hasPairing()) button(R.string.btn_reconnect) {
            if (cameraAllowed()) {
                scanRequested = false
                stopScanner()
                session.connectStored()
            } else requestPermissions(arrayOf(Manifest.permission.CAMERA), 1)
        }
        val controls = LinearLayout(this)
        fun control(textId: Int, action: () -> Unit) {
            controls.addView(Button(this).apply {
                setText(textId)
                setOnClickListener { action() }
            }, LinearLayout.LayoutParams(0, -2, 1f))
        }
        control(R.string.btn_disconnect) { scanRequested = false; stopScanner(); session.disconnect(true) }
        control(R.string.switch_camera) { session.userSwitchCamera() }
        layout.addView(controls)
        layout.addView(Switch(this).apply {
            setText(R.string.mic_on)
            setTextColor(0xfff1f5f9.toInt())
            setOnCheckedChangeListener { _, checked ->
                if (checked && checkSelfPermission(Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
                    isChecked = false
                    requestPermissions(arrayOf(Manifest.permission.RECORD_AUDIO), 2)
                } else session.userSetMicMuted(!checked)
            }
        })
        val faceSwitch = Switch(this).apply {
            setText(R.string.face_pixelation)
            setTextColor(0xfff1f5f9.toInt())
            isChecked = prefs.facePixelation
        }
        layout.addView(faceSwitch)
        val strengthLabel = label(getString(R.string.face_strength, prefs.facePixelationStrength))
        val faceStrength = SeekBar(this).apply {
            max = 99
            progress = prefs.facePixelationStrength - 1
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    strengthLabel.text = getString(R.string.face_strength, value + 1)
                    if (fromUser) session.userSetFacePixelation(faceSwitch.isChecked, value + 1)
                }
                override fun onStartTrackingTouch(bar: SeekBar?) = Unit
                override fun onStopTrackingTouch(bar: SeekBar?) = Unit
            })
        }
        layout.addView(faceStrength)
        faceSwitch.setOnCheckedChangeListener { _, enabled ->
            session.userSetFacePixelation(enabled, faceStrength.progress + 1)
        }
        label(getString(R.string.face_info)).textSize = 12f
        session.userSetMicMuted(true)
        stats = label("")
        preview = SurfaceView(this)
        layout.addView(preview, LinearLayout.LayoutParams(-1, 0, 1f))
        preview.holder.addCallback(object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) = Unit
            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
                session.setPreviewSurface(holder.surface, width, height)
                if (lastDeviceDegrees < 0) session.setDeviceDegrees(windowManager.defaultDisplay.rotation * 90)
                startScannerIfReady()
            }
            override fun surfaceDestroyed(holder: SurfaceHolder) {
                stopScanner()
                session.setPreviewSurface(null, 0, 0)
            }
        })
        setContentView(layout)
        scanRequested = !prefs.hasPairing()
        if (!cameraAllowed()) requestPermissions(arrayOf(Manifest.permission.CAMERA), 1)
    }

    private fun cameraAllowed() = checkSelfPermission(Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    private fun startScannerIfReady() {
        if (!resumed || !scanRequested || scanner != null || !cameraAllowed() ||
            !preview.holder.surface.isValid || session.state != SessionState.IDLE) return
        status.setText(R.string.scan_hint)
        scanner = QrScanSession(this, { value ->
            runOnUiThread {
                if (scanner != null) {
                    val info = try { PairingUri.parse(value) } catch (_: Exception) { null }
                    if (info == null) status.setText(R.string.invalid_qr) else {
                        scanRequested = false
                        stopScanner()
                        session.connectQr(info)
                    }
                }
            }
        }, { error -> runOnUiThread { status.text = error; stopScanner() } })
        scanner?.start(preview.holder.surface)
    }

    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>, grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == 1) {
            if (cameraAllowed()) startScannerIfReady() else status.setText(R.string.permission_denied)
        }
    }

    private fun stopScanner() { scanner?.stop(); scanner = null }
    override fun onResume() { super.onResume(); orientationListener.enable(); resumed = true; if (::preview.isInitialized) startScannerIfReady() }
    override fun onStop() { orientationListener.disable(); resumed = false; scanRequested = false; stopScanner(); session.disconnect(true); super.onStop() }
    override fun onDestroy() { session.close(); super.onDestroy() }
    override fun onStateChanged(state: SessionState, detail: String) {
        runOnUiThread {
            if (scanner == null) status.text = when (state) {
                SessionState.CONNECTED -> getString(R.string.status_connected)
                SessionState.STREAMING -> getString(R.string.live_badge)
                SessionState.IDLE -> getString(R.string.status_idle)
                else -> "$state: $detail"
            }
            if (state == SessionState.IDLE) startScannerIfReady()
        }
    }
    override fun onStats(snapshot: StatsSnapshot) {
        runOnUiThread {
            stats.text = "${snapshot.fps.toInt()} fps | ${snapshot.bitrateKbps} kbps | ${snapshot.frames} frames"
            if (snapshot.frames == 0L) status.setText(R.string.waiting_camera_frames)
            else status.setText(R.string.live_badge)
        }
    }
    override fun onStreamActive(active: Boolean) = Unit
    override fun onVideoConfig(width: Int, height: Int, fps: Int, bitrateKbps: Int, gopS: Int) {
        runOnUiThread { stats.text = "${width}×${height} · $fps fps" }
    }
    override fun onUserMicMuteChanged(muted: Boolean) = Unit
    override fun onPairingStored(ip: String, port: Int, name: String) {
        runOnUiThread { Toast.makeText(this, getString(R.string.pairing_saved, name), Toast.LENGTH_SHORT).show() }
    }
}