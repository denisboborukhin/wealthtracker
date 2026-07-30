# Building WealthTracker

## Supported development targets

- Windows x86-64 with MSVC 2022
- macOS arm64 or x86-64 with Apple Clang
- Linux x86-64 with GCC or Clang, best effort

## Configure

Install Conan 2, CMake, and Ninja, then create or detect a Conan profile:

```sh
conan profile detect --force
conan install . --build=missing -s build_type=Debug \
  -s compiler.cppstd=23 \
  -of build/macos-debug
cmake --preset macos-debug
```

Build and test:

```sh
cmake --build --preset macos-debug
ctest --preset macos-debug
```

Conan must use locked dependency revisions for release builds. Qt is linked
dynamically so users retain the replacement rights required by LGPL v3.
