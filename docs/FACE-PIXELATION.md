# Face pixelation (Android + Windows OBS)

Enable **Pixelate faces** on the Android phone or in the Phone Camera source
properties in OBS. Strength ranges from 1 to 100; larger values make larger
blocks. The phone preference persists between connections, and OBS saves its
setting with the source. Either switch enables the effect. When both are on,
the larger strength wins; disable both to turn it off.

The phone applies the same GPU effect to its preview and encoder input, before
compression and network transmission. OBS receives the processed video.
Camera2 face metadata supplies moving face rectangles, with a margin around
each face. There is no added AI model, cloud service, or face recognition
dependency. The camera manufacturer's implementation determines how faces
are detected; this does not guarantee that the hardware itself uses no ML.

If the camera does not support face detection, reports no faces, stops reporting
for 250 ms, or reports more than eight faces, the entire image is pixelated.
This is a fallback, not a guarantee of anonymization: missed faces alongside
detected faces, rapid movement and low strengths can still reveal details.
Physical-device verification is required for front/back cameras, rotation,
movement, multiple faces, camera switching and reconnects.

Use the updated Android app together with the updated Windows plugin. Older
phone apps ignore the new settings. iOS does not implement this feature.

Protocol extension: `config.camera.face_pixelation` (boolean, default false)
and `config.camera.face_pixelation_strength` (integer 1..100, default 50).
Legacy configurations keep the OBS setting off; the phone's independent
local setting still applies.

API reference: [Camera2 face bounds](https://developer.android.com/reference/android/hardware/camera2/params/Face)
and [face detection capabilities](https://developer.android.com/reference/android/hardware/camera2/CameraCharacteristics#STATISTICS_INFO_AVAILABLE_FACE_DETECT_MODES).
