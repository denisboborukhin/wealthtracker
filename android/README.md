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
android/output/wealthtracker-0.1.10-debug.apk
android/output/wealthtracker-0.1.10-debug.apk.sha256
```

The package ID is `org.wealthtracker.android`. Keep
`android/.local/user/debug.keystore` backed up and private: future updates must
use the same signing key. This is a test signing identity, not a Play Store
release key. APKs and keys are excluded from Git.

## Install

Copy the APK to your phone, open it in Files, and permit installation from that
source. Or authorize USB debugging on your phone and run:

```sh
"$ANDROID_SDK_ROOT/platform-tools/adb" install -r android/output/wealthtracker-0.1.10-debug.apk
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
status message. The APK includes the bank-recommended Russian Trusted Root CA
only for the exact bank login and verified asset hosts in `res/xml/network_security_config.xml`.
Other hosts use system CAs only; arbitrary user-installed CAs are not trusted.
Ozon's connector supports main balances, savings, MKK installment debt and
selected monthly cashback. These reads were confirmed on macOS. In the Android
emulator, choosing **Продолжить в браузере** reached Ozon ID, whose own anti-bot
page reported a connection error; authenticated Android retrieval is still
unverified. No Ozon TLS exception or anti-bot bypass is included.
This uses Android's declarative trust configuration, preserving TLS chain,
validity, and hostname checks without changing the phone's certificate store.
Qt/OpenSSL requests to the exact `www.tbank.ru` API host receive the same
additional root per request; the global Qt trust configuration is unchanged.
Credentials are encrypted with
AES-GCM and a non-exportable Android Keystore key; financial history continues
to use SQLCipher. Cloud backup and device-transfer extraction are excluded.
Manual cashback entries use app-private Qt settings, as on desktop; these entries
are not encrypted by SQLCipher.

Local Node-based ZenPlugins are unavailable on Android. Native bank readers are
included, but live bank login and cashback compatibility require validation on
your device. Android preserves browser sessions between launches and clears only the selected
bank on disconnect. Android may suspend background synchronization while the app
is not active.

## Bank certificate provenance and checks

The root in `res/raw/russian_trusted_root_ca.pem` was downloaded over verified
HTTPS from the Android certificate link on
<https://www.tbank.ru/bank/help/certificates/>:
<https://help-static2.tbank-online.com/certs/Android/Root/Russian_Trusted_Root_CA.cer>.
Its DER SHA-256 fingerprint is
`d26d2d0231b7c39f92cc738512ba54103519e4405d68b5bd703e9788ca8ecf31`.
The bank's separate Linux root download produced the same DER fingerprint.
The root is valid from March 2022 through February 2032.

`BankCertificateTrustTest` checks Android's actual trust manager against public
Alfa, VTB, and T-Bank certificate chains captured on October 30, 2026, and
their verified `s1.vtb.ru`, `s2.vtb.ru`, and `cdn.tbank.ru` asset chains captured
on October 31. VTB's public page selects `s2.vtb.ru` as its CDN; these hosts
receive the additional CA only for resource loading, with no change to bank
navigation allowlists. The test also
checks the verified VTB CMS (`h2.vtb.ru`), authentication service
(`sso-app4.vtb.ru`), and certificate-check endpoint (`cert.vtb.ru`). The bank's
public core uses a credential-free fetch to the certificate-check endpoint;
trusting the main page alone does not satisfy that check. All three additional
public chains were captured on October 31 with hostname validation enabled.
The phone-identification flow additionally uses `sso-ib.vtb.ru`, whose public
Russian Trusted Sub CA chain was captured and hostname-verified on November 1.
The bank's current public page enables this flow and selects that exact host
for its post-phone identification request. A regression against APK 0.1.7
reproduced Android's missing trust-anchor failure; the updated exact-host
configuration accepts the chain and still rejects sibling and suffix hosts.
The test
checks rejection outside the exact host scope and rejection of a damaged bank
certificate signature. Fixtures contain public certificates only and expire
with the banks' certificates; replace them from verified live TLS handshakes
when they expire. Test assets are excluded from the installable app APK.

## Native JavaScript callback check

`tests/view_probe` embeds a real C++ `QWebView` using synthetic HTML.
`--callback-probe` records the platform callback thread; on Qt 6.11 Android it
differs from Qt's application thread. `--guarded-callback-probe` verifies the
shared bank callback dispatcher: three completions including nested evaluation,
correct thread, continued timer progress, and discarded results after receiver
destruction. Only fixed synthetic results and thread flags are logged.
Account, login, and cashback readers dispatch results through this helper before
using Qt timers, networking, or application models.
