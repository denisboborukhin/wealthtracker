# Bank connections

WealthTracker ships documented, read-only API connections and an optional
local bridge for external ZenPlugins bundles. An official connection token is
saved under a separate key in macOS Keychain or Windows Credential Manager.
The application never stores that token in its database, follows no HTTP
redirects automatically, and sends official requests only to the exact host
listed for the connector.

## Support matrix

| Connection | Accounts | Balances | Operations | Who can use it |
| --- | --- | --- | --- | --- |
| Alfa-Bank B2C API | Yes | Yes | Yes | A registered integration with a user access token |
| T-Bank Business | Yes | Yes | Yes | An organization with the required T-API permissions |
| Modulbank | Yes | Yes | Yes | A Modulbank business customer or the public sandbox |
| T-Invest | Yes | Portfolio | Yes | A user with a read-only investment token |
| Retail T-Bank | Yes | Yes | Yes | A retail customer who signs in on the official T-Bank page |
| External modular ZenPlugin | Plugin-dependent | Plugin-dependent | Plugin-dependent | A user who separately reviewed and built the plugin |
| Retail Alfa-Bank | Adapter-required | Adapter-required | Adapter-required | Needs a separate local phone-code adapter |
| Sber retail | Adapter-required | Adapter-required | Adapter-required | Needs a separate local phone-code adapter |
| VTB retail | Adapter-required | Adapter-required | Adapter-required | Needs a separate local phone-code adapter |

The documented API connectors retrieve up to the previous 12 months and follow
every pagination cursor. Data is normalized locally and combined across
connected banks. RUB totals are used for dashboard summaries; foreign-currency
accounts remain visible in their original currencies and are not silently
converted.

## Retail T-Bank

The built-in retail connection uses two isolated parts:

1. A local helper starts an installed Chrome, Chromium, Edge, or Brave browser
   with a fresh temporary profile and opens the official T-Bank login. Phone,
   password, OTP, cookies, headers, request bodies, and page content stay in
   the browser.
2. The helper uses a random DevTools endpoint bound only to `127.0.0.1`. It
   evaluates one fixed expression that filters the page's resource-timing
   names to exact `https://www.tbank.ru/api/common/v1/` URLs and returns only
   bounded `sessionid` values. DevTools Network events and cookie APIs are not
   enabled.

C++ rejects malformed helper events and accepts the candidate only after
T-Bank reports access level `CLIENT`. It then validates the session by reading
accounts and operations. Only the opaque session identifier is kept in macOS
Keychain or Windows Credential Manager; the phone, password, OTP, and browser
state are not persisted or written to logs or QML settings. The helper closes
the isolated browser and deletes its profile after success, cancellation, or
failure.

The bundled adapter makes `GET` requests only to these paths at the exact
origin `https://www.tbank.ru`:

- `/api/common/v1/ping`
- `/api/common/v1/accounts_light_ib`
- `/api/common/v1/operations`

The local plugin host blocks other origins and all redirects for this bundle.
The separate authorization client calls only `ping` for the candidate that the
isolated browser explicitly handed to C++.

This is an undocumented retail web API, not T-Bank's supported OpenAPI. It may
change without notice. An expired or rejected session is removed from the
credential store and the application asks the user to sign in again. Automatic
refresh runs every six hours while WealthTracker is open; it stores no phone,
password, or one-time code.

