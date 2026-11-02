#!/usr/bin/env sh
set -eu
runner=$1
source_dir=$2
module_dir=$3
scratch=$(mktemp -d "${TMPDIR:-/tmp}/wealthtracker-ui.XXXXXX")
trap 'rm -rf "$scratch"' EXIT HUP INT TERM
# The executable embeds its QML. The standalone test runner needs a filesystem module.
cp -R "$source_dir/ui/qml" "$scratch/WealthTracker"
sed '/^prefer /d' "$module_dir/qmldir" > "$scratch/WealthTracker/qmldir"
# qmlcachegen supplies the implicit module import in the application build.
for file in "$scratch"/WealthTracker/pages/*.qml "$scratch"/WealthTracker/components/*.qml; do
    awk '{ print } $0 == "import QtQuick" { print "import WealthTracker" }' "$file" > "$file.tmp"
    mv "$file.tmp" "$file"
done
QT_QUICK_CONTROLS_STYLE=Basic "$runner" -platform offscreen -input "$source_dir/tests/ui" -import "$scratch"
