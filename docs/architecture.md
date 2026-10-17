# Architecture

WealthTracker uses C++23 named modules and Qt Quick. Headers remain at Qt `moc`,
platform ABI, and third-party C API boundaries.

## Code ownership

| Directory | Implemented components |
| --- | --- |
| `src/core` | Money values, categorization, and analytics |
| `src/storage` | SQLCipher connections, migrations, and repositories |
| `src/platform` | Secure buffers, key wrapping, and OS credential storage |
| `src/connectors` | Statement parsers, bank clients, network policy, normalization, and deduplication |
| `src/app` | Controller, synchronization, reporting, transfer matching, WebViews, plugin runner, and Qt models |
| `include/app` | Qt `moc` boundary headers |
| `ui/qml` | Pages and reusable UI components |
| `tests` | Catch2 tests and synthetic fixtures |

## Runtime data flow

The QML root receives an explicit `app` property backed by
[AppController](../src/app/AppController.cpp). The controller owns connection
state and financial models; it exposes commands, model roles, and status text.

Each completed synchronization produces a `NormalizedBatch`. The controller
replaces that connection's in-memory batch and rebuilds the models. A failed
refresh preserves the previous snapshot. Replacing a token cancels pending
work and clears that connection's old data.

| Integration | Runtime path |
| --- | --- |
| Retail T-Bank | `TBankLoginCoordinator` validates a session; `BankSyncCoordinator` reads accounts and balances |
| Retail Alfa-Bank | `AlfaLoginCoordinator` projects accounts and balances from dashboard bootstrap data |
| Retail VTB | `VtbLoginCoordinator` uses the authenticated page client to read products and balances |
| T-Invest | `InvestmentSyncCoordinator` lists accounts and reads one RUB-valued portfolio per account |
| Alfa B2C, T-Bank Business, Modulbank | `BankSyncCoordinator` reads accounts and bounded operation pages or statements |
| External bundle | `ZenPluginRunner` validates process output and publishes a normalized batch |

Alfa and VTB have separate browser adapters because their page contracts differ.
Both parse their projection once and use the same
[account validator](../src/connectors/retail_accounts.cpp). Their browser
sessions use persistent browser storage on macOS. T-Bank preserves its browser data
as well as the separately stored API session. Startup restores retail
connections sequentially so busy checks do not discard another bank's refresh.
VTB gives a new page client a bounded startup authentication-restoration window
before requiring login. Native view cleanup is deferred out of loading and
JavaScript callbacks.

Sources: [bank coordinator](../src/app/bank_sync.cppm),
[investment coordinator](../src/app/investment_sync.cppm),
[plugin runner](../src/app/zen_plugin_runner.cppm).

## Reporting and local state

[Reporting](../src/app/reporting.cppm) filters operations by an inclusive date
range, initially the latest month through today. Current balances and portfolio
values are independent of that range. Aggregate totals use RUB.

[Transfer matching](../src/app/transfer_matching.cppm) preserves explicit bank
markers and pairs opposite amounts in the same currency across different
connections within three days. Matches are deterministic and one-to-one;
ambiguous batches remain unclassified. Transfers stay visible in the operations
list and are excluded from cash-flow and category totals.

T-Invest portfolio value uses the API total. Holding rows use instrument
identifiers and quantity times current price in the instrument's currency.

Financial batches remain in process memory. The desktop controller does not use
the SQLCipher repository. QSettings stores connection flags, refresh times,
and non-sensitive plugin preferences; the OS credential backend stores tokens,
the T-Bank API session, and reusable plugin state.

The statement-checking command selects a parser and reports sample operation
and warning counts. It does not publish the parsed records to the models.
