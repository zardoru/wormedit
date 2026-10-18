#!/usr/bin/env bash
# Cross-compile the Windows application with mingw-w64 and pack it into a zip.
# Usage: scripts/windows-cross-package.sh [output-dir]   (default: build/dist)
# Build the SDK first with scripts/windows-ffmpeg.sh; override it with WORMEDIT_FFMPEG_ROOT.
# Extra cmake configure arguments can be passed via CMAKE_ARGS.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env-mingw.sh
sdk=$(realpath -e "${WORMEDIT_FFMPEG_ROOT:-build/windows-ffmpeg/prefix}")
export PKG_CONFIG_LIBDIR="$sdk/lib/pkgconfig"
export PKG_CONFIG_PATH=
jobs=${WORMEDIT_BUILD_JOBS:-$(scripts/gate.sh --print-jobs | cut -d' ' -f1)}
out="$(realpath -m "${1:-build/dist}")"
stage="build/windows-cross-stage"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
hash="$(git rev-parse --short=8 HEAD 2>/dev/null || echo nogit)"
name="wormedit-${version}-${hash}-windows-x64"
if [[ -n $(git status --porcelain --untracked-files=normal) ]]; then
    name="wormedit-${version}-${hash}-preview-windows-x64"
fi

# shellcheck disable=SC2086
cmake --preset windows-cross-release ${CMAKE_ARGS:-}
cmake --build --preset windows-cross-release --parallel "$jobs"
rm -rf "$stage"
cmake --install build/windows-cross-release --prefix "$stage/$name" --strip
mkdir -p "$stage/$name/third-party/ffmpeg"
cp -r "$sdk/share/wormedit-ffmpeg/." "$stage/$name/third-party/ffmpeg/"
cp scripts/windows-ffmpeg.sh scripts/env-mingw.sh "$stage/$name/third-party/ffmpeg/"
cat > "$stage/$name/START-HERE.txt" <<'EOF'
Wormedit Windows preview (64-bit)

Extract the entire ZIP into a folder, then run wormedit.exe.
Keep the DLLs and assets folder next to the executable.
This preview includes FFmpeg video playback and audio metadata support.

FFmpeg is dynamically linked under the GNU LGPL version 2.1 or later.
Its license, corresponding source archive and build configuration are in
third-party/ffmpeg. You may replace the FFmpeg DLLs with compatible builds.
EOF
git rev-parse HEAD > "$stage/$name/BUILD-REVISION.txt"
git diff --binary HEAD > "$stage/$name/BUILD-CHANGES.patch"
mkdir -p "$out"
rm -f "$out/$name.zip"
(cd "$stage" && zip -qr -9 "$out/$name.zip" "$name")
(cd "$out" && sha256sum "$name.zip" > "$name.zip.sha256")
echo "$out/$name.zip"
