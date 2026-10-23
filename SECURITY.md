# Security policy

Security fixes target the current `0.1` technical-preview line.

## Reporting a vulnerability

Contact a maintainer privately with a redacted description. Do not attach
credentials, session identifiers, bank statements, databases, or personal
financial data to a public issue. Agree on a secure channel before sharing
sensitive reproduction material.

## Current boundary

Built-in bank connectors and EVM wallet tracking issue read requests. Credentials use macOS Keychain,
Windows Credential Manager, or the system WebView. Balance history persists in
a SQLCipher-encrypted database with its key in the OS credential store; public
wallet addresses and names are stored in the same encrypted database. Rabby receives queried public addresses through public HTTPS read endpoints.
Wallet tracking uses no service API key.
Wallet private keys and seed phrases are neither requested nor stored. Other
financial data remains in process memory. The privacy screen hides views and cancels active work;
unlocking it does not require authentication.

External JavaScript bundles execute with the current user's file and network
permissions. Review their source before running them.

See [Security design](docs/security-design.md) for the implemented controls and
their limits.
