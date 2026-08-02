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
- The retail T-Bank login runs in an off-the-record profile on T-Bank's
  `tbank.ru` pages. Top-level navigation also permits exactly
  `id.tinkoff.ru`, the bank's legacy identity host used by its current migration
  redirect. WealthTracker does not read phone, password, or one-time-code
  fields. It retains only the latest secure `psid` session cookie for
  `tbank.ru` in wiped process memory during login. The value is never logged
  and is persisted to the operating-system credential store only after
  explicit user confirmation and successful read-access validation. Invalid
  certificate errors are shown to the user and are never bypassed.
- T-Bank TLS diagnostics contain only the ASCII hostname, numeric certificate
  category, and main-frame/subresource scope. URL paths, query strings,
  cookies, and credentials are not included in status text or runtime logs.
- The isolated login profile blocks only `mddc.tbank.ru` and
  `mddc.tinkoff.ru`, whose optional device-collector subresources use the
  Russian Trusted CA. WealthTracker does not install that root, accept their
  authority-invalid certificates, or weaken validation for any other host.
- The built-in T-Bank importer is restricted to the exact HTTPS origin
  `https://www.tbank.ru`, blocks redirects, and implements only account,
  balance, and operation reads. The underlying retail web API is undocumented
  and may stop working without notice.
- Optional external ZenPlugins may request login fields and one-time codes in
  the local connection dialog. Passwords and prompt responses are
  session-only. Non-secret stable preferences such as phone, login, or start
  date can be saved in local settings for refresh. Returned plugin data and
  cookies are stored in the operating system credential store.
- Telemetry and remote crash reporting are not included.

The T-Bank `psid` grants access to financial data and must be treated like an
API token. Disconnecting T-Bank deletes its credential entry and browser
storage. If a device is lost or compromised, terminate active sessions through
the bank and change the bank password according to T-Bank's guidance.

## External plugin boundary

An external ZenPlugins bundle is arbitrary third-party JavaScript and is not
sandboxed by WealthTracker. It can access the network and the current user's
files with the permissions of its Node.js child process. WealthTracker
validates message sizes and result schemas, suppresses plugin stdout logs, and
does not pass database keys or official API tokens to the process, but it
cannot make an untrusted bundle safe.

Build plugins in a separate checkout, pin and review the source commit, and do
not run bundles received as opaque downloads. The GPL-3.0 plugin and bundle are
not distributed as part of WealthTracker.

The application cannot protect data from an administrator who controls the
running operating system, malware executing as the same user, or screenshots
captured while the vault is unlocked.
