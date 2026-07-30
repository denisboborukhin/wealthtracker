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

Release packages must include the generated Conan dependency inventory, Qt
LGPL text, Qt SBOM for the shipped version, and notices for the third-party
components embedded in the selected Qt modules. Qt libraries are distributed
dynamically and must remain replaceable by the user.

