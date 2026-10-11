# Installation

Installing the OBS plugin on Windows, and getting a phone connected.

## 1. Install the plugin

### Option A — install from the build tree

Build the project (see the README for the full build steps), then:

```powershell
cmake --install build --config Release --prefix "C:/Program Files/obs-studio"
```

This uses the `install()` rules in `src/plugin/CMakeLists.txt` and copies:

* the module to `<prefix>/obs-plugins/64bit/obs-phone-camera.dll`
* the locale files to `<prefix>/data/locale/`

### Option B — copy by hand

Copy these two pieces into your OBS installation:

```
<obs-studio>/obs-plugins/64bit/obs-phone-camera.dll
<obs-studio>/data/locale/en-US.ini
<obs-studio>/data/locale/fa-IR.ini
```

`<obs-studio>` is usually `C:\Program Files\obs-studio`.

### Restart OBS

Restart OBS Studio. The plugin loads at startup; if it fails to load, OBS will
report it in *Help → Log Files → View Current Log*. See
[TROUBLESHOOTING.md](TROUBLESHOOTING.md).

## 2. Allow the plugin through the firewall

The plugin listens on three local ports and the phone must be able to reach the
first two. Windows will normally prompt you the first time; if it does not,
allow it manually:

| Port | Protocol | Purpose |
| --- | --- | --- |
| 47100 | TCP | Control channel — pairing, auth, session control |
| 47101 | UDP | Media channel — H.264 video (and optional audio) |
| 47109 | UDP | Discovery — finding the server on the LAN |

All three are configurable in the source's properties (the media port is
control+1). Windows Firewall rules should be scoped to **Private** networks.
Running OBS and the phone on different networks (for example, phone on a guest
Wi-Fi or on cellular) will not work without extra routing — they must be able to
reach each other on the local network.

## 3. Add the source

In OBS: *Sources → + → Phone Camera*. A status card appears showing the
pairing code and the addresses the server is listening on.

## 4. Pair the phone

Two ways to pair:

* **QR code** — click *Show pairing QR* in the source properties, then scan it
  with the phone app. The QR encodes `ocp1://pair?h=<ip>&p=<port>&s=<secret>&n=<name>`.
* **Pairing code** — read the 6-digit code off the status card and type it into
  the phone app. The code expires after 5 minutes; *New pairing code* refreshes it.

Pairing is required by default (*Require pairing* is on). Once a phone has
paired successfully it is remembered, and you can *Forget paired devices* to
start over.

## 5. Configure the source

Select the source to see its properties:

* **Phone device** — *Any paired device*, or a specific phone. *Refresh device
  list* re-scans.
* **Resolution** — *Auto* (from the phone), 2160p / 1440p / 1080p / 720p / 480p,
  or *Custom* with explicit width and height (160–3840 × 120–2160, even values).
* **Frame rate** — *Auto*, 15 / 24 / 30 / 60.
* **Bitrate (kbps)** — `0` means auto.
* **Keyframe interval (s)** — 1–10. Larger values compress better but add latency
  and make recovery after packet loss slower.
* **Camera** — back or front.
* **Orientation** — auto, portrait, landscape.
* **Crop aspect ratio** — no crop, 16:9, 9:16, or 1:1 (center crop).
* **Send phone audio** — off by default.
* **Show status overlay when idle** — renders the waiting card into the source
  so it is never blank in the preview.
* **Language** — auto (match OBS), English, or Persian. Applies to all sources.
* **Require pairing** — leave on unless you understand the risk.
* **Control port** — 1024–65535. Changing it restarts the server immediately;
  no OBS restart is needed.

## Uninstall

Delete `obs-phone-camera.dll` from `obs-plugins/64bit/` and the
`data/locale` entries if you copied them there, then restart OBS. Paired-device
records are stored by the plugin; removing the module removes them on next
launch.

## Next steps

* [TROUBLESHOOTING.md](TROUBLESHOOTING.md) — if something is not working.
* [SECURITY.md](SECURITY.md) — what pairing does and does not protect.
* [SUPPORTED_PLATFORMS.md](SUPPORTED_PLATFORMS.md) — what is verified.
