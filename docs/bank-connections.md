# Bank connections

Open **Подключения** to configure a connection. The table describes code paths
in the [controller](../src/app/AppController.cpp), not live-account verification.
Retail web contracts are undocumented and can change; check synchronized account
counts and balances against the bank's own application.

Connection indicators distinguish **Не подключено**, saved sessions/tokens with
unverified access, **Обновляем…**, **Данные получены**, and saved sessions/tokens
whose latest refresh returned no data. Saved snapshots and refresh timestamps do
not establish current access. A successful data indicator requires a validated
response during this app run. Failed reads retain previous balances and cashback.

| Integration | Data supplied | Authentication |
| --- | --- | --- |
| Retail T-Bank | Accounts and balances | Embedded official login |
| Retail Alfa-Bank | Accounts and balances | Embedded official login |
| Ozon Bank | Main RUB account, savings total, MKK debt and cashback | Embedded official login |
| Retail VTB | Accounts and balances | Embedded official login |
| Alfa-Bank B2C API | Accounts, balances, and operations | Issued integration token |
| T-Bank Business | Accounts, balances, and statements | T-API token |
| Modulbank | Accounts, balances, and operations | API token or public sandbox token |
| T-Invest | Accounts, portfolio balances, and positions | User-created read-only token |
| External ZenPlugins bundle | Bundle-dependent | Bundle-defined |

Each successful balance refresh saves an encrypted history snapshot. History
survives app restarts and disconnections; failed refreshes add no observations.
Other live financial data remains in memory until exit. Failed refreshes preserve
the previous in-memory snapshot. Removing a connection cancels active work and removes its
in-memory rows and saved credential. On macOS, disconnect deletes browser data for that bank
without deleting other banks' data. Retail auto-update runs every six hours while the app is running.
Retail T-Bank, Alfa, VTB, and Ozon background refreshes run independently and start
together on launch. Each refresh has a ten-second deadline. A timeout retains
credentials, browser pages, and previous balances; it does not establish expiry.
Interactive bank verification keeps its separate ten-minute window and only one
interactive login is active at a time. Background refreshes already in progress
may finish while you enter a code.
If interactive login or the privacy screen blocks a due refresh, it is deferred
by ten seconds instead of being dropped.

## Cashback

Open **Кэшбэк** to view categories already selected in the bank's own app.
Use **Обновить банк** on one bank's card for its balance and cashback refresh;
other banks are not refreshed. **Обновить банки** keeps the combined refresh.
T-Bank, Alfa-Bank, and VTB each have separate lists for the current calendar
month and the next month. Enter a category and a percentage (0.01–100, up to two
decimal places). Saving the same category for the same bank and month updates
its rate; **Удалить** removes only that entry. Entries do not require a connected
bank. A new calendar month promotes next month's entries without copying the
previous month's choices.

T-Bank, Alfa-Bank, and VTB categories are read automatically after a successful balance
refresh, including startup, scheduled, and manual refreshes. VTB's authenticated
page client makes this exact GET request, without a query or body:
`/msa/api-gw/private/loya/loya-web/v2/bonus/month/categories`.
Only bounded category names and rates cross the WebView boundary. Credentials
and bank response objects remain page-local. The read has its own ten-second
deadline and never delays publishing balances. The page displays the source of
each month's list and a separate cashback status and last-success time.

The VTB response's current and next month slots must match their Russian month
names and HTTP response date. Its `choice: true` means the bank offers selection,
not that those categories are confirmed; the app reports that state instead of
promoting offers. Archived categories and the separate available-offer catalog
are excluded. Percentage qualifiers such as `до` are preserved. Failed reads
keep the last successful in-memory snapshot and its original timestamp.

