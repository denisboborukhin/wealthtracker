#!/usr/bin/env sh
# Build an ARM64 APK for personal sideloading; keep the signing key for updates.
set -eu
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
qt_android="${project_dir}/build/qt/6.11.1/android_arm64_v8a"
qt_host="${project_dir}/build/qt/6.11.1/macos"
if [ ! -f "${qt_android}/lib/cmake/Qt6/Qt6Config.cmake" ] ||
   [ ! -x "${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang-scan-deps" ] ||
   [ ! -f "${ANDROID_SDK_ROOT}/platforms/android-36/android.jar" ] ||
   [ ! -x "${JAVA_HOME}/bin/javac" ]; then
    echo "Android prerequisites are incomplete; see android/README.md." >&2
    exit 1
fi
mkdir -p "$ANDROID_USER_HOME" "$GRADLE_USER_HOME" build/conan-android android/output
# Conan 2.21's base schema predates NDK 29's Clang 21. Extend only this cache.
cat > build/conan-android/settings_user.yml <<'YAML'
compiler:
    clang:
        version: ["21"]
YAML
CONAN_HOME="${project_dir}/build/conan-android" conan install . \
    --profile:host=android/conan.profile \
    --profile:build=conan/profiles/macos-arm64 --build=missing -of build/android-release
cmake -S . -B build/android-release -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${project_dir}/build/android-release/generators/conan_toolchain.cmake" \
    -DCMAKE_PREFIX_PATH="$qt_android" -DQT_HOST_PATH="$qt_host" \
    -DANDROID_SDK_ROOT="$ANDROID_SDK_ROOT" -DANDROID_NDK_ROOT="$ANDROID_NDK_ROOT" \
    -DCMAKE_CXX_COMPILER_CLANG_SCAN_DEPS="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang-scan-deps" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE=Release -DQT_ANDROID_DEPLOYMENT_TYPE=Debug
cmake --build build/android-release --target wealthtracker_make_apk -j 6
android_version=$(python -c 'import json; print(json.load(open("version.json"))["androidName"])')
apk="android/output/wealthtracker-${android_version}-debug.apk"
cp build/android-release/ui/qml/android-build/wealthtracker.apk "$apk"
"${ANDROID_SDK_ROOT}/build-tools/36.0.0/apksigner" verify --verbose "$apk"
(cd android/output && shasum -a 256 "${apk##*/}") > "${apk}.sha256"
cat "${apk}.sha256"
