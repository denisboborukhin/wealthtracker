#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool_dir="${project_dir}/build/tooling"

python3 -m venv "${tool_dir}"
"${tool_dir}/bin/python" -m pip install \
  "conan==2.19.1" \
  "cmake==4.1.2" \
  "ninja==1.13.0"

echo "Tools installed under ${tool_dir}"
echo "Use ${tool_dir}/bin/conan and ${tool_dir}/bin/cmake."

