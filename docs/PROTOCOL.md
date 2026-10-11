# OCP/1 — OBS Phone Camera Protocol Specification

Version: `ocp/1` (protocol version integer `1`)
Status: implemented by this repository (desktop plugin = server, mobile app = client).

This document is the **contract** between the desktop plugin, the Android app,
the iOS app and the automated tests. Every implementation MUST follow it
exactly.

---

## 1. Roles, transports and ports

| Role | Entity | Listens on | Connects to |
|------|--------|-----------|-------------|
| **Server** | OBS plugin (`obs-phone-camera`) | TCP `47100` (control), UDP `47101` (media), UDP `47109` (discovery) | — |
| **Client** | Phone app | UDP `47109` (discovery) | server TCP `47100`, sends media to server UDP `47101` |

* All ports are **decimal, host order** in JSON; binary headers use **network
  byte order (big endian)**.
* Default ports are configurable in the OBS source ("Advanced" section).
  The server tells the client its media UDP port inside `auth_ok`, so the
  media port may differ from `tcp+1`.
* Everything is local-network only. There is no cloud component.
* Address families: IPv4 (primary). IPv6 is not used in `ocp/1`.

## 2. Discovery (UDP port 47109)

Discovery packets are **unencrypted JSON datagrams** (they contain no
secrets). Both sides bind `0.0.0.0:47109` with `SO_REUSEADDR`/`SO_BROADCAST`,
and also send to `255.255.255.255:47109` and to the multicast group
`239.255.74.79:47109`.

Server announcement (every 2 s while at least one source exists):

```json
{"t":"obs","v":1,"id":"<32-hex instance id>","name":"DESKTOP-NAME","tcp":47100,"udp":47101}
```

Client announcement (while the app is idle / waiting, every 2 s):

```json
{"t":"phone","v":1,"id":"<32-hex device id>","name":"Pixel 7","os":"android","proto":"ocp/1"}
```

Fields:

* `t` — message type: `"obs"` | `"phone"`.
* `v` — protocol version, currently `1`.
* `id` — stable random 128-bit id, lowercase hex (32 chars).
* `name` — human readable device/host name (UTF-8, ≤ 64 bytes).
* `tcp` — server control port (server announcements only).
* `udp` — server **media** port (server announcements only). This is the port
  the phone sends media packets to (media base port, `47101` by default); it is
  *not* the discovery port (`47109`).
* `os` — `"android"` | `"ios"` (phone announcements only).

A receiver MUST ignore datagrams that fail JSON parsing, have an unknown `t`,
or `v != 1`.

## 3. Control channel (TCP)

Byte frame layout (all integers big endian):

```
offset  size  field
0       4     magic  = ASCII "OCP1"
4       4     u32    payload length N (1 .. 65536)
8       1     u8     flags   bit0 = payload encrypted
9       2     u8[2]  reserved = 0
11      16    u8[16] tag (HMAC-SHA256/128 truncated), zeros when flags.bit0 = 0
27      N     payload (JSON, UTF-8)
```

Total header size = **27 bytes**.

### 3.1 Encryption of control frames

Before `auth_ok` is sent by the server, frames are plaintext
(`flags = 0`, `tag = 0`).

From `auth_ok` onward **both directions** encrypt every payload:

* key material derived in §4.
* `k_ctrl_enc` = first 32 bytes of `HKDF(master, salt, "ocp1-ctrl", 64)`
* `k_ctrl_mac` = last 32 bytes of the same 64 bytes.
* Cipher: ChaCha20 (RFC 8439), nonce = `u64 frame_counter` (little endian,
  8 bytes) ++ `0x00`*4 — i.e. the 12-byte nonce is the little-endian
  counter in the low 8 bytes.
  * Frame counters are **per direction**, start at 0 for the first encrypted
    frame sent by that side.
* `tag` = first 16 bytes of `HMAC-SHA256(k_ctrl_mac, frame_header[0..26]
  with the tag field zeroed || ciphertext)`.
* The receiver MUST reject a frame when the tag is invalid or when the
  counter is not strictly greater than the last accepted counter.

### 3.2 Handshake state machine (client = phone, server = OBS plugin)

