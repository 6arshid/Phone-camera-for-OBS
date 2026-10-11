# Packaging

How to produce a distributable build of the OBS plugin and the phone apps.

## OBS plugin (Windows)

### Build a release

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The artifacts are:

| File | Purpose |
| --- | --- |
| `build/src/plugin/Release/obs-phone-camera.dll` | The plugin module |
| `build/tests/Release/ocp_tests.exe` | Unit tests (not shipped) |
| `build/tests/Release/module_host.exe` | Integration test (not shipped) |

The DLL links against `obs.dll` (via the import library in `deps/obs-import/`)
and the Windows Media Foundation libraries. It does **not** require you to
redistribute `obs.dll` — the target OBS installation already has it.

### Stage a package tree

`cmake --install` uses the rules in `src/plugin/CMakeLists.txt`, which place
files in an OBS-shaped layout:

```powershell
cmake --install build --config Release --prefix "package"
```

produces:

```
package/
├── obs-plugins/64bit/obs-phone-camera.dll
└── data/
    └── locale/
        ├── en-US.ini
        └── fa-IR.ini
```

Zip the contents of `package/` (not the folder itself) so that the archive
extracts directly into an OBS installation root.

### Install manually instead

Copy `obs-phone-camera.dll` to `<obs-studio>/obs-plugins/64bit/` and the two
locale files to `<obs-studio>/data/locale/`, then restart OBS. This is the
simplest option for a single machine and needs no packaging step.

### Verify before shipping

Always run the test suite against the exact DLL you intend to distribute:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

`ocp_module_host` loads that DLL into a real libobs instance and fails if the
module does not initialize or the source does not register. If it passes, the
package is loadable. See [TESTING.md](TESTING.md).

### What cannot be packaged

* **macOS.** The CMake `APPLE` branch references `decoder_mac.mm`,
  `status_card_mac.mm` and `qr_window_mac.mm`, but those files do not exist in
  this repository. macOS packaging is not possible.
* **Linux.** There is no Linux decoder, QR window or CI. Not possible.

See [SUPPORTED_PLATFORMS.md](SUPPORTED_PLATFORMS.md).

## Phone apps

### Android

```powershell
cd mobile/android
./gradlew assembleDebug       # unminified debug APK
./gradlew assembleRelease     # release APK — needs a signing config
```

The build currently sets `isMinifyEnabled = false` for release and defines **no
signing config**, so `assembleRelease` produces an unsigned APK. To ship, add a
`signingConfigs` block referencing your keystore, then use
`bundleRelease` for Play Store distribution or sign the APK manually.

**This app has not been built or run in this repository's environment** (no JDK
installed). Treat the APK as unverified until you build and test it. See
[SUPPORTED_PLATFORMS.md](SUPPORTED_PLATFORMS.md).

### iOS

The Xcode project is generated from `project.yml` with
[XcodeGen](https://github.com/yonaskolb/XcodeGen):

```bash
cd mobile/ios
xcodegen generate
open PhoneCamera.xcodeproj
```

Then archive from Xcode (*Product → Archive*) and export via the organizer,
signing with your own team and provisioning profile. No signing configuration is
committed to the repository.

**This app has not been built or run in this repository's environment** (no
macOS toolchain here). See [SUPPORTED_PLATFORMS.md](SUPPORTED_PLATFORMS.md).

## Versioning

The plugin version is declared in the top-level `CMakeLists.txt`
(`project(obs-phone-camera VERSION 1.0.0 ...)`) and mirrored in
`mobile/android/app/build.gradle.kts` (`versionName` / `versionCode`). Bump both
together, and keep the phone apps and plugin built from the **same commit**, so
the `OCP/1` handshake cannot drift between endpoints.

## Licensing

Everything here is GPL-2.0. Ship the `LICENSE` file alongside any binary
distribution. The vendored QR generator in `vendor/` is included under its own
upstream license terms — keep the accompanying notices intact when
redistributing.
