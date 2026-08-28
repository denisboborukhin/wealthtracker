#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool_dir="${project_dir}/build/tooling"

python_command=${WT_BOOTSTRAP_PYTHON:-}
if [ -z "${python_command}" ]; then
  for candidate in python3.13 python3.12 python3.11 python3.10 python3; do
    if command -v "${candidate}" >/dev/null 2>&1 &&
      "${candidate}" -c 'import ssl, sys; raise SystemExit(0 if sys.version_info >= (3, 9) and ssl.OPENSSL_VERSION.startswith("OpenSSL ") and ssl.OPENSSL_VERSION_INFO >= (1, 1, 1) else 1)' \
        >/dev/null 2>&1; then
      python_command=$(command -v "${candidate}")
      break
    fi
  done
fi

if [ -z "${python_command}" ] ||
  ! "${python_command}" -c 'import ssl, sys; raise SystemExit(0 if sys.version_info >= (3, 9) and ssl.OPENSSL_VERSION.startswith("OpenSSL ") and ssl.OPENSSL_VERSION_INFO >= (1, 1, 1) else 1)' \
    >/dev/null 2>&1; then
  echo "Python 3.9+ linked to OpenSSL 1.1.1 or newer is required." >&2
  echo "Install a current Python or set WT_BOOTSTRAP_PYTHON to its executable." >&2
  exit 1
fi

qt_mirror=${WT_QT_MIRROR:-https://ftp.fau.de/qtproject/}
case "${qt_mirror}" in
  https://*) ;;
  *)
    echo "WT_QT_MIRROR must use HTTPS." >&2
    exit 1
    ;;
esac

"${python_command}" -m venv --clear "${tool_dir}"
"${tool_dir}/bin/python" -m pip install \
  "aqtinstall==3.3.0" \
  "conan==2.19.1" \
  "cmake==4.1.2" \
  "ninja==1.13.0"

case "$(uname -s)" in
  Darwin)
    qt_host="mac"
    qt_arch="clang_64"
    ;;
  Linux)
    qt_host="linux"
    qt_arch="linux_gcc_64"
    ;;
  *)
    echo "Unsupported host. Use scripts/bootstrap.ps1 on Windows." >&2
    exit 1
    ;;
esac

"${tool_dir}/bin/aqt" -c "${project_dir}/scripts/aqt.ini" install-qt \
  "${qt_host}" desktop 6.11.1 "${qt_arch}" \
  --modules qtwebview \
  --base "${qt_mirror}" \
  -O "${project_dir}/build/qt"

echo "Tools installed under ${tool_dir}"
echo "Qt installed under ${project_dir}/build/qt/6.11.1"
echo "Activate the tools with: . build/tooling/bin/activate"
