# Architecture

This describes the desktop implementation. Connection setup and user-visible
limits are in [Bank connections](bank-connections.md); trust boundaries are in
[Security design](security-design.md).

## Boundaries

WealthTracker is a Qt Quick desktop application built with C++23 named modules.
Headers are retained for Qt `moc`, platform ABIs, and third-party C APIs.

| Layer | Responsibility |
| --- | --- |
| `src/core` | Exact money values, categorization, and analytics |
| `src/storage` | SQLCipher connections, migrations, import transactions, and repositories |
| `src/platform` | Secure buffers, key wrapping, and operating-system credentials |
| `src/connectors` | File parsing, network policy, bank clients, normalization, and deduplication |
| `src/app` | Synchronization, reporting, cross-bank transfer matching, WebView lifecycles, plugin execution, and Qt models |
| `ui/qml` | Pages and reusable visual components |

The QML root receives one explicit `app` property. The controller exposes commands,
status, and financial models, without a database handle or stored credential
object. Tokens and plugin answers entered in QML exist in their input controls
until submission; the fields are then cleared.

## Synchronization flows

All successful flows produce a `NormalizedBatch`. The controller replaces the
connection's in-memory batch and rebuilds the Qt models only after completion.
A failed refresh leaves the previous batch visible. Replacing a token clears
that connection's old batch and cancels its pending synchronization.

| Connection | Coordinator and data path |
| --- | --- |
| Retail T-Bank | `TBankLoginCoordinator` validates and stores a session; `BankSyncCoordinator` fetches accounts and balances with history disabled |
| Retail Alfa-Bank | `AlfaLoginCoordinator` projects accounts and balances from known dashboard bootstrap state in its WebView |
| Retail VTB | `VtbLoginCoordinator` uses the authenticated page's own client for active products and an optional balance supplement |
| T-Invest | `InvestmentSyncCoordinator` lists accounts, then requests one RUB-valued portfolio per account serially |
| Alfa B2C, T-Bank Business, Modulbank | `BankSyncCoordinator` discovers accounts and retrieves bounded operation pages or statements |
| External bundle | `ZenPluginRunner` validates child-process output before publishing a batch |

Native requests load credentials from the operating-system store, construct an
exact-host request, and clear temporary credential bytes. `BankSyncCoordinator`
bounds streamed responses to 20 MiB and supports at most four requests in flight.
History remains enabled for the three token-based banking APIs; it is disabled
for retail T-Bank. The desktop T-Invest path requests accounts and portfolios
only, even though its connector library also implements operation read methods.

Sources: [controller](../src/app/AppController.cpp),
[bank synchronization](../src/app/bank_sync.cppm),
[investment synchronization](../src/app/investment_sync.cppm).

## Retail WebViews

T-Bank's official page handles credentials and MFA. A fixed resource-timing
expression retains the eight most recently observed unique session candidates
from exact T-Bank API URLs. C++ accepts a candidate only after `ping` confirms
`CLIENT` access, then stores it in the OS credential backend. Balance refresh
calls `accounts_light_ib`; it never requests operation history.

Alfa keeps its session in the system WebView. A fixed expression projects known
`initialState` or inline `runApp` dashboard data into account fields. Refresh
reloads the dashboard. The coordinator detects the bank's certificate landing
page, bounds dashboard loading to 30 seconds once observed, and caps a refresh
at 45 seconds. No history observer or history navigation is used.

VTB keeps its session in the system WebView and obtains the frontend's exported
`./HttpClient`. It reads active products and an optional financial summary,
retrying a loading portfolio at most twice. Refresh resets the app's page-local
projection state and reuses the authenticated client without reloading or
navigating to history. It is capped at 45 seconds.

Browser-owned Alfa and VTB session material stays inside their WebViews.
Native view cleanup is deferred out of loading and JavaScript callbacks.
Removing a connection or enabling the privacy screen cancels active work;
successful browser sessions are retained for subsequent refreshes while the
app is running. A saved connected flag does not establish a live session after
restart. Retail refresh timers use a six-hour interval.

Sources: [T-Bank login](../src/app/tbank_login.cppm),
[Alfa login](../src/app/alfa_login.cppm), [Alfa projection](../src/app/alfa_browser.cppm),
[VTB login](../src/app/vtb_login.cppm), [VTB projection](../src/app/vtb_browser.cppm).

## Models and persistence

Reporting defaults to the latest month through today. Calendar filters apply
to operations; current account balances, net worth, and portfolio value are
not filtered by that period. Aggregate dashboard and portfolio totals use RUB;
foreign-currency balances remain visible without implicit conversion.

Cross-bank transfer matching is deterministic and one-to-one: opposite amounts
must have the same currency, belong to different connectors, and occur within
three days. Explicit bank markers are preserved. Matched transfers remain in
the operations list but are excluded from cash-flow totals and categories.
Balance-only retail and investment connections do not supply cash-flow history.

T-Invest portfolio value uses API portfolio totals. Holding rows use instrument
identifiers and quantity times current price in the instrument's currency.
Instrument names, allocation, returns, dividends, and XIRR are not calculated.

Financial batches are not persisted by the controller. Settings and credentials
are stored separately; the SQLCipher repository and key-wrapping components are
independently tested libraries awaiting desktop integration. Statement import
runs a parser preview and does not publish or commit imported records.

## External JavaScript plugins

A selected bundle runs under Node.js through a private temporary copy of the
compatibility host. C++ validates paths, preferences, bounded JSON-lines messages,
prompts, state, and normalized output. Reusable state uses a bundle-specific
credential entry; non-sensitive preferences use local settings. The child has
the current user's file and network permissions. Process separation does not
sandbox the bundle.
