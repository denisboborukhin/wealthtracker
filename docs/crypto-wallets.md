# Crypto wallets

WealthTracker watches public EVM addresses through Rabby's public read endpoints.
No account registration, service API key, wallet connection, private key, or seed
phrase is required. Add a name and address in **Криптокошельки**.

## Networks and refresh

The app fetches Rabby's current network catalog, discovers networks used by the
address, and reads each used network explicitly. Native coins and indexed ERC-20
tokens are included. The verified catalog contained 71 networks on 2026-10-22,
including Ethereum, BNB Chain, Arbitrum, Base, Optimism, Polygon, Avalanche, Gnosis,
zkSync, Linea, and Scroll. The wallet page shows discovered networks and its
supported-network catalog after a successful refresh.

Coverage is limited to Rabby's indexed EVM networks. An arbitrary custom network
that MetaMask accepts is not necessarily indexed. NFT inventories and DeFi
position discovery are outside this integration's scope.

Refresh runs after adding a wallet, at startup, manually, and every six hours
while the app is running and unlocked. Requests use Rabby's own
[used-chain and per-chain token-list sequence](https://github.com/RabbyHub/Rabby/blob/master/src/ui/utils/portfolio/tokenUtils.ts).
The app requests `is_all=true`, preserving unknown/unpriced holdings. Cached
portfolio responses alone are not treated as completed fresh refreshes.

Rabby receives queried public addresses. These read routes accepted keyless,
unsigned requests in live public-address checks; Rabby does not publish a
third-party keyless availability guarantee. A denied request, rate limit, timeout,
malformed response, or failed chain preserves the previous combined snapshot.
Requests are sequential, spaced by 1.1 seconds. Up to two rate-limit responses
per wallet wait for a bounded Retry-After delay (ten seconds when absent) before
retrying; the overall deadline still applies. Requests are bounded to 128 networks, 5,000 tokens
per chain, 20,000 tokens per wallet, 4 MiB per response, and five minutes per wallet.

## Values

Exact hexadecimal balances and decimal quote lexemes feed integer arithmetic.
USDT valuation divides the sum of asset quantities times their USD quotes by
the actual USDT/USD quote. One USDT is not assumed to equal one USD. The subtotal
and rows are rounded independently to two decimals and can differ by rounding.

Assets without usable prices/precision, or marked suspicious/scam by the provider,
remain visible and are excluded from the valued subtotal. Missing USDT pricing
rejects the refresh. Provider indexing and price coverage can therefore make this
subtotal smaller than the wallet's full economic value.

## Saved history

Names and addresses persist in encrypted `balance-history.db`. Each successful
complete wallet refresh saves its valued subtotal in integer hundredths of USDT.
Removing a wallet retains its saved history. Existing Ethereum history retains
its identifier when the same address begins tracking multiple networks; the
scope increase may itself change the total. Token-level holdings and network
coverage remain in memory and are refreshed after restart.

**История** shows balances grouped by connection: each color identifies a bank
or EVM wallet, and a line shows the net total. Hover details show signed amounts
and percentage shares. Positive balances stack above zero and negative balances
below it; shares use absolute connection balances when negatives are present.
**Настроить** contains source, currency, and date controls. Currencies remain
separate. Earlier snapshots may carry forward to the period start, with their
original timestamp retained. Balance changes are not investment returns.

## Verification

Tests cover exact valuation and exponent quotes, captured public response fields,
multichain identities, missing prices, failures, cancellation, encrypted history,
restart restoration, and chart composition. Public-address endpoint probes
succeeded without credentials. Those probes do not guarantee future provider
availability or verify a particular user's complete portfolio.

Sources: [coordinator](../src/app/crypto_wallet.cppm),
[controller](../src/app/AppController.cpp),
[history projection](../src/app/balance_history_view.cppm),
[tests](../tests/app/crypto_wallet_test.cpp).