```
client -> server : hello          (plaintext)
server -> client : challenge      (plaintext)
client -> server : auth           (plaintext)
server -> client : auth_ok        (plaintext)   <- keys confirmed, media port sent
client -> server : ready          (encrypted)
server -> client : config         (encrypted, may repeat)
client -> server : stats          (encrypted, 1 Hz while streaming)
server -> client : hint           (encrypted, 1 Hz while streaming)
server -> client : ping           (encrypted)
client -> client : pong           (encrypted)
either  -> either: error          (plaintext or encrypted, then close)
either  -> either: bye            (encrypted, then close)
```

**hello** (client → server)

```json
{"type":"hello","proto":"ocp/1","kdf":"hkdf-sha256","iter":0,
 "salt":"<32-hex>","nonce":"<32-hex>",
 "device":{"id":"<32-hex>","name":"Pixel 7","os":"android","app":"1.0.0"}}
```

* `kdf = "hkdf-sha256"` → the pre-shared secret is used directly
  (QR code secret or a previously stored device secret). `iter` must be `0`.
* `kdf = "pbkdf2-sha256"` → the secret is the 6-digit pairing code, stretched
  with `PBKDF2-HMAC-SHA256(password = code ASCII digits, salt = salt bytes
  from the hello frame, c = iter, dkLen = 32)`. `iter` MUST be ≥ 100000; the
  server uses the value from the frame (and rejects `iter < 100000`).
* `salt` — 16 random bytes, hex encoded.
* `nonce` — 16 random bytes, hex encoded (client nonce).

**challenge** (server → client)

```json
{"type":"challenge","nonce":"<32-hex>","kdf":"<echo>","iter":<echo>}
```

* `nonce` — 16 random bytes (server nonce).

Both sides now compute:

```
master        = PSK (see below)
transcript    = SHA256( hello_payload_bytes || challenge_payload_bytes )
salt_kdf      = client_nonce (16B) || server_nonce (16B)
k_auth        = HKDF-SHA256(master, salt_kdf, "ocp1-auth",   32)
k_ctrl        = HKDF-SHA256(master, salt_kdf, "ocp1-ctrl",    64)
k_media       = HKDF-SHA256(master, salt_kdf, "ocp1-media",   64)
```

`master` resolution on the **server**:

1. `hello.device.id` is looked up in the persisted paired-device table;
   if found, `master = stored secret`.
2. else if `kdf == "hkdf-sha256"` and a QR secret is currently valid,
   `master = qr_secret`.
3. else if `kdf == "pbkdf2-sha256"` and a pairing code is currently valid,
   `master = PBKDF2(code, salt, iter, 32)`.
4. else → send `error {code:"pairing_failed"}` and close.

On the **client** `master` is: the QR secret, or `PBKDF2(code, …)`, or the
secret the client stored after a previous successful pairing.

**auth** (client → server)

```json
{"type":"auth","nonce":"<32-hex>","proof":"<64-hex>"}
```

* `nonce` — repeats the client hello nonce.
* `proof` = `HMAC-SHA256(k_auth, "phone" || client_nonce || server_nonce ||
  transcript)` (64 hex chars).

**auth_ok** (server → client)

```json
{"type":"auth_ok","session":"<32-hex>","media_port":47101,
 "proof":"<64-hex>","paired":true}
```

* `session` — 16 random bytes, id of this session (used in every media
  packet).
* `media_port` — UDP port for media packets.
* `proof` = `HMAC-SHA256(k_auth, "obs" || server_nonce || client_nonce ||
  transcript)`.
* `paired` — `true` when the server has (just) stored this device as paired.

On proof verification failure the client MUST send
`error {code:"auth_failed"}` and close.

**ready** (client → server) — capability report, sent once after `auth_ok`:

```json
{"type":"ready","caps":{
  "video":{"codec":["h264"],"resolutions":[[3840,2160],[1920,1080],[1280,720],[640,360]],
           "fps":[60,30,24],"aspect_ratios":["4:3","16:9"],"max_bitrate_kbps":50000},
  "audio":{"codecs":["pcm_s16le"],"sample_rates":[48000],"channels":[1]},
  "cameras":[{"id":"back","label":"Back"},{"id":"front","label":"Front"}],
  "features":{"torch":true,"manual_focus":true,"exposure":true,"white_balance":true}}}
```

**config** (server → client) — full state; sent once after `ready` and again
whenever the user changes a property. Unknown keys MUST be ignored by the
client.

