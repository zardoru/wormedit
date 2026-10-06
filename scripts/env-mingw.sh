# Sourced by the Windows cross scripts: point the toolchain at mingw-w64.
# Optional: MINGW_PREFIX (default x86_64-w64-mingw32).
prefix="${MINGW_PREFIX:-x86_64-w64-mingw32}"
export WORMEDIT_WINDOWS_C_COMPILER="$(command -v "$prefix-gcc")"
export WORMEDIT_WINDOWS_CXX_COMPILER="$(command -v "$prefix-g++")"
export WORMEDIT_WINDOWS_RC_COMPILER="$(command -v "$prefix-windres")"
if [ -z "$WORMEDIT_WINDOWS_CXX_COMPILER" ]; then
    echo "$prefix-g++ not found; install mingw-w64 or set MINGW_PREFIX" >&2
    exit 1
fi
