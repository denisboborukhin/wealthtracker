# Architecture

WealthTracker is a native C++23 desktop application with a Qt Quick interface.
Core subsystem boundaries are C++ named modules. Conventional headers are
limited to Qt `moc`, operating-system ABI, and third-party C API boundaries.

## Module boundaries

- `core`: domain values, financial rules, categorization, and analytics.
- `storage`: SQLCipher connection, migrations, repositories, and backups.
- `platform`: credential storage, secure memory, and OS integration.
- `connectors`: file imports, read-only API integrations, and validation plus
  normalization for external local plugin messages.
- `app`: composition, use cases, and QML-facing controllers.
- `ui`: QML pages, components, and translations.

QML does not receive database handles, encryption keys, or tokens. Controllers
expose narrow asynchronous operations and immutable models. Bank authentication
is delegated to the operating system's browser; the application does not embed
a browser engine or expose browser storage to a connector.

## Data flow

Statement imports follow a preview-first atomic pipeline:

1. Probe the source and select a parser.
2. Parse into a normalized batch without changing the database.
3. Show warnings, mapping choices, and duplicate candidates.
4. Reconcile accounts and balances.
5. Commit the whole batch in one transaction or roll it back.

User edits are overlays on immutable source records so re-importing a statement
does not discard categories, splits, tags, or notes.

## Connectors

Connector API v1 separates file imports, token-based read-only synchronization,
and interactive bank sessions. An interactive connector advances through
typed challenges:

1. Collect a small set of named fields such as phone, login, password, or MFA.
2. Optionally open a clean HTTPS bank authorization URL.
3. Return normalized accounts, balances, and transactions only after the
   session is authenticated.

Password and one-time-code fields must declare `Never` retention. A connector
may retain a revocable session or refresh token in the operating system's
credential store, but it cannot expose secrets to QML.

Every built-in network connector declares an HTTPS host allowlist. Write,
trading, payment, and transfer endpoints are outside that contract.

Retail T-Bank uses the same normalized plugin result boundary with a bundled
MIT adapter, but authentication stays in the official bank page:

1. C++ creates a bounded anonymous session through T-Bank's common API with
   cookie loading, cookie saving, and automatic redirects disabled.
2. C++ asks that API for a signed T-ID authorization URL and accepts only the
   exact HTTPS identity host, path, client, callback, and PKCE method.
3. The operating system opens the validated URL in the user's normal browser.
   The user enters credentials and MFA into the bank-owned page; WealthTracker
   has no access to that browser's cookies or storage.
4. C++ polls the original opaque session through the common API. Only a
   `CLIENT` access level is accepted, and only that session identifier is
   placed in the operating-system credential store.
5. The local Node host exposes `fetch` only for the exact
   `https://www.tbank.ru` origin, permits only `GET`/`HEAD`, and blocks
   redirects.
6. The adapter validates the session, reads accounts and operations, and emits
   ordinary normalized plugin records.
7. C++ validates those records before updating application models. Only a
   successful import marks the connection active.

The retail endpoints are undocumented and are deliberately isolated from the
official API clients. A bank-side change becomes a connector failure rather
than expanding network or write permissions.

External ZenPlugins use a separate trust boundary:

1. C++ validates a user-selected bundle and its `preferences.xml`.
2. The app extracts its MIT host script to a private temporary file.
3. Node.js executes the separately built bundle in a child process.
4. The app and host exchange bounded JSON-lines messages over stdin/stdout.
5. User prompts travel as typed events; passwords and OTPs are not retained.
6. C++ validates and normalizes the result before it reaches application
   models.
7. Returned plugin state is stored in the operating-system credential store.
8. Successful runs record a local timestamp and non-secret stable preferences,
   letting a compatible session-based bundle refresh on a timer while the app
   is running.

The host does not sandbox user-selected external JavaScript. An empty allowlist
preserves its existing network behavior; this is intentional and visible in
the UI because installing an external bundle is an explicit arbitrary-code
trust decision. Built-in bundles always receive a non-empty exact-origin
allowlist. Neither GPL plugin source nor generated ZenPlugins bundles are part
of the MIT distribution.
