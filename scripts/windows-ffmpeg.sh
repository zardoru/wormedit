#!/usr/bin/env bash
# Build the pinned LGPL shared FFmpeg SDK used by the Windows release package.
# Usage: scripts/windows-ffmpeg.sh [sdk-root] (default: build/windows-ffmpeg)
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env-mingw.sh
root=$(realpath -m "${1:-build/windows-ffmpeg}")
[[ $root != / ]] || { echo 'SDK root cannot be /.' >&2; exit 1; }
version=9.0.2
archive="ffmpeg-$version.tar.xz"
checksum=8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e
jobs=${WORMEDIT_BUILD_JOBS:-$(scripts/gate.sh --print-jobs | cut -d' ' -f1)}
# Cross configure otherwise disables pkg-config when its triplet-prefixed tool is absent.
pkg_config=$(command -v pkg-config)
export CC="$WORMEDIT_WINDOWS_C_COMPILER" CXX="$WORMEDIT_WINDOWS_CXX_COMPILER"
export AR="$prefix-ar" STRIP="$prefix-strip"
export PKG_CONFIG_LIBDIR="$root/prefix/lib/pkgconfig" PKG_CONFIG_PATH=
# Rebuild every configured codec; cached MP3-only SDKs are not video SDKs.
bash packaging/build-video-codecs-sdk.sh build "$root/video-codecs" "$root/prefix" "$jobs" "$prefix"
CC="$WORMEDIT_WINDOWS_C_COMPILER" bash packaging/build-lame-sdk.sh "$root/lame" "$root/prefix" "$jobs" "$prefix"
mkdir -p "$root/source" "$root/build"
if [[ ! -f "$root/source/$archive" ]]; then
    curl --fail --location --retry 2 "https://ffmpeg.org/releases/$archive" \
        -o "$root/source/$archive"
fi
echo "$checksum  $root/source/$archive" | sha256sum --check
# Fresh owned FFmpeg source/build avoids stale objects or unreviewed cache edits.
rm -rf "$root/source/ffmpeg-$version" "$root/build"
tar -xf "$root/source/$archive" --no-same-owner -C "$root/source"
mkdir -p "$root/build" "$root/prefix/share/wormedit-ffmpeg"
notices="$root/prefix/share/wormedit-ffmpeg"
bash packaging/build-video-codecs-sdk.sh patch-ffmpeg "$root/source/ffmpeg-$version" "$notices"
flags_text=$(bash packaging/build-video-codecs-sdk.sh ffmpeg-flags)
mapfile -t video_flags <<< "$flags_text"
helper="$PWD/packaging/build-video-codecs-sdk.sh"
runtime_helper="$PWD/packaging/stage-mingw-runtimes.sh"
runtime_notice="$PWD/cmake/notices/GCC-RUNTIME-LIBRARY-EXCEPTION.txt"
cd "$root/build"
configure=("$root/source/ffmpeg-$version/configure" \
    --prefix="$root/prefix" --target-os=mingw32 --arch=x86_64 \
    --enable-cross-compile --cross-prefix="$prefix-" --pkg-config="$pkg_config" \
    --enable-shared --disable-static --pkg-config-flags=--static --disable-programs --disable-doc \
    --disable-debug --disable-autodetect --disable-network \
    --disable-avdevice --disable-avfilter --disable-encoders --enable-libmp3lame \
    --enable-encoder=libmp3lame --disable-muxers --enable-muxer=mp3 \
    --extra-cflags="-I$root/prefix/include" --extra-ldflags="-L$root/prefix/lib -static-libgcc" \
    --disable-gpl --disable-nonfree --disable-version3 \
    --disable-x86asm "${video_flags[@]}")
# Capture exact argv and all build output alongside the original sources/patch.
printf '%q ' "${configure[@]}" > "$notices/ffmpeg-build.commands"
printf '\nmake V=1 -j%q\nmake install\n' "$jobs" >> "$notices/ffmpeg-build.commands"
"${configure[@]}" 2>&1 | tee "$notices/ffmpeg-build.log"
make V=1 -j"$jobs" 2>&1 | tee -a "$notices/ffmpeg-build.log"
make install 2>&1 | tee -a "$notices/ffmpeg-build.log"
mkdir -p "$root/prefix/share/wormedit-ffmpeg"
cp "$root/source/$archive" "$root/prefix/share/wormedit-ffmpeg/"
cp "$root/source/ffmpeg-$version/COPYING.LGPLv2.1" \
    "$root/source/ffmpeg-$version/LICENSE.md" config.h config_components.h ffbuild/config.log ffbuild/config.mak \
    "$root/prefix/share/wormedit-ffmpeg/"
# GCC's Windows atomic support can import winpthreads even with FFmpeg's native
# Windows threads. Keep that runtime and its copyright notices with the SDK.
pthread_library=$("$WORMEDIT_WINDOWS_C_COMPILER" -print-file-name=libwinpthread.dll.a)
pthread_root=$(realpath -m "$(dirname "$pthread_library")/..")
if [[ -f "$pthread_root/include/pthread.h" ]]; then
    cp "$pthread_root/include/pthread.h" "$root/prefix/share/wormedit-ffmpeg/winpthreads-notices.h"
fi
bash "$runtime_helper" stage "$root/prefix" "$prefix" "$runtime_notice"
bash "$helper" seal "$root/prefix"
bash "$helper" verify "$root/prefix"
echo "Windows FFmpeg SDK: $root/prefix"
