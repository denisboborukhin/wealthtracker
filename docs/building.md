# Building WealthTracker

## Supported development targets

- Windows x86-64 with MSVC 2022
- macOS arm64 or x86-64 with Apple Clang
- Linux x86-64 with GCC or Clang, best effort

## Configure

Install the pinned toolchain and official prebuilt Qt SDK:

```sh
./scripts/bootstrap.sh
. build/tooling/bin/activate
```

On Windows, run `scripts/bootstrap.ps1` followed by
`& build/tooling/Scripts/Activate.ps1` from PowerShell instead. Keep the
tool environment active while installing dependencies, because Conan recipes
invoke CMake and Ninja by name.

Install the remaining dependencies with Conan and configure the project:

```sh
conan install . \
  --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug \
  -of build/macos-debug
./scripts/configure.sh macos-debug
```

The configure script selects full Xcode Clang or Homebrew LLVM together with
its `clang-scan-deps` executable, which CMake needs for C++23 named modules.
Set `WEALTHTRACKER_LLVM_ROOT` to select another LLVM installation. On Windows,
configure with `cmake --preset windows-debug` instead.

Build and test:

```sh
cmake --build --preset macos-debug
ctest --preset macos-debug
```

Conan must use locked dependency revisions for release builds. Qt comes from
the official prebuilt SDK and is linked dynamically so users retain the
replacement rights required by LGPL v3.
