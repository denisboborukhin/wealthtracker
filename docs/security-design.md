# Security design

Manual cashback category names, percentages, and calendar months are saved in
local application settings without encryption. They contain no bank credentials
or bank responses. The privacy screen and demo mode hide these entries and block
changes; they do not encrypt the settings. Automatic VTB cashback uses one exact,
read-only, same-origin route through the page-owned client, with a separate
deadline and bounded name/rate/month projection. The raw response and browser
session never cross into C++ or QML. Validated automatic category projections
are saved in the encrypted balance-history database with each month's observation
time; failed reads preserve the previous snapshot. No category choices are submitted
to a bank.

T-Bank cashback uses only the exact bonuses read POST from its frontend, with
JSON loyalty IDs from validated account discovery. It uses the existing native
session, exact HTTPS origin, manual redirect/cookie policies, bounded responses,
and independent cancellation. No cookie extraction is added. The parser checks
account ownership and explicit category activation before publishing a month.

Alfa cashback invokes only the observed account, promoted-program, and v2
category GET wrappers. Their service paths and GET declarations must match
before invocation. Existing bank authentication/XSRF headers stay inside its
WebView. The verified fetch adapter rejects redirects; no logout or reconnect
is inferred from a failed optional read. A single abort signal and ten-second
deadline bound the work. Rich category titles are parsed in an inert template,
with active/resource markup rejected, and only explicit confirmed categories,
recognized months, and unambiguous rates reach the application model.

This describes implemented controls. Report vulnerabilities using the
[security policy](../SECURITY.md).

## Credentials and local data

API tokens, the T-Bank API session, reusable plugin state, and the balance-history
database key use macOS Keychain or Windows Credential Manager. macOS uses a single
versioned `credential-vault-v1` item under `org.wealthtracker.desktop`, loaded once
into secure process buffers. Its file-based Keychain access is governed by macOS
Keychain ACLs; the configured accessibility attribute does not establish a
data-protection Keychain protection class. Windows keeps its per-entry backend.
Other platforms return an unsupported-backend error. Temporary credential byte
arrays are cleared after request construction;
Qt, browser, and OS copies are outside that clearing operation.

Legacy macOS items migrate lazily: the exact secret bytes are committed to the
bundle before legacy cleanup. The database key is preserved unchanged. Migration
can still require permission for each old item once. Later reads use the process
cache; a rejected or malformed bundle never falls back to empty credentials.
Durable deletion markers prevent an undeleted legacy item from reappearing.
A denied legacy read stops further credential requests for that process. The app
holds a single-instance lock while using the cached bundle to avoid concurrent
writers. Cached secrets are wiped at process exit; the privacy screen covers the
UI and cancels work but does not discard this process cache.

The normal path performs one protected bundle read per launch. The OS controls
actual dialogs: first migration, locked Keychain, changed signing identity, and
credential updates can still require authorization. Debug binaries use ad hoc
signing; relinking changes their code identity. Stable certificate-based signing
is needed to preserve trust across app builds.

Alfa and VTB sessions stay inside system WebViews. The app projects financial
fields without extracting their cookies, authorization headers, or browser
storage. On macOS, browser data persists across launches. Disconnect removes
website data records for only the selected bank's domains and defers native-view
cleanup.
Reconnection is blocked until that asynchronous deletion completes. On macOS,
T-Bank retains its browser data during login, completion, and cancellation.

QML receives validated financial models and status text. Tokens and plugin
answers typed in the interface remain in their controls until submission, when
the controls are cleared. Persisted credentials and raw session projections
are not exposed as model properties.

Balance-history observations are stored in a separate SQLCipher database,
`balance-history.db`, under Qt's `AppDataLocation`. Its random 32-byte key uses
the `balance-history-key` credential entry under `org.wealthtracker.desktop`.
The database retains account identifiers, names, currencies, integer balances,
connection/institution labels, and observation timestamps. Successful captures
are committed atomically; they survive restart and disconnect. Storage requires
SQLCipher and never falls back to a plaintext database. An unavailable or invalid
key does not cause an existing database to be replaced.
Schema version 2 also stores public EVM addresses and their local names in
this encrypted database and permits USDT-valued captures. Migration from version 1
preserves existing captures. Removing a wallet configuration retains its history.
Schema version 3 adds bounded automatic cashback snapshots for the three retail
banks. Its additive, transactional migration preserves captures and wallets.
Disconnect removes the selected bank's automatic cashback snapshot; manual
fallbacks and balance history remain. On restart, the latest complete balances
of still-connected banks populate the overview before synchronization. Capital's
update note uses the oldest contributing bank snapshot so a partial refresh
does not imply every bank was refreshed together.

