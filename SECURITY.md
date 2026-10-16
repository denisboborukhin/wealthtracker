# Security policy

Security fixes target the current `0.1` technical-preview line.

## Reporting a vulnerability

Contact a maintainer privately with a redacted description. Do not attach
credentials, session identifiers, bank statements, databases, or personal
financial data to a public issue. Agree on a secure channel before sharing
sensitive reproduction material.

## Current boundary

Built-in bank connectors issue read requests. Credentials use macOS Keychain,
Windows Credential Manager, or the system WebView. Financial snapshots remain
in process memory. The privacy screen hides views and cancels active work;
unlocking it does not require authentication.

External JavaScript bundles execute with the current user's file and network
permissions. Review their source before running them.

See [Security design](docs/security-design.md) for the implemented controls and
their limits.
