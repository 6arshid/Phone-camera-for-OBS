# Architecture

How the code in this repository is organized, and why.

## Layering

```
                    ┌──────────────────────────────┐
   Phone apps       │  mobile/android, mobile/ios  │   Kotlin / Swift
                    └──────────────┬───────────────┘
                                   │  OCP/1 wire protocol (docs/PROTOCOL.md)
                    ┌──────────────▼───────────────┐
   Protocol core    │          src/core            │   C++17, no libobs
                    │  crypto · protocol · pairing │
                    │  discovery · settings · h264 │
                    └──────────────┬───────────────┘
                                   │
                    ┌──────────────▼───────────────┐
   OBS plugin       │         src/plugin           │   C++17, libobs + MF
                    │  module · source · server    │
                    │  session · decoder · ui      │
                    └──────────────────────────────┘
```

`src/core` has **no dependency on libobs or Media Foundation**. It is a plain
static library (`ocp_core`) that can be unit-tested in isolation, and it is the
single C++ reference implementation of the protocol. The phone apps re-implement
the same protocol natively, and are kept honest by sharing test vectors with the
C++ tests.

`src/plugin` is the only layer that touches OBS and the OS. On Windows it uses
Media Foundation for H.264 decode and Win32/GDI for the pairing-QR window.

## `src/core` — protocol core

| Module | Responsibility |
| --- | --- |
| `crypto/` | SHA-256, HMAC-SHA256, HKDF-SHA256, PBKDF2-HMAC-SHA256, ChaCha20 (RFC 8439), CSPRNG, hex codec. All hand-rolled against published test vectors. |
| `protocol/` | The `OCP/1` wire format: control-frame header + ChaCha20/HMAC sealing, media-packet header, fragmentation and reassembly, JSON message encode/decode. |
| `pairing/` | Master-key derivation (from QR secret or 6-digit code), session-key derivation (HKDF), and the `phone`/`obs` authentication proofs. |
| `discovery/` | The unencrypted UDP discovery announcements used to find a server on the LAN. |
| `settings/` | Typed configuration model shared by the source UI and the server. |
| `codec/` | H.264 bitstream helpers — Annex-B splitting, SPS parsing (used for resolution probing and tests). |
| `json/` | A small hand-rolled JSON parser/serializer. The core takes **no third-party dependency** — crypto, JSON and QR generation are all in-tree. |

### A note on proof nonce order

Authentication proofs bind the two session nonces in **role-dependent order**:

* phone proof: `HMAC(k_auth, "phone" || client_nonce || server_nonce || transcript)`
* obs proof: `HMAC(k_auth, "obs"    || server_nonce || client_nonce || transcript)`

This is intentional and specified in `docs/PROTOCOL.md`. The C++ API therefore
exposes separate `computePhoneProof`/`computeObsProof` and
`verifyPhoneProof`/`verifyObsProof` functions rather than a single function with
a role argument, so the two orders cannot be accidentally swapped at a call
site. This distinction was the subject of a real bug (a shared helper used the
phone order for both roles, which would have made every phone fail to
authenticate) and is now pinned by `pairing_proofs_use_distinct_nonce_order`.

## `src/plugin` — the OBS module

| File | Responsibility |
| --- | --- |
| `module.cpp` | OBS module entry points: `obs_module_load`, locale plumbing, unload. |
| `source.cpp` / `source.h` | The `obs_phone_camera` source. Owns user settings, drives the session, renders video (and the idle status card). |
| `server.cpp` / `server.h` | The network server: binds the control TCP port, media UDP port and discovery UDP port; owns sessions and the paired-device store. |
| `session.cpp` / `session.h` | One connected phone: handshake, key schedule, auth, then the control/media loops. |
| `decoder_win.cpp`, `decoder.h` | H.264 → NV12 decoding. On Windows this wraps a Media Foundation `IMFTransform`. |
| `paired_store.cpp` / `.h` | Persistent list of paired devices, keyed by device id. |
| `status_card.cpp` / `.h` | Renders the "waiting for a phone" overlay as an NV12 frame. |
| `qr_window.cpp` / `.h` | A small Win32 window that shows the pairing QR code. |
| `net_util.cpp` / `.h` | Interface enumeration and other socket helpers. |
| `nv12_util.cpp` / `.h` | NV12 frame helpers for feeding OBS. |
| `tr.h`, `locale.cpp` | Two-language (en/fa) string table; OBS's own lookup is used for the Auto mode. |

### Threading model

Each connected phone runs **three threads** (`session.cpp`): `controlLoop`,
`videoLoop` and `audioLoop`. The server itself runs **three more**
(`server.cpp`): `acceptLoop`, `mediaLoop` and `discoveryLoop`. The pairing-QR
window runs its own Win32 message loop on a fourth thread (`qr_window.cpp`).

Shared state is guarded by per-concern mutexes — `sourceMutex`,
`settingsMutex`, `videoMutex`, `audioMutex`, `thermalMutex` in a session, and a
single `mutex` in the server and paired-device store — while counters and flags
that cross threads (`stop`, `authed`, `mediaReady`, the latency/loss stats) are
`std::atomic`. Locks are always taken in a consistent order within a
translation unit; no lock is held across a network send.

Decoded frames are handed to OBS via `obs_source_output_video`, which copies
the frame, so a pointer into a decoder buffer can be passed safely — this is
what lets the decoder keep reusing its output buffer.

### Ports

Defaults are 47100 (control, TCP), 47101 (media, UDP) and 47109 (discovery,
UDP). The media port is derived as control+1, and the authoritative media port
is communicated to the phone in `auth_ok` rather than being inferred from
discovery. See `docs/PROTOCOL.md` §1.

## `mobile/` — the phone apps

Both apps implement the same client role against the same spec, in the idioms
of their platform.

* **Android** (`mobile/android`): Kotlin, CameraX for capture, `MediaCodec` for
  H.264 encoding, and a `SharedPreferences`-backed paired-device store.
* **iOS** (`mobile/ios`): Swift, AVFoundation for capture, `VideoToolbox` for
  H.264 encoding, and the Keychain for the paired-device store. The Xcode
  project is generated from `project.yml` with XcodeGen.

Neither app has been verified end-to-end from this repository's CI. Treat them
as reference implementations that a reviewer has checked against the protocol
spec, not as shipped, tested artifacts.

## Vendor code

`vendor/qrcodegen.{hpp,cpp}` is a vendored copy of [Project Nayuki's QR Code
generator](https://github.com/nayuki/QR-Code-generator), used to render the
pairing QR code. It is included unmodified.
