# WealthTracker

A C++23 and Qt Quick desktop application for viewing bank accounts, balances,
operations, cash-flow reports, investment holdings, and public Ethereum wallets locally.

The app starts empty on first launch. Demonstration data can be enabled in Settings.
Balance observations from successful connections and refreshes are saved in an
encrypted local database and remain available after restart or disconnect.
Other synchronized financial data lives in memory and is lost when the app exits.
Connection settings and credentials are stored separately. On macOS, retail browser data
is retained across launches; a bank can still require reauthentication when its
session expires.

## Current functionality

- Retail T-Bank, Alfa-Bank, and VTB: embedded bank login, accounts, and balances.
- Alfa-Bank B2C API, T-Bank Business, and Modulbank: token-based account and
  transaction synchronization.
- T-Invest: token-based account, portfolio balance, and position synchronization.
- EVM wallets: native coins and tokens across Rabby-indexed networks, valued
  in USDT. Public-address tracking requires no registration or API key.
- External ZenPlugins bundles: local execution through an optional Node.js host.
- Cash-flow reports: date-filtered income, spending, savings, and categories,
  with own-account transfers excluded from totals.
- Balance history: saved observations, date filters, selected banks and wallets,
  and opening/closing totals and changes for each currency.
- Statement checking: local parsing with operation and warning counts;
  selected files do not add records to the financial models.
- Privacy screen: hides financial views and cancels active work.

Retail connections and T-Invest supply balances without transaction history.
Cash-flow reports use operations supplied by the other connectors. Aggregate
financial totals use RUB; foreign-currency accounts remain visible separately.
Balance-history totals keep currencies separate without exchange-rate conversion.
They combine the latest known balances from each connection, which may have
different observation times. Total history fills each calendar date with the
latest known balances, and date tooltips show connection amounts and shares. This
shows recorded balances, not investment returns.
Open **История** for balances grouped by bank or EVM wallet. Positive balances
stack above zero and negative balances below; shares use absolute connection
balances when negatives are present. **Настроить** contains source, currency,
and date selection.
The dashboard also shows combined history. A period can carry forward the last observation before
its start, with its original timestamp. Changing the selected connections can
change the total. Recording starts with successful refreshes in this version;
earlier balances are not reconstructed.

**Криптокошельки** watches public addresses without wallet keys or seed phrases.
Refresh runs at startup, every six hours while the app is running, or manually.
Assets without a usable price remain visible and are excluded from the USDT
subtotal. See [Crypto wallets](docs/crypto-wallets.md) for setup, valuation,
coverage, and verification limits.

macOS credentials consolidate into one Keychain item and are read once per
launch. The first migration may still request access to the old entries; macOS
can also ask again after a debug rebuild.

Bank integrations are implemented, but automated tests do not establish current
compatibility with a bank's live service. See [Bank connections](docs/bank-connections.md)
for setup and refresh behavior. External plugins run with the current user's
file and network permissions; review their source before running them.

## Build and run

On macOS arm64, install Xcode or LLVM, then run from the repository root:

```sh
./scripts/bootstrap.sh &&
. build/tooling/bin/activate &&
conan install . --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug -of build/macos-debug &&
./scripts/configure.sh macos-debug &&
cmake --build --preset macos-debug &&
open build/macos-debug/WealthTracker.app
```

The sequence stops if a step fails. Bootstrap must finish installing both the
build tools and Qt before configuration can succeed.

See [Building](docs/building.md) for Windows instructions, tests, and installable
packages. Node.js 18 or newer is needed only for external plugins and the host's
syntax check.

## Documentation

| Document | Contents |
| --- | --- |
| [Bank connections](docs/bank-connections.md) | Available integrations and setup |
| [Crypto wallets](docs/crypto-wallets.md) | Keyless EVM tracking, coverage, valuation, and history |
| [Architecture](docs/architecture.md) | Code ownership and runtime data flow |
| [Security design](docs/security-design.md) | Credential handling, network controls, and privacy |
| [Building](docs/building.md) | Build, verification, and packaging commands |
| [Contributing](CONTRIBUTING.md) | Development and submission rules |
| [Security policy](SECURITY.md) | Vulnerability reporting |
| [Agent instructions](AGENTS.md) | Coding-agent instructions |
| [Third-party notices](THIRD_PARTY_NOTICES.md) | Dependency licenses and provenance |

## License

[MIT](LICENSE). Dependencies retain their own licenses; see
[Third-party notices](THIRD_PARTY_NOTICES.md).
