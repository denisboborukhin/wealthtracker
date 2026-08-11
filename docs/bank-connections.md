# Bank connections

All built-in connections are read-only. Tokens and reusable sessions use a
separate entry in macOS Keychain or Windows Credential Manager. Synchronized
financial records currently remain in memory and disappear when the app exits.

## Support matrix

| Connection | Accounts | Balances | Operations | Authentication |
| --- | --- | --- | --- | --- |
| Retail T-Bank | Yes | Yes | Yes | Embedded official login |
| Alfa-Bank B2C API | Yes | Yes | Yes | Issued integration token |
| T-Bank Business | Yes | Yes | Yes | T-API token |
| Modulbank | Yes | Yes | Yes | API token or public sandbox token |
| T-Invest | Access check | No | No | User-created read-only token |
| External ZenPlugins bundle | Plugin-dependent | Plugin-dependent | Plugin-dependent | Plugin-defined |
| Retail Alfa-Bank, Sber, VTB | No | No | No | Adapter not implemented |

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

These retail endpoints are undocumented and unsupported by T-Bank as a public
API. A bank-side change can break login or synchronization. WealthTracker does
not fall back to certificate bypasses, cookie export, direct password APIs, or
write endpoints.

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

Retail Alfa-Bank Russia, Sber, and VTB rows describe planned phone-and-code
adapters. They are deliberately unavailable until a maintainable, reviewable
implementation exists. A similarly named `altabanka-rs` plugin targets Alta
Banka Serbia and is not an Alfa-Bank Russia connector.
