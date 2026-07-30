# Architecture

WealthTracker is a native C++23 desktop application with a Qt Quick interface.
Core subsystem boundaries are C++ named modules. Conventional headers are
limited to Qt `moc`, operating-system ABI, and third-party C API boundaries.

## Module boundaries

- `core`: domain values, financial rules, categorization, and analytics.
- `storage`: SQLCipher connection, migrations, repositories, and backups.
- `platform`: credential storage, secure memory, and OS integration.
- `connectors`: file imports and explicitly read-only network integrations.
- `app`: composition, use cases, and QML-facing controllers.
- `ui`: QML pages, components, and translations.

QML does not receive database handles, encryption keys, tokens, or arbitrary
network access. Controllers expose narrow asynchronous operations and immutable
models.

## Data flow

Statement imports follow a preview-first atomic pipeline:

1. Probe the source and select a parser.
2. Parse into a normalized batch without changing the database.
3. Show warnings, mapping choices, and duplicate candidates.
4. Reconcile accounts and balances.
5. Commit the whole batch in one transaction or roll it back.

User edits are overlays on immutable source records so re-importing a statement
does not discard categories, splits, tags, or notes.

## Connectors

Connector API v1 separates file imports, token-based read-only synchronization,
and interactive bank sessions. An interactive connector advances through
typed challenges:

1. Collect a small set of named fields such as phone, login, password, or MFA.
2. Optionally open a clean HTTPS bank authorization URL.
3. Return normalized accounts, balances, and transactions only after the
   session is authenticated.

Password and one-time-code fields must declare `Never` retention. A connector
may retain a revocable session or refresh token in the operating system's
credential store, but it cannot expose secrets to QML.

Every network connector declares an HTTPS host allowlist. Write, trading,
payment, and transfer endpoints are outside the contract.

The interface is a clean-room C++ design inspired by ZenMoney's local plugin
model, not a source or binary compatibility layer. No retail-bank plugin is
bundled yet. The current live connector is T-Invest's official read-only API;
retail banks remain available through statement import until audited
bank-specific plugins are implemented.
