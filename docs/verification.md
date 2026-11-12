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
| Android runtime | `sh android/verify.sh emulator-5554` with JDK/SDK variables set and an owned emulator booted | Android trust/credential/browser instrumentation plus guarded native C++ callback checks |
| UI | Compare phone/desktop screenshots after layout changes | Visible geometry and native window rendering |
| Live banks | User independently signs in and checks refreshed balances/categories | Current authenticated compatibility; synthetic tests alone do not establish this |

See [Android verification instructions](../android/README.md#reproduce-android-verification) for prerequisites, exact device selection and log locations.

`version.json` is the release metadata source: `product` supplies desktop/Conan
versions, `androidName` supplies Android display version and artifact name, and
`androidCode` is the monotonic Android upgrade code. Package IDs, signing keys,
credential keys and local data paths remain stable across refactoring.
