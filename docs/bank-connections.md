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

1. A private, off-the-record Qt WebEngine profile opens
   `https://www.tbank.ru/login`. Phone, password, and one-time-code fields are
   owned by the bank page; WealthTracker does not read them.
2. During login, WealthTracker observes only the opaque `sessionid` parameter
   already used by the bank page on exact HTTPS common-API requests. A secure
   `psid` cookie for a `tbank.ru` domain is retained only as a compatibility
   fallback. After the user explicitly confirms, the adapter validates the
   candidate by reading accounts and operations. Only a valid opaque session
   identifier is kept in macOS Keychain or Windows Credential Manager; the
   browser profile, cookie jar, local storage, phone, password, and OTP are not
   persisted or written to logs and QML settings.

The bundled adapter makes `GET` requests only to these paths at the exact
origin `https://www.tbank.ru`:

- `/api/common/v1/ping`
- `/api/common/v1/accounts_light_ib`
- `/api/common/v1/operations`

The local host blocks other origins and all redirects for this bundle. The
login page itself may load resources required by T-Bank. Top-level browser
navigation is restricted to `tbank.ru`, its subdomains, and exactly
`id.tinkoff.ru`; the latter is T-Bank's legacy identity migration step in the
current login chain. Other `tinkoff.ru` hosts remain blocked. Browser storage
is off-the-record and is cleared after a successful connection or cancellation.

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
It reuses the current bank page's own opaque API session and retains no reusable
PIN or device credential.

Connection steps:

1. Install Node.js 18 or newer.
2. Open **Connections → Т-Банк** and choose **Войти на tbank.ru**.
3. Complete the official bank page.
4. Wait for **API-сессия обнаружена** or **Сессия обнаружена**, then choose
   **Подключить**. If the bank page has completed but neither message has
   appeared, choose **Проверить сессию** to enumerate the secure-cookie
   fallback. The app validates the session before marking the connection as
   active or closing the login page.

Certificate errors stop the login and are displayed in the dialog. Fix the
local certificate trust or TLS-inspection configuration instead of bypassing
the warning.

### T-Bank login troubleshooting

Qt WebEngine can print messages from Chromium that do not, by themselves,
mean that authentication failed:

- `Failed to create WebGPU Context Provider` means GPU-based WebGPU was not
  available. The login form does not require WebGPU.
- DNS failures for `stun3.l.google.com` or `stun4.l.google.com` come from
  WebRTC discovery used by page scripts. They are unrelated to T-Bank session
  storage.
- The malformed `t-static.ruimagessolid` favicon URL is rejected by the bank
  page's own Content Security Policy.

The isolated login browser disables non-proxied WebRTC UDP before WebEngine
starts. This prevents the direct UDP probes responsible for the public STUN
lookups while retaining WebRTC's TCP and configured-proxy paths. It also
filters the known WebGPU, geolocation-order, and malformed-favicon console
messages listed above. An unknown JavaScript error still produces one generic
warning without forwarding bank-controlled message text that could contain
private data. Navigation, loading, and certificate errors continue to use
their dedicated handlers.

`net_error -202` is different: Chromium rejected a certificate whose authority
is not trusted. WealthTracker rejects it by default. The connection status and
runtime log report only the failing hostname, numeric certificate category, and
whether the failure was for the main frame or a subresource. URL paths, query
strings, cookies, and credentials are deliberately omitted.

T-Bank currently loads `mddc.tbank.ru` and `mddc.tinkoff.ru` as device-data
collector subresources. Their certificates are issued by the Russian Trusted
Sub CA, which is not trusted by macOS and Chromium by default. T-Bank documents
the same CA migration and uses `mddc.tbank.ru` in its
[certificate setup example](https://developer.tbank.ru/docs/tls-settings).
WealthTracker blocks these two exact hosts before a request is sent. It also
blocks the optional analytics hosts `api-statist.tinkoff.ru` and
`api-statist.dev-tcsgroup.io`; the latter's observed name-mismatch error is
never overridden.

The authenticated page also contacts `api-common.tinkoff.ru`, which is required
for the login flow and now uses the same Russian CA. For this one exact host,
WealthTracker accepts only an overridable `CertificateAuthorityInvalid` error
whose Chromium-validated peer chain ends at the pinned SHA-256 fingerprint of
the official Russian Trusted Root CA. Intermediate rotations are allowed below
that pinned root. Host-name mismatch, expiry, revocation, interception, every
other error category, every other host, and every different root remain fatal.

This policy neither installs the Russian root system-wide nor enables a generic
certificate-ignore flag. It is applied only while the off-the-record T-Bank
login profile is active. The read-only adapter continues to call
`www.tbank.ru`, whose current certificate is accepted by the normal system
trust store.

When `-202` from any other host prevents login:

1. Ensure macOS date and time are set automatically.
2. Open `https://www.tbank.ru/login` in a regular browser on the same Mac and
   inspect any certificate warning.
3. Check whether a VPN, antivirus, corporate proxy, or filtering DNS service is
   intercepting HTTPS. If it is required, its administrator-provided root must
   be correctly installed and trusted in Keychain Access.
4. Rebuild and retry. Do not enable Chromium's certificate-ignore flags and do
   not accept an unknown certificate for a bank login.

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