Other synchronized financial data remains in process memory. QSettings stores
connection flags, refresh times, and non-sensitive plugin preferences. The privacy screen
cancels active login, synchronization, and plugin work before covering the UI.
Unlocking requires no authentication and does not erase the financial models.

Sources: [credential store](../src/platform/credential_store.cpp),
[credential bundle](../src/platform/credential_vault.cpp),
[encrypted database](../src/storage/database.cpp),
[balance-history repository](../src/storage/balance_history.cpp),
[controller](../src/app/AppController.cpp).

## Native requests

Bank clients require exact HTTPS hosts, reject URL user information and custom
ports, use manual redirects, and expose fixed read methods. The executable
initializes Qt's OpenSSL backend; install rules stage its shared runtime.
Certificate and hostname verification remain enabled. WebViews use platform TLS.

| Path | Enforced bounds |
| --- | --- |
| Bank synchronization | 20 MiB per response, 256 accounts, four concurrent requests, 250 pages per account, 1,000 pages and 250,000 transactions per run |
| T-Invest | Serial requests, 20 MiB per response, 256 accounts, 20,000 positions, one minute per run |
| Alfa/VTB financial projection | 8 MiB UTF-8 JSON, 256 accounts, validated integer minor-unit balances |
| EVM wallet | Serial requests, 4 MiB per response, 128 chains, 5,000 tokens per chain, 20,000 total tokens, five minutes per wallet |

HTTP authentication, throttling, server, and transport failures are classified
separately. Failed synchronization stops outstanding work and preserves the
previous complete snapshot. Parsers reject invalid schema and monetary values;
some parsers skip malformed or duplicate rows with warnings.

Sources: [host policy](../src/connectors/network_policy.cpp),
[bank coordinator](../src/app/bank_sync.cppm),
[investment coordinator](../src/app/investment_sync.cppm),
[account validator](../src/connectors/retail_accounts.cpp),
[startup](../src/app/main.cpp).

## Public Ethereum wallets

Wallet tracking reads public EVM addresses through exact HTTPS `api.rabby.io`,
using fixed `/v1/chain/list`, `/v1/user/used_chain_list`, `/v1/user/token`, and
`/v1/user/token_list` routes. Rabby receives the address and network request;
local encryption does not hide the query from the provider. Requests use manual
redirects, the shared TLS policy and 30-second transfer timeout, a five-minute
wallet deadline, sequential requests spaced by 1.1 seconds, and bounded catalog,
response, and token counts. No provider key, cookie, or request signature is sent.

Chain and token identities must match the discovered catalog and requested
network. Token metadata renders as bounded plain text. Exact raw hexadecimal
amounts and decimal quote lexemes avoid floating-point valuation. Missing prices
and suspicious assets are visibly excluded from the subtotal. A missing USDT
quote or failed chain rejects the refresh; previously saved values remain.
The app does not request wallet private keys/seed phrases or submit transactions.

