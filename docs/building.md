# Building WealthTracker

The checked-in presets target macOS arm64 and Windows x86_64. Run commands from
the repository root. Linux requires a manually supplied Qt/compiler toolchain
and has no supported credential backend for stored-token connections.

These instructions follow [CMake presets](../CMakePresets.json), the
[Conan recipe](../conanfile.py), and the scripts under `scripts/`. Windows
commands require a matching MSVC 194 toolchain in a developer PowerShell.

## Toolchain

On macOS, the bootstrap script installs pinned CMake, Ninja, Conan, and the
official Qt SDK under the ignored `build/` directory:

```sh
./scripts/bootstrap.sh
. build/tooling/bin/activate
```

Bootstrap reuses an existing complete Qt 6.11.1 SDK. A fresh installation
downloads archives over HTTPS and checks them against the SHA-256 digests in
`scripts/qt-macos-sha256.json` before extraction. These digests were cross-checked
against matching archives from FAU and NetCologne. No connection to
`download.qt.io` is needed. Downloads are cached under `build/qt-downloads`;
cached files are hash-checked before reuse.

Bootstrap must exit successfully before continuing. Installing Conan and CMake
alone does not install Qt. If configuration reports a missing Qt SDK, rerun
`./scripts/bootstrap.sh` and resolve its download error first; the expected
configuration file is `build/qt/6.11.1/macos/lib/cmake/Qt6/Qt6Config.cmake`.

Keep that environment active during Conan installation. Dependency recipes
invoke CMake and Ninja by name. C++23 module builds also require the selected
compiler's dependency scanner; `scripts/configure.sh` validates it.

On macOS, the script selects Python 3.9 or newer backed by OpenSSL 1.1.1 or
newer. Apple's Command Line Tools Python uses an unsupported LibreSSL runtime.
If no supported Python is found, install a current Python and select it
explicitly:

```sh
brew install python@3.13
WT_BOOTSTRAP_PYTHON=/opt/homebrew/opt/python@3.13/bin/python3.13 \
  ./scripts/bootstrap.sh
```

Qt downloads use a TLS-valid HTTPS mirror because `download.qt.io` is not
reachable from every network. Override it when necessary without disabling TLS:

```sh
WT_QT_MIRROR=https://mirror.netcologne.de/qtproject/ ./scripts/bootstrap.sh
```

On Windows, bootstrap installs the pinned tools and Qt MSVC SDK. It does not
install Visual Studio or the Windows SDK; open a developer PowerShell with the
x64 compiler available before building.

## Debug build on macOS

```sh
conan install . \
  --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug \
  -of build/macos-debug
./scripts/configure.sh macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug --output-on-failure
cmake --build --preset macos-debug --target wealthtracker_qmllint
node --check scripts/zen_plugin_host.mjs
```

Run the application with:

```sh
./build/macos-debug/WealthTracker.app/Contents/MacOS/WealthTracker
```

Node.js is optional for running the app, but the `node --check` verification
command requires it. The native macOS WebView lifecycle test requires access
to a desktop display; a headless or restricted session may abort with
`Cannot create window: no screens available`.

Rerun `conan install` after dependency or shared-library option changes before
reconfiguring. An old static-OpenSSL dependency graph cannot satisfy the current
TLS runtime installation rules.

## Debug build on Windows

```powershell
./scripts/bootstrap.ps1
& ./build/tooling/Scripts/Activate.ps1
conan install . `
  --profile:host=conan/profiles/windows-x86_64 `
  --profile:build=conan/profiles/windows-x86_64 `
  --build=missing -s:h build_type=Debug -s:h compiler.runtime_type=Debug `
  -c tools.env.virtualenv:powershell=powershell.exe `
  -of build/windows-debug
. ./build/windows-debug/generators/conanrun.ps1
$env:PATH = "$(Resolve-Path ./build/qt/6.11.1/msvc2022_64/bin);$env:PATH"
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
cmake --build --preset windows-debug --target wealthtracker_qmllint
node --check scripts/zen_plugin_host.mjs
& ./build/windows-debug/WealthTracker.exe
```

The Debug host settings override the profiles' Release defaults. Keep the build
profile in Release for dependency build tools. Activate Conan's runtime
environment and add Qt's `bin` directory before building: Catch2 test discovery
executes binaries during the build and needs the dependency DLLs on `PATH`.
Windows CI initializes MSVC and carries the Conan runtime and Qt paths into
each subsequent build, test, and staging step. Bootstrap stops on a failed
Python, build-tool, or Qt installation instead of reporting success.

## Sanitizers

For an already configured local Clang build, run
`cmake --preset macos-debug -DENABLE_SANITIZERS=ON`, then rebuild and run the full
test suite. Pass the option to CMake directly; `scripts/configure.sh` only reads
the preset argument. Sanitizers are off by default.

## TLS runtime

OpenSSL 3.6.3 is a direct, lockfile-pinned shared dependency. The executable
initializes and selects Qt's OpenSSL backend, so installation does not depend
on Homebrew or Qt's native Secure Transport backend. The package must include
both OpenSSL libraries and Qt's OpenSSL TLS plugin. Certificate and hostname
verification remain enabled. See [Qt's TLS packaging guidance](https://doc.qt.io/qt-6/ssl.html).

## Release packages

For macOS, with project tooling activated:

```sh
conan install . \
  --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Release \
  -of build/macos-release
./scripts/configure.sh macos-release
cmake --build --preset macos-release
ctest --preset macos-release --output-on-failure
cmake --build --preset macos-release --target wealthtracker_qmllint
cmake --build --preset macos-release --target package
```

CPack writes the DMG under `build/macos-release`. The install step runs Qt's
QML deployment script and stages OpenSSL, the project license, third-party
notices, the Conan lockfile, and Qt SPDX files when present in the SDK. To inspect
an installed tree separately, use:

```sh
cmake --install build/macos-release --prefix build/stage/macos-release
```

On Windows, install dependencies with `build_type=Release` and
`compiler.runtime_type=Release` into `build/windows-release`, retaining the
PowerShell environment option above. Activate that configuration's generated
`conanrun.ps1`, then configure, build, test, and lint with `windows-release`.
Keep Qt's `bin` on `PATH`. The `package` target uses CPack's
WIX generator and requires WiX on the build machine; bootstrap does not install
it. The CI workflow builds, tests, lints, and stages the application, but does
not create, sign, or publish a release package.

No signing, notarization, or release-publishing automation is configured.
