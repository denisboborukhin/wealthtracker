# Bank connections

Open **Подключения** to configure a connection. The table describes code paths
in the [controller](../src/app/AppController.cpp), not live-account verification.
Retail web contracts are undocumented and can change; check synchronized account
counts and balances against the bank's own application.

| Integration | Data supplied | Authentication |
| --- | --- | --- |
| Retail T-Bank | Accounts and balances | Embedded official login |
| Retail Alfa-Bank | Accounts and balances | Embedded official login |
| Retail VTB | Accounts and balances | Embedded official login |
| Alfa-Bank B2C API | Accounts, balances, and operations | Issued integration token |
| T-Bank Business | Accounts, balances, and statements | T-API token |
| Modulbank | Accounts, balances, and operations | API token or public sandbox token |
| T-Invest | Accounts, portfolio balances, and positions | User-created read-only token |
| External ZenPlugins bundle | Bundle-dependent | Bundle-defined |

Financial snapshots remain in memory until exit. Failed refreshes preserve the
previous snapshot. Removing a connection cancels active work and removes its
in-memory rows and saved credential. On macOS, disconnect deletes browser data for that bank
without deleting other banks' data. Retail auto-update runs every six hours while the app is running.

## Retail T-Bank

Choose **Т-Банк → Войти** and complete the official page. The app validates the
observed API session with the bank before storing it in the OS credential store.
Refresh reads `/api/common/v1/accounts_light_ib` at `https://www.tbank.ru`.

Authentication rejection expires the saved session. Transport failures retain
the connection and previous snapshot for retry. A timeout with HTTP 0 means
that no HTTP response was received.

## Retail Alfa-Bank

Choose **Альфа-Банк → Войти** and complete the official phone/SMS page. The
session stays inside the system WebView. The app projects known dashboard
account and balance fields; refresh reloads the dashboard.

Certificate failures stop login. The app offers the fixed bank help link
`https://alfa.bank/cert`; it does not install certificates or change system
trust. If you change certificate trust through the bank's procedure, restart
the app before retrying.

## Retail VTB

Choose **ВТБ → Войти** and complete the official page. The session stays inside
the system WebView. The authenticated page client reads only these routes:

- `/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active`
- `/msa/api-gw/private/portfolio/portfolio-balances/financial/summary`

The portfolio's loading response is retried at most twice. The financial
summary supplements balances; failures other than HTTP 401 or 403 from that
supplement retain a valid portfolio response. A startup HTTP 401 gives the page
client up to ten seconds to restore its session; persistent rejection then
requires login. Once a session has supplied
a valid snapshot, HTTP 401 requires login immediately. HTTP 403 stops the
refresh as access denial and preserves the saved connection.

Refresh reuses the page client without reloading, and is capped at 45 seconds.
On macOS, browser data is retained across app launches. Restored connections
refresh one at a time on startup. A saved connected flag alone does not establish
a valid bank session; the bank may still require login after expiry.

## Token-based connections

Choose the integration, save its token, and select **Синхронизировать**. Use
read-only permissions: WealthTracker issues read requests but does not inspect
the token's bank-side scopes.

| Integration | Implemented reads |
| --- | --- |
| Alfa-Bank B2C | `GET /api/pp/v1/accounts` and `GET /api/pp/v1/operations` at `https://baas.alfabank.ru` |
| T-Bank Business | `GET /openapi/api/v4/bank-accounts` and `GET /openapi/api/v1/statement` at `https://business.tbank.ru` |
| Modulbank | Account information and operation history at `https://api.modulbank.ru`; the exact token `sandboxtoken` selects the public synthetic sandbox |
| T-Invest | `GetAccounts` and `GetPortfolio` at `https://invest-public-api.tbank.ru` |

T-Invest requests a RUB-valued portfolio for each accessible open account.
Portfolio value comes from `totalAmountPortfolio`; holding rows show instrument
identifiers and quantity times current price in the instrument's currency.

Retail connections and T-Invest do not retrieve transaction history. Calendar
filters affect operations already supplied by other integrations and never
trigger history requests. Dashboard cash-flow totals include RUB operations
and exclude own-account transfers; foreign-currency accounts remain visible
without automatic conversion.

## External ZenPlugins bundles

This optional path requires Node.js 18 or newer and a separately built bundle.
In **Подключения → Local ZenPlugins**, select `build/<plugin-id>.js`, review
the detected fields, and run it. The app reads matching `preferences.xml`, keeps
passwords and OTP answers for the active process, and saves non-sensitive
preferences locally. Reusable plugin state uses a bundle-specific OS credential
entry.

Bundles run with the current user's file and network permissions. Review and
pin their source before building and running them. WealthTracker does not
include ZenPlugins source or generated bundles. See
[Security design](security-design.md#external-plugins) for the process boundary.
