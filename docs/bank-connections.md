# Bank connections

WealthTracker ships only documented, read-only API connections. A connection
token is saved under a separate key in macOS Keychain or Windows Credential
Manager. The application never stores that token in its database, follows no
HTTP redirects automatically, and sends requests only to the exact host listed
for the connector.

## Support matrix

| Connection | Accounts | Balances | Operations | Who can use it |
| --- | --- | --- | --- | --- |
| Alfa-Bank B2C API | Yes | Yes | Yes | A registered integration with a user access token |
| T-Bank Business | Yes | Yes | Yes | An organization with the required T-API permissions |
| Modulbank | Yes | Yes | Yes | A Modulbank business customer or the public sandbox |
| T-Invest | Yes | Portfolio | Yes | A user with a read-only investment token |
| Retail T-Bank | No | No | No | No supported official consumer account API |
| Sber retail | No | No | No | No supported official consumer account API |
| VTB retail | No | No | No | No supported official consumer account API |

The three bank connectors retrieve up to the previous 12 months and follow
every pagination cursor. Data is normalized locally and combined across
connected banks. RUB totals are used for dashboard summaries; foreign-currency
accounts remain visible in their original currencies and are not silently
converted.

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

## Why retail password login is not included

ZenMoney demonstrates a useful local plugin architecture, but a bank-specific
implementation still needs a maintained endpoint strategy and a compatible
license. WealthTracker does not copy GPL-3.0 ZenPlugins code into this
MIT-licensed repository and does not ship fragile password-scraping against
private web or mobile endpoints.
