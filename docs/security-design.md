# Security design

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

Financial snapshots remain in process memory. QSettings stores connection
flags, refresh times, and non-sensitive plugin preferences. The privacy screen
cancels active login, synchronization, and plugin work before covering the UI.
Unlocking requires no authentication and does not erase the financial models.

Sources: [credential store](../src/platform/credential_store.cpp),
[credential bundle](../src/platform/credential_vault.cpp),
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

HTTP authentication, throttling, server, and transport failures are classified
separately. Failed synchronization stops outstanding work and preserves the
previous complete snapshot. Parsers reject invalid schema and monetary values;
some parsers skip malformed or duplicate rows with warnings.

Sources: [host policy](../src/connectors/network_policy.cpp),
[bank coordinator](../src/app/bank_sync.cppm),
[investment coordinator](../src/app/investment_sync.cppm),
[account validator](../src/connectors/retail_accounts.cpp),
[startup](../src/app/main.cpp).

## Browser adapters

On macOS, bank WebViews enable persistent browser storage. Local-file access
remains disabled on all platforms.
Bank-controlled cookies and browser storage remain managed by WebKit on macOS.
Native destruction is deferred out of loading and JavaScript callbacks.

T-Bank top-level navigation permits exact HTTPS `www.tbank.ru`, `id.tbank.ru`,
and `id.tinkoff.ru` origins. A fixed resource-timing expression returns at most
eight recent API-session candidates. A cookie-disabled, redirect-disabled
`ping` must confirm `CLIENT` access before a session is accepted. Validation
uses HTTP/1.1, a 15-second transfer timeout, and a 64 KiB response limit.
The account client permits only `accounts_light_ib` reads.

Alfa projects known global or inline dashboard bootstrap objects on exact
`https://web.alfabank.ru`. Inline scanning considers at most 64 scripts, with
8 MiB per script and a 16 MiB aggregate budget. Certificate failures remain
blocking; help opens the fixed bank certificate URL without modifying trust.
An authentication-host redirect alone does not prove expiry. The coordinator
waits within its 45-second refresh deadline, then preserves an unresolved
authentication page for the user to inspect or complete.

VTB validates exact `https://online.vtb.ru` and uses the page's exported
`./HttpClient` for the two routes in [Bank connections](bank-connections.md#retail-vtb).
Responses are bounded to 8 MiB page-locally. A new browser client has a ten-second
startup restoration window after its first HTTP 401; persistent rejection then
stops publication and reports unconfirmed access while retaining the saved
connection and WebView. The retained page can be opened without navigation.
HTTP 403 rejects the complete refresh, including denial from the
optional balance supplement, without labelling the session expired. No financial
batch is published during restoration. Compatibility status uses fixed codes.
VTB authentication diagnostics log only an allowlisted request-stage code and
elapsed milliseconds; cookies, headers, storage, URLs, and response bodies are not logged.

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
