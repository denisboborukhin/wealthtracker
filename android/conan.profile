[settings]
os=Android
os.api_level=28
arch=armv8
compiler=clang
compiler.version=21
compiler.libcxx=c++_shared
compiler.cppstd=23
build_type=Release

[conf]
tools.android:ndk_path={{ os.getenv("ANDROID_NDK_ROOT") }}
tools.cmake.cmaketoolchain:generator=Ninja
tools.build:jobs=6
sqlcipher/*:tools.build:exelinkflags=["-llog"]
sqlcipher/*:tools.build:sharedlinkflags=["-llog"]
tools.cmake.cmaketoolchain:user_presets=
