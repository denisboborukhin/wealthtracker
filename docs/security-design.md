# Security design

## Assets and trust

The sensitive assets are bank tokens, the retail T-Bank API session, the Alfa
Online browser session, external plugin state, and normalized financial
records. The local operating system, the running application binary, and each
bank's own login page are trusted.
User-selected JavaScript plugins are trusted only when the user has reviewed
them; WealthTracker cannot sandbox them.

## Credential handling

macOS uses Keychain items restricted to the unlocked device. Windows uses
Credential Manager. Linux currently has no supported secure credential backend,
so token and API-session connections that require it fail closed there.

Credentials are loaded for request construction and temporary byte arrays are
cleared immediately afterward. They are not exposed as QML properties or
included in status text. Removing a connection deletes its credential entry.
Alfa Online is different: the system WebView owns its browser cookie store and
WealthTracker never extracts or copies cookie values into application memory.

The desktop controller currently retains normalized financial data only in
memory. The SQLCipher and Argon2id components are independently tested building
blocks, not an active application vault. Documentation and UI must not describe
the privacy screen as database encryption or memory protection.

## Built-in network policy

- Exact HTTPS hosts are declared by each connector.
- Redirects are manual or rejected.
- Only documented read methods, or the isolated retail T-Bank read paths, are
  represented in connector APIs.
- Responses, record counts, pagination, cursors, and concurrent requests are
  bounded.
- Parsers reject malformed dates, currencies, amounts, duplicate identifiers,
  and integer overflow.
- Removing a connection or hiding data aborts owned replies and pending pages.

## Retail T-Bank login

The operating-system WebView is embedded in the Connections dialog. Local
storage and local-file access are disabled, cookies are cleared around each
attempt, and normal operating-system TLS policy remains in force.

A fixed expression reads resource-timing URL names and projects at most eight
session candidates from exact T-Bank HTTPS API paths. C++ validates the bounded
projection, checks a candidate without cookies or redirects, and accepts only
`CLIENT` access. The login coordinator owns every WebView callback, timer,
reply, cancellation path, and candidate buffer. QML sees only the native
window and textual status.

The synchronization connector separately permits only account and operation
reads at `www.tbank.ru`. The API is undocumented, so parse or authentication
changes fail closed.

## Retail Alfa-Bank login

The official Alfa Online phone/SMS page runs inside a separate coordinator-owned
system WebView. File access and persistent local storage are disabled. Standard
operating-system TLS and browser-cookie policy remain in force; the application
does not install certificate exceptions or inspect cookie contents.

Alfa Online's current chain is rooted at the Russian Trusted Root CA, which may
be absent from the operating-system trust store. A certificate-related WebView
failure remains blocking. The UI exposes only a user-triggered link to
`https://alfabank.ru/cert/`, the bank's official installation guidance; it does
not fetch a root certificate, mutate a trust store, or accept an invalid chain.
Failure text is derived from a fixed classification and an allowlisted hostname,
never from a complete URL or raw renderer error that could contain login data.

A fixed expression checks the exact `https://web.alfabank.ru` origin and reads
only known page-state objects. It projects a versioned allowlist of financial
fields rather than returning raw page state. C++ enforces an 8 MiB response
limit, 256-account and 20,000-operation limits, exact integer amounts, bounded
text, valid currencies and dates, and account ownership for each operation.
QML receives only the native window and textual status.

The browser session remains in the operating-system WebView for automatic
dashboard refreshes and is cleared when the connection is removed. A redirect
to `private.auth.alfabank.ru` during synchronization invalidates the connection.
The page-state schema is undocumented, so missing or changed data fails closed
without switching to direct password requests, cookie export, or write calls.

## External plugin process

The compatibility host is copied to an owner-only temporary file. Node.js
starts with a reduced environment and exchanges bounded JSON-lines messages
over pipes. Standard error is drained but not surfaced, and preference values
are redacted from returned errors. C++ validates all events and normalized
records before they reach application models.

This process still has the current user's file and network permissions. An
external bundle can import Node.js modules, spawn processes, or exfiltrate data
it can access. Only reviewed source should be executed.

## Privacy screen

Hiding data closes an active bank login and cancels plugin, investment, and
bank synchronization work before covering the interface. Existing model data
remains in process memory. This feature protects against casual screen viewing,
not same-user malware, debugging, memory inspection, or screenshots.
