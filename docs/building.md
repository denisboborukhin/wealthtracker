# Building WealthTracker

## Supported development targets

- Windows x86-64 with MSVC 2022
- macOS arm64 or x86-64 with Apple Clang
- Linux x86-64 with GCC or Clang, best effort

## Configure

Install the pinned toolchain and official prebuilt Qt SDK:

```sh
./scripts/bootstrap.sh
```

On Windows, run `scripts/bootstrap.ps1` from PowerShell instead. Then install
the remaining dependencies with Conan and configure the project:

```sh
build/tooling/bin/conan install . \
  --profile:host=conan/profiles/macos-arm64 \
  --profile:build=conan/profiles/macos-arm64 \
  --build=missing -s:h build_type=Debug \
  -of build/macos-debug
build/tooling/bin/cmake --preset macos-debug
```

Build and test:

```sh
build/tooling/bin/cmake --build --preset macos-debug
build/tooling/bin/ctest --preset macos-debug
```

Conan must use locked dependency revisions for release builds. Qt comes from
the official prebuilt SDK and is linked dynamically so users retain the
replacement rights required by LGPL v3.
