# WealthTracker

WealthTracker is a local-first, open-source desktop application for viewing
personal finances across Russian banks and investment accounts.

The application keeps its database on your computer, encrypts it with
SQLCipher, and never asks for an online-banking password or SMS code.

## Planned v0.1 capabilities

- Local CSV, XLSX, OFX, QIF, 1C-style, and reviewed PDF statement imports.
- Import presets for T-Bank, Alfa-Bank, Sber, and VTB.
- Read-only synchronization with T-Invest using a user-created token.
- Accounts, balances, transaction splits, categories, rules, and transfers.
- Income, spending, savings, net-worth, and investment analytics.
- Russian interface with localization support.

Russian retail banks do not currently provide uniform consumer transaction
APIs to independent local applications. Bank support therefore means local
statement import unless a legitimate read-only API is available.

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

See [Building](docs/building.md), [Architecture](docs/architecture.md), and
[Security](SECURITY.md) for details.

## License

Application source is licensed under the [MIT License](LICENSE). Dynamically
linked Qt libraries and other dependencies retain their respective licenses.
