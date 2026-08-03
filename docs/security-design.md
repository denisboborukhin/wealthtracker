# Security design

## Vault

A new vault receives a random 256-bit SQLCipher key. By default it is stored in
the operating-system credential store. When PIN protection is enabled, the
database key is wrapped with a key derived using Argon2id and the direct
credential-store entry is removed.

The application locks on suspend and after an idle timeout. Decrypted key
material is cleared when the vault locks.

## Connectors

T-Invest accepts only a user-generated read-only token. Other Russian banks are
supported through statement files until legitimate consumer APIs are generally
available.

Logs redact credentials, authorization headers, account identifiers, and
imported descriptions. No telemetry or remote crash reporter is present.

The retail T-Bank browser uses an off-the-record profile. Optional device and
analytics collectors are blocked by exact hostname. The one Russian-CA endpoint
required by login, `api-common.tinkoff.ru`, is accepted only for Chromium's
unknown-authority error when the verified chain ends at the pinned official
Russian Trusted Root CA. This does not alter the operating-system trust store;
all host-name, date, revocation, interception, and other certificate errors
remain fatal.

## Backups

Portable backups are encrypted with a distinct user passphrase and include an
authenticated manifest containing schema and application versions. Plain CSV
exports require a warning because they are not encrypted.
