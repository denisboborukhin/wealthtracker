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

Connector API v1 separates file imports from live read-only synchronization.
Only compiled, audited connectors are loaded in v0.1.

Every network connector declares an HTTPS host allowlist. Write, trading,
payment, and transfer endpoints are outside the contract.
