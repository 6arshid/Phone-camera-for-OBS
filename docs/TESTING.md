# Testing

What is tested, how to run it, and what is deliberately *not* covered.

## Running the tests

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Two CTest entries are registered by `tests/CMakeLists.txt`:

* `ocp_unit_tests` — the protocol/crypto/bitstream suite. Runs everywhere.
* `ocp_module_host` — loads the built plugin into a live libobs instance. Only
  registered on Windows, and only when both libobs and the plugin target were
  found.

To run a single executable directly:

```powershell
./build/tests/Release/ocp_tests.exe
```

## `ocp_unit_tests` — 52 tests

The suite uses a small self-registering framework (`tests/test_framework.h`):
each `TEST(name)` registers itself, and `CHECK` / `CHECK_EQ` / `CHECK_STR_EQ`
throw on failure. `tests/main.cpp` runs everything and returns a non-zero exit
code if any test failed. There is currently no test-name filter.

### `test_crypto.cpp` (7)

Every primitive is checked against its published test vector, not against
itself:

* `sha256_vectors`, `sha256_incremental_matches_oneshot` — FIPS 180-4 vectors,
  plus incremental updates matching one-shot hashing.
* `hmac_sha256_rfc4231` — RFC 4231 cases.
* `hkdf_rfc5869_case1` — RFC 5869 case 1.
* `pbkdf2_hmac_sha256_vectors` — PBKDF2-HMAC-SHA256 vectors.
* `chacha20_rfc8439_section2_4_2` — RFC 8439 §2.4.2.
* `random_and_hex` — CSPRNG output and the hex codec.

### `test_control_frame.cpp` (7)

The control channel's framing and AEAD sealing:

* `control_frame_plaintext_round_trip`, `control_frame_encrypted_round_trip`.
* `control_frame_incremental_feed` — reassembly from a partial TCP stream.
* `control_frame_tamper_rejected`, `control_frame_wrong_key_rejected`,
  `control_frame_bad_magic_rejected`,
  `control_frame_encrypted_before_keys_rejected` — every misuse is rejected.

### `test_media.cpp` (7)

The UDP media path, including loss and reordering:

* `media_packet_round_trip`, `media_packet_bit_flip_rejected`,
  `media_packet_wrong_key_rejected`.
* `fragment_and_reassemble_large_frame` — a frame larger than one datagram.
* `reassembler_out_of_order_and_loss`, `reassembler_interleaved_streams`,
  `reassembler_stale_frame_dropped`.

### `test_pairing.cpp` (6)

Key schedule and pairing:

* `pairing_url_round_trip`, `pairing_code_validation`,
  `pairing_secret_hex`, `pairing_code_derivation`.
* `pairing_session_keys_agree` — both sides derive the same keys.
* `pairing_proofs_use_distinct_nonce_order` — pins the role-dependent nonce
  order described in `docs/ARCHITECTURE.md`. This test exists because a bug
  once used the phone nonce order for both roles, which would have made every
  phone fail to authenticate.

### `test_messages.cpp` (6) and `test_json.cpp` (6)

Message-level encode/decode (`hello`, `config`, `stats`/`hint`/`error`,
`ready`/`auth_ok`/`bye`), partial-config tolerance, and rejection of malformed
payloads; plus the underlying JSON parser (escapes, Unicode escapes, numbers,
error cases).

### `test_discovery.cpp` (3)

`discovery_obs_round_trip`, `discovery_phone_round_trip`,
`discovery_rejects_garbage` — the unencrypted LAN announcements.

### `test_config.cpp` (3)

`config_video_presets`, `config_auto_bitrate`, `config_center_crop` — the
resolution/fps/bitrate presets and the crop-aspect maths used by the source.

### `test_h264.cpp` (6)

Bitstream parsing against real encoded fixtures in `tests/data/`:

* `sps_320x240`, `sps_176x144`, `sps_1920x1080`, `sps_crop` — SPS parsing,
  including a cropped frame.
* `sps_rejects_garbage` — malformed input does not read out of bounds.
* `split_annex_b` — Annex-B NAL splitting.

### `test_decoder_win.cpp` (1)

`decoder_mf_h264` — decodes a real H.264 fixture through the Media Foundation
decoder and checks the NV12 output dimensions. Windows-only; built only when
`ocp_decoder` exists.

## `ocp_module_host` — integration

`tests/module_host/main.cpp` is a minimal OBS host. It starts libobs, opens and
initializes the built `obs-phone-camera.dll`, and then verifies:

* `obs_startup`, `obs_open_module`, `obs_init_module` all succeed.
* The `obs_phone_camera` source type is registered.
* A source can be created with settings, and reports width/height `0` while the
  status overlay is off and nothing is connected.
* Settings round-trip through `obs_source_update` / `obs_source_get_settings`.
* The source and module shut down cleanly (`obs_source_release`,
  `obs_shutdown`).

CTest sets `PATH` to the OBS `bin/64bit` directory so the test can find
`obs.dll`. This test is the one that proves the module actually loads into OBS
rather than merely compiling.

## Phone app tests

Both phone apps have protocol unit tests that mirror the C++ vectors:

* **Android** — `mobile/android/app/src/test/`, JUnit 4 + `kotlin.test`. Run
  with `./gradlew testDebugUnitTest` from `mobile/android` (requires a JDK 17).
* **iOS** — `mobile/ios/Tests/`, XCTest. Run from Xcode, or
  `xcodebuild test` after `xcodegen generate`.

**Neither suite has been run in this repository's CI.** The Android tests need a
JDK that is not installed here, and the iOS tests need a macOS toolchain. They
are written to the same vectors as the C++ suite, but treat them as unverified
until you run them.

## What is not covered

* **A real phone connecting to a real OBS instance.** No end-to-end test drives
  an actual handshake and video stream between the built plugin and a built
  phone app. This is the largest gap.
* **Network failure behaviour** — cable pulls, address changes, and Wi-Fi drops
  during a live stream are not simulated.
* **macOS plugin builds** (`decoder_mac.mm` and siblings are not built or
  tested by CI here).
* **The pairing-QR UI and status-card rendering** are exercised only indirectly
  (the module-host test checks that a source is created; it does not assert on
  rendered pixels).
* **Performance and latency** — no benchmark for encode/decode latency or for
  behaviour under packet loss at scale.

## Adding a test

1. Create `tests/test_<area>.cpp` (or add to an existing file).
2. `#include "test_framework.h"` and write a `TEST(name) { ... }` block using
   `CHECK` / `CHECK_EQ`.
3. Add the file to the `ocp_tests` sources in `tests/CMakeLists.txt`.
4. Rebuild and re-run `ctest`.

If the test needs H.264 fixtures, put them in `tests/data/` and read them via
the `OCP_TEST_DATA_DIR` compile definition (see `test_decoder_win.cpp`).
