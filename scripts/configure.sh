#!/usr/bin/env sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tool_bin="${project_dir}/build/tooling/bin"
preset="${1:-macos-debug}"

if [ ! -x "${tool_bin}/cmake" ]; then
  echo "Build tools are missing. Run ./scripts/bootstrap.sh first." >&2
  exit 1
fi

PATH="${tool_bin}:${PATH}"
export PATH

if [ "$(uname -s)" != "Darwin" ]; then
  exec cmake --preset "${preset}"
fi

compiler=
scanner=

if [ -n "${WEALTHTRACKER_LLVM_ROOT:-}" ]; then
  compiler="${WEALTHTRACKER_LLVM_ROOT}/bin/clang++"
  scanner="${WEALTHTRACKER_LLVM_ROOT}/bin/clang-scan-deps"
elif command -v xcrun >/dev/null 2>&1 &&
  scanner=$(xcrun --find clang-scan-deps 2>/dev/null); then
  compiler=$(xcrun --find clang++)
elif scanner=$(command -v clang-scan-deps 2>/dev/null); then
  compiler="$(dirname -- "${scanner}")/clang++"
elif command -v brew >/dev/null 2>&1; then
  llvm_root=$(brew --prefix llvm 2>/dev/null || true)
  compiler="${llvm_root}/bin/clang++"
  scanner="${llvm_root}/bin/clang-scan-deps"
fi

if [ ! -x "${compiler}" ] || [ ! -x "${scanner}" ]; then
  echo "C++23 modules require Clang and clang-scan-deps." >&2
  echo "Install full Xcode or Homebrew LLVM, then retry." >&2
  echo "A custom LLVM can be selected with WEALTHTRACKER_LLVM_ROOT." >&2
  exit 1
fi

cache_file="${project_dir}/build/${preset}/CMakeCache.txt"
if [ -f "${cache_file}" ]; then
  cached_compiler=$(awk -F= \
    '/^CMAKE_CXX_COMPILER:[^=]*=/{sub(/^[^=]*=/, ""); print; exit}' \
    "${cache_file}")
  if [ "${cached_compiler}" != "${compiler}" ]; then
    exec cmake --fresh --preset "${preset}" \
      -DCMAKE_CXX_COMPILER="${compiler}" \
      -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="${scanner}"
  fi
fi

exec cmake --preset "${preset}" \
  -DCMAKE_CXX_COMPILER="${compiler}" \
  -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="${scanner}"
