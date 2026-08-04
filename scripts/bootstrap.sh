#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool_dir="${project_dir}/build/tooling"

python3 -m venv "${tool_dir}"
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

"${tool_dir}/bin/aqt" install-qt \
  "${qt_host}" desktop 6.11.1 "${qt_arch}" \
  --modules qtwebview \
  -O "${project_dir}/build/qt"

echo "Tools installed under ${tool_dir}"
echo "Qt installed under ${project_dir}/build/qt/6.11.1"
echo "Activate the tools with: . build/tooling/bin/activate"
