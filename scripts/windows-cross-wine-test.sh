#!/usr/bin/env bash
# Cross-compile the headless core with mingw-w64 and run the tests under Wine.
# Optional: MINGW_PREFIX (default x86_64-w64-mingw32), WINE (default wine).
set -euo pipefail
cd "$(dirname "$0")/.."
prefix="${MINGW_PREFIX:-x86_64-w64-mingw32}"
export WORMEDIT_WINDOWS_C_COMPILER="$(command -v "$prefix-gcc")"
export WORMEDIT_WINDOWS_CXX_COMPILER="$(command -v "$prefix-g++")"
export WORMEDIT_WINDOWS_RC_COMPILER="$(command -v "$prefix-windres")"
export WORMEDIT_CROSS_EMULATOR="$(command -v "${WINE:-wine}")"
export WINEDEBUG="${WINEDEBUG:--all}"
cmake --preset windows-cross-headless "$@"
cmake --build build/windows-cross-headless -j"$(nproc)"
ctest --test-dir build/windows-cross-headless -j8 --timeout 120 --output-on-failure
