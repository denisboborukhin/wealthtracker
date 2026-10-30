# Android APK

WealthTracker's native Qt app can be built as an ARM64, debug-signed APK for
personal installation on Android 9 or newer. The APK includes its Qt runtime and
OpenSSL libraries. Google Play and a server are not required.

## Build on macOS

Run the desktop bootstrap first to install the pinned host Qt and build tools.
Install JDK 21, Android SDK platform 36, build-tools 36.0.0, platform-tools, and
NDK 29.0.14206865. Accept the Android SDK licenses using `sdkmanager --licenses`.
NDK 29 supplies the Clang 21 compiler, matching module scanner, and C++23 library.

Set `JAVA_HOME` and `ANDROID_SDK_ROOT` to those installations, then run:

```sh
. build/tooling/bin/activate
python android/tools/install_qt.py https://ftp.fau.de/qtproject/ build/qt
./android/build.sh
```

The Qt installer verifies pinned SHA-256 hashes. The build uses a separate Conan
cache under `build/conan-android`, preserving desktop packages. OpenSSL staging
changes only ELF dynamic library names to Android's `_3.so` convention; it does
not change crypto code or TLS validation. Gradle verifies its distribution hash.

Output:

```text
android/output/wealthtracker-0.1.1-debug.apk
android/output/wealthtracker-0.1.1-debug.apk.sha256
```

The package ID is `org.wealthtracker.android`. Keep
`android/.local/user/debug.keystore` backed up and private: future updates must
use the same signing key. This is a test signing identity, not a Play Store
release key. APKs and keys are excluded from Git.

## Install

Copy the APK to your phone, open it in Files, and permit installation from that
source. Or authorize USB debugging on your phone and run:

```sh
"$ANDROID_SDK_ROOT/platform-tools/adb" install -r android/output/wealthtracker-0.1.1-debug.apk
```

Install future versions over this app rather than uninstalling; uninstalling
removes local app data. Android and desktop data are separate. No desktop
credentials, bank sessions, or financial database are included in the APK.

## Android behavior

The phone layout uses a top navigation selector, single-column forms and cards,
and scrollable pages. Bank login opens a separate full-screen native view. DOM
storage is enabled for bank frontends and removed for the selected bank on
explicit disconnect using WebView’s site-scoped deletion API. Bank connections
require an up-to-date Android System WebView with browsing-data deletion support;
older providers receive an update message before login. Certificate failures remain blocked and receive a visible
status message. Credentials are encrypted with
AES-GCM and a non-exportable Android Keystore key; financial history continues
to use SQLCipher. Cloud backup and device-transfer extraction are excluded.
Manual cashback entries use app-private Qt settings, as on desktop; these entries
are not encrypted by SQLCipher.

Local Node-based ZenPlugins are unavailable on Android. Native bank readers are
included, but live bank login and cashback compatibility require validation on
your device. Android preserves browser sessions between launches and clears only the selected
bank on disconnect. Android may suspend background synchronization while the app
is not active.
