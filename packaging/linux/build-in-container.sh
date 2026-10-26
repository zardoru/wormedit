#!/usr/bin/env bash
# Container entry point. No host compiler or host library enters the AppDir.
set -euo pipefail
# Serialize users of the reusable build/AppDir, including release verification.
exec 9>/work/build.lock
flock 9
jobs=${WORMEDIT_BUILD_JOBS:-3}
[[ $jobs =~ ^[1-3]$ ]] || { echo 'Build jobs must be 1..3.' >&2; exit 1; }
command -v asciidoctor >/dev/null || { echo 'The offline manuals require the Asciidoctor CLI.' >&2; exit 1; }
# Compiler switches can reset CMake's explicit feature flags. Clear that cache
# only on a compiler change, preserving objects during ordinary packaging retries.
configure_mode=()
if [[ -f /work/build/CMakeCache.txt ]]; then
    for language in C CXX; do
        if [[ $language == C ]]; then compiler=$CC; else compiler=$CXX; fi
        cached_compiler=$(sed -n "s/^CMAKE_${language}_COMPILER:[^=]*=//p" /work/build/CMakeCache.txt)
        if [[ $cached_compiler != "$(command -v "$compiler")" ]]; then
            configure_mode=(--fresh)
            break
        fi
    done
fi
# Use the shared warnings policy, including its non-fatal GCC optimizer diagnostics.
cmake "${configure_mode[@]}" -S /source -B /work/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
    -DWORMEDIT_WARNINGS_AS_ERRORS=ON \
    -DWORMEDIT_ENABLE_LUA=ON -DWORMEDIT_REQUIRE_FFMPEG=ON \
    -DWORMEDIT_FFMPEG_NOTICES_DIR=/opt/ffmpeg/share/wormedit-ffmpeg \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build /work/build --parallel "$jobs"
# Prefer T-761's public config.h; older builds generated only the frontend copy.
config=/work/build/config.h
if [[ ! -f $config ]]; then config=/work/build/generated/wormedit/config.h; fi
version=$(sed -n 's/^#define WORMEDIT_VERSION "\([-.0-9A-Za-z]*\)"$/\1/p' "$config")
[[ -n $version ]] || { echo 'Missing or unsafe WORMEDIT_VERSION in config.h.' >&2; exit 1; }
appdir=/work/AppDir
rm -rf "$appdir"
DESTDIR="$appdir" cmake --install /work/build --strip
# Upstream Ogg/Vorbis install their development files along with the app.
# The AppImage carries their linked code and notices, not headers/static SDKs.
rm -rf "$appdir/usr/include"
find "$appdir/usr/lib" -type f \( -name '*.a' -o -name '*.cmake' -o -name '*.pc' \) -delete
find "$appdir/usr/lib" -depth -type d -empty -delete
notices="$appdir/usr/bin/third-party"
cp /source/packaging/linux/Dockerfile "$notices/ffmpeg/build.Dockerfile"
printf '%s\n' "$WORMEDIT_BUILD_REVISION" > "$appdir/usr/bin/BUILD-REVISION.txt"
# Reject a GPL/nonfree FFmpeg, including changes accidentally made to its build.
for feature in GPL NONFREE VERSION3; do
    if ! grep -q "^#define CONFIG_$feature 0$" /opt/ffmpeg/share/wormedit-ffmpeg/config.h; then
        echo "FFmpeg must remain LGPL 2.1: CONFIG_$feature is not zero." >&2
        exit 1
    fi
done
# linuxdeploy carries the AppImage excludelist (libc and graphics drivers).
# Explicit exclusions cover Vulkan/GL dispatch libraries as well.
/opt/tools/linuxdeploy/AppRun --appdir "$appdir" --executable "$appdir/usr/bin/wormedit" \
    --library "$("$CXX" -print-file-name=libstdc++.so.6)" \
    --library "$("$CC" -print-file-name=libgcc_s.so.1)" \
    --custom-apprun /source/packaging/linux/AppRun \
    --desktop-file /source/packaging/linux/wormedit.desktop \
    --icon-file /source/packaging/linux/wormedit.png \
    --exclude-library 'libvulkan.so*' --exclude-library 'libGL*.so*' \
    --exclude-library 'libEGL.so*' --exclude-library 'libdrm*.so*' --exclude-library 'libgbm.so*'
