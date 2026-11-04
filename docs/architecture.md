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
| `src/app` | Controller, synchronization, Ethereum wallet valuation, history projection, reporting, transfer matching, WebViews, plugin runner, and Qt models |
| `include/app` | Qt `moc` boundary headers |
| `ui/qml` | Pages and reusable UI components |
| `tests` | Catch2 tests and synthetic fixtures |

## Runtime data flow

The QML root receives an explicit `app` property backed by
[AppController](../src/app/AppController.cpp). The controller owns connection
state and financial models; it exposes commands, model roles, and status text.

Each completed bank or plugin synchronization produces a `NormalizedBatch`. The controller
replaces that connection's in-memory batch and rebuilds the models. A failed
refresh preserves the previous snapshot. Successful connections and refreshes
also append a complete balance capture to local history. Replacing a token
cancels pending work and clears that connection's current in-memory data;
recorded history remains available.

| Integration | Runtime path |
| --- | --- |
| Retail T-Bank | `TBankLoginCoordinator` validates a session; `BankSyncCoordinator` reads accounts and balances |
| Retail Alfa-Bank | `AlfaLoginCoordinator` projects accounts and balances from dashboard bootstrap data |
| Ozon Bank | `OzonLoginCoordinator` reads main/savings balances and installment debt; cashback uses its loyalty context |
| Retail VTB | `VtbLoginCoordinator` uses the authenticated page client to read products and balances |
| T-Invest | `InvestmentSyncCoordinator` lists accounts and reads one RUB-valued portfolio per account |
| Alfa B2C, T-Bank Business, Modulbank | `BankSyncCoordinator` reads accounts and bounded operation pages or statements |
| External bundle | `ZenPluginRunner` validates process output and publishes a normalized batch |
| EVM wallet | `CryptoWalletSync` discovers Rabby-indexed networks and reads their native coins/tokens and a USDT/USD quote; the controller saves the valued subtotal to history |

Alfa and VTB have separate browser adapters because their page contracts differ.
Both parse their projection once and use the same
[account validator](../src/connectors/retail_accounts.cpp). Their browser
sessions use persistent browser storage on macOS. T-Bank preserves its browser data
as well as the separately stored API session. Startup restores retail
connections concurrently using their separate coordinators. Each retail refresh
has a ten-second deadline; timeout preserves the browser page and saved credentials.
Interactive verification keeps a separate ten-minute deadline and one active
login at a time. Other banks' background refreshes do not block opening a bank.
Periodic refresh collisions and privacy-screen blocking are retried after ten seconds.
VTB gives a new page client a bounded startup authentication-restoration window
before reporting access as unconfirmed; it retains the connection and page for
inspection. Alfa allows authentication redirects to finish within its refresh
deadline and also preserves an unresolved authentication page. Opening either
bank reuses the retained page. Native view cleanup is deferred out of loading and
JavaScript callbacks.

Sources: [bank coordinator](../src/app/bank_sync.cppm),
[investment coordinator](../src/app/investment_sync.cppm),
[plugin runner](../src/app/zen_plugin_runner.cppm).

Public EVM wallets have a separate serial refresh queue and a six-hour timer.
Saved wallets refresh at startup, on manual refresh, and after adding an address.
No provider registration or API key is required. Demo mode and the privacy screen
block refreshes. [CryptoWalletSync](../src/app/crypto_wallet.cppm) reads Rabby's
chain catalog and used-chain list, then explicitly fetches every used chain's
native and ERC-20 holdings. Whole-array token responses are bounded; any failed
chain rejects the combined refresh and preserves the previous snapshot.

Raw hexadecimal token amounts use exact integers. Numeric quote lexemes are
preserved as decimal strings; quantity/quote products accumulate as scaled
integers before conversion through the actual USDT/USD quote. Only final displayed
amounts are rounded to hundredths. Unpriced and suspicious assets remain visible
and are excluded from the valuation. See [Crypto wallets](crypto-wallets.md) for
provider coverage and availability limits.

## Reporting and local state

The [cashback book](../src/app/cashback.cppm) validates manual bank/month/category
selections and stores percentages as integer basis points. The controller saves
its bounded versioned JSON in `cashback/categories` application settings, not
the balance-history database. It exposes only this month and next month and
notifies the cashback view from the existing retail connection signals.
[CashbackSync](../src/app/cashback_sync.cppm) runs the verified VTB read after
successful balance publication. Its independent deadline, page-owned abort
signal, and per-request ID prevent failures or late callbacks from replacing
a prior result or a newer request. [TBankCashbackSync](../src/app/tbank_cashback.cppm)
reuses validated account loyalty IDs and makes the bank frontend's exact bonuses
read through the existing native session. Each automatic month has explicit
coverage, selection state, and an observation timestamp. Partial refreshes
replace only covered months; never-fetched months use manual fallbacks.
Automatic snapshots persist in the encrypted balance-history store (schema v4),
while manual selections retain their original settings format.

[Alfa cashback](../src/app/alfa_cashback.cppm) loads the bank-owned loyalty
reader module without mounting its UI. It discovers monthly baskets through
the existing account/program GET methods and uses the confirmed category
sections. Each wrapper's declared GET and service path are checked before
invocation; its authentication headers remain in the page. The shared
`CashbackSync` coordinator handles Alfa and VTB origin checks, cancellation,
timeouts, and normalized month projections.

[Reporting](../src/app/reporting.cppm) filters operations by an inclusive date
range, initially the latest month through today. Current balances and portfolio
values are independent of that range. Aggregate totals use RUB.