```json
{"type":"config",
 "video":{"codec":"h264","width":1920,"height":1080,"fps":30,
          "bitrate_kbps":4000,"gop_s":2,"camera":"back",
          "orientation":"auto"},
 "camera":{"focus_mode":"auto","focus_distance":0.5,
           "exposure_mode":"auto","exposure_bias":0.0,
           "wb_mode":"auto","wb_kelvin":5500,"torch":false},
 "audio":{"enabled":false,"sample_rate":48000,"channels":1},
 "stream":{"active":true}}
```

Field notes:

* `video.camera` — `"back"` | `"front"` | `"toggle"` (toggle = switch to the
  other camera **without** reconnecting / re-initialising the session).
* `video.orientation` — `"auto"` (sensor), `"portrait"`, `"landscape"`.
  The client is responsible for rotating frames before encoding so that the
  bitstream already has the requested orientation.
* `video.width/height` — the requested output size. The client picks the
  closest supported size ≥ the requested area and reports the actual size via
  the first decoded frame / `stats`.
* `video.bitrate_kbps` — encoder target; `0` means "client picks a default".
* `video.gop_s` — key frame interval in seconds (client rounds to frames).
* `camera.focus_mode` — `"auto"` | `"continuous"` | `"manual"`.
  `focus_distance` ∈ [0,1] (0 = near, 1 = far), used when manual.
* `exposure_mode` — `"auto"` | `"manual"`; `exposure_bias` in EV (−2..+2).
* `wb_mode` — `"auto"` | `"manual"`; `wb_kelvin` 2500..10000.
* `audio.enabled` — microphone streaming. When `false` the client MUST stop
  sending audio packets.
* `stream.active` — `true` start producing media, `false` stop producing
  media but keep the session.

**stats** (client → server, every 1 s while `stream.active`)

```json
{"type":"stats","fps":29.8,"bitrate_kbps":3980,"frames":1234,
 "battery":72,"thermal":"nominal","camera":"back",
 "ts_rel_ns":5000000000}
```

* `ts_rel_ns` — nanoseconds since the client started the stream (media
  timestamps use the same clock, see §5).

**ping / pong**

```json
{"type":"ping","t":<unix ms>}      {"type":"pong","t":<unix ms>}
```

**hint** (server → client, ~1 s while streaming; the client may ignore it)

```json
{"type":"hint","loss_pct":2.5,"rtt_ms":12,"queue_ms":30}
```

* `loss_pct` — percentage of media frames the server failed to reassemble in
  the last interval; `rtt_ms` — control round trip; `queue_ms` — server-side
  decode queue depth in milliseconds.
* The client MAY use this for adaptive bitrate: decrease `bitrate_kbps` by
  20 % when `loss_pct > 5`, increase by 10 % (never above the configured
  target) after 10 consecutive seconds with `loss_pct < 1`. Adaptation MUST
  NOT change resolution or fps, and MUST NOT reconfigure the encoder more
  than once every 2 s.


**error**

```json
{"type":"error","code":"pairing_failed","msg":"Pairing code rejected"}
```

Standard codes: `pairing_failed`, `auth_failed`, `protocol_error`,
`unsupported`, `busy`, `internal`.

**bye** — `{"type":"bye","reason":"shutdown"}`; either side may send it
before closing the TCP connection.

## 4. Media channel (UDP)

One datagram = one packet. Header (32 bytes, **big endian**, plaintext but
authenticated as additional data):

```
offset  size  field
0       4     magic = ASCII "OCPM"
4       1     u8    version = 1
5       1     u8    stream  (0 = video, 1 = audio)
6       1     u8    flags   (bit0 = keyframe)
7       1     u8    reserved = 0
8       8     u64   session tag = first 8 bytes of session id
16      4     u32   frame id (per stream, starts at 1, increments by 1 per media frame)
20      2     u16   fragment index (0-based)
22      2     u16   fragment count (≥1)
24      8     u64   timestamp, nanoseconds relative to stream start (client clock)
32      var   ciphertext (datagram_len - 32 - 16 bytes)
end-16  16    HMAC-SHA256/128 tag
```

