# Bank connections

All built-in connectors expose financial data to WealthTracker as read-only.
API tokens and the retail T-Bank session use separate entries in macOS Keychain
or Windows Credential Manager. Alfa Online and VTB Online keep their browser
sessions inside the operating-system WebView; WealthTracker does not extract
them. Synchronized
financial records currently remain in memory and disappear when the app exits.

## Support matrix

| Connection | Accounts | Balances | Operations | Authentication |
| --- | --- | --- | --- | --- |
| Retail T-Bank | Yes | Yes | Windowed two-year request | Embedded official login |
| Retail Alfa-Bank | Yes | Yes | Bounded two-year history; partial state explicit | Embedded official login |
| Retail VTB | Yes | Yes | Bounded two-year history; partial state explicit | Embedded official login |
| Alfa-Bank B2C API | Yes | Yes | Yes | Issued integration token |
| T-Bank Business | Yes | Yes | Yes | T-API token |
| Modulbank | Yes | Yes | Yes | API token or public sandbox token |
| T-Invest | Access check | No | No | User-created read-only token |
| External ZenPlugins bundle | Plugin-dependent | Plugin-dependent | Plugin-dependent | Plugin-defined |
| Retail Sber | No | No | No | Adapter not implemented |

Bank operations are normalized locally. Dashboard totals include RUB values
only; foreign-currency accounts stay visible without an implicit conversion.

## Local cash-flow reporting

Income, spending, savings, categories, and the operations list use the inclusive
date range selected with the two calendar controls. The default range is the
latest month through today. Savings are an amount in rubles: RUB income minus
RUB spending. Built-in retail synchronization targets the latest two years
independently of this selection. The calendar filters operations already in
memory; it does not start an older on-demand bank request.

A bank's explicit own-account transfer marker is always honored. WealthTracker
also pairs an outgoing operation with one incoming operation from another
connected bank when currency and absolute amount match exactly and their
posting times are no more than three days apart. The closest deterministic
one-to-one match wins. Both operations remain visible under **Внутренний
перевод**, but neither affects income, spending, savings, or spending-category
totals. Same-bank operations without a bank marker are not inferred, which
avoids guessing when only one side of a transfer was loaded. Highly ambiguous
batches are also left unclassified instead of making an unsafe guess.

## Retail T-Bank

Open **Подключения → Т-Банк → Войти** and complete the official page inside
the dialog. No Node.js installation is required.

The WebView handles all login fields. Persistent local storage and file access
are disabled, and cookies are deleted before and after the attempt. A fixed
page expression returns only bounded session identifiers observed in exact
T-Bank API resource URLs; it does not read cookies, DOM fields, headers,
request bodies, or local storage. C++ accepts a candidate only after `ping`
reports `CLIENT` access.

Login starts only at exact `https://www.tbank.ru`. Top-level navigation is
limited to that origin and the exact T-ID origins `https://id.tbank.ru` and
`https://id.tinkoff.ru`; subdomains, CDN hosts, user information, non-default
ports, and plain HTTP are rejected.

The native connector permits `GET` requests to the exact origin
`https://www.tbank.ru` and these paths only:

- `/api/common/v1/accounts_light_ib`
- `/api/common/v1/operations`

The login validator also calls `/api/common/v1/ping`. Cookies and redirects are
disabled for these C++ requests. The session is removed only when the bank
reports an authentication failure; a transport or history rejection keeps the
already validated connection available for retry. Successful connections
refresh every six hours while the application is running when auto-update is
enabled.

Each synchronization freezes one UTC two-year range and sends one observed
`GET` request per account with that exact start and end. `NO_DATA_FOUND` means
an empty account history, not a failed account. Malformed individual operations
are skipped with a visible count while valid records are retained. The
undocumented response has no cursor or terminal marker, so the application
labels the requested period but does not claim that the bank returned a
complete statement. It does not invent window or pagination requests that the
bank may reject.
Operations marked `isInner` by T-Bank appear under **Внутренний перевод** and
do not contribute to income, spending, savings, or spending-category totals.

These retail endpoints are undocumented and unsupported by T-Bank as a public
API. A bank-side change can break login or synchronization. WealthTracker does
not fall back to certificate bypasses, cookie export, direct password APIs, or
write endpoints.

## Retail Alfa-Bank

Open **Подключения → Альфа-Банк → Войти** and complete the official Alfa Online
phone/SMS page inside the dialog. The page owns the phone number, password when
requested, one-time code, TLS connection, and browser cookies. QML and C++ do
not receive those values.

