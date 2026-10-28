#!/usr/bin/env bash
# Build the shared LGPL MP3 encoder used by the release FFmpeg SDKs.
# Usage: packaging/build-lame-sdk.sh <work-dir> <prefix> <jobs> [host-triplet]
set -euo pipefail
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
cd "$work/build"
CFLAGS="${CFLAGS:--O2 -std=gnu11}" "$work/source/lame-3.100/configure" \
    --prefix="$destination" "${host_options[@]}" --enable-shared --disable-static \
    --disable-frontend --disable-decoder --disable-nasm
make -j"$jobs"
make install
notices="$destination/share/wormedit-ffmpeg/lame"
mkdir -p "$notices"
cp "$work/source/$archive" "$work/source/lame-3.100/COPYING" config.log "$notices/"