ZenMoney's former public retail Tinkoff plugin used a different direct API
flow: it stored a generated device identifier, PIN hash, and previous API
session so it could create a new `sessionid` without a browser. ZenMoney
[removed that plugin as deprecated and non-working in 2020](https://github.com/zenmoney/ZenPlugins/commit/1efdd11c671ea5f5d18cd5e4bb26a12189780c0b).
An independent T-Bank web-API client likewise reports that the direct SMS
confirmation flow has been broken since June 2024 and recommends browser-based
authentication ([package documentation](https://pkg.go.dev/github.com/jfk9w-go/tbank-api#AuthFlow)).
WealthTracker therefore does not revive that obsolete password/SMS protocol.
It observes the API session used by the current official browser page and
retains no reusable PIN, device credential, or browser cookie.

Connection steps:

1. Install Node.js 22.4 or newer and one supported Chromium browser.
2. Open **Connections → Т-Банк** and choose **Войти в отдельном Chrome**.
3. Complete the official T-ID page in the separate browser window. Keep the
   WealthTracker dialog and browser window open; validation is automatic and
   the attempt expires after ten minutes.
4. WealthTracker validates accounts and operations before marking the
   connection active. If the undocumented API rejects the session, start a new
   login.

### T-Bank login troubleshooting

The previous embedded Chromium and system-browser implementations have been
removed. Their WebGPU, STUN, CSP, `net_error -202`, internal-authorization,
and unrelated-session polling failures are no longer part of this login path.
If no separate window opens, check the visible error for a missing Node.js or
supported browser installation. If the official page completes but the app
keeps waiting, cancel and start a fresh isolated login; a bank-side resource or
API change appears as a connection error rather than falling back to cookies.

The browser and WealthTracker each use the operating system's normal TLS trust
policy. Do not bypass a certificate warning shown by either one; inspect VPN,
proxy, antivirus, system time, and trusted-root configuration instead.

## Alfa-Bank B2C API

WealthTracker calls only:

- `GET https://baas.alfabank.ru/api/pp/v1/accounts`
- `GET https://baas.alfabank.ru/api/pp/v1/operations`

The access token must come from Alfa-Bank's Authorization Code Flow and include
the `accounts` and `operations-history` permissions required by the two
products. Register or manage the integration through the
[Alfa API developer portal](https://developers.alfabank.ru/). The current
desktop app accepts an already-issued access token; it does not yet launch the
bank's browser authorization flow.

## T-Bank Business

WealthTracker calls only:

- `GET https://business.tbank.ru/openapi/api/v4/bank-accounts`
- `GET https://business.tbank.ru/openapi/api/v1/statement`

Create a T-API bearer token and grant the organization permissions for account
information and statements. The official
[accounts method](https://developer.tbank.ru/docs/api/get-api-v-4-bank-accounts)
is explicitly for organization settlement and investment accounts; it does not
provide retail T-Bank cards. Statement data is available from June 2023
according to the
[statement documentation](https://developer.tbank.ru/docs/api/get-api-v-1-statement).

## Modulbank

Generate a token from the Modulbank online bank with the `account-info` and
`operation-history` scopes, then paste it into Connections.
[Modulbank's API documentation](https://api.modulbank.ru/) describes the
self-service token flow.

For a test without an account, use the public token `sandboxtoken`.
WealthTracker recognizes that exact value and adds the required `sandbox: on`
header. Sandbox data is synthetic.

## Connecting and synchronizing

1. Obtain the appropriate read-only token from the bank.
2. Open **Connections** and select the matching official API row.
3. Paste the token and choose **Save token**.
4. Open the row again and choose **Synchronize**.

Removing a connection deletes its credential-store entry and removes its
in-memory synchronized data from the current view. Other connected banks remain
intact.

## External ZenPlugins

WealthTracker can execute a modular, fetch-based bundle built in a separate
ZenPlugins checkout:

1. Install Node.js 18 or newer.
2. Clone `https://github.com/zenmoney/ZenPlugins.git` outside WealthTracker.
3. Run `corepack enable`, `yarn`, and `yarn build <plugin-id>`.
4. Open **Connections → Local ZenPlugins**.
5. Choose `build/<plugin-id>.js`, review the detected preferences, enter the
   session-only login values, and choose **Run**.
6. Answer OTP prompts in the same dialog.
7. Enable auto-update only after the first successful run if the bundle can
   reuse returned session state.

The app infers the preference schema from
`src/plugins/<plugin-id>/preferences.xml`. It accepts only bounded scalar
fields and normalizes the plugin's accounts and transactions in C++. Passwords
and OTP responses are not persisted. Non-secret stable preferences, for
example a phone number or start date, are saved in local settings so a
compatible session-based bundle can refresh again. Returned plugin data and
cookies are stored under a bundle-specific key in the operating-system
credential store.

ZenPlugins does not require every plugin to authenticate with cookies. Its
runtime offers both a cookie jar and persistent plugin data, and each bank
adapter chooses the state its protocol needs. For example, the
[ePayments plugin](https://github.com/zenmoney/ZenPlugins/blob/master/src/plugins/epayments/auth.js)
persists access and refresh tokens, while
[Chelinvest](https://github.com/zenmoney/ZenPlugins/blob/master/src/plugins/chelinvest/index.js)
persists an API `sessionid`. Other adapters use browser-style cookies, and some
place access and refresh tokens into that cookie jar. The built-in T-Bank
adapter does not use the ZenPlugins cookie jar; its only input is the API
session that the isolated browser handed to C++ and the read-only API verified.

This is an explicit trust boundary. The bundle is third-party JavaScript
running as a local child process without an operating-system sandbox. Its
network destinations are controlled by that bundle, not by the allowlists used
for the built-in official clients. Review its source and pinned commit before
running it.

## Other retail-bank availability

ZenPlugins is GPL-3.0, so WealthTracker does not copy its source or generated
bundles into this MIT repository. The external process boundary lets the user
obtain and run such a bundle separately.

The Connections page includes first-class rows for retail Alfa-Bank, Sber, and
VTB because those are target user flows: phone number, one-time code, then
reusable session state. Those rows are adapter slots, not built-in working
connectors. Once a clean-room adapter or a user-built compatible bundle exists,
it should authenticate locally, return normalized accounts, balances, and
operations, and store only revocable session state through the same
credential-store path.

The upstream tree inspected at commit `b9b5bcf` on 2026-07-22 does not contain
retail plugins for Alfa-Bank Russia, T-Bank, Sber, or VTB. The linked
`altabanka-rs` plugin is Alta Banka Serbia: it uses `online.altabanka.rs`, RSD
accounts, and an OTP from the Alta mobile app. It must not be presented as an
Alfa-Bank Russia connector.
