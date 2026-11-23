# Verification

Run `sh scripts/verify.sh` on macOS after bootstrap/configuration. It builds the
native app and tests, runs QML/JavaScript checks, Python tooling tests and CTest.
Native WebView tests need macOS display access. Use isolated fixtures; never
supply real credentials to automated tests.

| Area | Check | What it establishes |
| --- | --- | --- |
| Native | `sh scripts/verify.sh` | C++ domain/storage/coordinator behavior, native WebView fixtures, QML interaction and tooling contracts |
| Windows (local) | Equivalent `windows-debug` build/CTest and QML lint | Desktop compile and platform-specific credential support |
| Android package | Set JDK/SDK variables, run `./android/build.sh` | ARM64 compilation, packaging and signature verification |
| Android runtime | `sh android/verify.sh emulator-5554` with JDK/SDK variables set and an owned emulator booted | Android trust/credential/browser instrumentation plus guarded native C++ callback checks |
| UI | Compare phone/desktop screenshots after layout changes | Visible geometry and native window rendering |
| Live banks | User independently signs in and checks refreshed balances/categories | Current authenticated compatibility; synthetic tests alone do not establish this |

See [Android verification instructions](../android/README.md#reproduce-android-verification) for prerequisites, exact device selection and log locations.

`version.json` is the release metadata source: `product` supplies desktop/Conan
versions, `androidName` supplies Android display version and artifact name, and
`androidCode` is the monotonic Android upgrade code. Package IDs, signing keys,
credential keys and local data paths remain stable across refactoring.

## Hosted checks

[GitHub Actions](../.github/workflows/ci.yml) builds, tests, and packages the
macOS application. It also builds an ARM64 Android APK and verifies its
signature. Push builds keep these downloads under **Actions → CI run →
Artifacts** for seven days.

- **WealthTracker-macOS-preview** contains the macOS DMG.
- **WealthTracker-Android-arm64-preview** contains the debug APK and its
  SHA-256 checksum. GitHub's runners use a fresh debug key for each build.
  For updates that preserve an existing installation, use the original
  signing key with the [local Android build](../android/README.md#build-on-macos).

GitHub Actions and [GitLab CI](../.gitlab-ci.yml) both run portable checks, a
redacted Gitleaks scan of fetched Git history, and an OSV scan of `conan.lock`.
The scans fail on findings or scanner errors and use pinned Linux container
images. Android runtime verification requires the local checks above.

Confirm the first hosted run and enable
[private vulnerability reporting](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/configure-vulnerability-reporting/configure-for-a-repository)
in the GitHub repository settings when publishing the repository.

OSV coverage for Conan packages is incomplete. An empty scan is not proof that
a dependency has no known vulnerabilities: review upstream Qt, OpenSSL and
libsodium advisories as well. Qt is installed separately and is not in the
Conan lockfile.

Publishing repository source and publishing installable binaries are separate
steps. Before public binary distribution, verify non-debug release signing,
applicable platform signing/notarization, and that the staged package contains
the actual dependency license/copyright texts and required Qt materials.
The current Android command deliberately builds a debug-signed APK for personal
installation; it is not a production-release workflow.

The currently pinned Qt 6.11.1 SDK requires upstream security updates before a
public binary release. In particular, review the
[Qt Network NTLM advisory](https://www.qt.io/blog/security-advisory-cve-2026-105946)
and the [HTTP response handling advisory](https://lists.qt-project.org/pipermail/announce/2026-November/000684.html).
Updating OpenSSL alone does not patch Qt Network. Updating the Qt SDK also
requires refreshing the verified archive manifests and validating native and
Android builds; do not bypass archive verification or claim the existing APKs
contain the updated dependencies.
