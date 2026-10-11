# Android APK

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
