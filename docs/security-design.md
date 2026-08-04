# Security design

## Vault

A new vault receives a random 256-bit SQLCipher key. By default it is stored in
the operating-system credential store. When PIN protection is enabled, the
database key is wrapped with a key derived using Argon2id and the direct
credential-store entry is removed.

The application locks on suspend and after an idle timeout. Decrypted key
material is cleared when the vault locks.

## Connectors

T-Invest accepts only a user-generated read-only token. Other Russian banks are
supported through statement files until legitimate consumer APIs are generally
available.

Logs redact credentials, authorization headers, account identifiers, and
imported descriptions. No telemetry or remote crash reporter is present.

Retail T-Bank authentication is delegated to the normal system browser.
WealthTracker neither embeds Chromium nor receives the browser's cookie jar,
local storage, phone, password, or one-time code. The browser remains subject to
the operating system's ordinary TLS and certificate policy; the application
does not override bank certificate failures.

A separate Qt network client creates and polls one bounded opaque `sessionid`
through exact HTTPS common-API endpoints. Cookie loading, cookie saving, and
automatic redirects are disabled on every authentication request. Before
opening the system browser, the returned authorization redirect is restricted
to exact `id.tbank.ru` HTTPS navigation with the expected T-Bank client,
callback, and PKCE parameters; duplicate security parameters and lookalike
hosts are rejected. Only a `CLIENT` access level is accepted. The candidate is
then validated through read-only account requests and is removed from the
credential store if validation fails. The identifier and signed authorization
URL are never logged or exposed to QML.

## Backups

Portable backups are encrypted with a distinct user passphrase and include an
authenticated manifest containing schema and application versions. Plain CSV
exports require a warning because they are not encrypted.
