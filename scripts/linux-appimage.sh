#!/usr/bin/env bash
# Usage: scripts/linux-appimage.sh [output-dir] (default: build/dist)
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD
out=$(realpath -m "${1:-build/dist}")
work=$(realpath -m build/linux-appimage)
jobs=${WORMEDIT_BUILD_JOBS:-3}
if [[ ! $jobs =~ ^[1-3]$ ]]; then
    echo 'WORMEDIT_BUILD_JOBS must be 1, 2 or 3 for the AppImage build.' >&2
    exit 1
fi
if [[ $(uname -m) != x86_64 ]]; then
    echo 'The pinned AppImage toolchain currently requires an x86_64 Linux host.' >&2
    exit 1
fi
if [[ ! -f third_party/libotoworm/CMakeLists.txt ]]; then
    echo 'Initialize the libotoworm submodule first: git submodule update --init' >&2
    exit 1
fi
mkdir -p "$out" "$work"
docker build --platform linux/amd64 --tag wormedit-appimage:jammy --file packaging/linux/Dockerfile packaging
# Run as the caller so repeated builds and artifacts remain user-owned.
docker run --rm --platform linux/amd64 --user "$(id -u):$(id -g)" \
    --env HOME=/work --env WORMEDIT_BUILD_JOBS="$jobs" \
    --env WORMEDIT_BUILD_REVISION="$(git rev-parse HEAD)" \
    --mount "type=bind,src=$root,dst=/source,readonly" \
    --mount "type=bind,src=$work,dst=/work" \
    --mount "type=bind,src=$out,dst=/out" \
    wormedit-appimage:jammy
