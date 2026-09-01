# Third-party notices

WealthTracker application source is licensed under MIT. The following principal
dependencies retain their own licenses:

- Qt 6 — GNU Lesser General Public License v3 for the dynamically linked
  modules used by the application. Qt source and license information:
  <https://www.qt.io/licensing/open-source-lgpl-obligations>
- SQLCipher — BSD 3-Clause: <https://github.com/sqlcipher/sqlcipher>
- OpenSSL — Apache License 2.0:
  <https://www.openssl.org/source/license.html>
- libsodium — ISC: <https://github.com/jedisct1/libsodium>
- Boost — Boost Software License 1.0:
  <https://www.boost.org/users/license.html>
- xlsxio — MIT: <https://github.com/brechtsanders/xlsxio>
- Catch2 — Boost Software License 1.0:
  <https://github.com/catchorg/Catch2>

The clean-room retail T-Bank adapter was informed by the endpoint and browser
session behavior documented in `jfk9w-go/tbank-api`, which is MIT-licensed:
<https://github.com/jfk9w-go/tbank-api>. WealthTracker does not embed that Go
library; the notice records protocol provenance and its compatible license.

Retail Alfa-Bank protocol research included Alfa's public web login and the
historical `ab-money-alfabank` provider in
<https://github.com/dukei/any-balance-providers>. WealthTracker does not copy,
execute, or distribute that provider; the connector is an independent C++/Qt
implementation and the reference is recorded only as protocol provenance.

Node.js is an optional user-installed runtime for external local plugins only;
it is not included in WealthTracker packages. ZenPlugins source and generated
bundles are GPL-3.0, are not included in this repository or release packages,
and remain separately obtained works selected by the user.

Development packages include the pinned Conan lockfile and the Qt SDK's SPDX
inventory. Public releases must additionally include the complete applicable
license texts and attribution notices for every shipped component. Qt libraries
are distributed dynamically and must remain replaceable by the user.
