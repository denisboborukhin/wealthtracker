# Security policy

Security fixes target the current `0.1` technical-preview line.

## Reporting a vulnerability

On GitHub, open **Security and quality → Advisories → Report a vulnerability**
to submit a private report. On GitLab, use a confidential issue and verify that
the issue is marked confidential before posting details.

Include a redacted description and reproduction steps. If private reporting is
unavailable, ask a maintainer for a private contact before sharing any details.
Keep credentials, session identifiers, bank statements, databases, and personal
financial data out of the initial report.

See [GitHub's private reporting instructions](https://docs.github.com/en/code-security/how-tos/report-and-fix-vulnerabilities/report-privately)
and [GitLab's confidential issue instructions](https://docs.gitlab.com/user/project/issues/confidential_issues/).

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
