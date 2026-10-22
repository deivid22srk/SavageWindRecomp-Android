#!/bin/bash
# scripts/build_deps_android.sh — everything libmain.so links against, from pinned sources:
#   Vulkan Headers, Vulkan Memory Allocator (headers), SDL3 3.4.16, FFmpeg 9.0.2 (LGPL, only the
#   movie codecs), shaderc v2026.3 (GLSL -> SPIR-V), plus the on-device compiler: clang + lld from
#   Termux's aarch64 packages, repackaged as lib*.so so the APK ships them in nativeLibraryDir
#   (the only place an app may exec from). The compiler's freestanding headers go into the
#   gamelib-src assets, next to the runtime's own.
# Runs in CI before Gradle; each part is skipped when its output exists. Nothing here builds the
# app itself: only its dependencies. Results land in app/src/main/cpp/deps, app/src/main/jniLibs
# and app/src/main/assets (gitignored where they are build products).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$PWD
DEPS=$ROOT/app/src/main/cpp/deps
JNI=$ROOT/app/src/main/jniLibs/arm64-v8a
ASSETS=$ROOT/app/src/main/assets
S=$ROOT/build/deps-src
W=$ROOT/build/deps-work
mkdir -p "$S" "$W" "$DEPS/include" "$DEPS/lib" "$JNI"

NDK=${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME}
PREB=$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin
API=28
CC=$PREB/aarch64-linux-android$API-clang
CXX=$PREB/aarch64-linux-android$API-clang++
AR=$PREB/llvm-ar
READELF=$PREB/llvm-readelf
JOBS=$(nproc)
export PKG_CONFIG_PATH=

fetch() {  # url sha256 file
    [ -f "$S/$3" ] || { echo "downloading $3"; curl -fsSL -o "$S/$3.part" "$1" && mv "$S/$3.part" "$S/$3"; }
    echo "$2  $S/$3" | sha256sum -c --quiet
}
unpack() { rm -rf "$W/$2" && mkdir -p "$W/$2" && tar -xf "$S/$1" -C "$W/$2" --strip-components 1; }
CMAKE_ARGS=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake"
            -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-$API -DCMAKE_BUILD_TYPE=Release)

# ---- Vulkan headers + Vulkan Memory Allocator (headers only)
if [ ! -f "$DEPS/include/vulkan/vulkan.h" ]; then
    fetch https://github.com/KhronosGroup/Vulkan-Headers/archive/refs/tags/vulkan-sdk-1.4.350.1.tar.gz \
          6d1bb65e49520344cc0a48af3dc02e993781efff14c7ebdcb8ae9fa23ddf7e83 Vulkan-Headers.tar.gz
    unpack Vulkan-Headers.tar.gz vulkan-headers
    cp -R "$W/vulkan-headers/include/vulkan" "$W/vulkan-headers/include/vk_video" "$DEPS/include/"
fi
if [ ! -f "$DEPS/include/vma/vk_mem_alloc.h" ]; then
    fetch https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.3.0.tar.gz \
          c4f6bbe6b5a45c2eb610ca9d231158e313086d5b1a40c9922cb42b597419b14e VMA.tar.gz
    unpack VMA.tar.gz vma
    mkdir -p "$DEPS/include/vma" && cp "$W/vma/include/vk_mem_alloc.h" "$DEPS/include/vma/"
fi

# ---- SDL3 (shared: its Java classes drive the app)
if [ ! -f "$DEPS/lib/libSDL3.so" ]; then
    fetch https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz \
          7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68 SDL3.tar.gz
    unpack SDL3.tar.gz sdl
    cmake -S "$W/sdl" -B "$W/sdl/build" "${CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib \
        -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_RPATH=OFF
    cmake --build "$W/sdl/build" -j"$JOBS" && cmake --install "$W/sdl/build"
fi

# ---- FFmpeg (static, LGPL, just what the game's movies need - the desktop AppImage's config)
if [ ! -f "$DEPS/lib/libavcodec.a" ]; then
    fetch https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz \
          8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e ffmpeg.tar.xz
    unpack ffmpeg.tar.xz ffmpeg
    (cd "$W/ffmpeg" && ./configure --prefix="$DEPS" \
        --enable-cross-compile --target-os=linux --arch=aarch64 \
        --cc="$CC --target=aarch64-linux-android$API" \
        --cxx="$CXX --target=aarch64-linux-android$API" \
        --ar=$AR --nm=$PREB/llvm-nm --ranlib=$PREB/llvm-ranlib --strip=$PREB/llvm-strip \
        --enable-static --disable-shared --enable-pic \
        --disable-autodetect --disable-programs --disable-doc --disable-network --disable-avdevice \
        --disable-avfilter --disable-swresample --disable-everything \
        --enable-protocol=file --enable-demuxer=matroska,mov \
        --enable-decoder=vp9,vp8,vorbis,opus,h264,aac --enable-parser=vp9,vp8,vorbis,opus,h264,aac \
        && make -j"$JOBS" && make install)
fi

