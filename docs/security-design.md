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

Retail T-Bank authentication runs in a fresh temporary Chrome/Chromium profile,
not embedded Qt WebEngine and not the user's normal browser profile. The
browser remains subject to the operating system's ordinary TLS and certificate
policy; the application does not override bank certificate failures.

The helper's DevTools endpoint uses a random port bound only to `127.0.0.1` and
exists only during login. The helper attaches before navigation and performs
only target attachment, page navigation, and a fixed runtime expression. It
does not enable DevTools Network events or request cookies, headers, bodies,
DOM content, local storage, phone, password, or OTP. The expression filters
resource-timing entries inside the page and returns only bounded session IDs
from exact HTTPS `www.tbank.ru/api/common/v1/` URLs. A malicious local process
that discovers the temporary port could still attach while login is active;
the isolated profile prevents exposure of the user's existing browser data.

C++ validates the bounded helper event, then a separate cookie-disabled Qt
client polls the exact candidate session. Only `CLIENT` access is accepted and
validated through read-only account requests. On success, cancellation, or
failure, Chrome is stopped and the temporary profile is deleted. The session
identifier is never logged or exposed to QML and is removed from the credential
store if account validation fails.

## Backups

Portable backups are encrypted with a distinct user passphrase and include an
authenticated manifest containing schema and application versions. Plain CSV
exports require a warning because they are not encrypted.
