# Troubleshooting

Symptoms, causes, and fixes. All log lines below appear in
*Help → Log Files → View Current Log*, prefixed with `[obs-phone-camera]`.

## Collect diagnostics first

1. *Copy diagnostics* in the source properties puts the current state on the
   clipboard.
2. Set `OCP_DEC_DEBUG=1` in OBS's environment before launching to get verbose
   H.264 decoder logging (`decoder_win.cpp`). Restart OBS after setting it.
3. Note the status shown on the source card: *Idle*, *Waiting*, *Connecting*,
   *Streaming*, or *Error*.

## The plugin does not appear in the source list

| Cause | Fix |
| --- | --- |
| DLL not in the right place | It must be at `<obs-studio>/obs-plugins/64bit/obs-phone-camera.dll`. OBS does not scan subfolders or the build tree. |
| OBS was not restarted | Restart OBS after copying the DLL. |
| Built against a mismatched OBS | Rebuild against the same OBS version you are running (32.0.4 in this repo). Check the log for a module-load failure. |
| Missing data files | Locale files at `<obs-studio>/data/locale/` are optional; the source still registers without them, but shows raw keys instead of labels. |

## `server start failed: cannot bind control port 47100`

Something else already holds the port, or the port number is out of range.

* Close the other application, or change **Control port** in the source
  properties (1024–65535). The server restarts immediately; no OBS restart
  needed.
* The same message can appear for `cannot bind media port` (control+1) and
  `cannot bind discovery port` (47109). If you change the control port, the
  media port moves with it (it is always control+1).
* Another OBS instance or a second *Phone Camera* source can also hold the
  ports — only one server runs per OBS process.

## The phone cannot find the OBS computer

Discovery is UDP broadcast/multicast on port 47109.

* Confirm the phone and computer are on the **same network**. Guest Wi-Fi and
  client isolation (common on hotspots and hotel/enterprise networks) block
  this.
* Check the firewall (see [INSTALL.md](INSTALL.md) §2). Both the TCP control
  port and the UDP media port must be allowed for inbound connections.
* Some routers block multicast/broadcast between wireless clients. If the phone
  never discovers the computer, use the **pairing code** path instead — it
  contains the address, so discovery is not required.

## The phone finds the computer but pairing fails

| Status / cause | Fix |
| --- | --- |
| `authentication failed` | The phone is not paired. Scan the QR or enter the pairing code. The code expires after 5 minutes — click *New pairing code*. |
| `expected hello` / `unsupported protocol version` / `unsupported kdf` | Version skew between the plugin and the phone app. Rebuild both from the same commit. |
| `pbkdf2 iteration count too low` | The phone is using fewer than 100000 iterations. Update the phone app. |
| `device id missing` / `malformed salt or nonce` | Malformed or truncated handshake — usually a proxy or firewall inspecting TCP. Disable TLS-intercepting software on the path. |

If pairing repeatedly fails, *Forget paired devices* and pair again to rule out
a stale stored secret.

## The phone connects but the video is black

* Wait a moment after connecting — the first keyframe must arrive before
  anything renders.
* Check **Resolution** and **Frame rate**; *Auto* is the safest starting point.
* Enable **Show status overlay when idle** to confirm the source itself
  renders.
* Set `OCP_DEC_DEBUG=1` and watch the log. `no H.264 decoder MFT found` means
  Windows has no usable Media Foundation H.264 decoder; `decoder rejected H.264
  input type` and `decoder activation failed` indicate a decoder problem —
  installing/repairing the Windows Media Feature Pack often helps.

## Audio is missing

**Send phone audio** is off by default. Turn it on in the source properties.
The phone must also grant microphone permission to the app.

## The video is choppy or keeps freezing

* Move the phone closer to the access point, or use 5 GHz Wi-Fi.
* Lower **Resolution** / **Frame rate**, or raise **Bitrate** only if you have
  bandwidth — an over-long keyframe interval also slows recovery after loss, so
  keep **Keyframe interval (s)** low (1–2) for unstable networks.
* 2.4 GHz networks and Bluetooth are common causes of stutter.

## The status overlay never goes away

The overlay is a placeholder drawn while no phone is connected. It is expected
to stay until a phone streams. Turn it off with **Show status overlay when
idle** if you prefer a black source.

## The plugin loads, then the log shows `session ended`

Normal on disconnect. The reasons come from the phone or network:
`handshake timeout`, `peer closed`, `socket closed`, `stopped during
handshake`. A single occurrence is usually the phone app backgrounding or
losing Wi-Fi; repeated occurrences point at the network or firewall.

## Still stuck

1. Capture the OBS log and the phone app's log for the same attempt.
2. Confirm the phone and computer can ping each other.
3. Note the exact status text and any `[obs-phone-camera]` log lines.
4. Check [TESTING.md](TESTING.md) to see whether the failing behaviour is
   covered by a test, which will narrow it down quickly.

## See also

* [INSTALL.md](INSTALL.md) — ports and firewall
* [SECURITY.md](SECURITY.md) — pairing and trust
* [SUPPORTED_PLATFORMS.md](SUPPORTED_PLATFORMS.md) — what is verified
