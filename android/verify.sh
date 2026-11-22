#!/usr/bin/env sh
# Build and verify synthetic fixtures on an explicitly selected emulator only.
set -eu
if [ "$#" -ne 1 ]; then
    echo "Usage: JAVA_HOME=... ANDROID_SDK_ROOT=... sh android/verify.sh emulator-5554" >&2
    exit 2
fi
serial=$1
case "$serial" in
    emulator-*) ;;
    *) echo "An explicit emulator serial is required; physical devices are not supported." >&2; exit 2 ;;
esac
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"
. build/tooling/bin/activate
: "${JAVA_HOME:?Set JAVA_HOME to a JDK 21 installation}"
: "${ANDROID_SDK_ROOT:?Set ANDROID_SDK_ROOT to an Android SDK installation}"
ANDROID_NDK_ROOT=${ANDROID_NDK_ROOT:-"${ANDROID_SDK_ROOT}/ndk/29.0.14206865"}
ANDROID_HOME=$ANDROID_SDK_ROOT
ANDROID_USER_HOME=${ANDROID_USER_HOME:-"${project_dir}/android/.local/user"}
GRADLE_USER_HOME=${GRADLE_USER_HOME:-"${project_dir}/build/android-tools/gradle"}
export JAVA_HOME ANDROID_SDK_ROOT ANDROID_HOME ANDROID_NDK_ROOT ANDROID_USER_HOME GRADLE_USER_HOME
adb="${ANDROID_SDK_ROOT}/platform-tools/adb"
if [ ! -x "$adb" ] || [ "$("$adb" -s "$serial" get-state)" != device ] ||
   [ "$("$adb" -s "$serial" shell getprop ro.kernel.qemu | tr -d '\r')" != 1 ] ||
   [ "$("$adb" -s "$serial" shell getprop sys.boot_completed | tr -d '\r')" != 1 ]; then
    echo "The selected emulator must be connected and fully booted." >&2
    exit 2
fi
# Reuse the normal package build and its prerequisite/signature checks.
sh android/build.sh
android_version=$(python -c 'import json; print(json.load(open("version.json"))["androidName"])')
app_apk="android/output/wealthtracker-${android_version}-debug.apk"
app_build="${project_dir}/build/android-release/ui/qml/android-build"
(cd "$app_build" && ./gradlew --no-daemon assembleDebugAndroidTest)
test_apk="${app_build}/build/outputs/apk/androidTest/debug/android-build-debug-androidTest.apk"
[ -f "$test_apk" ] || { echo "Instrumentation APK was not produced." >&2; exit 1; }
"$adb" -s "$serial" install -r "$app_apk"
"$adb" -s "$serial" install -r "$test_apk"
"$adb" -s "$serial" shell am instrument -w \
    org.wealthtracker.android.test/androidx.test.runner.AndroidJUnitRunner \
    > build/android-instrumentation.log 2>&1
cat build/android-instrumentation.log
python - <<'PY'
from pathlib import Path
import re
text = Path('build/android-instrumentation.log').read_text()
if (not re.search(r'OK \([1-9][0-9]* tests?\)', text)
        or re.search(r'FAILURES!!!|INSTRUMENTATION_FAILED|shortMsg=|Process crashed', text)):
    raise SystemExit('Android instrumentation did not report a successful nonempty test run')
PY
qt_android="${project_dir}/build/qt/6.11.1/android_arm64_v8a"
qt_host="${project_dir}/build/qt/6.11.1/macos"
cmake -S android/tests/view_probe -B build/android-view-probe -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${qt_android}/lib/cmake/Qt6/qt.toolchain.cmake" \
    -DQT_HOST_PATH="$qt_host" -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" \
    -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" -DCMAKE_BUILD_TYPE=Debug
cmake --build build/android-view-probe --target view_probe_make_apk -j 6
"$adb" -s "$serial" install -r build/android-view-probe/android-build/view_probe.apk
"$adb" -s "$serial" shell am force-stop org.wealthtracker.android.probe
# Only the owned emulator's log is cleared; an old success cannot satisfy this run.
"$adb" -s "$serial" logcat -c
"$adb" -s "$serial" shell am start -n \
    org.wealthtracker.android.probe/org.qtproject.qt.android.bindings.QtActivity \
    --es applicationArguments --guarded-callback-probe
python - "$adb" "$serial" <<'PY'
from pathlib import Path
import subprocess
import sys
import time
adb, serial = sys.argv[1:]
expected = ('WT_CALLBACK_PROBE guarded_owner_thread=1 callbacks=3 '
            'ticks_progress=1 canceled_callback=0 values_valid=1')
deadline = time.monotonic() + 30
while time.monotonic() < deadline:
    text = subprocess.check_output([adb, '-s', serial, 'logcat', '-d', '-v', 'brief'],
                                   timeout=5, text=True, errors='replace')
    Path('build/android-callback-probe.log').write_text(text)
    lines = [line for line in text.splitlines() if 'WT_CALLBACK_PROBE' in line]
    if lines:
        print('\n'.join(lines))
        if not any(expected in line for line in lines):
            raise SystemExit('Native callback probe reported a failed contract')
        break
    time.sleep(0.25)
else:
    raise SystemExit('Native callback probe timed out without a fresh result')
PY
"$adb" -s "$serial" shell am force-stop org.wealthtracker.android.probe
echo "Android instrumentation and guarded native callback verification passed on $serial."
