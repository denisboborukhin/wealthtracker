# Architecture

WealthTracker is a Qt Quick desktop application built with C++23 named modules.
Headers are retained only where Qt `moc` or a platform ABI requires them.

## Boundaries

- `core`: exact money values, categorization, transfers, and analytics.
- `storage`: SQLCipher connections, migrations, import transactions, and
  repositories.
- `platform`: secure buffers, key wrapping, and operating-system credentials.
- `connectors`: file parsing, network policy, bank clients, normalization, and
  deduplication.
- `app`: synchronization coordinators, the external-plugin runner, retail-bank
  WebView lifecycles, models, and QML commands.
- `ui`: QML pages and reusable visual components.

The QML root receives one explicit `app` property. It has no database handle,
credential object, token, T-Bank session value, or Alfa cookie value. List
delegates declare their roles explicitly so runtime binding errors are caught
by QML lint.

## Runtime data flow

Built-in synchronization is:

1. Load one credential from the operating-system store.
2. Construct a request through the connector's exact-host policy.
3. Clear the temporary credential bytes.
4. Parse a bounded response into `NormalizedBatch`.
5. Follow bounded pagination with at most four requests in flight.
6. Project validated records into in-memory Qt models.

Each connector has at most one coordinator. Removing a connection or enabling
the privacy screen aborts its replies and discards pending work. Retail T-Bank
has separate login and synchronization coordinators so WebView callbacks,
network replies, timers, cancellation, and session erasure have one owner.
Retail Alfa-Bank has one WebView coordinator for both login and snapshot
refresh, so its browser-owned session never crosses into the C++ network stack.

The current desktop controller does not persist normalized financial batches.
The `storage` module is tested independently and is intended for that later
integration. Statement import likewise performs probing and parsing for a
preview but does not commit the result.

## Retail T-Bank

1. A coordinator creates a Qt WebView and disables persistent local storage
   and file access.
2. The official T-Bank login page handles phone, password, and MFA.
3. A fixed expression examines resource-timing URLs only. It returns at most
   eight bounded session candidates from exact
   `https://www.tbank.ru/api/common/v1/` URLs.
4. C++ validates the projection and checks the latest candidate with a
   cookie-disabled, redirect-disabled request. Only `CLIENT` access succeeds.
5. The opaque session is stored in the operating-system credential store.
6. A native connector reads only `accounts_light_ib` and `operations` from the
   exact T-Bank origin and validates the returned records.

The retail API is undocumented. A schema or authentication change fails closed
without expanding the permitted host or introducing write requests.

## Retail Alfa-Bank

1. A coordinator opens the official Alfa Online page in a native Qt WebView.
2. The bank page owns phone/SMS authentication and its browser session.
3. A fixed expression checks the exact Alfa Online origin and converts known
   global or inline dashboard bootstrap state (`initialState` or `runApp`
   `appState`) into bounded account and balance fields.
4. After accounts are available, a one-shot observer follows the bank's normal
   `/history` route and accepts only the successful same-origin
   `/api/v1/operations-history/operations` response. It neither constructs nor
   modifies the bank page's request and restores the browser API immediately.
5. `AlfaRetailClient` validates bounds, exact amounts, identifiers, dates,
   currencies, duplicates, and account ownership before producing a
   `NormalizedBatch`.
6. The WebView session stays browser-owned for periodic dashboard reloads and
   is deleted when the user disconnects the bank.

No cookie, password, OTP, authorization header, or raw page-state object is
passed through QML or persisted by WealthTracker. The projection is an
undocumented compatibility boundary and fails closed when either schema changes.
The coordinator distinguishes the authentication page from a dashboard that is
still loading. If no supported account state appears within 30 seconds after
the dashboard is reached, or the operations response does not complete within
30 seconds, it reports an explicit compatibility error.

## External JavaScript plugins

External bundles use a separate child-process boundary:

1. C++ validates the selected path, identifier, preferences, and input size.
2. A private temporary copy of the MIT compatibility host starts under Node.js
   with a reduced environment.
3. The host and application exchange bounded JSON-lines messages.
4. C++ validates prompts, state, accounts, balances, and transactions before
   updating models.
5. Reusable plugin state is stored under a bundle-specific credential key;
   sensitive prompt values are not persisted.

This is process isolation, not a sandbox. External code retains the current
user's operating-system permissions and unrestricted network behavior.
