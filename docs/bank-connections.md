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
| Retail T-Bank | Yes | Yes | Not requested | Embedded official login |
| Retail Alfa-Bank | Yes | Yes | Not requested | Embedded official login |
| Retail VTB | Yes | Yes | Not requested | Embedded official login |
| Alfa-Bank B2C API | Yes | Yes | Yes | Issued integration token |
| T-Bank Business | Yes | Yes | Yes | T-API token |
| Modulbank | Yes | Yes | Yes | API token or public sandbox token |
| T-Invest | Yes | Portfolio total | Not requested | User-created read-only token |
| External ZenPlugins bundle | Plugin-dependent | Plugin-dependent | Plugin-dependent | Plugin-defined |
| Retail Sber | No | No | No | Adapter not implemented |

Bank operations are normalized locally. Dashboard totals include RUB values
only; foreign-currency accounts stay visible without an implicit conversion.

## Local cash-flow reporting

Income, spending, savings, categories, and the operations list use the inclusive
date range selected with the two calendar controls. The default range is the
latest month through today. Savings are an amount in rubles: RUB income minus
RUB spending. Built-in retail synchronization loads account balances only. Calendar filters
apply to operations loaded by other connectors and never trigger bank-history
requests.

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

Open **Подключения → Т-Банк → Войти** and complete the official login. A bounded
page expression reads API-session identifiers only from exact T-Bank resource
URLs. C++ accepts a session only after `ping` confirms `CLIENT` access, then
stores it in the operating-system credential store. Both login validation and
account retrieval use HTTP/1.1, bounded responses, TLS validation, and no cookies
or automatic redirects. Navigation permits only `www.tbank.ru`, `id.tbank.ru`,
and `id.tinkoff.ru` over HTTPS with no custom port or user information.

Synchronization calls `/api/common/v1/accounts_light_ib` for accounts and their
balances. It does not call `/operations`. HTTP authentication rejection expires
the saved session; a transport failure retains the validated connection and
previous snapshot for retry. A timeout with HTTP 0 means no HTTP response was
received, not that the bank rejected the login.

## Retail Alfa-Bank

Open **Подключения → Альфа-Банк → Войти** and complete the official phone/SMS
page. Authentication stays inside the system WebView. The app reads only a
bounded projection of known dashboard account and balance fields, validates
exact integer minor units, and completes without opening history or waiting
for operations. Refresh reloads the dashboard to obtain a new snapshot.

Alfa Online uses Russian TLS certificates. If macOS does not trust the bank's
certificate chain, the app links to [Alfa's official instructions](https://alfa.bank/cert).
It does not install a trust anchor or disable TLS validation. After installing
certificates through the official procedure, restart the app and retry.

## Retail VTB

Open **Подключения → ВТБ → Войти** and complete the official page. The app keeps
the authenticated WebView and calls its own HTTP client only for:

- `/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active`
- `/msa/api-gw/private/portfolio/portfolio-balances/financial/summary`

The portfolio's loading response is retried at most twice. The balance summary
is an optional supplement; non-authentication failures retain balances already
present in the portfolio. Authentication errors remain blocking. Account
projection covers recognized cards, accounts, deposits, savings, loans, metals,
and wallets without reading private headers, cookies, or browser storage.

Refresh resets only the app's projection state and reuses the authenticated
page client. It does not reload the page or navigate to history. This preserves
session state held in page memory. A new app process may still require login;
a saved connected flag is not proof of a live browser session. The refresh is
bounded to 45 seconds even if a JavaScript callback stalls.

## Retail balance scope and limits

These retail interfaces are undocumented and can change. Verify the account
count and balances against each bank's own application. Synchronization does
not retrieve transaction history. Cash-flow reporting therefore cannot be
computed from these balance-only connections. Previously loaded balances survive
a transport or schema failure; status text and the last successful refresh time
identify the failed update. Explicit authentication rejection requires login.

Built-in retail auto-update runs every six hours while the app is running.
Disconnecting removes that connection's in-memory rows and stored session.
No phone numbers, passwords, OTPs, cookie values, or raw page state are exposed
to QML or status messages.

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
- T-Invest uses a user-created read-only token at
  `https://invest-public-api.tbank.ru`. It lists accessible open accounts and
  requests one RUB-valued portfolio per account. It does not request operation
  history. Requests are serial, bounded to 20 MiB per response, 256 accounts,
  20,000 positions, and one minute per synchronization. Failed refreshes retain
  the previous snapshot. Locking, disconnecting, or replacing the token cancels
  pending work.

  Overall portfolio value comes from the API's `totalAmountPortfolio`. Position
  rows show instrument identifiers and quantity times current price in the
  instrument's currency; separate accrued coupon interest is excluded from row
  estimates. Names, allocation, yield, dividend totals, and XIRR are not yet
  calculated. The official contract is available in the
  [operations schema](https://github.com/RussianInvestments/investAPI/blob/main/src/docs/contracts/operations.proto).

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
