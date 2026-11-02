#!/usr/bin/env bash
# Shared pinned SDK/receipt owner, used by the native and MinGW FFmpeg builds.
# build <work> <prefix> <jobs> [host-triplet]; patch-ffmpeg <source> <notices>
# seal/verify <prefix>; ffmpeg-flags; source-patch <checkout> <manifest.json> <output>
set -euo pipefail
script=$(realpath "${BASH_SOURCE[0]}")
script_dir=$(dirname "$script")
# name|version|archive|SHA256|URL|complete redistribution notices
codec_sources=(
    'openh264|2.6.0|openh264-2.6.0.tar.gz|558544ad358283a7ab2930d69a9ceddf913f4a51ee9bf1bfb9e377322af81a69|https://codeload.github.com/cisco/openh264/tar.gz/refs/tags/v2.6.0|LICENSE'
    'libvpx|1.15.2|libvpx-1.15.2.tar.gz|26fcd3db88045dee380e581862a6ef106f49b74b6396ee95c2993a260b4636aa|https://codeload.github.com/webmproject/libvpx/tar.gz/refs/tags/v1.15.2|LICENSE PATENTS third_party/x86inc/LICENSE'
    'opus|1.5.2|opus-1.5.2.tar.gz|65c1d2f78b9f2fb20082c38cbe47c951ad5839345876e46941612ee87f9a7ce1|https://downloads.xiph.org/releases/opus/opus-1.5.2.tar.gz|COPYING'
)
ffmpeg_features=(LIBMP3LAME_ENCODER MP3_MUXER LIBOPENH264_ENCODER LIBVPX_VP9_ENCODER AAC_ENCODER LIBOPUS_ENCODER MP4_MUXER WEBM_MUXER)
level_before=4c86c9d87fdfb122f2892ca7984a0544877f99f43e4fe49eec9286021cef96eb
level_after=44b5763c7d46b422940ebca0c89739039fc87f5a20982478ce8db0dc73a27da0
level_patch=4f5d00c670da4ae46f23cac7cfaec99631c3c693d237f073a695effd54369dc5