Alfa Online uses certificates rooted at the Russian Trusted Root CA. A default
macOS installation may not trust that root, in which case the WebView blocks the
page before login. WealthTracker identifies the certificate failure and shows
**Как установить сертификаты**, which opens
[Alfa-Bank's official instructions](https://alfa.bank/cert). Installing a
system trust anchor is an explicit operating-system security decision: the app
does not download or install the certificate, accept the failed certificate,
or disable TLS checks. After following the official instructions, restart
WealthTracker and retry the connection.

Alfa may complete phone/SMS authentication and then replace the dashboard with
its static certificate-installation page. WealthTracker recognizes that exact
same-origin page without reading its displayed text and reports the certificate
requirement immediately instead of mislabeling it as an account-schema failure.

Alfa Online does not expose a public retail account API comparable to its
partner B2C product. The built-in connector therefore keeps the authenticated
page in the operating-system WebView and runs a fixed projection expression.
The expression reads only known global state or the JSON argument of the bank's
inline `window.runApp({ appState: ... })`/`initialState` dashboard bootstrap for
accounts and balances. It then installs a short-lived observer while Alfa's
normal `/history` page loads. The observer accepts only successful responses
from the exact same-origin `/api/v1/operations-history/operations` path. For
that POST only, it parses a body of at most 64 KiB, rewrites recognized existing
date fields to the latest two years, resets an existing page or offset field,
and raises an existing page-size field to 100. It then follows serial page-local
POST continuations on the same path. Unrelated filters stay unchanged, and only
`Content-Type` is set on continuation requests. An explicit terminal flag or a
short final page proves completion; an unknown continuation shape is partial.
The browser API is restored when capture finishes. The history page is opened
after every successful account projection, even if the dashboard contains a
short preview. That preview is merged and deduplicated but is not a completion
signal.
The resulting projection contains only these bounded fields:

- at most 256 account identifiers, names, types, currencies, masked numbers,
  and balances;
- at most 20,000 operations projected from the targeted two-year history with
  account, date, amount, description,
  merchant, category, MCC, and internal-transfer state;
- a version and authenticated flag.

It does not read cookie values, authorization headers, form fields, local
storage, rendered text, or arbitrary DOM elements. Request rewriting and
pagination stay inside the bank page. Inline script scanning is limited by
count and size. History capture requires 2xx JSON, stores at most 200 responses,
and limits their combined text to 8 MiB. Raw bootstrap, request, and response
objects never leave the bank page. C++ limits the returned projection to 8 MiB,
validates exact integer minor units and ISO dates, rejects malformed accounts,
and skips malformed or duplicate operations with a visible warning count.
Internal transfers do not contribute to income, spending, savings, or category
totals.
Operations carrying Alfa's `cashout` marker use the source account or masked
card from the sender object when needed and appear under **Наличные**.

After Alfa redirects back to its dashboard, the dialog shows **Вход выполнен.
Получаем счета из Альфа-Онлайн…**. If the bank no longer exposes a supported
bootstrap schema, synchronization stops with a compatibility error after 30
seconds instead of leaving the login dialog waiting indefinitely. The same
bounded failure applies if history does not finish within 150 seconds; the
overall synchronization is capped at 180 seconds.

The system WebView retains Alfa's browser session so the app can reload the
dashboard every six hours while auto-update is enabled. Disconnecting clears
that browser session. If Alfa redirects a refresh back to its authentication
host, the connection is marked expired and its in-memory rows are removed.

This path uses undocumented Alfa Online page state. If the two-year range or a
terminal page cannot be proven, the UI says the history is incomplete and a
partial refresh retains distinct operations from still-present accounts inside
the current two-year cutoff. That retention is in memory and does not survive
an app restart. The connector may still provide fewer operations than a bank
statement and can stop working after a frontend change.
WealthTracker does not bypass certificate validation, submit direct credential
requests, extract cookies, replay private headers, or call write endpoints. The
separate **Альфа-Банк API** row remains the supported option for users who
already have an issued B2C integration token.

## Retail VTB

Open **Подключения → ВТБ → Войти** and complete the official VTB Online
login inside the dialog. The bank page owns the phone number, password, SMS
code, TLS connection, and browser session. WealthTracker disables local storage
and file access and does not read cookies, form fields, authorization headers,
or browser storage.

After login, a fixed expression checks the exact `https://online.vtb.ru`
origin and requests the bank frontend's exported `./HttpClient`. It uses that
client only for these account and balance paths:

- `/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active`
- `/msa/api-gw/private/portfolio/portfolio-balances/financial/summary`

For operations, the expression temporarily observes `XMLHttpRequest`, opens the
official `/history` screen, and accepts only successful same-origin `GET`
responses whose exact final path component is `byUser`, `byProducts`,
`byAccount`, or `byCard` under
`/msa/api-gw/private/history-hub/history-hub-homer/v1/history/`. It captures at
most 16 exact path and query templates and restores the browser API. An
unfiltered `byUser` template covers the portfolio; otherwise an observed
single-product `byProducts`, `byAccount`, or `byCard` template is replayed for
every product identifier returned by the same portfolio response. The exact
observed path and parameter name are retained.

The page-owned HTTP client replays inclusive 31-day windows covering two years,
with at most four requests in flight. Non-date query fields are preserved; only
recognized dates, page or offset fields, and the proven single-product value
change. Every returned operation must belong to its requested window.
Continuation follows explicit `hasNext`, `hasMore`, or `last` metadata, or a
short final page with a known page size. WealthTracker supplies only a generated
request ID and the fixed `web_history_page` and smart-update headers; it never
copies private headers. Replay is capped at 512 requests and 8 MiB of combined
operation data.

A portfolio response that is still loading is retried at most twice, matching
the current bank frontend. The financial-summary route is an optional balance
supplement in VTB's frontend; a missing or unavailable
supplement does not discard products and balances already present in the active
portfolio. Authentication errors remain blocking. Raw responses stay inside
the bank page. The expression returns only account identifiers, names, types,
currencies, masked numbers and balances, plus operation identifiers, accounts,
dates, amounts, descriptions, merchants, categories, MCCs, and internal-transfer
markers. Each bank response and the final projection are limited to 8 MiB; C++
also limits the projection to 256 accounts and 20,000 operations and validates
exact integer minor units, dates, currencies, identifiers, and account
ownership. Account aliases are resolved independently; an obsolete alias does
not hide another valid alias on the same operation. Conflicting duplicate
identifiers, unknown account ownership, out-of-window rows, or incomplete
product/page coverage make the snapshot partial. Partial operations are not
published; a previously complete in-memory history is preserved when available,
and the status shows a bounded rejected-operation count without exposing bank
data.

VTB operation codes and parent categories used by the current frontend for
transfers between a user's own accounts are normalized as **Внутренний
перевод**. They do not contribute to income, spending, savings, or
spending-category totals.

The browser session remains in the system WebView for six-hour refreshes while
auto-update is enabled. Disconnecting deletes that WebView session. A 401/403
response from the page-owned read client marks the connection expired. The SPA
pathname is not used as an authentication signal because presentation routing
can remain unchanged after the session transition. An unknown origin, route
contract, module shape, or response schema stops synchronization instead of
broadening access. An unsupported date or continuation template, timeout,
request limit, or non-authentication replay failure reports **история за 2 года
неполная**. A partial refresh retains distinct operations from still-present
accounts inside the current two-year cutoff; retention is in memory only.
Authentication and malformed history schemas remain blocking.
Compatibility errors include only fixed stage codes such as `VTB-P3` or
`VTB-H1`; codes contain no account, operation, balance, cookie, or response
value.

VTB does not publish this retail frontend contract as a supported API. A
bank-side frontend change can break synchronization. WealthTracker does not
fall back to password APIs, cookie export, certificate bypasses, redirects to
lookalike origins, or write requests.

## Token-based APIs

For each connection, obtain a token from the corresponding bank or registered
integration, open its row in **Подключения**, save the token, then select
**Синхронизировать**.

- Alfa-Bank B2C uses `GET /api/pp/v1/accounts` and
  `GET /api/pp/v1/operations` at `https://baas.alfabank.ru`. The desktop app
  accepts an already-issued token; it does not run an authorization-code flow.
- T-Bank Business uses `GET /openapi/api/v4/bank-accounts` and
  `GET /openapi/api/v1/statement` at `https://business.tbank.ru`.
- Modulbank uses account-info and operation-history methods at
  `https://api.modulbank.ru`. The exact token `sandboxtoken` enables the bank's
  public synthetic sandbox mode.
- T-Invest currently calls the read-only account-list method at
  `https://invest-public-api.tbank.ru`. Portfolio and operation import are not
  wired into the interface yet.

Removing a connection deletes its credential entry, cancels active requests,
and removes that connection's in-memory rows.

## External ZenPlugins bundles

WealthTracker can run a separately built modular ZenPlugins bundle:

```sh
git clone https://github.com/zenmoney/ZenPlugins.git
cd ZenPlugins
corepack enable
yarn
yarn build <plugin-id>
```

`corepack enable` installs the package-manager shims shipped with Node.js so
the repository's pinned Yarn version is used. Node.js 18 or newer is required
only for this external-plugin path.

In WealthTracker, open **Подключения → Local ZenPlugins**, select
`build/<plugin-id>.js`, review the detected fields, and run it. The application
reads the matching `preferences.xml`, keeps passwords and OTP responses for the
active process only, and saves non-sensitive stable preferences locally.
Returned plugin state is stored under a bundle-specific credential key.

The plugin is arbitrary JavaScript without an operating-system sandbox. It can
read files and use the network with the current user's permissions. The
reduced child environment, bounded protocol, and output validation protect the
application boundary but cannot make untrusted code safe. Review and pin the
source before building it.

ZenPlugins and generated bundles are GPL-3.0 and are not copied into this MIT
repository or distributed with WealthTracker.

## Unsupported retail banks

The retail Sber row remains unavailable until a maintainable, reviewable
implementation exists. ZenPlugins does not currently contain a Russian
Alfa-Bank retail connector; similarly
named `alfabank-by`, `alfabank-ua`, and `altabanka-rs` plugins target other
countries or banks.
