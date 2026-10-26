#!/usr/bin/env bash
# Install a cross-compiled Windows build into a directory (e.g. a Wine prefix folder).
# Usage: scripts/windows-install.sh <destination> [--run]
# Builds the windows-cross preset if needed. --run starts the app under Wine afterwards.
set -euo pipefail
cd "$(dirname "$0")/.."
dest="${1:?usage: $0 <destination> [--run]}"
source scripts/env-mingw.sh
cmake --preset windows-cross
jobs=${WORMEDIT_BUILD_JOBS:-$(scripts/gate.sh --print-jobs | cut -d' ' -f1)}
cmake --build --preset windows-cross --parallel "$jobs"
for component in Wormedit Manual; do
    cmake --install build/windows-cross --prefix "$dest" --strip --component "$component"
done
echo "installed to $dest"
if [ "${2:-}" = "--run" ]; then
    exec "${WINE:-wine}" "$dest/wormedit.exe"
fi
