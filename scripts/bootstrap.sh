#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool_dir="${project_dir}/build/tooling"

if [ "$(uname -s)" != "Darwin" ]; then
  echo "Automatic bootstrap is supported on macOS only." >&2
  echo "Use scripts/bootstrap.ps1 on Windows; Linux builds require a manual Qt toolchain." >&2
  exit 1
fi

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

qt_dir="${project_dir}/build/qt/6.11.1/macos"
qtpaths="${qt_dir}/bin/qtpaths"
qt_version=
if [ -x "${qtpaths}" ]; then
  qt_version=$("${qtpaths}" --qt-version 2>/dev/null || true)
fi

if [ "${qt_version}" = "6.11.1" ] &&
  [ -x "${qt_dir}/bin/qmllint" ] &&
  [ -f "${qt_dir}/lib/cmake/Qt6/Qt6Config.cmake" ] &&
  [ -f "${qt_dir}/lib/cmake/Qt6WebView/Qt6WebViewConfig.cmake" ] &&
  [ -f "${qt_dir}/lib/QtQuick.framework/QtQuick" ] &&
  [ -f "${qt_dir}/lib/QtWebView.framework/QtWebView" ] &&
  [ -f "${qt_dir}/plugins/webview/libqtwebview_darwin.dylib" ]; then
  echo "Reusing complete Qt 6.11.1 SDK under ${qt_dir}"
else
  qt_checksum_url="https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6111/qt6_6111/qt.qt6.6111.clang_64/6.11.1-0-202605090526qtbase-MacOS-MacOS_15-Clang-MacOS-MacOS_15-X86_64-ARM64.7z.sha256"
  if ! "${tool_dir}/bin/python" "${project_dir}/scripts/check_qt_checksum.py" \
    "${qt_checksum_url}"; then
    echo "Qt archives are mirrored, but aqt requires SHA-256 sidecars from download.qt.io." >&2
    echo "Connect through a network or VPN that reaches download.qt.io, or set HTTPS_PROXY, then retry." >&2
    echo "Checksum verification remains enabled and must not be bypassed." >&2
    exit 1
  fi

  "${tool_dir}/bin/aqt" -c "${project_dir}/scripts/aqt.ini" install-qt \
    mac desktop 6.11.1 clang_64 \
    --modules qtwebview \
    --base "${qt_mirror}" \
    -O "${project_dir}/build/qt"
fi

echo "Tools installed under ${tool_dir}"
echo "Qt installed under ${project_dir}/build/qt/6.11.1"
echo "Activate the tools with: . build/tooling/bin/activate"
