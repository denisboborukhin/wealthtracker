# Changelog

## Unreleased

### Added

- Native retail T-Bank login and read-only account/operation synchronization.
- Official read-only Alfa-Bank B2C, T-Bank Business, Modulbank, and T-Invest
  clients.
- Optional child-process compatibility for separately built ZenPlugins bundles.
- Statement parsers, normalized finance models, analytics, SQLCipher storage
  components, and operating-system credential backends.

### Changed

- Retail T-Bank history requests are serialized, bounded by a transfer timeout,
  and no longer read failed TLS replies.
- Internal T-Bank transfers use a separate category and are excluded from
  income, spending, savings-rate, and spending-category totals.
- A malformed operation is reported and skipped without discarding valid
  account balances and other operations from the same synchronization.
- Split bank synchronization, T-Bank login, and external-plugin execution into
  dedicated C++23 modules with bounded lifecycle ownership.
- Limited synchronization concurrency, pagination, responses, records, and
  exact decimal conversion.
- Replaced implicit QML context access with an explicit application property and
  removed all QML lint warnings.
- Removed non-functional backup, rule, currency, autolock, and startup-sync
  controls from the interface.
- Reframed the lock control as a privacy screen and cancel active work when it
  is enabled.
- Rewrote documentation to distinguish implemented behavior from standalone
  storage components and planned features.

### Removed

- The JavaScript retail T-Bank bundle and its Node.js dependency.
- Obsolete browser/cookie integration code and stale troubleshooting guidance.