Source contract: VTB's public category module `mf_1757_categories` version
`4.12.18`, [read client](https://s1.vtb.ru/mf/mf_1757_categories/4.12.18/static/js/902.f2200977.js)
and [month renderer](https://s1.vtb.ru/mf/mf_1757_categories/4.12.18/static/js/138.31748f30.js).
Synthetic tests exercise that contract; they do not establish live compatibility.

T-Bank's reader uses the existing native session and the loyalty IDs from that
same `accounts_light_ib` response; it does not repeat account discovery. The
official frontend's read request is a JSON POST to
`/api/ms-gateway/loyalty_api/api/bonuses`, with fixed `origin`, `appName`,
`appVersion`, and the existing `sessionid` query. Its body contains deduplicated
`loyaltyIds` and `origin`. The optional subscription bundle is omitted, as in
the bank's own caller. Only `regularEssences` entries and explicitly active
categories are used. Program account ownership, dates in Europe/Moscow, exact
rates, and size limits are validated. Different rates from distinct card
programs retain account/program labels; contradictory rates within a program
are rejected. The read has a separate ten-second deadline and cannot invalidate
a successful balance refresh.

Source contract: T-Bank's observed public
[bonuses module](https://cdn.tbank.ru/s3aas/ib/bonuses/mm/desktop-bonuses-main-aggregated/1.215.1/desktop-bonuses-main-aggregated.client.js)
and [production API configuration](https://cdn.tbank.ru/s3aas/ib/supreme/assets/platform.b4b97599281fd742.js).

Alfa uses its existing browser-owned API wrappers. It reads eligible account
types from GET `/api/v1/loyalty/accounts`, then monthly programs from GET
`/api/v1/loyalty-view/promoted/programs?loyaltyType=...`. Categorical programs
provide explicit dates, confirmation state, and bank deep links containing
their basket IDs. GET `/api/v1/loyalty-view/v2/categories?basketOfferId=...`
provides the free and paid category sections. Only `isConfirmed: true` entries
are published; draft `isSelected` values are not confirmation. All reads use
the bank's own authentication and XSRF handling inside the page, with redirects
rejected, bounded requests, and a separate ten-second overall deadline.

Alfa date strings must match a supported explicit ISO/Russian date or current/
next Russian month label. Month labels are checked against the HTTP response
date; month identity is never inferred from basket IDs or response ordering.
Rich category titles are decoded inertly and must contain exactly one explicit
percentage; ambiguous rates or unknown fields reject the refresh. No category
acceptance, purchase, or game methods are invoked.

Source contract: the observed `newclick-marketplace-ui` overview and
`newclick-loyalty-ui` `0.0.35` API declarations/renderers, served under
`/newclick-marketplace-ui/assets/` and `/newclick-loyalty-ui/assets/` on the
official Alfa origin. These software assets were inspected through an
authenticated WebView; no credentials or bank data were exported for discovery.

**Обновить банки** runs the same balance-and-cashback sequence. A month received from a bank takes
precedence over its saved manual fallback, without overwriting that fallback.
Partial responses replace only explicitly covered months. Other previously
fetched months retain both their categories and original refresh timestamps.
An empty manual list does not establish that the bank has no selected categories.

Manual names, percentages, and months are stored in local application settings,
separately from encrypted balance history and OS credentials. The privacy screen
and demo mode hide them and block edits. Disconnecting a bank preserves manual
entries; delete them on the cashback page when they are no longer wanted.
Use **Записать вручную** to open the fallback form. Retrieved categories are
read-only here; change bank selections through the bank's own application.
Automatic bank snapshots and their per-month update times are saved in the
encrypted balance-history database and restored on restart. Failed or canceled
refreshes preserve them. Disconnect clears the selected bank's automatic snapshot.
The privacy screen and demo mode also cancel pending cashback reads.

## Retail T-Bank

Choose **Т-Банк → Войти** and complete the official page. The app validates the
observed API session with the bank before storing it in the OS credential store.
Refresh reads `/api/common/v1/accounts_light_ib` at `https://www.tbank.ru`.

Authentication rejection stops refreshes and requires verification, but retains
the saved session without rewriting the Keychain. A successful new login replaces
it; explicit disconnect deletes it. Transport failures retain the connection and
previous snapshot for retry. A timeout with HTTP 0 means
that no HTTP response was received.

## Retail Alfa-Bank

Choose **Альфа-Банк → Войти** and complete the official phone/SMS page. The
session stays inside the system WebView. The app projects known dashboard
account and balance fields from the frontend's current financial store, falling
back to known bootstrap data when that store is unavailable; refresh reloads
the dashboard. Empty or still-loading pages do not establish successful login.
An authentication-host redirect is allowed to finish within the ten-second refresh
limit. If it does not finish, the saved connection and page remain available through
**Открыть банк**. This button is also available during that bank's refresh: it
switches to interactive verification on the same page without restarting the
authentication flow.

Certificate failures stop login. The app offers the fixed bank help link
`https://alfa.bank/cert`; it does not install certificates or change system
trust. If you change certificate trust through the bank's procedure, restart
the app before retrying.

## Retail VTB

On Android, the phone-identification flow uses `sso-ib.vtb.ru` with the bank's
Russian CA. The additional trust anchor is scoped to that exact resource host;
it does not extend top-level navigation. A missing trust anchor can surface as
the bank's generic temporary-login/VPN message after phone submission. The
Android regression checks this chain against the app's actual trust manager;
successful phone login still requires a separate live-device check.

Choose **ВТБ → Войти** and complete the official page. The session stays inside
the system WebView. The authenticated page client reads only these routes:

- `/msa/api-gw/private/portfolio/portfolio-main-page/portfolios/active`
- `/msa/api-gw/private/portfolio/portfolio-balances/financial/summary`

The portfolio's loading response is retried at most twice. The financial
summary supplements balances; failures other than HTTP 401 or 403 from that
supplement retain a valid portfolio response. A startup HTTP 401 gives the page
client time to restore access within the total ten-second refresh deadline. If access remains unconfirmed, the
refresh stops without publishing data or deleting the saved connection or WebView.
After a successful snapshot, a subsequent HTTP 401 stops the refresh immediately
with the same preservation behavior. **Открыть ВТБ** displays that same page without
reloading it, including while that bank's refresh is running; complete a
bank-requested verification there if necessary.
HTTP 403 stops the refresh as access denial and preserves the saved connection.
Diagnostic logs contain only a fixed request-stage code (`VTB-AUTH-P` for portfolio,
`VTB-AUTH-B` for the summary) and elapsed time, never bank response bodies or credentials.

Refresh reuses the page client without reloading, and is capped at ten seconds.
On macOS, browser data is retained across app launches. Restored connections
refresh together on startup. A saved connected flag alone does not establish
a valid bank session; the bank may still require login after expiry.
Quitting with Cmd+Q ends live page memory. Persistent browser data does not
guarantee that a bank will restore authentication without a new code. The app
does not export or replay browser authentication secrets to bypass that requirement.

## Ozon Bank

Choose **Ozon Банк → Войти** and complete the official `finance.ozon.ru`
page, allowing its observed redirect to `sso.ozon.ru` for Ozon ID. No financial
reader runs on the identity origin. Only validated financial data confirms login. The reader reuses the
dashboard's already-loaded software module and its `GetMainPage` query handler
(`POST /apps/main/api/mainPage`, empty JSON object). Authentication headers and
cookies stay inside the bank page.

Coverage is the main RUB account, displayed savings total and MKK installment
debt. Each refresh reads `InstallmentInfoV4` through the bank's credit
client (`POST /apps/credit/api/installment/info`, body `{}`). Only
`me.client.installment.userInfo.debt.cents` becomes a negative RUB balance,
including when the dashboard hides the installment widget. Only an explicit
null installment establishes that the product is absent;
approved and available limits never become assets. Failure to read a required
liability preserves the previous entire balance snapshot. Bank-issued annuity
installments use a different schema and are not substituted with the MKK read.
When both
a savings aggregate and a single-product widget are present, the aggregate
replaces the single-product presentation so the amount is counted once.
No transaction history is read.

Ozon cashback follows balance synchronization through the verified loyalty
client: `GetLoyaltyProgramV2` / `loyalty/bonusState/loyaltyProgramV2`, then
`GetCashbackProgram` / `loyalty/bonusState/cashbackProgram`. Both are read-only
POSTs with empty JSON bodies under `/apps/loyalty/api/`. The public
`MFBonuses.DVjct8rH.js` module owns bank authentication; no headers or tokens leave
the page. Only `ACCEPTED` categories with explicit current/next `yyyy-MM` periods
are published. Decimal percentage values are already percent units. Unavailable
future choices remain unknown, and the separate Stars program does not become
cashback. Snapshots and their original month timestamps persist in encrypted
storage; failures preserve them. Manual category fallback is also available.
The credit and loyalty clients are initialized using the bank's own service
contexts (`GET /apps/credit/_mf/context` and `GET /apps/loyalty/_mf/context`)
before native reads. Context JSON stays inside the bank page and is passed
directly to the native setter; it is never projected to the application model.

On Android, select **Продолжить в браузере** if the bank first offers its native
apps. The emulator reached Ozon ID but its anti-bot page reported a connection
error; authenticated Android retrieval remains unverified.

Balances refresh independently with a ten-second background deadline, restore
from encrypted history on startup, and survive failed refreshes. Explicit
disconnect removes the bank from the current capital total while retaining
history. The current software contract is the observed `bozon-fe-main`
`B6d1cMwe.js` module, export `ki`, with `GetMainPage` / `query` / `mainPage`
declarations checked before use; changed or ambiguous financial schemas stop
publication. Public source inspection and synthetic WebView tests do not prove
live authentication on every device. A user-run macOS check on November 1, 2026
confirmed balances, MKK installment debt and selected cashback categories after
the service-context initialization fix. Android authentication remains unverified.

## Token-based connections

Choose the integration, save its token, and select **Синхронизировать**. Use
read-only permissions: WealthTracker issues read requests but does not inspect
the token's bank-side scopes.

| Integration | Implemented reads |
| --- | --- |
| Alfa-Bank B2C | `GET /api/pp/v1/accounts` and `GET /api/pp/v1/operations` at `https://baas.alfabank.ru` |
| T-Bank Business | `GET /openapi/api/v4/bank-accounts` and `GET /openapi/api/v1/statement` at `https://business.tbank.ru` |
| Modulbank | Account information and operation history at `https://api.modulbank.ru`; the exact token `sandboxtoken` selects the public synthetic sandbox |
| T-Invest | `GetAccounts`, `GetPortfolio`, and `GetInstrumentBy` at `https://invest-public-api.tbank.ru` |

T-Invest requests a RUB-valued portfolio for each accessible open account.
Portfolio value comes from `totalAmountPortfolio`; holding rows use ticker and
name from the official [GetInstrumentBy](https://developer.tbank.ru/invest/api/instruments-service-get-instrument-by)
read method, with quantity times current price in the instrument's currency.
Instrument metadata is optional: each unique identifier is resolved once per
refresh, with at most 256 lookups and a five-second metadata deadline within the
existing overall synchronization deadline. Failed, malformed, or unavailable
metadata does not reject valid balances; unresolved holdings use a generic label
instead of an internal identifier.

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
