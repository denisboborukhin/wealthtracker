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
- A local interactive-session contract for future retail-bank plugins.
- Accounts, balances, transaction splits, categories, rules, and transfers.
- Income, spending, savings, net-worth, and investment analytics.
- Russian interface with localization support.

The bundled live connectors use documented bank APIs only. They do not scrape
web or mobile banking sessions and never ask for a bank password or SMS code.
T-Bank Business and Modulbank cover business accounts. Alfa-Bank B2C requires
an access token issued to a registered integration; WealthTracker does not yet
run Alfa's Authorization Code flow itself. Retail T-Bank, Sber, and VTB login
plugins are not bundled, and the Connections page reports that limitation
instead of presenting a non-functional login form.

The connector design is informed by the local, normalized plugin flow used by
ZenMoney. WealthTracker does not copy or embed ZenPlugins source: ZenPlugins is
GPL-3.0 while this project is MIT-licensed.

## Technology

- C++23
- Qt 6 Quick/QML
- CMake and Conan 2
- SQLCipher and libsodium
- Catch2

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

See [Bank connections](docs/bank-connections.md),
[Building](docs/building.md), [Architecture](docs/architecture.md), and
[Security](SECURITY.md) for details.

## License

Application source is licensed under the [MIT License](LICENSE). Dynamically
linked Qt libraries and other dependencies retain their respective licenses.
