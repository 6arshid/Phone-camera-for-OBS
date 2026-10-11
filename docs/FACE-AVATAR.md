# Face avatar: image or character sheet (Android + Windows OBS)

Replace each tracked face with an image or a frame from a character sheet.
The sprite follows the face as it moves, scales with distance and tilts with
the head when the camera reports eye positions. There is no AI model, cloud
service or online API. Positions come from the camera's own Camera2 face
metadata, the same source as [face pixelation](FACE-PIXELATION.md).

## Where to set it

**OBS** (Phone Camera source properties → *Face avatar*): tick the group, then
choose an image file. PNG, JPEG, WebP, GIF (first frame) and BMP up to 4 MB are
accepted. OBS sends the file to the phone over the encrypted control channel.
The phone caches it by SHA-256, so reconnecting does not send it again.

**Phone** (*Face avatar* switch → *Avatar…*): pick an image from the
gallery and set the same options. The phone keeps this image and its settings.

If OBS has an avatar enabled with an image, OBS's avatar is used once it has
arrived. Otherwise the phone's own avatar is used when its switch is on.

## Character sheets

A sheet is a grid of equally sized cells, numbered left to right and top to
bottom. Set *columns* and *rows* to match. A 1×1 sheet is a single image.
Transparent PNGs blend over the video.

| Mode | Behaviour |
|------|-----------|
| Animate while moving | Frame 1 when the face is still; cycles through all cells at *speed* fps while it moves. |
| Pick cell by face position | The cell is chosen from where the face is in the picture, e.g. a 3×3 sheet of "looking" poses. |

Other options:

- **Size**: sprite height as a percentage of face height (50–300 %).
- **Vertical offset**: shift along the face's up direction (negative = up).
- **Mirror when moving left**: flips the sprite horizontally based on movement
  direction, for sheets drawn facing right.

Images larger than 2048 px on their longest side are downsampled on the phone.

## How it works

1. Camera2 reports face rectangles (and, in FULL mode, eye positions). When
   the avatar is on, the app requests FULL mode if the camera supports it.
2. `AvatarTracker` maps them into output space using the same texture matrix
   that rotates and mirrors the video. It smooths position, size and roll,
   then picks the sheet frame.
3. The GL fragment shader draws the sprite cell over the camera image, before
   both the preview and the encoder. OBS receives video that already contains it.

When the face is lost, the sprite stays in its last position for 0.7 s, then
disappears. Up to eight faces each get their own sprite. Face pixelation and
the avatar can be on together; the sprite is drawn on top.

This is not an anonymization feature. Missed detections show the real face.
Use [face pixelation](FACE-PIXELATION.md) when faces must be hidden.

## Compatibility

Use the 1.0.5 Android app with the 1.0.5 Windows plugin. OBS uploads only to
phones that advertise `caps.features.avatar`, so older apps are unaffected.
With an older plugin, the phone's own avatar still works. iOS does not
implement this feature.

Protocol details: [PROTOCOL.md §3.3](PROTOCOL.md#33-face-avatar-extension).
