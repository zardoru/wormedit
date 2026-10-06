#!/usr/bin/env bash
# Cross-compile the Windows application with mingw-w64 and pack it into a zip.
# Usage: scripts/windows-cross-package.sh [output-dir]   (default: build/dist)
# Extra cmake configure arguments can be passed via CMAKE_ARGS.
set -euo pipefail
cd "$(dirname "$0")/.."
source scripts/env-mingw.sh
out="$(realpath -m "${1:-build/dist}")"
stage="build/windows-cross-stage"
version="$(sed -n 's/^ *VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1)"
hash="$(git rev-parse --short=8 HEAD 2>/dev/null || echo nogit)"
name="wormedit-${version}-${hash}-windows-x64"

# shellcheck disable=SC2086
cmake --preset windows-cross ${CMAKE_ARGS:-}
cmake --build --preset windows-cross -j"$(nproc)"
rm -rf "$stage"
cmake --install build/windows-cross --prefix "$stage/$name" --strip
mkdir -p "$out"
rm -f "$out/$name.zip"
(cd "$stage" && zip -qr -9 "$out/$name.zip" "$name")
echo "$out/$name.zip"
