# WealthTracker

WealthTracker is a local-first, open-source desktop application for viewing
personal finances across Russian banks and investment accounts.

The application keeps its database on your computer and encrypts it with
SQLCipher. Official API tokens are stored in the operating-system credential
store and copied into temporary memory only while a request is constructed.

## Current capabilities

- Local CSV, XLSX, OFX, QIF, 1C-style, and reviewed PDF statement imports.
- Import presets for T-Bank, Alfa-Bank, Sber, and VTB.
- Read-only Alfa-Bank B2C API synchronization for registered integrations.
- Read-only T-Bank Business account and statement synchronization.
- Read-only Modulbank synchronization, including its public sandbox.
- Read-only synchronization with T-Invest using a user-created token.
- Retail T-Bank login through the official `tbank.ru` page, followed by
  read-only account, balance, and operation synchronization.
- A local compatibility bridge for separately built, modular ZenPlugins
  bundles, including dynamic login fields and one-time-code prompts.
- Accounts, balances, transaction splits, categories, rules, and transfers.
- Income, spending, savings, net-worth, and investment analytics.
- Russian interface with localization support.

The official live connectors use documented bank APIs. T-Bank Business and
Modulbank cover business accounts. Alfa-Bank B2C requires an access token
issued to a registered integration; WealthTracker does not yet run Alfa's
Authorization Code flow itself.

Retail T-Bank is a separate, explicitly unofficial integration. WealthTracker
creates a separate anonymous API session, asks T-Bank for its signed T-ID
authorization URL, and opens that URL in the normal system browser. The browser
owns the phone, password, one-time code, and its own cookies; WealthTracker
cannot read them. Its separate, cookie-disabled network client polls only the
opaque API session until T-Bank marks it `CLIENT`, then stores that session in
the operating-system credential store. The bundled MIT-licensed adapter can
contact only `https://www.tbank.ru` and never calls payment, transfer,
card-management, or trading endpoints. Because T-Bank does not publish this
retail web API, the connection can break when the bank changes its site and may
require signing in again.

Retail Alfa-Bank, Sber, and VTB adapters are not yet bundled. The Connections
page shows them as explicit local-adapter slots instead of presenting a
non-functional bank login form.

The connector design is informed by the local, normalized plugin flow used by
ZenMoney. WealthTracker does not copy or embed ZenPlugins source: ZenPlugins is
GPL-3.0 while this project is MIT-licensed. A user can select a bundle built in
a separate ZenPlugins checkout. WealthTracker runs it as a child process and
normalizes the result through a bounded JSON-lines protocol. The external
bundle is not sandboxed, so only run code and commits you have reviewed.

The often-confused `altabanka-rs` plugin is for Alta Banka in Serbia (`.rs`,
RSD), not Alfa-Bank Russia. At the upstream revision inspected on
2026-07-22, ZenPlugins does not include retail plugins for Alfa-Bank Russia,
T-Bank, Sber, or VTB.

## Technology

- C++23
- Qt 6 Quick/QML
- CMake and Conan 2
- SQLCipher and libsodium
- Catch2
- Node.js 18+ for retail T-Bank and external ZenPlugins bundles

## Build

Run the bootstrap script to install pinned build tools and the official
prebuilt Qt SDK under the ignored `build/` directory. Conan installs the
remaining C++ dependencies and generates a toolchain under
`build/<preset>/generators`.

```sh
./scripts/bootstrap.sh
. build/tooling/bin/activate
conan install . \
  --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug \
  -of build/macos-debug
./scripts/configure.sh macos-debug
cmake --build --preset macos-debug
ctest --preset macos-debug
```

## Run

After a Debug build:

```sh
./build/macos-debug/WealthTracker.app/Contents/MacOS/WealthTracker
```

On Windows, run `build/windows-debug/WealthTracker.exe`.

To connect retail T-Bank, open **Connections → Т-Банк**, choose **Открыть
T-Bank в браузере**, and complete the official T-ID page. Return to
WealthTracker; it checks the API session automatically, or you can choose
**Проверить сейчас**. After the first successful import, auto-update can
refresh the same session while the application is running.

## Use an external ZenPlugins bundle

Clone and build ZenPlugins outside this repository so its GPL-3.0 source and
bundle remain separate:

```sh
git clone https://github.com/zenmoney/ZenPlugins.git
cd ZenPlugins
corepack enable
yarn
yarn build <plugin-id>
```

In WealthTracker, open **Connections → Local ZenPlugins**, choose
`ZenPlugins/build/<plugin-id>.js`, review the detected fields, and run it.
Keep the bundle in that `build/` directory: the app reads the corresponding
`src/plugins/<plugin-id>/preferences.xml` to construct the local form.
Passwords and one-time codes remain session-only. Non-secret stable fields
such as phone, login, or start date can be saved locally so a compatible
session-based bundle can auto-refresh while the app is running. Plugin data
and cookies returned for reuse are stored in the operating-system credential
store.

See [Bank connections](docs/bank-connections.md),
[Building](docs/building.md), [Architecture](docs/architecture.md), and
[Security](SECURITY.md) for details.

## License

Application source is licensed under the [MIT License](LICENSE). Dynamically
linked Qt libraries and other dependencies retain their respective licenses.
