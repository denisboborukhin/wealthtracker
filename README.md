# WealthTracker

WealthTracker is an open-source C++23 desktop application for viewing accounts,
balances, operations, spending categories, and cash-flow statistics locally.
The interface starts empty; demonstration data is opt-in.

> **Status:** technical preview. Bank results currently live in memory and are
> lost when the application exits. File import is a validated preview, and the
> T-Invest flow currently checks account access without loading a portfolio.
> The SQLCipher repository is implemented and tested but is not connected to
> the desktop session yet.

## Connections

| Connection | Current behavior |
| --- | --- |
| Retail T-Bank | Embedded official login; accounts, balances, and operations through a native read-only connector |
| Retail Alfa-Bank | Embedded official login; bounded account, balance, and recent-operation snapshots from Alfa Online |
| Alfa-Bank B2C API | Accounts and operations with an already-issued integration token |
| T-Bank Business | Accounts and statements with a T-API token |
| Modulbank | Accounts and operations, including the public sandbox |
| T-Invest | Validates a user-created read-only token and lists accessible accounts |
| External ZenPlugins bundle | Optional compatibility host for a separately reviewed and built bundle |
| Retail Sber, VTB | Visible adapter slots; no built-in connector |

Retail T-Bank uses undocumented web endpoints and may stop working after a
bank-side change. The application accepts the session only after the bank
reports `CLIENT` access, stores it in the operating-system credential store,
and sends it only to exact `https://www.tbank.ru` read endpoints. It never
implements payments, transfers, card management, or trading.

Retail Alfa-Bank uses the official Alfa Online phone/SMS page. Its login is
browser-session based: WealthTracker does not read cookie values, headers,
login fields, or web storage. A fixed page expression projects only bounded
account, balance, and recent-operation fields into C++, and the system WebView
retains the browser session for six-hour refreshes while auto-update is enabled.
This interface is undocumented and can break after a bank-side change. Alfa
Online uses Russian TLS certificates; on systems that do not trust the Russian
Trusted Root CA, the connection dialog links to
[Alfa-Bank's official installation instructions](https://alfabank.ru/cert/).
WealthTracker never installs a root certificate or disables TLS verification.

External JavaScript plugins are not sandboxed. They run as the current user and
can access local files and the network. WealthTracker does not distribute
ZenPlugins source or bundles; only run a bundle whose source and pinned commit
you reviewed.

## Build and run on macOS

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
ctest --preset macos-debug --output-on-failure
./build/macos-debug/WealthTracker.app/Contents/MacOS/WealthTracker
```

Keep `build/tooling` activated while Conan builds dependencies; its recipes
invoke CMake and Ninja by name. Windows commands and release checks are in
[Building](docs/building.md).

Node.js is not required for the built-in T-Bank or Alfa-Bank connections.
Install Node.js 18 or newer only if you choose to run an external JavaScript
plugin.

## Project boundaries

- C++23 named modules separate finance, storage, platform, connector, and
  application code.
- Qt Quick/QML receives models and commands, not credentials or bank-session
  projections.
- Official tokens and the T-Bank API session use macOS Keychain or Windows
  Credential Manager. Alfa Online's browser session stays inside the operating
  system WebView and is removed when the connection is deleted.
- Built-in network connectors use exact HTTPS origins, bounded responses, and
  read-only methods.
- Active login and synchronization work is cancelled when data is hidden or a
  connection is removed.
- No telemetry or remote crash reporter is included.

See [Bank connections](docs/bank-connections.md),
[Architecture](docs/architecture.md), [Security design](docs/security-design.md),
and [Security policy](SECURITY.md).

## License

WealthTracker is licensed under the [MIT License](LICENSE). Dependencies retain
their own licenses; see [Third-party notices](THIRD_PARTY_NOTICES.md).
