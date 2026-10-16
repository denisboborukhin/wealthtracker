# WealthTracker

A C++23 and Qt Quick desktop application for viewing bank accounts, balances,
operations, cash-flow reports, and investment holdings locally.

The app starts empty. Demonstration data can be enabled in Settings.
Synchronized financial data lives in memory and is lost when the app exits.
Connection settings and credentials are stored separately.

## Current functionality

- Retail T-Bank, Alfa-Bank, and VTB: embedded bank login, accounts, and balances.
- Alfa-Bank B2C API, T-Bank Business, and Modulbank: token-based account and
  transaction synchronization.
- T-Invest: token-based account, portfolio balance, and position synchronization.
- External ZenPlugins bundles: local execution through an optional Node.js host.
- Cash-flow reports: date-filtered income, spending, savings, and categories,
  with own-account transfers excluded from totals.
- Statement checking: local parsing with operation and warning counts;
  selected files do not add records to the financial models.
- Privacy screen: hides financial views and cancels active work.

Retail connections and T-Invest supply balances without transaction history.
Cash-flow reports use operations supplied by the other connectors. Aggregate
financial totals use RUB; foreign-currency accounts remain visible separately.

Bank integrations are implemented, but automated tests do not establish current
compatibility with a bank's live service. See [Bank connections](docs/bank-connections.md)
for setup and refresh behavior. External plugins run with the current user's
file and network permissions; review their source before running them.

## Build and run

On macOS arm64, install Xcode or LLVM, then run from the repository root:

```sh
./scripts/bootstrap.sh
. build/tooling/bin/activate
conan install . --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug -of build/macos-debug
./scripts/configure.sh macos-debug
cmake --build --preset macos-debug
open build/macos-debug/WealthTracker.app
```

See [Building](docs/building.md) for Windows instructions, tests, and installable
packages. Node.js 18 or newer is needed only for external plugins and the host's
syntax check.

## Documentation

| Document | Contents |
| --- | --- |
| [Bank connections](docs/bank-connections.md) | Available integrations and setup |
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
