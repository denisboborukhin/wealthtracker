# Security design

For vulnerability reporting, see [Security policy](../SECURITY.md). This document
describes implemented controls and their limits, rather than an audit guarantee.

## Assets and trust

Sensitive assets include bank tokens, the retail T-Bank API session, Alfa and
VTB browser sessions, external plugin state, and normalized financial records.
The OS, running binary, and each bank's own login page are trusted. A compromised
bank page or arbitrary code running as the same user is outside this boundary.

## Credentials and stored data

macOS uses Keychain with `WhenUnlockedThisDeviceOnly` accessibility. Windows
uses Credential Manager. Linux has no supported secure credential backend, so
connections requiring stored tokens or API sessions fail closed there.

Temporary credential byte arrays are cleared after request construction. This
does not guarantee erasure of all copies held by Qt, the browser, or the OS.
Persisted credentials are not exposed as QML properties or status text. Tokens
and plugin answers typed in QML exist temporarily in input controls, which are
cleared after submission. Removing a token connection deletes its credential
entry; replacing its token cancels pending work and clears the old snapshot.

Alfa and VTB browser sessions remain owned by the system WebView. The application
does not extract their cookies, authorization headers, or browser storage.
Disconnecting requests cookie deletion and deferred WebView destruction.

Normalized financial batches currently remain in process memory. SQLCipher and
Argon2id components are tested separately and are not an active desktop vault.
Local settings retain connection flags, refresh times, and non-sensitive plugin
preferences. Credential storage does not encrypt the in-memory financial models.

Sources: [credential backend](../src/platform/credential_store.cpp),
[controller](../src/app/AppController.cpp).

## Native network policy

Native connectors use exact HTTPS host allowlists, reject URL user information
and non-default ports, and use manual redirects. Request APIs expose fixed read
methods; there is no payment, transfer, or trading implementation. Users should
supply read-only tokens; the app does not independently audit their bank-side
permissions.

The executable initializes and selects Qt's OpenSSL backend. Install rules stage
the shared OpenSSL runtime. System WebViews use their platform TLS handling.
Neither path disables certificate or hostname verification.

`BankSyncCoordinator` bounds streamed responses to 20 MiB, accounts to 256,
concurrency to four, pages to 250 per account and 1,000 per run, and transactions
to 250,000. HTTP 401/403, throttling, server errors, and transport errors are
classified separately. The first failure aborts remaining requests. Parsers
validate schema and money; depending on the parser, malformed or duplicate rows
may produce warnings and be skipped rather than failing the entire batch.

Sources: [host policy](../src/connectors/network_policy.cpp),
[bank coordinator](../src/app/bank_sync.cppm), [startup](../src/app/main.cpp).

## Retail T-Bank

A coordinator-owned WebView handles login and MFA. Local storage and file access
are disabled, and cookies are cleared around each login attempt. The entry origin
is exact `https://www.tbank.ru`; subsequent top-level navigation also permits
exact `https://id.tbank.ru` and `https://id.tinkoff.ru`. Other subdomains, resource
hosts, URL user information, non-default ports, and HTTP are rejected.

A fixed expression reads resource-timing URL names and returns at most eight
bounded API-session candidates. C++ validates the projection and accepts only a
candidate whose cookie-disabled, redirect-disabled `ping` confirms `CLIENT`
access. Validation uses HTTP/1.1, a 15-second transfer timeout, and a 64 KiB
response limit. QML receives the native window and status, never the candidates.

The desktop synchronization path uses `accounts_light_ib` for accounts and
balances only. The connector library retains operation-reading code, but the
controller disables history. HTTP authentication rejection expires the stored
session. Failed refreshes retain the previous balance snapshot, which is stale
data rather than proof of an authenticated session. This retail API is
undocumented and can stop working after a bank-side change.

Sources: [login](../src/app/tbank_login.cppm),
[session projection](../src/app/tbank_browser.cppm),
[retail connector](../src/connectors/tbank_retail_client.cpp).

## Retail Alfa-Bank

The official phone/SMS page runs in a separate system WebView with local storage
and file access disabled. Certificate failures remain blocking; the UI links
to the fixed `https://alfa.bank/cert` help URL without installing certificates,
changing system trust, or accepting an invalid chain. Load failures use fixed
classifications and allowlisted hostnames rather than raw URLs or renderer text.
The bank's certificate landing page is detected through its root path, absence
of scripts/forms, and exact certificate-help link.

A fixed expression checks exact `https://web.alfabank.ru` and projects only known
global or inline dashboard bootstrap objects. Inline scanning considers at most
64 scripts, each limited to 8 MiB, with a 16 MiB aggregate scan budget. The script
does not read login inputs, rendered text, cookies, or browser storage. It emits
at most 256 accounts with identifiers, bounded names, currencies, masked numbers,
and integer minor-unit balances. It emits no transactions and does not navigate
to history or intercept requests. C++ revalidates the projection with an 8 MiB
UTF-8 limit. A changed schema or unsupported dashboard state stops the refresh.

Sources: [coordinator](../src/app/alfa_login.cppm),
[projection](../src/app/alfa_browser.cppm),
[parser](../src/connectors/alfa_retail_client.cpp).

## Retail VTB

The official page runs in a coordinator-owned WebView with local storage and
file access disabled. A fixed expression validates exact `https://online.vtb.ru`
and obtains the page's exported `./HttpClient`. It invokes only the active
portfolio and financial-summary GET routes listed in
[Bank connections](bank-connections.md#retail-vtb). The optional balance
supplement may fail without discarding a valid portfolio; 401/403 remain blocking.

Responses are bounded to 8 MiB page-locally. A separate projection emits at most
256 account records, masks card numbers, and prefers financial-summary balances
where available. It emits no transactions, reads no private headers, and does
not inspect login fields, cookies, resource timing, or browser storage. C++
revalidates the 8 MiB projection and integer minor-unit balances.

Refresh reuses the authenticated page client without a reload. A stale SPA
pathname is not treated as evidence of authentication; page-client 401/403 marks
the connection unauthenticated. Compatibility diagnostics contain only fixed
stage codes and no raw bank response. The frontend contract is undocumented;
missing exports, unexpected origins, and schema changes stop synchronization.

Sources: [coordinator](../src/app/vtb_login.cppm),
[projection](../src/app/vtb_browser.cppm),
[parser](../src/connectors/vtb_retail_client.cpp).

## T-Invest

The desktop coordinator calls only account listing and portfolio reads at exact
`invest-public-api.tbank.ru`. Requests are serial, limited to 20 MiB per response,
256 accounts, 20,000 positions, and one minute per run. Tokens are bounded before
requests; API error envelopes and invalid portfolio totals are rejected. Failed
refreshes preserve the last complete snapshot. Locking, disconnecting, or
replacing the token cancels pending work and prevents late publication.

Sources: [coordinator](../src/app/investment_sync.cppm),
[client](../src/connectors/t_invest_client.cpp).

## External plugins and privacy screen

The compatibility host runs from an owner-only temporary file under Node.js with
a reduced environment and bounded JSON-lines communication. Standard error is
drained without being displayed, and preference values are redacted from errors.
No official connector token is passed to the child. These controls validate the
application boundary; a selected bundle still has the user's file and network
permissions and can execute arbitrary code.

The privacy screen cancels active login, synchronization, and plugin work before
covering the UI. Existing models remain in memory. Unlocking the screen does not
require authentication. It protects casual viewing, without providing memory
encryption or protection against debugging, malware, or screenshots.

Source: [plugin runner](../src/app/zen_plugin_runner.cppm).