Source: [EVM wallet coordinator and parser](../src/app/crypto_wallet.cppm).
Provider route contract: [Rabby portfolio helper](https://github.com/RabbyHub/Rabby/blob/master/src/ui/utils/portfolio/tokenUtils.ts).

## Browser adapters

On macOS, bank WebViews enable persistent browser storage. Local-file access
remains disabled on all platforms.
Bank-controlled cookies and browser storage remain managed by WebKit on macOS.
Native destruction is deferred out of loading and JavaScript callbacks.
Result-bearing bank JavaScript callbacks are queued onto Qt's application
thread through `runBankJavaScript`, with a receiver lifetime guard before any
timer, network, or model access. Qt 6.11's Android backend invokes its raw
completion on Android's separate UI thread; the native synthetic probe checks
both this boundary and canceled-result handling.

T-Bank top-level navigation permits exact HTTPS `www.tbank.ru`, `id.tbank.ru`,
and `id.tinkoff.ru` origins. A fixed resource-timing expression returns at most
eight recent API-session candidates. A cookie-disabled, redirect-disabled
`ping` must confirm `CLIENT` access before a session is accepted. Validation
uses HTTP/1.1, a 15-second transfer timeout, and a 64 KiB response limit.
The same guarded request policy supplies Android's bank CA to `ping` and account
reads. A separate 15-second timer aborts a stalled verification request; the
interactive ten-minute deadline remains independent of reply completion.
The account client permits only `accounts_light_ib` reads. An authentication
rejection publishes no data and disables refresh for that process until login,
but does not delete the saved session or rewrite the Keychain. Explicit
disconnect still removes it. Retail refreshes have independent ten-second
deadlines and may run concurrently; only one interactive bank login is active
at a time. Interactive verification retains a ten-minute deadline.

Alfa reads the loaded frontend's current financial layout from its existing
global store on exact `https://web.alfabank.ru`, with known global or inline
dashboard bootstrap objects as a fallback when the live store is unavailable.
Live layout data takes precedence over stale bootstrap accounts. Only bounded,
validated account projections leave the page; no new HTTP endpoint is invoked.
The mobile `products` collection is read through the same strict dashboard
product mapper as `allProducts`, preserving the latter's duplicate precedence.
The observed host bootstrap uses cached module `51824`'s `globalStore` and
module `32792`'s financial-layout atom `iZ`; the reader does not initialize module
factories. This contract was verified in the public
[host bootstrap](https://alfaonline.servicecdn.ru/public/newclick-host-ui/assets/static-legacy/js/async/desktop_bootstrap.82fb0d00.js).
Inline scanning considers at most 64 scripts, with
8 MiB per script and a 16 MiB aggregate budget. Certificate failures remain
blocking; help opens the fixed bank certificate URL without modifying trust.
An authentication-host redirect alone does not prove expiry. The coordinator
waits within its ten-second refresh deadline, then preserves an unresolved
authentication page for the user to inspect or complete. Alfa and VTB pages can
also be opened during their own refresh without waiting for the deadline or
navigating away from the current document.

VTB validates exact `https://online.vtb.ru` and uses the page's exported
`./HttpClient` for the two routes in [Bank connections](bank-connections.md#retail-vtb).
If its already-loaded core auth proxy explicitly reports `isAuthorized=false`,
the reader waits without constructing the private client or issuing portfolio
requests. Only that boolean is read; token properties and module factories are
not inspected or initialized. Unknown proxy availability retains server-side
access verification, and account responses must still pass strict validation.
Responses are bounded to 8 MiB page-locally. A new browser client may restore access within the total ten-second
refresh deadline after its first HTTP 401; persistent rejection then
stops publication and reports unconfirmed access while retaining the saved
connection and WebView. The retained page can be opened without navigation.
HTTP 403 rejects the complete refresh, including denial from the
optional balance supplement, without labelling the session expired. No financial
batch is published during restoration. Compatibility status uses fixed codes.
VTB authentication diagnostics log only an allowlisted request-stage code and
elapsed milliseconds; cookies, headers, storage, URLs, and response bodies are not logged.
Frontend availability, pending requests and API-session discovery do not prove
authentication. Alfa and VTB report success only after validated financial data;
interactive login is not stopped by the shorter dashboard-loading deadline.

Sources: [T-Bank login](../src/app/tbank_login.cppm),
[Alfa adapter](../src/app/alfa_browser.cppm),
[Alfa login](../src/app/alfa_login.cppm),
[VTB adapter](../src/app/vtb_browser.cppm),
[VTB login](../src/app/vtb_login.cppm).

## External plugins

The Node.js host runs from a private temporary copy with a reduced environment
and bounded JSON-lines messages. C++ validates bundle paths, preferences,
prompts, reusable state, and normalized output. Standard error is drained
without display; preference values are redacted from errors. Built-in bank
tokens are not passed to the child.

The child retains the current user's file and network permissions. These
controls do not sandbox a selected bundle. The OS, local binary, and bank pages
are trusted; code running as the same user can access process memory.

Source: [plugin runner](../src/app/zen_plugin_runner.cppm).
