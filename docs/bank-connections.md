# Bank connections

All built-in connectors expose financial data to WealthTracker as read-only.
API tokens and the retail T-Bank session use separate entries in macOS Keychain
or Windows Credential Manager. Alfa Online keeps its browser session inside the
operating-system WebView; WealthTracker does not extract it. Synchronized
financial records currently remain in memory and disappear when the app exits.

## Support matrix

| Connection | Accounts | Balances | Operations | Authentication |
| --- | --- | --- | --- | --- |
| Retail T-Bank | Yes | Yes | Yes | Embedded official login |
| Retail Alfa-Bank | Yes | Yes | Recent snapshot | Embedded official login |
| Alfa-Bank B2C API | Yes | Yes | Yes | Issued integration token |
| T-Bank Business | Yes | Yes | Yes | T-API token |
| Modulbank | Yes | Yes | Yes | API token or public sandbox token |
| T-Invest | Access check | No | No | User-created read-only token |
| External ZenPlugins bundle | Plugin-dependent | Plugin-dependent | Plugin-dependent | Plugin-defined |
| Retail Sber, VTB | No | No | No | Adapter not implemented |

Bank operations are normalized locally. Dashboard totals include RUB values
only; foreign-currency accounts stay visible without an implicit conversion.

## Retail T-Bank

Open **Подключения → Т-Банк → Войти** and complete the official page inside
the dialog. No Node.js installation is required.

The WebView handles all login fields. Persistent local storage and file access
are disabled, and cookies are deleted before and after the attempt. A fixed
page expression returns only bounded session identifiers observed in exact
T-Bank API resource URLs; it does not read cookies, DOM fields, headers,
request bodies, or local storage. C++ accepts a candidate only after `ping`
reports `CLIENT` access.

The native connector permits `GET` requests to the exact origin
`https://www.tbank.ru` and these paths only:

- `/api/common/v1/accounts_light_ib`
- `/api/common/v1/operations`

The login validator also calls `/api/common/v1/ping`. Cookies and redirects are
disabled for these C++ requests. The session is removed if initial validation
or later authentication fails. Successful connections refresh every six hours
while the application is running when auto-update is enabled.

Account histories are requested serially to avoid overlapping requests on the
bank connection. `NO_DATA_FOUND` means an empty history, not a failed account.
Malformed individual operations are skipped with a visible count while valid
accounts, balances, and operations are retained. Operations marked `isInner`
by T-Bank appear under **Внутренний перевод** and do not contribute to income,
spending, savings rate, or spending-category totals.

These retail endpoints are undocumented and unsupported by T-Bank as a public
API. A bank-side change can break login or synchronization. WealthTracker does
not fall back to certificate bypasses, cookie export, direct password APIs, or
write endpoints.

## Retail Alfa-Bank

Open **Подключения → Альфа-Банк → Войти** and complete the official Alfa Online
phone/SMS page inside the dialog. The page owns the phone number, password when
requested, one-time code, TLS connection, and browser cookies. QML and C++ do
not receive those values.

Alfa Online does not expose a public retail account API comparable to its
partner B2C product. The built-in connector therefore keeps the authenticated
page in the operating-system WebView and runs a fixed projection expression.
The expression reads only known in-page state objects and returns these bounded
fields:

- at most 256 account identifiers, names, types, currencies, masked numbers,
  and balances;
- at most 20,000 recent operations with account, date, amount, description,
  merchant, category, MCC, and internal-transfer state;
- a version and authenticated flag.

It does not read cookie values, authorization headers, form fields, request
bodies, local storage, or arbitrary DOM content. C++ limits the projection to
8 MiB, validates exact integer minor units and ISO dates, rejects malformed
accounts, and skips malformed or duplicate operations with a visible warning
count. Internal transfers do not contribute to income, spending, savings rate,
or category totals.

The system WebView retains Alfa's browser session so the app can reload the
dashboard every six hours while auto-update is enabled. Disconnecting clears
that browser session. If Alfa redirects a refresh back to its authentication
host, the connection is marked expired and its in-memory rows are removed.

This path uses undocumented Alfa Online page state. It may provide fewer
operations than a full statement and can stop working after a frontend change.
WealthTracker does not bypass certificate validation, submit direct credential
requests, extract cookies, replay private headers, or call write endpoints. The
separate **Альфа-Банк API** row remains the supported option for users who
already have an issued B2C integration token.

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

Retail Sber and VTB rows describe planned phone-and-code adapters. They remain
unavailable until a maintainable, reviewable implementation exists. ZenPlugins
does not currently contain a Russian Alfa-Bank retail connector; similarly
named `alfabank-by`, `alfabank-ua`, and `altabanka-rs` plugins target other
countries or banks.
