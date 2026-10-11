# Android APK

## Version 1.0.7

Fixes the stream sometimes never starting ("Connected — waiting for OBS to
start the stream" while OBS keeps showing its waiting card). `drawFrame` held
the GlPipeline lock while waiting up to 500 ms for a camera frame and
re-acquired it immediately. Before the camera opened, the session thread's
`setCameraTransform`/`setRotation` calls starved on that lock, so the camera
never opened. The frame wait now happens outside the lock.

Validation: APK builds; 27 Android unit tests pass. Needs on-device confirmation.

## Version 1.0.6

Fixes black video on the phone preview and in OBS introduced in 1.0.5. The
avatar's `sampler2D` defaulted to texture unit 0, which the camera's
`samplerExternalOES` already uses. GLES forbids sampler types sharing a unit,
so every draw failed with `GL_INVALID_OPERATION`. The sprite sampler is now
always bound to unit 1. Shader compile/link failures and the first draw-time
GL error are now logged under the `OcpGl` logcat tag.

Validation: both shaders compile with glslangValidator (GLSL ES 1.00); APK
builds; 27 Android unit tests pass. Still needs on-device confirmation.
The 1.0.5 Windows plugin is unchanged and remains compatible.

## Version 1.0.5

Adds **Face avatar**: an image or character sheet replaces each tracked face
and follows its movement, size and head tilt. Camera2 face metadata drives it;
no AI model or online API is used. Choose the image in the OBS source
properties (sent to the phone over the encrypted control channel and cached by
SHA-256) or on the phone (*Avatar…*). Sheets support *animate while moving*
and *cell by face position* modes, with size, offset, speed and direction-flip
options. OBS's avatar takes priority over the phone's. See [face avatar](../docs/FACE-AVATAR.md).

Validation: debug APK builds; 27 Android unit tests pass (avatar tracker,
avatar protocol/reassembly, messages, texture transform). The Windows plugin
builds; 56 C++ unit tests and the OBS module integration test pass. No Android
device was connected, so on-device sprite alignment (front/back, rotation),
FULL-mode head tilt and the gallery picker still need physical verification.
iOS is not included.

Install `Phone-Camera-for-OBS-1.0.5.apk` together with the 1.0.5 Windows plugin.

## Version 1.0.4

Adds a persistent **Pixelate faces** switch and strength slider (1..100) on
Android and matching controls in the Windows OBS source properties. Camera2
face metadata drives a GPU mosaic applied to both preview and encoder input.
No additional AI model or cloud service is used. Either endpoint can enable
the effect; when both do, the higher strength wins. Missing, stale or unsupported
face tracking pixelates the whole image. See [face pixelation](../docs/FACE-PIXELATION.md).

Validation: APK builds; message and texture-transform regression tests pass;
Windows C++ tests and the OBS module integration test pass. The full Android
suite still has the previously documented ControlCipher counter test failure.
No Android device was connected, so moving-face coverage and front/back camera
alignment still need physical-device verification. iOS is not included.

Install `Phone-Camera-for-OBS-1.0.4.apk` together with the updated Windows plugin.

## Version 1.0.3

Fixes sideways video caused by applying sensor rotation/mirroring twice:
Camera2's SurfaceTexture transform is compensated before the requested
orientation is applied, while preserving producer crop. Both preview and OBS
use the corrected image. Camera frames retain their aspect ratio with letterboxing.
Auto orientation follows the physical phone sensor even when screen rotation
is locked, with a dead zone to avoid jitter. Orientation updates use the active
camera after switching between front and back.

Validation: three texture-transform regression tests pass (front/back portrait,
landscape, crop preservation); debug APK builds successfully. Physical-phone
preview and OBS streaming still require verification.

Install `Phone-Camera-for-OBS-1.0.3.apk` over the existing Android app.

## Version 1.0.2

The Android home screen now uses QR scanning instead of manual IP entry, shows
the installed version and sent frame count, and includes an adaptive camera
icon. The OBS waiting card contains a pairing QR directly in its video output.
Android Auto video settings now resolve to 1280x720 at 30 fps. EGL context
ownership is released between rendering operations and acquired before camera
texture updates. Visible OBS sources enable streaming even in preview.

Validation: Android APK builds; 12 message/configuration tests pass, including
Auto settings. C++ tests and TCP pairing/reconnection/active-stream tests pass.
The rendered pairing QR payload decodes with ZXing at full and half size.
Physical-phone streaming still requires user verification. The existing
control-counter test limitation described below remains.

An iOS AppIcon asset was added; iOS was not built on this Windows machine.

`Phone-Camera-for-OBS-debug.apk` is a debug-signed, directly installable build
for Android 8.0 and newer. Copy it to the phone and open it to install.

Keep the app open while streaming. Connect the phone and OBS computer to the
same local network, add the Phone Camera source in OBS, then scan its pairing
QR or enter the computer IP address, control port and pairing code manually.

Validation: APK signature verified; 71 of 72 unit tests passed. The existing
`ControlCipherTest.countersArePerDirectionAndStrictlySequential` test fails:
the current protocol MAC does not bind the implicit frame counter. This build
preserves compatibility with the OBS plugin's current protocol implementation.
Streaming and camera behavior have not yet been tested on a physical phone.

The updated APK fixes the authentication transcript and waits for auth_ok key
installation before reading encrypted configuration. A first manual connection
requires the current six-digit OBS code; a blank code is valid after pairing.
Install it together with the updated OBS plugin, which now accepts QR/stored
HKDF secrets and sends auth_ok as plaintext. TCP integration tests pass for
pairing by code, stored-secret reconnection and encrypted configuration; QR
window opening and reopening are also checked.

The missing launcher activity and manifest were added, and Android API compile
errors in camera controls, OpenGL, discovery and thermal reporting were fixed.
