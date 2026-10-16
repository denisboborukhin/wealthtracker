# Security policy

## Supported versions

Security fixes target the latest release. The current `0.1` line is a technical
preview and should not be treated as an audited financial application.

## Reporting a vulnerability

Do not attach credentials, session identifiers, bank statements, database
files, or other personal financial data to a public issue. Contact a maintainer
privately and start with a redacted description. Share sensitive reproduction
material only after agreeing on a secure channel and retention period.

## Implemented security boundary

- Built-in connectors use exact HTTPS origins, read-only methods, manual
  redirects, response limits, and validated normalized output.
- Official tokens and the retail T-Bank API session are stored in macOS Keychain
  or Windows Credential Manager. Temporary byte arrays are cleared after use.
- T-Bank credentials and MFA stay inside the operating-system WebView. QML
  receives only its native window, never the extracted session projection.
- T-Bank browser storage is restricted, cookies are deleted around each login,
  and the accepted session is validated before the connection becomes active.
- Alfa-Bank credentials and MFA stay inside the official page. Its browser
  session remains owned by the system WebView; the app receives only a bounded,
  versioned financial projection and clears the session on disconnect.
- VTB follows the same browser-owned session boundary and reads balances through
  the authenticated page's client. Retail connections do not request history.
- Hiding financial data cancels active bank login, synchronization, and plugin
  processes. It is a privacy screen, not an encrypted-memory lock.
- Telemetry and remote crash reporting are not included.

Financial records are currently kept in process memory and are not persisted
by the desktop controller. SQLCipher, migration, and key-wrapping components
exist as tested libraries but are not yet wired into the running application.
The [security design](docs/security-design.md) describes the implementation and
its limits, including T-Invest synchronization and browser-specific controls.

## External plugins

A user-selected ZenPlugins bundle is arbitrary JavaScript executed by Node.js
with the current user's file and network permissions. The child environment and
JSON-lines protocol are restricted, outputs are bounded and validated, and no
official connector token is passed to the child. These controls do not sandbox
an untrusted bundle.

Review the plugin source and pinned commit, build it separately, and never run
an opaque download. ZenPlugins and generated GPL-3.0 bundles are not distributed
with WealthTracker.

## Out of scope

The application cannot defend against an administrator, malware running as the
same user, a compromised bank page, screenshots, or a modified local binary.
If a device or bank session may be compromised, revoke sessions through the
bank and rotate affected credentials.
