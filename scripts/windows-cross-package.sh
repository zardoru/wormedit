#!/usr/bin/env bash
# Cross-compile the Windows application with mingw-w64 and pack it into a zip.
# Usage: scripts/windows-cross-package.sh [output-dir]   (default: build/dist)
# Build the SDK first with scripts/windows-ffmpeg.sh; override it with WORMEDIT_FFMPEG_ROOT.
# Extra cmake configure arguments can be passed via CMAKE_ARGS.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env-mingw.sh
sdk=$(realpath -e "${WORMEDIT_FFMPEG_ROOT:-build/windows-ffmpeg/prefix}")

validate_ffmpeg_sdk() {
    local notices="$sdk/share/wormedit-ffmpeg"
    local notice flag
    for notice in COPYING.LGPLv2.1 LICENSE.md config.h config.log; do
        if [[ ! -s "$notices/$notice" ]]; then
            echo "Missing FFmpeg redistribution file: $notices/$notice" >&2
            return 1
        fi
    done
    local sources=("$notices"/ffmpeg-*.tar.xz)
    if [[ ! -s ${sources[0]:-} ]]; then
        echo "Missing FFmpeg corresponding source archive: $notices/ffmpeg-*.tar.xz" >&2
        return 1
    fi
    for flag in GPL NONFREE; do
        if ! grep -Eq "^#define[[:space:]]+CONFIG_${flag}[[:space:]]+0$" "$notices/config.h"; then
            echo "FFmpeg SDK must have CONFIG_${flag}=0 for the Windows release" >&2
            return 1
        fi
    done
    if [[ -f "$sdk/bin/libwinpthread-1.dll" && ! -s "$notices/winpthreads-notices.h" ]]; then
        echo "Missing FFmpeg winpthreads redistribution notice: $notices/winpthreads-notices.h" >&2
        return 1
    fi
}

validate_ffmpeg_sdk
export PKG_CONFIG_LIBDIR="$sdk/lib/pkgconfig"
export PKG_CONFIG_PATH=
jobs=${WORMEDIT_BUILD_JOBS:-$(scripts/gate.sh --print-jobs | cut -d' ' -f1)}
out="$(realpath -m "${1:-build/dist}")"
stage="build/windows-cross-stage"

# shellcheck disable=SC2086
cmake --preset windows-cross-release ${CMAKE_ARGS:-}
version=$(sed -n 's/^#define WORMEDIT_VERSION "\([^"]*\)"$/\1/p' build/windows-cross-release/config.h)
name="wormedit-${version}-windows-x64"
cmake --build --preset windows-cross-release --parallel "$jobs"
rm -rf "$stage"
for component in Wormedit Manual; do
    cmake --install build/windows-cross-release --prefix "$stage/$name" --strip --component "$component"
done
# The shared notices installer supplies the SDK's FFmpeg sources and licenses.
cp scripts/windows-ffmpeg.sh packaging/build-lame-sdk.sh scripts/env-mingw.sh "$stage/$name/third-party/ffmpeg/"
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
# Refuse accidental host libraries, dependency SDKs and debug runtime DLLs.
unexpected=$(find "$stage/$name" -type f \( -name '*.so' -o -name '*.so.*' -o -name '*.a' \
    -o -iname '*.lib' -o -iname '*d.dll' -o -iname '*.pdb' \) -print)
if [[ -n $unexpected ]]; then
    printf 'Unexpected release files:\n%s\n' "$unexpected" >&2
    exit 1
fi
while IFS= read -r -d '' binary; do
    if ! "$prefix-objdump" -f "$binary" | grep -q 'file format pei-x86-64'; then
        echo "Not a Windows x64 binary: $binary" >&2
        exit 1
    fi
done < <(find "$stage/$name" -type f \( -iname '*.exe' -o -iname '*.dll' \) -print0)
(cd "$stage/$name" && find . -type f | LC_ALL=C sort) > "$out/$name.zip.files.txt"
rm -f "$out/$name.zip"
(cd "$stage" && zip -qr -9 "$out/$name.zip" "$name")
(cd "$out" && sha256sum "$name.zip" > "$name.zip.sha256")
echo "$out/$name.zip"
