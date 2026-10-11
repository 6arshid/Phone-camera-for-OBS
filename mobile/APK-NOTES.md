# Android APK

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
