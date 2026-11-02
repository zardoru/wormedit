#!/usr/bin/env bash
# Colocate the selected MinGW compiler runtimes in the existing Windows SDK.
# stage <SDK-prefix> <host-triplet> <runtime-exception-notice>; verify <SDK-prefix>
set -euo pipefail
fail() { printf '%s\n' "$*" >&2; exit 1; }
mingw_runtimes=(libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll)
stage_mingw_runtimes() {
    local sdk host notice receipts runtime query source
    sdk=$(realpath "${1:?SDK prefix required}")
    host=${2:?host triplet required}
    notice=${3:?runtime exception notice required}
    receipts="$sdk/share/wormedit-ffmpeg"
    [[ $(cat "$receipts/sdk-host.txt") == "$host" && $host == x86_64*-mingw32 ]] || fail 'Runtime toolchain differs from SDK host'
    [[ -s $notice ]] || fail 'Missing GCC runtime exception notice'
    : > "$receipts/mingw-runtime-sources.txt"
    for runtime in "${mingw_runtimes[@]}"; do
        source=
        for query in file prog; do
            source=$("$host-g++" "-print-$query-name=$runtime")
            [[ $source == /* && -f $source ]] && break
        done
        [[ $source == /* && -f $source ]] || fail "Missing matching toolchain runtime: $runtime"
        source=$(realpath "$source")
        "$host-objdump" -f "$source" | grep -q 'file format pei-x86-64' || fail "Not an x64 runtime: $source"
        if [[ -e $sdk/bin/$runtime ]]; then
            cmp -s "$source" "$sdk/bin/$runtime" || fail "Conflicting SDK runtime: $runtime"
        else
            cp -p "$source" "$sdk/bin/$runtime"
        fi
        sha256sum "$source" >> "$receipts/mingw-runtime-sources.txt"
    done
    "$host-g++" --version > "$receipts/mingw-runtime-compiler.txt"
    cp "$notice" "$receipts/GCC-RUNTIME-LIBRARY-EXCEPTION.txt"
    cp "${BASH_SOURCE[0]}" "$receipts/stage-mingw-runtimes.sh"
    (cd "$sdk"; for runtime in "${mingw_runtimes[@]}"; do sha256sum "bin/$runtime"; done) > "$receipts/MINGW-RUNTIMES.sha256"
}
verify_mingw_runtimes() {
    local sdk receipts runtime notice
    sdk=$(realpath "${1:?SDK prefix required}")
    receipts="$sdk/share/wormedit-ffmpeg"
    [[ $(cat "$receipts/sdk-host.txt") == x86_64*-mingw32 ]] || fail 'Not a MinGW SDK'
    for runtime in "${mingw_runtimes[@]}"; do [[ -s $sdk/bin/$runtime ]] || fail "Missing SDK runtime: $runtime"; done
    for notice in MINGW-RUNTIMES.sha256 mingw-runtime-sources.txt mingw-runtime-compiler.txt GCC-RUNTIME-LIBRARY-EXCEPTION.txt; do
        [[ -s $receipts/$notice ]] || fail "Missing runtime receipt: $notice"
    done
    (cd "$sdk"; sha256sum --check --status "$receipts/MINGW-RUNTIMES.sha256") || fail 'Matching toolchain runtimes changed'
}
case ${1:-} in
stage) shift; stage_mingw_runtimes "$@" ;;
verify) shift; verify_mingw_runtimes "$@" ;;
*) fail 'Expected stage or verify' ;;
esac
