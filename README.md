# WealthTracker

WealthTracker is an open-source C++23 desktop application for viewing accounts,
balances, operations, spending categories, and cash-flow statistics locally.
The interface starts empty; demonstration data is opt-in.

Income, spending, savings, categories, and the operations list use an inclusive
date range selected from calendar controls. The default is the latest month.
Savings are shown as the RUB amount left after spending. Matching opposite
operations between two connected banks are kept in the history as own-account
transfers but excluded from income, spending, savings, and category totals.

> **Status:** technical preview. Bank results currently live in memory and are
> lost when the application exits. File import is a validated preview, and the
> T-Invest flow currently checks account access without loading a portfolio.
> The SQLCipher repository is implemented and tested but is not connected to
> the desktop session yet.

## Connections

| Connection | Current behavior |
| --- | --- |
| Retail T-Bank | Embedded official login; accounts, balances, and a windowed two-year operation request |
| Retail Alfa-Bank | Embedded official login; accounts, balances, and bounded two-year history with explicit partial status |
| Retail VTB | Embedded official login; accounts, balances, and bounded two-year history with explicit partial status |
| Alfa-Bank B2C API | Accounts and operations with an already-issued integration token |
| T-Bank Business | Accounts and statements with a T-API token |
| Modulbank | Accounts and operations, including the public sandbox |
| T-Invest | Validates a user-created read-only token and lists accessible accounts |
| External ZenPlugins bundle | Optional compatibility host for a separately reviewed and built bundle |
| Retail Sber | Visible adapter slot; no built-in connector |

Retail T-Bank uses undocumented web endpoints and may stop working after a
bank-side change. The application accepts the session only after the bank
reports `CLIENT` access, stores it in the operating-system credential store,
and sends it only to exact `https://www.tbank.ru` read endpoints. It never
implements payments, transfers, card management, or trading.
Synchronization requests 24 serial windows covering two years. The undocumented
operation response has no continuation marker, so silent bank-side truncation
inside a successful window cannot be detected.

Retail Alfa-Bank uses the official Alfa Online phone/SMS page. Its login is
browser-session based: WealthTracker does not read cookie values, authorization
headers, login fields, or web storage. A fixed page expression reads the bank's
known dashboard state for accounts. On the history page, it rewrites only
recognized existing date, page, offset, and page-size fields in the exact
same-origin POST body, targets two years, and follows serial continuation pages
inside the WebView. Completion requires an explicit terminal flag or a short
final page; otherwise the result is labeled partial. Only bounded account,
balance, and operation fields reach C++. The system WebView retains the browser
session for six-hour refreshes while auto-update is enabled.
This interface is undocumented and can break after a bank-side change. Alfa
Online uses Russian TLS certificates; on systems that do not trust the Russian
Trusted Root CA, the connection dialog links to
[Alfa-Bank's official installation instructions](https://alfa.bank/cert).
WealthTracker never installs a root certificate or disables TLS verification.

Retail VTB keeps authentication inside the official VTB Online page. The app
does not read cookies, login fields, private headers, or web storage. It invokes
the authenticated page's own HTTP client for two fixed account and balance
routes. On the history screen it captures an exact same-origin GET template,
preserves its non-date query fields, and replays inclusive 31-day windows over
two years through that page-owned client. Explicit continuation pages are
followed under fixed request and payload limits. Incomplete retrieval preserves
compatible prior in-memory operations and is shown as partial; authentication
and schema failures still stop the sync. This frontend interface is undocumented
and can break after a bank change.

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

Node.js is not required for the built-in T-Bank, Alfa-Bank, or VTB connections.
Install Node.js 18 or newer only if you choose to run an external JavaScript
plugin.

## Project boundaries

- C++23 named modules separate finance, storage, platform, connector, and
  application code.
- Official bank credentials and session projections are not exposed as QML
  properties or status text. Tokens and external-plugin answers entered in the
  interface exist in QML memory only until submission and are then cleared.
- Official tokens and the T-Bank API session use macOS Keychain or Windows
  Credential Manager. Alfa Online and VTB Online browser sessions stay inside
  the operating-system WebView and are removed when a connection is deleted.
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
