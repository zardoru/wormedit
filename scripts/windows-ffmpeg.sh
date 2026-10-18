#!/usr/bin/env bash
# Build the pinned LGPL shared FFmpeg SDK used by the Windows release package.
# Usage: scripts/windows-ffmpeg.sh [sdk-root] (default: build/windows-ffmpeg)
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env-mingw.sh
root=$(realpath -m "${1:-build/windows-ffmpeg}")
version=9.0.2
archive="ffmpeg-$version.tar.xz"
checksum=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
jobs=${WORMEDIT_BUILD_JOBS:-$(scripts/gate.sh --print-jobs | cut -d' ' -f1)}
mkdir -p "$root/source" "$root/build"
if [[ ! -f "$root/source/$archive" ]]; then
    curl --fail --location --retry 2 "https://ffmpeg.org/releases/$archive" \
        -o "$root/source/$archive"
fi
echo "$checksum  $root/source/$archive" | sha256sum --check
if [[ ! -d "$root/source/ffmpeg-$version" ]]; then
    tar -xf "$root/source/$archive" -C "$root/source"
fi
cd "$root/build"
"$root/source/ffmpeg-$version/configure" \
    --prefix="$root/prefix" --target-os=mingw32 --arch=x86_64 \
    --enable-cross-compile --cross-prefix="$prefix-" \
    --enable-shared --disable-static --disable-programs --disable-doc \
    --disable-debug --disable-autodetect --disable-network \
    --disable-avdevice --disable-avfilter --disable-encoders --disable-muxers \
    --disable-x86asm --extra-ldflags=-static-libgcc
make -j"$jobs"
make install
mkdir -p "$root/prefix/share/wormedit-ffmpeg"
cp "$root/source/$archive" "$root/prefix/share/wormedit-ffmpeg/"
cp "$root/source/ffmpeg-$version/COPYING.LGPLv2.1" \
    "$root/source/ffmpeg-$version/LICENSE.md" config.h ffbuild/config.log \
    "$root/prefix/share/wormedit-ffmpeg/"
# GCC's Windows atomic support can import winpthreads even with FFmpeg's native
# Windows threads. Keep that runtime and its copyright notices with the SDK.
pthread_library=$("$WORMEDIT_WINDOWS_C_COMPILER" -print-file-name=libwinpthread.dll.a)
pthread_root=$(realpath -m "$(dirname "$pthread_library")/..")
if [[ -f "$pthread_root/bin/libwinpthread-1.dll" ]]; then
    cp "$pthread_root/bin/libwinpthread-1.dll" "$root/prefix/bin/"
    cp "$pthread_root/include/pthread.h" "$root/prefix/share/wormedit-ffmpeg/winpthreads-notices.h"
fi
echo "Windows FFmpeg SDK: $root/prefix"
