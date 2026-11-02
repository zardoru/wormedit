#!/usr/bin/env bash
# Build the shared LGPL MP3 encoder used by the release FFmpeg SDKs.
# Usage: packaging/build-lame-sdk.sh <work-dir> <prefix> <jobs> [host-triplet]
set -euo pipefail
script=$(realpath "${BASH_SOURCE[0]}")

patch_mingw_encoder_exports() {
    local source_dir=${1:?LAME source required} notices=${2:?receipt directory required}
    local target="$source_dir/include/libmp3lame.sym" actual
    local before=eed7f2b0c82a11ef4eee47b34f42b0c029431d6bf930e2822e75c10abac1db5f
    local after=268fee827e250ffd2cc13f109457450b71f33897b06b945dfaf46ed1b8908753
    mkdir -p "$notices"
    # LAME3.100's PE export table includes the disabled decoder and removed
    # legacy initializer. Keep this encoder-only build's advertised API exact.
    cat > "$notices/mingw-encoder-exports.patch" <<'PATCH'
--- a/include/libmp3lame.sym
+++ b/include/libmp3lame.sym
@@ -1,5 +1,4 @@
 lame_init
-lame_init_old
 lame_set_num_samples
 lame_get_num_samples
 lame_set_in_samplerate
@@ -183,23 +182,6 @@
 lame_get_lametag_frame
 lame_close
 lame_encode_finish
-hip_decode_init
-hip_decode_exit
-hip_set_errorf
-hip_set_debugf
-hip_set_msgf
-hip_decode
-hip_decode_headers
-hip_decode1
-hip_decode1_headers
-hip_decode1_headersB
-lame_decode_init
-lame_decode
-lame_decode_headers
-lame_decode1
-lame_decode1_headers
-lame_decode1_headersB
-lame_decode_exit
 id3tag_genre_list
 id3tag_init
 id3tag_add_v2
PATCH
    actual=$(sha256sum "$target"); actual=${actual%% *}
    if [[ $actual == "$before" ]]; then
        patch --batch --fuzz=0 --no-backup-if-mismatch -p1 -d "$source_dir" \
            < "$notices/mingw-encoder-exports.patch" > "$notices/mingw-encoder-exports.log" 2>&1
        if grep -Ei 'offset|fuzz' "$notices/mingw-encoder-exports.log"; then
            echo 'LAME export patch drift; refusing SDK.' >&2; exit 1
        fi
    elif [[ $actual == "$after" ]]; then
        printf '%s\n' 'Verified already-patched encoder export table.' > "$notices/mingw-encoder-exports.log"
    else
        echo 'Pinned LAME export table differs; refusing SDK.' >&2; exit 1
    fi
    printf '%s  %s\n' "$after" "$target" | sha256sum --check --status
    printf '%s\n' "original $before" "patched $after" > "$notices/mingw-encoder-exports.sha256"
    cp "$target" "$notices/libmp3lame.sym"
    cp "$script" "$notices/build-lame-sdk.sh"
}

if [[ ${1:-} == patch-mingw-exports ]]; then
    shift
    patch_mingw_encoder_exports "$@"
    exit
fi

work=${1:?work directory required}
destination=${2:?prefix required}
jobs=${3:?job count required}
host=${4:-}
archive=lame_3.100.orig.tar.gz
# Debian's signed lame_3.100-6.dsc records this upstream archive checksum.
checksum=ddfe36cab873794038ae2c1210557ad34857a4b6bdc515785d1da9e175b1da1e
mkdir -p "$work/source" "$work/build" "$destination"
if [[ ! -f "$work/source/$archive" ]]; then
    curl --fail --location --retry 2 "https://deb.debian.org/debian/pool/main/l/lame/$archive" \
        -o "$work/source/$archive"
fi
echo "$checksum  $work/source/$archive" | sha256sum --check
if [[ ! -d "$work/source/lame-3.100" ]]; then
    tar -xf "$work/source/$archive" -C "$work/source"
fi
host_options=()
if [[ -n $host ]]; then host_options=("--host=$host"); fi
notices="$destination/share/wormedit-ffmpeg/lame"
if [[ $host == *-mingw32 ]]; then
    patch_mingw_encoder_exports "$work/source/lame-3.100" "$notices"
fi
cd "$work/build"
CFLAGS="${CFLAGS:--O2 -std=gnu11}" "$work/source/lame-3.100/configure" \
    --prefix="$destination" "${host_options[@]}" --enable-shared --disable-static \
    --disable-frontend --disable-decoder --disable-nasm
make -j"$jobs"
make install
mkdir -p "$notices"
cp "$work/source/$archive" "$work/source/lame-3.100/COPYING" config.log "$notices/"
cp "$script" "$notices/build-lame-sdk.sh"