[Transfer matching](../src/app/transfer_matching.cppm) preserves explicit bank
markers and pairs opposite amounts in the same currency across different
connections within three days. Matches are deterministic and one-to-one;
ambiguous batches remain unclassified. Transfers stay visible in the operations
list and are excluded from cash-flow and category totals.

T-Invest portfolio value uses the API total. Holding rows use validated portfolio tickers and instrument reference names,
with quantity times current price in the instrument's currency.

Current financial batches remain in process memory. Balance history is stored
separately in `balance-history.db` under Qt's `AppDataLocation`, using SQLCipher.
The database key is stored as `balance-history-key` in the OS credential backend.
QSettings stores connection flags, refresh times, and non-sensitive plugin
preferences; the credential backend also stores tokens, the T-Bank API session,
and reusable plugin state.

On macOS the credential factory wraps the system store in a single-item vault.
It reads `credential-vault-v1` once, caches values in `SecureBuffer`, and preserves
legacy bytes before deleting old entries. Explicit deletion persists a tombstone
before cleanup. Startup holds an application-instance lock for the cached
bundle's lifetime; Windows retains separate Credential Manager entries.

[BalanceHistoryRepository](../src/storage/balance_history.cppm) stores each
complete connection capture atomically, including an empty account set. Account
identity includes the connection and currency, and amounts remain integer minor
units. Schema version 2 adds encrypted wallet-address/name configuration and
permits USDT-valued snapshots, migrating version 1 captures within a transaction.
Versions 3 and 4 add persisted cashback snapshots, including Ozon.
This database has its own schema version and does not modify the ledger
migrations. Captures survive restart and disconnect; removing a wallet deletes
its tracking configuration but retains its captures. Failed refreshes and demo
data do not add observations.

The [history projection](../src/app/balance_history_view.cppm) plots individual
accounts or totals in one currency at a time without FX conversion. A total
combines each connection's latest known account set at that observation time;
an empty or changed capture replaces that connection's previous set. Banks
refresh independently, so totals can mix observation times and continue to
include the last recorded balance of a disconnected bank. These are balance
observations, not investment-performance or return calculations.

The **История** page filters connections and local calendar dates, reports
the selected currency's total and change, and a stacked area for each connection
(bank or EVM wallet). Source, currency, and date controls live in **Настроить**.
Aggregate history uses the latest complete snapshot for each local calendar
day and carries each account forward on days without updates, through the
selected end date or today. Multiple same-day updates use the latest values.
The observation counter still counts real captures. Accounts are netted within
each connection before stacking positive connection balances above zero and
negative balances below it. A separate line shows the net total. Date tooltips
list signed connection amounts and percentage shares; when negative balances
are present, shares use the sum of absolute connection balances. Monetary sums
and share rounding use exact integer arithmetic. Selecting no connections
produces no totals. When an
earlier observation exists, the projection carries it to the start of the
period while retaining its original observation timestamp; otherwise it identifies
the first available observation. Horizontal coordinates follow actual timestamps.
Changes also reflect the selected account set, deposits, withdrawals, and price
movements, so they are not investment returns.

Crypto history stores each wallet's valued subtotal as a USDT balance under
`ethereum:<address>` (the retained identifier preserves earlier Ethereum captures
when tracking expands to EVM networks). The last saved subtotal can be shown after restart; token
quantities and price-availability details require a fresh request. History does
not restore transaction or holding models after restart.

The statement-checking command selects a parser and reports sample operation
and warning counts. It does not publish the parsed records to the models.

Ledger import (`ImportSession`, `LedgerRepository`) and core categorization/XIRR
are implemented library capabilities with tests but no current application callers.
They are retained pending a product decision on persistent transaction import.

## Extracted presentation and state policies

`CashbackState` owns automatic selections, observation timestamps and status per
bank. Failed reads update status without replacing prior selections or their
observation time. The controller continues to own transport commands and database
access. `ConnectionState` is the typed input to the shared connection presentation
policy; saved access material remains distinct from a validated financial read.
The shared scheduling policy preserves six-hour refreshes and a ten-second minimum
retry. Individual coordinators still own their bank's active operations.

The connection list is rendered by `ConnectionList.qml`; `RetailConnection.qml`
adapts only the four supported retail IDs to existing controller properties and
commands. Unknown IDs have no retail commands. Native view ownership and deferred
cleanup remain with the existing login dialog/coordinators.

`HistoryStore` owns the SQLCipher connection, capture collection, saved wallet
configuration and observation times. The controller uses read-only views and
explicit save/remove/append methods, and retains presentation notifications.
Initialization preserves the existing key name, schema and restoration order;
missing keys never replace an existing database. Wallet history failures do not
change the bank history error. `CredentialBackend.hpp` is the shared credential
boundary used by the controller and store. `BankTokenEditor.qml` owns token entry,
password masking, save eligibility and clearing after save/dialog closure.

`WalletService` owns current wallet rows, refresh queue, failure counts and request
coordinators; `HistoryStore` retains persisted configuration and captures. Privacy
and demo guards stay in the controller, including rechecking after save signals.
`projectFinance` builds rows and formatted totals from explicit batches, metadata,
reporting dates and today's date; it performs no network or model mutation.
`PluginService` owns the selected descriptor, host runner, prompt correlation,
private startup message and credential-state persistence. The facade retains
presentation signals, published batches and scheduled refresh policy.

The application and native `qml_tests` runner link the same static WealthTracker
QML module/plugin. Qt Quick tests therefore instantiate compiled application
resources directly, without modifying copies of QML source. The existing
`connection_pages_ui` CTest command and `wealthtracker_qmllint` target remain.
