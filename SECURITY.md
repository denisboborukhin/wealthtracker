# Security policy

## Supported versions

Security updates are provided for the latest released version.

## Reporting

Do not open a public issue containing secrets, bank statements, tokens, or
other personal financial information. Contact a maintainer privately before
sharing reproduction material.

## Security model

- WealthTracker is a local, single-user application.
- The database is encrypted with SQLCipher.
- API tokens are stored in the operating-system credential store.
- Live connectors allow only their documented HTTPS host and read-only
  account/statement methods. Redirects are not followed automatically.
- Token byte arrays are wiped after each request is constructed and are never
  included in application status messages or logs.
- Optional PIN protection wraps the database key using Argon2id-derived key
  material and authenticated encryption.
- Bank login passwords, SMS codes, browser cookies, and trading credentials
  are never requested by the bundled connectors.
- Telemetry and remote crash reporting are not included.

The application cannot protect data from an administrator who controls the
running operating system, malware executing as the same user, or screenshots
captured while the vault is unlocked.