# Use our own shell launcher; the only third-party entry code is the pinned
# type-2 runtime, whose notices and rebuild sources travel with the image.
cp -a /opt/runtime-notices "$notices/appimage-runtime"
# Archive the distro copyright notice for each bundled library, including GCC's
# Runtime Library Exception. Unowned/local libraries have upstream notices above.
while IFS= read -r library; do
    soname=$(basename "$library")
    original=$(ldconfig -p | awk -v name="$soname" '$1 == name && !found {print $NF; found=1}')
    [[ -n $original ]] || continue
    package=$(dpkg-query -S "$original" "$(realpath "$original")" 2>/dev/null | head -1 | sed 's/: .*//' || true)
    if [[ -z $package ]]; then
        echo "No distro copyright owner for bundled library: $original" >&2
        exit 1
    fi
    copyright="/usr/share/doc/${package%%:*}/copyright"
    [[ -f $copyright ]] || { echo "Missing copyright: $package" >&2; exit 1; }
    mkdir -p "$notices/system/$package"
    cp -L "$copyright" "$notices/system/$package/copyright"
done < <(find "$appdir/usr/lib" -type f -name '*.so*' | sort)
# Resolve every bundled ELF after deployment. Excluded system/driver libraries
# must resolve inside the baseline container; any missing dependency is fatal.
: > /work/bundled-libraries.txt
while IFS= read -r binary; do
    file "$binary" | grep -q ELF || continue
    printf '\n%s\n' "${binary#"$appdir/"}" >> /work/bundled-libraries.txt
    dependencies=$(LD_LIBRARY_PATH="$appdir/usr/lib" ldd "$binary")
    printf '%s\n' "$dependencies" >> /work/bundled-libraries.txt
    if grep -q 'not found' <<< "$dependencies"; then
        echo "Unresolved dependency: $binary" >&2; exit 1
    fi
done < <(find "$appdir/usr/bin" "$appdir/usr/lib" -type f | sort)
# Assert the promised compatibility floor on the deployed ELFs, including the
# bundled C++ runtime; never rely on the base image tag alone.
python3 - "$appdir" <<'PYTHON'
import pathlib, re, subprocess, sys
floor = (2, 35)
highest = (0, 0)
for root in ("usr/bin", "usr/lib"):
    for binary in (pathlib.Path(sys.argv[1]) / root).rglob("*"):
        if not binary.is_file() or binary.open("rb").read(4) != b"\x7fELF":
            continue
        report = subprocess.check_output(["readelf", "--version-info", str(binary)], text=True)
        versions = [tuple(map(int, value.split(".")))
                    for value in re.findall(r"Name: GLIBC_([0-9.]+)", report)]
        required = max(versions, default=(0, 0))
        highest = max(highest, required)
        if required > floor:
            raise SystemExit(f"{binary} requires GLIBC_{'.'.join(map(str, required))}, above 2.35")
print(f"Highest required GLIBC: {'.'.join(map(str, highest))}")
PYTHON
name="wormedit-$version-x86_64.AppImage"
# Pinned runtime avoids appimagetool's implicit latest-runtime download.
ARCH=x86_64 /opt/tools/appimagetool/AppRun --runtime-file /opt/tools/runtime-x86_64 \
    --mksquashfs-opt -processors --mksquashfs-opt "$jobs" "$appdir" "/work/$name"
cp /work/bundled-libraries.txt "/out/$name.libraries.txt"
cp "/work/$name" "/out/$name"
(cd /out && sha256sum "$name" > "$name.sha256")
find "$appdir" -type f -printf '%P\n' | sort > "/out/$name.files.txt"
stat --printf='%n: %s bytes\n' "/out/$name"