# ---- shaderc (static: libshaderc + glslang + SPIRV-Tools; linked as a --start-group)
if [ ! -f "$DEPS/lib/shaderc/libshaderc.a" ]; then
    fetch https://github.com/google/shaderc/archive/refs/tags/v2026.3.tar.gz \
          ee493ccf1b3038b4ef2fe024664c5eb2dc4bcc1f6b05b33e3909de0e19c81024 shaderc.tar.gz
    unpack shaderc.tar.gz shaderc
    (cd "$W/shaderc" && python3 utils/git-sync-deps)
    cmake -S "$W/shaderc" -B "$W/shaderc/build" "${CMAKE_ARGS[@]}" \
        -DCMAKE_INSTALL_PREFIX="$DEPS" -DCMAKE_INSTALL_LIBDIR=lib/shaderc \
        -DSHADERC_SKIP_TESTS=ON -DSHADERC_SKIP_EXAMPLES=ON -DSHADERC_SKIP_COPYRIGHT_CHECK=ON \
        -DSPIRV_SKIP_TESTS=ON -DSPIRV_SKIP_EXECUTABLES=ON -DENABLE_GLSLANG_BINARIES=OFF \
        -DSPIRV_TOOLS_BUILD_STATIC=ON -DBUILD_SHARED_LIBS=OFF
    cmake --build "$W/shaderc/build" --target shaderc -j"$JOBS"
    # install would demand archives the shaderc target never builds: collect the .a files instead
    mkdir -p "$DEPS/lib/shaderc"
    find "$W/shaderc/build" -name '*.a' -not -path '*deqp*' -exec cp -n {} "$DEPS/lib/shaderc/" \;
    mkdir -p "$DEPS/include/shaderc"
    cp "$W/shaderc/libshaderc/include/shaderc/"*.h "$DEPS/include/shaderc/"
fi

# ---- the on-device compiler: clang + lld (Termux aarch64 packages, repackaged as lib*.so).
# The app spawns them from nativeLibraryDir (the one exec-allowed location), with ld.lld reached
# through a symlink named for clang's -fuse-ld=lld and the freestanding headers in the gamelib-src
# assets (no sysroot: the game code includes only the runtime's own headers and the shims).
CLANG_VER=$(curl -fsSL https://packages.termux.dev/apt/termux-main/pool/main/c/clang/ | \
            grep -oE 'clang_[0-9.]+-[0-9]+_aarch64\.deb' | sort -V | tail -1 | cut -d_ -f2)
[ -n "$CLANG_VER" ] || { echo "can't pick a Termux clang version"; exit 1; }
LIBLLVM_DEB=$(curl -fsSL https://packages.termux.dev/apt/termux-main/pool/main/libl/libllvm/ | \
              grep -oE "libllvm_${CLANG_VER}_aarch64\.deb" | head -1)
echo "on-device compiler: Termux clang $CLANG_VER"

if [ ! -f "$JNI/libclang.so" ]; then
    T=$W/toolchain
    rm -rf "$T" && mkdir -p "$T"
    deb() {  # url-in-pool package file -> extract
        curl -fsSL -o "$S/$2" "https://packages.termux.dev/apt/termux-main/pool/main/$1" \
            && dpkg -x "$S/$2" "$T/root"
    }
    deb "c/clang/clang_${CLANG_VER}_aarch64.deb" clang.deb
    deb "l/lld/lld_${CLANG_VER}_aarch64.deb" lld.deb
    deb "libl/libllvm/${LIBLLVM_DEB}" libllvm.deb
    deb "libc/libc++/libc++_30_aarch64.deb" libcxx.deb

    U=$T/root/data/data/com.termux/files/usr
    BIN=$U/bin
    CLANG_BIN=$(ls "$BIN" | grep -E '^clang(-[0-9]+)?$' | head -1)
    [ -n "$CLANG_BIN" ] || { echo "clang binary not found in the package"; exit 1; }
    cp "$BIN/$CLANG_BIN" "$JNI/libclang.so"
    LLD_BIN=$(ls "$BIN" | grep -E '^ld\.lld$' | head -1)
    [ -n "$LLD_BIN" ] || LLD_BIN=lld
    cp "$BIN/$LLD_BIN" "$JNI/libld.lld.so"
    # every non-system shared library the two binaries need (libLLVM-21.so and friends), keeping
    # their real names so their sonames resolve
    for exe in "$JNI/libclang.so" "$JNI/libld.lld.so"; do
        $READELF -d "$exe" | grep NEEDED | grep -oE '\[[^]]+\]' | tr -d '[]' | while read -r lib; do
            case $lib in
                libc.so|libm.so|libdl.so|liblog.so|libz.so|libstdc++.so|ld-android.so|libbase.so|libc++_demo.so) ;;
                *)
                    src=$(find "$U/lib" -name "$lib" | head -1)
                    if [ -n "$src" ] && [ ! -f "$JNI/$lib" ]; then cp "$src" "$JNI/$lib"; fi
                    ;;
            esac
        done
    done
    # clang's freestanding builtin headers (stddef, stdarg, stdint, ...) into the gamelib-src
    # assets: the on-device compile has no sysroot, and the runtime/gamebuild.c flow passes
    # -I<gamelib-src> for everything the generated code includes.
    RES=$(find "$U/lib" -maxdepth 3 -type d -name include -path '*clang*' | head -1)
    [ -n "$RES" ] || { echo "clang builtin headers not found"; exit 1; }
    mkdir -p "$ASSETS/clang-include" && cp "$RES"/*.h "$ASSETS/clang-include/"
fi

touch "$DEPS/.ok"
echo "deps ready:"
ls "$DEPS/lib" "$JNI"
