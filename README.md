# Phone Camera for OBS

Use your phone as a wireless camera source in OBS Studio. The phone captures
video (and optionally audio), encodes it, and streams it over the local network
to an OBS source — no cables, no cloud, no accounts.

The plugin, both phone apps, and the wire protocol they speak (`OCP/1`) are all
implemented in this repository.

| Component | Location | Status |
| --- | --- | --- |
| OBS plugin (source + server) | `src/plugin`, `src/core` | Builds and loads; unit + module-host tests pass |
| Protocol / crypto core | `src/core` | Complete; 52 unit tests pass |
| Android app | `mobile/android` | Complete; unit tests written (not yet run in CI) |
| iOS app | `mobile/ios` | Complete; unit tests written (not yet run in CI) |
| Wire protocol spec | `docs/PROTOCOL.md` | Complete |

* **Windows / OBS plugin:** verified by building, running the test suite, and
  loading the module into a live libobs instance.
* **Android / iOS:** the code is complete and reviewed against the protocol
  spec, but has **not** been verified on device or compiled in CI from this
  machine. Treat them as unverified until you build and run them yourself.

## How it works

```
 Phone                                Local network                 OBS Studio
┌────────────────────┐   TCP 47100    ┌──────────────────────────┐
│ Camera → H.264     │───────────────▶│  Control channel         │
│ Mic → PCM (opt.)   │   UDP 47101    │  Media channel           │
│ QR / pairing code  │───────────────▶│  obs_phone_camera source │
└────────────────────┘   UDP 47109    └──────────────────────────┘
                          discovery
```

1. OBS shows a pairing QR code (or a 6-digit code) inside the source.
2. The phone scans the QR, derives session keys, and authenticates over TCP.
3. The phone streams H.264 video (and optionally PCM audio) over UDP.
4. The plugin decodes and renders it as a normal OBS source.

Everything is encrypted per-session with keys derived from the pairing secret.
See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the full specification.

## Requirements

* **OBS Studio** 32.0.4 (headers and `obs.dll` are used for building).
* **Windows** with Visual Studio 2022 (C++ desktop workload) for building the
  plugin.
* A phone on the **same local network** as the computer running OBS.
* For the phone apps: Android 8.0+ (API 26) or iOS 15+.

macOS and Linux builds of the plugin are not implemented yet.

## Building the OBS plugin (Windows)

```powershell
# 1. Fetch the OBS headers (shallow clone of the sources)
./scripts/fetch-obs-headers.ps1            # defaults to version 32.0.4

# 2. Generate the import library from your installed obs.dll
./scripts/make-obs-import.ps1              # defaults to C:\Program Files\obs-studio

# 3. Configure and build
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

This produces:

* `build/src/plugin/Release/obs-phone-camera.dll` — the plugin module
* `build/tests/Release/ocp_tests.exe` — unit tests
* `build/tests/Release/module_host.exe` — module load test

If CMake cannot find OBS, point it explicitly:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" `
  -DOBS_ROOT="C:/Program Files/obs-studio"
```

## Installing

The build system already defines the install layout, so installing is:

```powershell
cmake --install build --config Release `
  --prefix "C:/Program Files/obs-studio"
```

That copies the module to `<prefix>/obs-plugins/64bit/` and the locale files to
`<prefix>/data/` (see the `install()` rules in `src/plugin/CMakeLists.txt`).
Alternatively, copy the two pieces by hand:

```
<obs-studio>/obs-plugins/64bit/obs-phone-camera.dll
<obs-studio>/data/locale/en-US.ini
<obs-studio>/data/locale/fa-IR.ini
```

Then restart OBS. The source appears as **Phone Camera** under
*Sources → + → Phone Camera*. For a packaged layout, see
[docs/PACKAGING.md](docs/PACKAGING.md).

## Running the tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

Two tests run:

* `ocp_unit_tests` — 52 tests covering crypto, protocol framing, pairing,
  discovery, config, H.264 parsing, and the Windows Media Foundation decoder.
* `ocp_module_host` — loads the built plugin into a live libobs instance and
  exercises source creation, settings round-trip, and teardown.

Set `OCP_DEC_DEBUG=1` to get verbose decoder logging while debugging.

## Building the phone apps

**Android** — open `mobile/android` in Android Studio, or:

```powershell
cd mobile/android
./gradlew assembleDebug          # requires a JDK 17 on PATH
./gradlew testDebugUnitTest      # runs the unit tests (requires a JDK)
```

**iOS** — the project is described by `mobile/ios/project.yml`
([XcodeGen](https://github.com/yonaskolb/XcodeGen)):

```bash
cd mobile/ios
xcodegen generate
open PhoneCamera.xcodeproj
```

Neither app has been built or run in CI from this repository, so expect to
do a first build yourself.

## Security notes

* The pairing secret never crosses the network in plaintext — only HMAC proofs.
* Each session derives fresh keys; compromise of one session does not affect
  others.
* Discovery packets are unencrypted but carry no secrets and are never trusted
  for anything but locating a server.
* Require pairing (on by default) so that unknown phones cannot connect.

Full details in [docs/SECURITY.md](docs/SECURITY.md) and
[docs/PROTOCOL.md](docs/PROTOCOL.md).

## Documentation

* [docs/PROTOCOL.md](docs/PROTOCOL.md) — the `OCP/1` wire protocol
* [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — how the code is organized
* [docs/INSTALL.md](docs/INSTALL.md) — installation in detail
* [docs/TESTING.md](docs/TESTING.md) — what each test covers
* [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) — common problems
* [docs/SUPPORTED_PLATFORMS.md](docs/SUPPORTED_PLATFORMS.md) — what is verified
* [docs/PACKAGING.md](docs/PACKAGING.md) — building distributable packages
* [docs/SECURITY.md](docs/SECURITY.md) — threat model and guarantees

The protocol spec is also available in Persian: [docs/PROTOCOL.fa.md](docs/PROTOCOL.fa.md).
## Developer

- **Developer:** 6arshid
- **Website:** [www.6arshid.com](https://www.6arshid.com)
- **Email:** [info@6arshid.com](mailto:info@6arshid.com)
## License

GPL-2.0. See [LICENSE](LICENSE).
