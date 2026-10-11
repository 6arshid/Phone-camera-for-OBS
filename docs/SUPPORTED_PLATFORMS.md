# Supported platforms

What is verified, what is written but unverified, and what does not exist yet.
Claims here are limited to what has actually been built and run in this
repository.

## Summary

| Platform | Plugin builds | Loads in OBS | Tested | Status |
| --- | --- | --- | --- | --- |
| Windows x64 + OBS 32.0.4 | Yes | Yes | Yes | **Verified** |
| macOS | No — missing sources | — | — | **Not implemented** |
| Linux | No — no decoder or UI | — | — | **Not implemented** |
| Android app | N/A (no JDK here) | — | Written, unrun | **Unverified** |
| iOS app | N/A (no macOS toolchain) | — | Written, unrun | **Unverified** |

## Windows x64 — verified

This is the only platform with end-to-end evidence:

* The plugin compiles and links against OBS Studio 32.0.4 (`obs.dll` plus the
  libobs headers).
* It **loads into a live OBS instance** — proven by the `ocp_module_host`
  integration test, which calls `obs_startup`, opens and initializes the module,
  creates a source, and tears it down.
* All 52 unit tests pass, including a Media Foundation H.264 decode of a real
  bitstream fixture (`decoder_mf_h264`).

Windows-specific implementation lives in `decoder_win.cpp` (Media Foundation),
`qr_window.cpp` (Win32/GDI pairing window) and the Winsock code in `server.cpp`
and `net_util.cpp`.

**Not verified on Windows:** an actual phone connecting to a real OBS instance
and streaming video. See [TESTING.md](TESTING.md) § What is not covered.

## macOS — not implemented

`src/plugin/CMakeLists.txt` has an `APPLE` branch that lists
`decoder_mac.mm`, `status_card_mac.mm` and `qr_window_mac.mm`. **Those files do
not exist in this repository.** A macOS build would therefore fail at configure
or compile time.

The intended design is documented — VideoToolbox for H.264 decode, AppKit for
the pairing-QR window — but none of it is written, so no macOS claim of support
should be made.

## Linux — not implemented

There is no Linux decoder, no QR-window implementation, and no Linux CI. The
protocol core (`src/core`) is portable C++17 with no OS-specific code beyond a
CSPRNG wrapper, so it would build, but the plugin layer would not.

## Android app — unverified

`mobile/android` is a complete Kotlin application (CameraX capture, `MediaCodec`
H.264 encoding, protocol/crypto reimplementation) with unit tests under
`mobile/android/app/src/test/`, written to the same vectors as the C++ suite.

It has **not been built or run** in this repository's environment, because no
JDK is installed here. Run `./gradlew testDebugUnitTest` from `mobile/android`
with a JDK 17 to execute the tests, and build/install the APK on a device to
verify behaviour.

## iOS app — unverified

`mobile/ios` is a complete Swift application (AVFoundation capture,
VideoToolbox H.264 encoding) described by `project.yml` for XcodeGen, with
XCTest suites under `mobile/ios/Tests/`.

It has **not been built or run** here — that requires a macOS toolchain. Generate
the project with `xcodegen generate` and run the tests from Xcode.

## Protocol compatibility

All four implementations (C++ server, Android client, iOS client) target the
same specification: `OCP/1`, documented in [PROTOCOL.md](PROTOCOL.md). The
shared test vectors are what keep them aligned; a divergence in nonce order or
framing would fail the corresponding unit test rather than surfacing as a
mysterious runtime failure.

## Recommended configuration

For a first run, use the verified combination:

* Windows 10/11 x64
* OBS Studio 32.0.4 (matching this repo's build)
* A phone on the same private network, with the phone app built from the same
  commit as the plugin

See [INSTALL.md](INSTALL.md) for setup and [SECURITY.md](SECURITY.md) before
exposing the source beyond a trusted LAN.
