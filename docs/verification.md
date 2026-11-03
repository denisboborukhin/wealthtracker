# Verification

Run `sh scripts/verify.sh` on macOS after bootstrap/configuration. It builds the
native app and tests, runs QML/JavaScript checks, Python tooling tests and CTest.
Native WebView tests need macOS display access. Use isolated fixtures; never
supply real credentials to automated tests.

| Area | Check | What it establishes |
| --- | --- | --- |
| Native | `sh scripts/verify.sh` | C++ domain/storage/coordinator behavior, native WebView fixtures, QML interaction and tooling contracts |
| Windows | Equivalent `windows-debug` build/CTest and QML lint | Desktop compile and platform-specific credential support |
| Android package | Set JDK/SDK variables, run `./android/build.sh` | ARM64 compilation, packaging and signature verification |
| Android runtime | Install APK and test APK on owned emulator; run `am instrument -w org.wealthtracker.android.test/androidx.test.runner.AndroidJUnitRunner` | Android trust, credentials and browser integration fixtures |
| UI | Compare phone/desktop screenshots after layout changes | Visible geometry and native window rendering |
| Live banks | User independently signs in and checks refreshed balances/categories | Current authenticated compatibility; synthetic tests alone do not establish this |

`version.json` is the release metadata source: `product` supplies desktop/Conan
versions, `androidName` supplies Android display version and artifact name, and
`androidCode` is the monotonic Android upgrade code. Package IDs, signing keys,
credential keys and local data paths remain stable across refactoring.
