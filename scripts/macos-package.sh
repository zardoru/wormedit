#!/usr/bin/env bash
# Native unsigned owner-verification build; no Developer ID or notarization.
# Prerequisites: current Xcode Command Line Tools (xcode-select --install), Homebrew,
# brew install cmake ninja pkgconf asciidoctor python ffmpeg
# CMake >= 4.2 is needed by libotoworm. SDL3/Lua/Ogg/Vorbis are built from pinned
# sources, zlib/iconv come from the macOS SDK. No system SDL3/Lua/libiconv package.
# Usage: scripts/macos-package.sh [output-dir] [extra CMake arguments...]
set -euo pipefail
cd "$(dirname "$0")/.."
fail() { echo "macos-package: $*" >&2; exit 1; }
[[ $(uname -s) == Darwin ]] || fail "run this script on a real Mac (native build only)"
arch=$(uname -m)
case "$arch" in arm64|x86_64) ;; *) fail "unsupported architecture: $arch" ;; esac
for tool in brew cmake ninja pkg-config asciidoctor python3 xcrun codesign otool iconutil ditto shasum; do
    command -v "$tool" >/dev/null || fail "missing $tool; see prerequisites at the top of this script"
done
xcode-select -p >/dev/null || fail "install Xcode Command Line Tools: xcode-select --install"
sdk=$(xcrun --sdk macosx --show-sdk-path)
cc=$(xcrun --find clang)
cxx=$(xcrun --find clang++)
xcrun --find swift >/dev/null
brew_prefix=$(brew --prefix)
ffmpeg_prefix=$(brew --prefix ffmpeg)
# Also handles keg-only FFmpeg, Intel /usr/local, and Apple Silicon /opt/homebrew.
export PKG_CONFIG_PATH="$ffmpeg_prefix/lib/pkgconfig:$brew_prefix/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
pkg-config --exists libavformat libavcodec libavutil libswscale || fail "FFmpeg development libraries not found"
jobs=${WORMEDIT_BUILD_JOBS:-3}
[[ $jobs =~ ^[1-9][0-9]*$ ]] || fail "WORMEDIT_BUILD_JOBS must be a positive integer"
out=${1:-build/dist}
if (( $# )); then shift; fi
mkdir -p "$out"
out=$(cd "$out" && pwd)
build="$(pwd)/build/macos-release-$arch"
deployment=${WORMEDIT_MACOS_DEPLOYMENT_TARGET:-$(sw_vers -productVersion | cut -d. -f1,2)}

# Fail early with the actual compiler/SDK error if CLT's C++23 library is too old.
probe=$(mktemp -d "${TMPDIR:-/tmp}/wormedit-macos-probe.XXXXXX")
trap 'rm -rf "$probe"' EXIT
printf '#include <expected>\nint main() { return std::expected<int, int>{0}.value(); }\n' > "$probe/check.cpp"
"$cxx" -std=c++23 -isysroot "$sdk" -mmacosx-version-min="$deployment" \
    "$probe/check.cpp" -o "$probe/check" || fail "update Xcode/CLT for C++23 std::expected support"

cmake -S . -B "$build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    "-DCMAKE_C_COMPILER=$cc" "-DCMAKE_CXX_COMPILER=$cxx" "-DCMAKE_OBJCXX_COMPILER=$cxx" \
    "-DCMAKE_OSX_SYSROOT=$sdk" "-DCMAKE_OSX_ARCHITECTURES=$arch" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=$deployment" "-DCMAKE_PREFIX_PATH=$brew_prefix;$ffmpeg_prefix" \
    -DWORMEDIT_BUILD_APP=ON -DWORMEDIT_ENABLE_LUA=ON \
    -DWORMEDIT_ENABLE_AUDIO_DECODER=ON -DWORMEDIT_ENABLE_IMAGE_DECODER=ON \
    -DWORMEDIT_ENABLE_VIDEO_DECODER=ON -DWORMEDIT_ENABLE_FFMPEG=ON \
    -DWORMEDIT_REQUIRE_FFMPEG=ON -DWORMEDIT_INSTALL_MANUAL=ON \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_FRAMEWORK=OFF "$@"
cmake --build "$build" --target wormedit manual --parallel "$jobs"

stage=$(mktemp -d "$build/stage.XXXXXX")
trap 'rm -rf "$probe" "$stage"' EXIT
# Exclude dependency SDK installs (Vorbis even uses an absolute header destination).
cmake --install "$build" --prefix "$stage" --config Release --component Wormedit
cmake --install "$build" --prefix "$stage" --config Release --component Manual
app="$stage/Wormedit.app"
resources="$app/Contents/Resources"
for resource in assets/fonts/AdwaitaMono-Regular.ttf assets/fonts/NotoSansCJKjp-Regular.otf \
    assets/themes/ui-dark.json assets/themes/field assets/hitsounds \
    manual/index.html manual/lua-api/index.html Wormedit.icns; do
    [[ -e $resources/$resource ]] || fail "missing bundle resource: $resource"
done
# TODO(T-763/T-764 merge): the shared notices module must install this directory.
# Do not silently archive a bundle with missing notices during lane integration.
[[ -d $resources/third-party ]] || fail "merge T-763 shared third-party-notices.cmake and its macOS destination override first"

cmake "-DWORMEDIT_APP=$app" -P "$build/macos-fixup.cmake"
python3 packaging/macos/verify-bundle.py "$app" "$arch" > "$build/macos-dependencies.txt"
cat "$build/macos-dependencies.txt"
/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$app/Contents/Info.plist"
plutil -lint "$app/Contents/Info.plist"
git rev-parse HEAD > "$resources/BUILD-REVISION.txt"
git diff --binary HEAD > "$resources/BUILD-CHANGES.patch"
sw_vers > "$resources/BUILD-MACOS.txt"
printf 'arch=%s\ndeployment-target=%s\n' "$arch" "$deployment" >> "$resources/BUILD-MACOS.txt"
"$cxx" --version >> "$resources/BUILD-MACOS.txt"
cp "$build/macos-dependencies.txt" "$resources/BUILD-DEPENDENCIES.txt"

# Sign inside out after ALL install-name/resource edits. Homebrew libraries can
# arrive signed; replace those signatures too. No hardened runtime/entitlements.
while IFS= read -r -d '' binary; do
    if file -b "$binary" | grep -q 'Mach-O'; then
        codesign --force -s - "$binary"
    fi
done < <(find "$app/Contents/Frameworks" -type f -print0)
codesign --force --deep -s - "$app"
codesign --verify --deep --strict --verbose=2 "$app"

version=$(cat "$build/macos-package-version.txt")
[[ $version =~ ^[0-9A-Za-z.-]+$ ]] || fail "unsafe package version: $version"
name="Wormedit-$version-macos-$arch.zip"
archive=$(mktemp -d "$out/.wormedit-archive.XXXXXX")
trap 'rm -rf "$probe" "$stage" "$archive"' EXIT
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive/$name"
mv -f "$archive/$name" "$out/$name"
(cd "$out" && shasum -a 256 "$name" > "$name.sha256")
echo "macos-package: $out/$name"
echo "STOP: run the macOS owner checklist in docs/building.md; this Mac build is not yet verified."
