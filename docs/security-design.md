# Security design

## Assets and trust

The sensitive assets are bank tokens, the retail T-Bank API session, Alfa
Online and VTB Online browser sessions, external plugin state, and normalized financial
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
Alfa Online and VTB Online are different: the system WebView owns each browser
session, and WealthTracker never extracts or copies cookie values into
application memory.

The desktop controller currently retains normalized financial data only in
memory. The SQLCipher and Argon2id components are independently tested building
blocks, not an active application vault. Documentation and UI must not describe
the privacy screen as database encryption or memory protection.

## Built-in network policy

- Exact HTTPS hosts are declared by each connector.
- Redirects are manual or rejected.
- Only documented read methods or isolated, fixed retail read paths are
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
reads at `www.tbank.ru`. It freezes a two-year range and serializes 24 bounded
history windows per account. The response exposes no continuation marker, so
the UI describes this as a requested period rather than asserting statement
completeness. The API is undocumented, so parse or authentication changes fail
closed.

## Retail Alfa-Bank login

The official Alfa Online phone/SMS page runs inside a separate coordinator-owned
system WebView. File access and persistent local storage are disabled. Standard
operating-system TLS and browser-cookie policy remain in force; the application
does not install certificate exceptions or inspect cookie contents.

Alfa Online's current chain is rooted at the Russian Trusted Root CA, which may
be absent from the operating-system trust store. A certificate-related WebView
failure remains blocking. The UI exposes only a user-triggered link to
`https://alfa.bank/cert`, the bank's current installation guidance; it does
not fetch a root certificate, mutate a trust store, or accept an invalid chain.
Failure text is derived from a fixed classification and an allowlisted hostname,
never from a complete URL or raw renderer error that could contain login data.
The bank can also return a static certificate landing page after authentication.
It is recognized only on the exact Alfa Online origin by its root path, absence
of scripts and forms, and exact `https://alfa.bank/cert` link; page text and
input values are not read.

A fixed expression checks the exact `https://web.alfabank.ru` origin and reads
only known global or inline dashboard bootstrap objects. Inline scripts are
bounded to 64 elements, 8 MiB each, and 16 MiB total; external scripts, rendered
DOM, fields, and storage are not read. Only the `initialState` object or
`runApp` `appState` JSON is considered for accounts and balances.

For history, a short-lived observer follows the bank's normal `/history` route
and accepts only 2xx JSON responses from the exact same-origin
`/api/v1/operations-history/operations` path. For that POST only, it may parse a
JSON request body of at most 64 KiB, rewrite recognized existing date fields to
two years, reset an existing page or offset field, and raise an existing
page-size field to 100. It constructs serial same-path continuation POSTs inside
the page and sets only `Content-Type`. Unrelated filters, method, host, and path
are unchanged. Bodies and values remain page-local. The observer restores the
browser API when capture finishes.
The coordinator always starts this route after it has accounts; dashboard
preview operations are not trusted as evidence that history is complete. At
most 200 responses and 8 MiB of combined response text are retained page-locally.
The projection returns an allowlist of fields rather than raw page or response
state, then releases its temporary response references. C++ enforces an 8 MiB
result limit, 256-account and 20,000-operation limits, exact integer amounts,
bounded text, valid currencies and dates, and account ownership for each
operation. QML receives only the native window and textual status.

The browser session remains in the operating-system WebView for automatic
dashboard refreshes and is cleared when the connection is removed. A redirect
to `private.auth.alfabank.ru` during synchronization invalidates the connection.
The page-state schema is undocumented, so missing or changed data fails closed
without switching to direct password requests, cookie export, or write calls.

## Retail VTB login

The official VTB Online page runs in a coordinator-owned system WebView with
local storage and file access disabled. Standard operating-system TLS and cookie
policy remain in force. The app validates the exact `https://online.vtb.ru`
origin and rejects credentials embedded in its URL.

A fixed expression obtains only the bank frontend's exported `./HttpClient` and
invokes two exact GET paths for active products and an optional balance
supplement. A non-authentication failure of the optional supplement does not
discard balances embedded in a valid active portfolio.

For history, a short-lived observer opens the official `/history` route. It
accepts only same-origin `GET` responses from four fixed final paths under the
exact VTB history prefix: `byUser`, `byProducts`, `byAccount`, and `byCard`.
The initial page request supplies an exact path and stable non-date query
template. After capture, the page-owned HTTP client replays inclusive 31-day
windows over two years and follows explicit pagination or a provable full page.
Only recognized dates and page or offset values change. The script generates
`x-request-id` and supplies fixed `x-initiator-service` and
`x-smart-update-request` values; it does not inspect or copy private headers.
At most 16 templates, 128 replay requests, and 8 MiB of combined operation data
are retained page-locally. The expression does not inspect cookies, login
fields, request bodies, resource timing, DOM contents, or browser storage. A
redirect to another origin is not treated as an authenticated VTB page.

Each response is checked for its expected top-level shape and an 8 MiB limit
before it is retained page-locally. A separate allowlist projection converts
only bounded account and operation fields. C++ revalidates an 8 MiB result,
256-account and 20,000-operation limits, exact minor units, dates, currencies,
duplicates, and operation ownership. Raw response objects and browser-session
material never cross the WebView boundary.

The WebView session is reused for automatic refresh and cleared on disconnect.
Authentication errors expire the connection; missing frontend exports or schema
changes stop with a compatibility error. The private frontend contract is
undocumented and never expands to new hosts, routes, methods, cookie export, or
direct credential requests at runtime.

The expression tracks only a fixed request-stage label and accepts numeric HTTP
status from `status`, `statusCode`, `response.status`, or
`response.statusCode`; no response text is copied into a diagnostic. A
non-authentication replay failure, unsupported date or continuation template,
timeout, or request limit returns an explicitly partial batch. Compatible prior
operations for still-present accounts are retained only in memory and only
inside the current two-year cutoff. A malformed history payload still fails
closed because it indicates an unknown schema rather than a temporary outage.

A login pathname can remain stale after MFA. WealthTracker therefore does not
infer authentication from that presentation route. It verifies the exact origin
and lets 401/403 from the page-owned, allowlisted read client mark the session
as unauthenticated; no session value is extracted or copied.

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