fail() { printf '%s\n' "$*" >&2; exit 1; }
check_hash() { printf '%s  %s\n' "$1" "$2" | sha256sum --check --status || fail "Checksum mismatch: $2"; }
run_logged() {
    local log=$1; shift
    printf '%q ' "$@" >> "$log.commands"
    printf '\n' >> "$log.commands"
    "$@" 2>&1 | tee -a "$log"
}
codec_receipt() {
    local name=$1 archive=$2 checksum=$3 licenses=$4 source=$5 build=$6 prefix=$7 notice
    local receipt="$prefix/share/wormedit-ffmpeg/$name"
    mkdir -p "$receipt"
    cp "$work/source/$archive" "$receipt/"
    for notice in $licenses; do
        mkdir -p "$receipt/$(dirname "$notice")"
        cp "$source/$notice" "$receipt/$notice"
    done
    cp "$build/build.log" "$build/build.log.commands" "$build/build.environment" "$receipt/"
    for notice in config.log config.mk vpx_config.h config.h; do
        if [[ -f $build/$notice ]]; then cp "$build/$notice" "$receipt/"; fi
    done
    printf '%s  %s\n' "$checksum" "$archive" > "$receipt/SOURCES.sha256"
}
build_openh264() {
    local source=$1 build=$2 prefix=$3 jobs=$4 host=$5 platform=linux
    if [[ -n $host ]]; then platform=mingw_nt; fi
    # Upstream supports out-of-tree Makefile/vpath builds and install-shared.
    # Keep upstream platform CFLAGS (PIC, dependency files, version defines).
    run_logged "$build/build.log" make -f "$source/Makefile" -j"$jobs" \
        OS="$platform" ARCH=x86_64 PREFIX="$prefix" USE_ASM=No BUILDTYPE=Release \
        CC="$CC" CXX="$CXX" AR="$AR" STRIP="$STRIP" install-shared
}
build_vpx() {
    local source=$1 build=$2 prefix=$3 jobs=$4 host=$5
    local platform=x86_64-linux-gcc link_flags=(--enable-shared --disable-static --enable-pic)
    if [[ -n $host ]]; then
        # Explicit MinGW-only exception: upstream has no shared Windows target.
        # This BSD archive is private input to replaceable shared libavcodec.
        platform=x86_64-win64-gcc
        link_flags=(--disable-shared --enable-static)
    fi
    run_logged "$build/build.log" env LD="$CC" CROSS="${host:+$host-}" "$source/configure" --prefix="$prefix" \
        --target="$platform" "${link_flags[@]}" \
        --disable-vp8 --enable-vp9 --enable-multithread --enable-runtime-cpu-detect \
        --disable-examples --disable-tools --disable-docs --disable-unit-tests \
        --disable-libyuv --disable-webm-io --as=nasm
    run_logged "$build/build.log" make -j"$jobs"
    run_logged "$build/build.log" make install
}
build_opus() {
    local source=$1 build=$2 prefix=$3 jobs=$4 host=$5
    local host_flags=()
    if [[ -n $host ]]; then host_flags=("--host=$host"); fi
    run_logged "$build/build.log" "$source/configure" --prefix="$prefix" "${host_flags[@]}" \
        --enable-shared --disable-static --disable-doc --disable-extra-programs
    run_logged "$build/build.log" make -j"$jobs"
    run_logged "$build/build.log" make install
}
build_codecs() {
    work=$(realpath -m "${1:?work directory required}")
    local prefix jobs=${3:?job count required} host=${4:-} row name version archive checksum url licenses source build
    prefix=$(realpath -m "${2:?prefix required}")
    [[ $work != / && $prefix != / && $work != "$prefix" ]] || fail 'Use separate non-root work and SDK directories.'
    [[ $jobs =~ ^[1-9][0-9]*$ ]] || fail 'jobs must be a positive integer'
    [[ -z $host || $host == x86_64*-mingw32 ]] || fail "Unsupported codec host: $host"
    export CC=${CC:-cc} CXX=${CXX:-c++} AR=${AR:-ar} STRIP=${STRIP:-strip}
    export CROSS=${host:+$host-}
    export PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" PKG_CONFIG_PATH=
    mkdir -p "$work/source" "$work/build" "$prefix/share/wormedit-ffmpeg"
    local sdk_host=${host:-native-x86_64-linux}
    if [[ -f $prefix/share/wormedit-ffmpeg/sdk-host.txt ]]; then
        [[ $(cat "$prefix/share/wormedit-ffmpeg/sdk-host.txt") == "$sdk_host" ]] || fail 'Native and MinGW SDK prefixes must be separate'
    fi
    printf '%s\n' "$sdk_host" > "$prefix/share/wormedit-ffmpeg/sdk-host.txt"
    for row in "${codec_sources[@]}"; do
        IFS='|' read -r name version archive checksum url licenses <<< "$row"
        if [[ ! -f $work/source/$archive ]]; then
            curl --fail --location --retry 3 "$url" -o "$work/source/$archive.part"
            check_hash "$checksum" "$work/source/$archive.part"
            mv "$work/source/$archive.part" "$work/source/$archive"
        fi
        check_hash "$checksum" "$work/source/$archive"
        source="$work/source/$name-$version"
        build="$work/build/$name"
        # Codec-owned directories only: no old configuration/source modifications
        # may masquerade as a newly built shared SDK, even with a cached prefix.
        rm -rf "$source" "$build"
        mkdir -p "$build"
        tar -xf "$work/source/$archive" --no-same-owner -C "$work/source"
        printf 'CC=%q\nCXX=%q\nAR=%q\nSTRIP=%q\nLD=%q\nCROSS=%q\nCFLAGS=%q\nCXXFLAGS=%q\nLDFLAGS=%q\nPKG_CONFIG_LIBDIR=%q\n' \
            "$CC" "$CXX" "$AR" "$STRIP" "${LD-}" "$CROSS" "${CFLAGS-}" "${CXXFLAGS-}" "${LDFLAGS-}" "$PKG_CONFIG_LIBDIR" > "$build/build.environment"
        run_logged "$build/build.log" "$CC" --version
        run_logged "$build/build.log" "$CXX" --version
        run_logged "$build/build.log" "$AR" --version
        (cd "$build"; "build_${name/libvpx/vpx}" "$source" "$build" "$prefix" "$jobs" "$host")
        codec_receipt "$name" "$archive" "$checksum" "$licenses" "$source" "$build" "$prefix"
        local mode=shared
        if [[ $name == libvpx && -n $host ]]; then
            mode=static-in-shared-ffmpeg
            (cd "$prefix"; sha256sum lib/libvpx.a) > "$prefix/share/wormedit-ffmpeg/libvpx/STATIC-ARTIFACT.sha256"
        fi
        printf '%s\n' "$mode" > "$prefix/share/wormedit-ffmpeg/$name/link-mode.txt"
        printf '%s\n' "${host:-native-x86_64-linux}" > "$prefix/share/wormedit-ffmpeg/$name/host.txt"
    done
    cp "$script" "$prefix/share/wormedit-ffmpeg/"
    printf '%s\n' "${codec_sources[@]}" > "$prefix/share/wormedit-ffmpeg/video-codecs.sources"
}
patch_ffmpeg() {
    local source=${1:?FFmpeg source required} notices=${2:?receipt directory required}
    local target="$source/libavcodec/libopenh264enc.c" patch="$script_dir/ffmpeg-openh264-level.patch" actual
    check_hash "$level_patch" "$patch"
    actual=$(sha256sum "$target"); actual=${actual%% *}
    mkdir -p "$notices"
    if [[ $actual == "$level_before" ]]; then
        patch --batch --fuzz=0 --no-backup-if-mismatch -p1 -d "$source" < "$patch" > "$notices/level-patch.log" 2>&1
        if grep -Ei 'offset|fuzz' "$notices/level-patch.log"; then fail 'Level patch drift; refusing SDK'; fi
    elif [[ $actual != "$level_after" ]]; then
        fail 'Pinned FFmpeg OpenH264 wrapper differs; refusing level patch'
    else
        printf '%s\n' 'Verified already-patched wrapper; patch not applied twice.' > "$notices/level-patch.log"
    fi
    check_hash "$level_after" "$target"
    cp "$patch" "$script" "$notices/"
    printf '%s\n' "original $level_before" "patched $level_after" "patch $level_patch" > "$notices/level-patch.sha256"
}
verify_sdk() {
    local prefix=${1:?SDK prefix required} notices row name version archive checksum url licenses notice feature
    notices="$prefix/share/wormedit-ffmpeg"
    for row in "${codec_sources[@]}"; do
        IFS='|' read -r name version archive checksum url licenses <<< "$row"
        check_hash "$checksum" "$notices/$name/$archive"
        for notice in $licenses build.log build.log.commands build.environment SOURCES.sha256 link-mode.txt host.txt; do
            [[ -s $notices/$name/$notice ]] || fail "Missing codec receipt: $name/$notice"
        done
    done
    local mode
    mode=$(cat "$notices/libvpx/link-mode.txt")
    case $mode in
    shared) [[ $(cat "$notices/libvpx/host.txt") == native-x86_64-linux ]] || fail 'Unexpected shared VPX host' ;;
    static-in-shared-ffmpeg)
        [[ $(cat "$notices/libvpx/host.txt") == x86_64*-mingw32 ]] || fail 'Static VPX exception is MinGW-only'
        [[ -s $notices/libvpx/STATIC-ARTIFACT.sha256 ]] || fail 'Missing static VPX artifact receipt'
        (cd "$prefix"; sha256sum --check --status "$notices/libvpx/STATIC-ARTIFACT.sha256") || fail 'Static VPX artifact changed'
        ;;
    *) fail 'Unknown VPX codec link mode' ;;
    esac
    [[ -s $notices/libvpx/config.mk && -s $notices/libvpx/vpx_config.h ]] || fail 'Missing VPX configuration receipts'
    [[ -s $notices/opus/config.log && -s $notices/opus/config.h ]] || fail 'Missing Opus configuration receipts'
    if [[ $mode == shared ]]; then
        grep -Eq '^#define CONFIG_SHARED 1$' "$notices/libvpx/vpx_config.h" || fail 'Native VPX must be shared'
    else
        grep -Eq '^#define CONFIG_SHARED 0$' "$notices/libvpx/vpx_config.h" || fail 'MinGW VPX must use the approved static route'
    fi
    for name in openh264 opus; do
        [[ $(cat "$notices/$name/link-mode.txt") == shared ]] || fail "Codec must stay shared: $name"
    done
    [[ $(cat "$notices/openh264/host.txt") == $(cat "$notices/opus/host.txt") &&
       $(cat "$notices/libvpx/host.txt") == $(cat "$notices/sdk-host.txt") &&
       $(cat "$notices/opus/host.txt") == $(cat "$notices/sdk-host.txt") ]] || fail 'Mixed native/cross SDK receipts'
    (cd "$prefix"; sha256sum --check --status "$notices/SDK-ARTIFACTS.sha256") || fail 'SDK binary hashes differ from build receipts'
    for notice in SDK-ARTIFACTS.sha256 video-codecs.sources level-patch.log level-patch.sha256 ffmpeg-build.log ffmpeg-build.commands config.h config_components.h config.log config.mak; do
        [[ -s $notices/$notice ]] || fail "Missing SDK receipt: $notice"
    done
    check_hash 8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e "$notices/ffmpeg-9.0.2.tar.xz"
    cmp -s <(printf '%s\n' "${codec_sources[@]}") "$notices/video-codecs.sources" || fail 'Pinned codec source inventory differs'
    check_hash "$level_patch" "$notices/ffmpeg-openh264-level.patch"
    grep -Fxq "patched $level_after" "$notices/level-patch.sha256" || fail 'Wrong patched wrapper receipt'
    if [[ $mode == static-in-shared-ffmpeg ]]; then
        grep -q -- '-lvpx' "$notices/ffmpeg-build.log" || fail 'Missing actual FFmpeg static VPX link command receipt'
    fi
    for feature in "${ffmpeg_features[@]}"; do
        grep -Eq "^#define CONFIG_$feature 1$" "$notices/config_components.h" || fail "Video SDK feature missing: $feature"
    done
    for feature in GPL NONFREE VERSION3; do
        grep -Eq "^#define CONFIG_$feature 0$" "$notices/config.h" || fail "Forbidden video SDK feature: $feature"
    done
}
seal_sdk() {
    local prefix notices
    prefix=$(realpath "${1:?SDK prefix required}")
    notices="$prefix/share/wormedit-ffmpeg"
    # Hash only this SDK's installed runtime code (plus the private VPX input).
    # No development archive is copied into an application package.
    mkdir -p "$prefix/bin"
    (cd "$prefix"; find lib bin -maxdepth 1 -type f \
        \( -name '*.dll' -o -name '*.so' -o -name '*.so.*' -o -name 'libvpx.a' \) \
        -print0 | sort -z | xargs -0 -r sha256sum) > "$notices/SDK-ARTIFACTS.sha256"
    [[ -s $notices/SDK-ARTIFACTS.sha256 ]] || fail 'SDK produced no installed runtime artifacts'
}
source_patch() {
    python3 - "${1:?checkout required}" "${2:?scoped manifest required}" "${3:?output required}" <<'PYTHON'
import hashlib, json, pathlib, subprocess, sys
root = pathlib.Path(sys.argv[1]).resolve()
manifest = json.loads(pathlib.Path(sys.argv[2]).read_text())
if manifest.get('checkout') != str(root):
    raise SystemExit('Package source manifest checkout differs')
head = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip()
if manifest.get('head') != head or not isinstance(manifest.get('paths'), dict):
    raise SystemExit('Package source manifest must pin current HEAD and exact path hashes')
paths = manifest['paths']
with pathlib.Path(sys.argv[3]).open('wb') as output:
    for name, digest in sorted(paths.items()):
        path = pathlib.PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts or not path.parts or path.parts[0].startswith('.'):
            raise SystemExit('Unsafe/unrelated package source input: ' + name)
        full = root / name
        if not full.resolve().is_relative_to(root) or full.is_symlink() or (full.exists() and not full.is_file()):
            raise SystemExit('Package source input must be a regular file: ' + name)
        actual = hashlib.sha256(full.read_bytes()).hexdigest() if full.exists() else None
        if actual != digest:
            raise SystemExit('Package source input changed: ' + name)
        tracked = subprocess.run(['git', '-C', str(root), 'ls-files', '--error-unmatch', '--', name],
                                 stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
        if not tracked and not full.exists():
            raise SystemExit('Missing untracked package source input: ' + name)
        command = ['git', 'diff', '--binary', 'HEAD', '--', ':(literal)' + name] if tracked else [
            'git', 'diff', '--binary', '--no-index', '--', '/dev/null', name]
        result = subprocess.run(command, cwd=root, stdout=output, check=False)
        if result.returncode not in (0, 1):
            raise SystemExit('Scoped package diff failed: ' + name)
PYTHON
    cp "$2" "$3.manifest.json"
}
case ${1:-} in
build) shift; build_codecs "$@" ;;
patch-ffmpeg) shift; patch_ffmpeg "$@" ;;
seal) shift; seal_sdk "$@" ;;
verify) shift; verify_sdk "$@" ;;
ffmpeg-flags) printf '%s\n' --enable-libopenh264 --enable-libvpx --enable-libopus \
    --enable-encoder=libopenh264,libvpx_vp9,aac,libopus --enable-muxer=mp4,webm ;;
source-patch) shift; source_patch "$@" ;;
*) fail 'Expected build, patch-ffmpeg, seal, verify, ffmpeg-flags or source-patch' ;;
esac