* Datagram MUST NOT exceed **1200 bytes** total (safe under Wi-Fi/MTU 1500).
  ⇒ ciphertext ≤ 1152 bytes; the payload of a frame is split into
  `ceil(len / 1152)` fragments.
* Frame ids are **per stream**: video frames count `1,2,3,…` and audio
  frames count `1,2,3,…` independently (both sequences appear in the same
  `frame_id` field; the `stream` field tells them apart).
* ChaCha20 nonce (12 bytes, big endian) =
  `session_tag[0..4) || u8 stream || u32 frame_id || u16 frag_idx || 0x00`.
  Every packet therefore uses a fresh nonce (unique key per session +
  unique `(stream, frame_id, frag_idx)`).
* `tag` = first 16 bytes of
  `HMAC-SHA256(k_media_mac, header[0..31] || ciphertext)`.
* `k_media_enc` = `k_media[0..32)`, `k_media_mac` = `k_media[32..64)`.

### 4.1 Video payload

Raw H.264 **Annex-B** access unit (may start with `00 00 00 01 SPS …`).
Key frames (flags bit0) MUST contain SPS/PPS before the first slice so a
decoder can start at any key frame. Non-IDR pictures MUST NOT be produced
without a preceding key frame after (re)start.

### 4.2 Audio payload

`pcm_s16le`, mono, 48000 Hz (as negotiated). One audio frame = 10 ms =
480 samples = 960 bytes ⇒ exactly one packet (`fragment count = 1`).
Larger frames are fragmented identically to video. The server re-packs audio
frames into OBS audio frames of ≤ 1024 samples.

## 5. Timestamps and latency

* The client records `t0` (its monotonic clock) when it sends `stream.active`
  start. Every media packet carries `timestamp = monotonic_now - t0` (ns).
* The server records `r0` when it processes `config.stream.active = true`.
* Estimated latency for packet *i*:
  `lat_i = (recv_monotonic_i - r0) - timestamp_i` (ns).
  Reported value = exponentially weighted moving average (α = 0.1).
* OBS frame timestamps are stamped with the **server's** monotonic clock at
  decode output, so audio/video stay in sync even though the two machines'
  clocks differ.

## 6. Pairing payloads

### 6.1 QR code (shown by the OBS plugin, scanned by the phone)

```
ocp1://pair?h=<ip>&p=<tcp-port>&s=<64-hex secret>&n=<server name>
```

* `h`, `p` — address and control port the phone must connect to (the server
  lists one URL per usable local IPv4 address; QR shows the first).
* `s` — 256-bit random secret = `master`.
* `n` — server display name (URL encoded).

The phone stores `(server ip, server id?, master)` after a successful
`auth_ok` for auto-reconnect. The secret is **burned** (rotated) when a new
QR is generated.

### 6.2 Pairing code (shown in the OBS properties dialog)

* 6 decimal digits, regenerated at least every 5 minutes, valid until
  regenerated.
* Used with `kdf = "pbkdf2-sha256"` as described in §3.2; the server rate
  limits failed attempts (≥ 1 s delay after each failure, and the code is
  rotated after 25 failures within 5 minutes).

### 6.3 Manual address entry

The phone app allows entering the OBS PC IPv4 address and control port
manually (shown by the OBS plugin as "Manual connection: 192.168.1.42:47100").
Pairing (QR or code) is still required unless the user explicitly disables
"Require pairing" on the OBS side.

## 7. Security requirements

1. All control frames after `auth_ok` and all media packets are
   encrypted and authenticated (ChaCha20 + HMAC-SHA256/128, encrypt-then-MAC).
2. `master` never crosses the network in plaintext (only HMAC proofs).
3. Failed proofs: close the connection; server delays ≥ 1 s before accepting
   another attempt from the same address; pairing code rotates after
   repeated failures.
4. Persisted device secrets are stored in the OBS module config directory
   with user-only permissions; the "Forget paired devices" button deletes
   them.
5. The phone shows a visible privacy indicator while the camera or
   microphone is streaming, and provides a stop control.
6. Discovery packets contain no secrets and are never trusted for anything
   but UI listing.
7. Media packets from unknown session tags are dropped.

## 8. Versioning

`proto`/`v` bump rules: incompatible changes to framing, crypto or handshake
increment the protocol version and change the magic/`v` fields. Additive
JSON keys (e.g. new optional `config` fields) do not.
