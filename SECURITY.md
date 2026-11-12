# Security policy

Security fixes target the current `0.1` technical-preview line.

## Reporting a vulnerability

Contact a maintainer privately with a redacted description. Do not attach
credentials, session identifiers, bank statements, databases, or personal
financial data to a public issue. Agree on a secure channel before sharing
sensitive reproduction material.

## Current boundary

Built-in bank connectors and EVM wallet tracking issue read requests. Credentials
use macOS Keychain, Windows Credential Manager, or Android Keystore-backed
AES-GCM storage. Bank browser sessions stay in the platform WebView.

| Data | Storage and protection |
| --- | --- |
| API tokens, T-Bank API session, database key; desktop plugin state | Platform credential backend described above |
| Bank cookies and browser storage | Platform WebView storage; not exported as application model data |
| Balance observations, public wallet addresses/names, automatic cashback | SQLCipher-encrypted local database with its key in the platform credential backend |
| Manual cashback names, percentages and months | Local Qt application settings without application-level encryption |
| Connection flags, refresh times and non-sensitive plugin preferences | Local Qt application settings without application-level encryption |
| Synchronized transaction and investment holding details | Process memory; require a fresh synchronization after restart |

Rabby receives queried public addresses through public HTTPS read endpoints.
Wallet tracking uses no service API key. Wallet private keys and seed phrases are
neither requested nor stored. The privacy screen hides views and cancels active
work; unlocking it does not require authentication and does not encrypt or erase
stored data.

External JavaScript bundles execute with the current user's file and network
permissions. Review their source before running them.

See [Security design](docs/security-design.md) for the implemented controls and
their limits.
