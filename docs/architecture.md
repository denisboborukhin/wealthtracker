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
- `app`: synchronization coordinators, reporting periods, cross-bank transfer
  matching, the external-plugin runner, retail-bank WebView lifecycles, models,
  and QML commands.
- `ui`: QML pages and reusable visual components.

The QML root receives one explicit `app` property. It has no database handle,
credential object, token, T-Bank session value, or retail-bank cookie value. List
delegates declare their roles explicitly so runtime binding errors are caught
by QML lint.

## Runtime data flow

Built-in synchronization is:

1. Load one credential from the operating-system store.
2. Construct a request through the connector's exact-host policy.
3. Clear the temporary credential bytes.
4. Parse a bounded response into `NormalizedBatch`.
5. Follow bounded pagination with at most four requests in flight.
6. Pair exact opposite operations across connected banks as own-account
   transfers, while preserving explicit connector markers.
7. Filter operations to the selected inclusive reporting period and project
   the records and RUB cash-flow totals into in-memory Qt models.

The default reporting period is the latest month through the current date.
Current balances and net worth are not filtered by that period. Cross-bank
matching is deterministic and one-to-one: it requires another connector, the
same currency, an exact opposite amount, and at most three days between the two
operations. Matching transfers remain visible but are excluded from income,
spending, savings, and spending-category totals.

Each connector has at most one coordinator. Removing a connection or enabling
the privacy screen aborts its replies and discards pending work. Retail T-Bank
has separate login and synchronization coordinators so WebView callbacks,
network replies, timers, cancellation, and session erasure have one owner.
Retail Alfa-Bank has one WebView coordinator for both login and snapshot
refresh, so its browser-owned session never crosses into the C++ network stack.
Retail VTB follows the same session boundary and uses the authenticated page's
own read client rather than exporting session material.

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
3. A fixed expression checks the exact Alfa Online origin. If the bank serves
   its static certificate-installation landing page instead of the application,
   the coordinator pauses data probing and exposes the certificate-help action.
4. The expression converts known
   global or inline dashboard bootstrap state (`initialState` or `runApp`
   `appState`) into bounded account and balance fields.
5. After accounts are available, a one-shot observer follows the bank's normal
   `/history` route and accepts only the successful same-origin
   `/api/v1/operations-history/operations` response. It neither constructs nor
   modifies the bank page's request and restores the browser API immediately.
6. `AlfaRetailClient` validates bounds, exact amounts, identifiers, dates,
   currencies, duplicates, and account ownership before producing a
   `NormalizedBatch`.
7. The WebView session stays browser-owned for periodic dashboard reloads and
   is deleted when the user disconnects the bank.

No cookie, password, OTP, authorization header, or raw page-state object is
passed through QML or persisted by WealthTracker. The projection is an
undocumented compatibility boundary and fails closed when either schema changes.
The coordinator distinguishes authentication, the bank's certificate landing
page, and a dashboard that is still loading. If no supported account state
appears within 30 seconds after the dashboard is reached, or the operations
response does not complete within 30 seconds, it reports an explicit
compatibility error.

## Retail VTB

1. A coordinator opens `https://online.vtb.ru` in a native Qt WebView with
   local storage and file access disabled.
2. The official page owns phone/SMS authentication and the browser session.
3. A fixed expression verifies the exact origin, obtains the current bank
   frontend's exported `./HttpClient`, and invokes three exact GET routes for
   active products, financial balances, and 90 days of history. It performs at
   most two delayed portfolio retries while VTB reports `loadStatus: LOADING`.
4. Raw responses remain page-local. A second fixed expression maps only bounded
   account and operation fields, associates child cards with their parent
   accounts, and marks VTB's own-account transfer codes as internal transfers.
5. `VtbRetailClient` validates the 8 MiB projection, record limits, exact money,
   identifiers, dates, currencies, duplicates, and account ownership.
6. The browser-owned session is reused for periodic reloads and deleted when the
   user disconnects VTB.

No login field, cookie, authorization header, request body, or browser-storage
value crosses into C++ or QML. The frontend module and routes are undocumented;
missing exports, authentication failure, unexpected origins, and response drift
all fail closed without adding new hosts or methods.

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
