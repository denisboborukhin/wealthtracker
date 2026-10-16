# Building WealthTracker

The maintained build presets target macOS and Windows. Linux can compile the
libraries but currently has no secure credential backend for live connections.

## Toolchain

On macOS, the bootstrap script installs pinned CMake, Ninja, Conan, and the
official Qt SDK under the ignored `build/` directory:

```sh
./scripts/bootstrap.sh
. build/tooling/bin/activate
```

Bootstrap reuses an existing complete Qt 6.11.1 SDK. A fresh installation
downloads archives from the configured mirror but still obtains their SHA-256
sidecars from `download.qt.io`. If that host is blocked, use a network or VPN
that can reach it, or set `HTTPS_PROXY`; do not disable checksum validation.

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

On Windows, run `scripts/bootstrap.ps1`, then activate
`build/tooling/Scripts/Activate.ps1` in PowerShell.

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

Use the equivalent `windows-debug` preset and
`conan/profiles/windows-x86_64` profile on Windows.

## Sanitizers

For a local Clang build, add `-DENABLE_SANITIZERS=ON` during configuration,
then rebuild and run the full test suite. Sanitizers are not enabled for the
packaged application.

## TLS runtime

OpenSSL 3.6.3 is a direct, lockfile-pinned shared dependency. The executable
initializes and selects Qt's OpenSSL backend, so installation does not depend
on Homebrew or Qt's native Secure Transport backend. The package must include
both OpenSSL libraries and Qt's OpenSSL TLS plugin. Certificate and hostname
verification remain enabled. See [Qt's TLS packaging guidance](https://doc.qt.io/qt-6/ssl.html).

## Release packages

Use `macos-release` or `windows-release`, run the same tests and QML lint, then
build the `package` target. Generated packages are unsigned development
artifacts. Public distribution additionally requires platform signing,
notarization where applicable, dependency/SBOM review, and inclusion of all
license notices. Do not describe an unsigned package as a production release.

Release dependency resolution must use the committed `conan.lock`. Qt remains
dynamically linked so recipients retain the replacement rights required by its
LGPL terms.
