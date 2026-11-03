#!/usr/bin/env sh
# Native verification; Android instrumentation and live-bank checks are separate.
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
. build/tooling/bin/activate
cmake --build --preset macos-debug
cmake --build --preset macos-debug --target wealthtracker_qmllint
node --check scripts/zen_plugin_host.mjs
python -m unittest discover -s tests/scripts
ctest --preset macos-debug --output-on-failure
