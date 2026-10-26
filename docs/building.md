# Building wormedit

The headless core and its tests build without a window or GPU; the application is an SDL3 GPU program with a Nuklear frontend. Both use the same CMake project and presets.

## Native headless build

nlohmann/json, Catch2, and Lua accept local source directories for offline/reproducible builds:

```sh
cmake --preset headless \
  -DWORMEDIT_NLOHMANN_JSON_SOURCE_DIR=/path/to/nlohmann-json-3.12.0 \
  -DWORMEDIT_CATCH2_SOURCE_DIR=/path/to/Catch2 \
  -DWORMEDIT_LUA_SOURCE_DIR=/path/to/lua-5.4.8
cmake --build --preset headless
ctest --preset headless
```

Omit the source-directory options when network-backed FetchContent is acceptable. nlohmann/json is pinned to the v3.12.0 release archive and SHA-256, uses its MIT license, and is built with diagnostic byte positions enabled. Catch2 is pinned to commit `2b60af89e23d28eefc081bc930831ee9d45ea58b` (v3.8.1 source), and Lua is pinned to `6e22fedb74cf0c9b6656e9fce8b7331db847c605` (Lua 5.4.8 source).

## UI frontend selection

Application builds select exactly one frontend with
`-DWORMEDIT_UI_BACKEND=nuklear|imgui|clay`. The initial Nuklear adapter is the
only implemented choice, so application configuration rejects `imgui` and
`clay` with a clear diagnostic until those adapters exist. Headless builds may
select any declared value without fetching or linking a UI dependency. An
unknown value always fails configuration. The executable links the selected
adapter through the private `wormedit::ui_frontend` target; `wormedit_core`
remains UI-library-free.

## Crash backtraces on Linux and macOS

The application installs fatal-signal and C++ termination handlers before SDL
initialization. Run a Debug build from a terminal to retain file and line
information in the executable and see the backtrace on standard error after
`SIGABRT`, `SIGSEGV`, `SIGILL`, `SIGFPE`, or `SIGBUS`. An unhandled C++ exception
also prints its message. The handler restores the signal's default termination
behavior, so the operating system's core-dump policy still applies. Stack
unwinding after severe memory corruption is best effort.

On Linux, an executable `+0x...` offset in the output can be resolved with
`addr2line -C -f -e build/wormedit 0x...`. On macOS, keep the matching
executable and dSYM to symbolize a crash with `atos` or `lldb`.

## Cross-compiling the headless core for Linux

The generic toolchain does not assume GCC versus Clang or a particular SDK. Set at least the two compiler paths:

```sh
export WORMEDIT_LINUX_C_COMPILER=/path/to/linux-cc
export WORMEDIT_LINUX_CXX_COMPILER=/path/to/linux-c++
export WORMEDIT_LINUX_SYSROOT=/path/to/linux-sysroot
export WORMEDIT_LINUX_TARGET=x86_64-unknown-linux-gnu

cmake --preset linux-cross-headless \
  -DWORMEDIT_NLOHMANN_JSON_SOURCE_DIR=/path/to/nlohmann-json-3.12.0 \
  -DWORMEDIT_CATCH2_SOURCE_DIR=/path/to/Catch2 \
  -DWORMEDIT_LUA_SOURCE_DIR=/path/to/lua-5.4.8
cmake --build --preset linux-cross-headless
```

`WORMEDIT_LINUX_SYSROOT` and `WORMEDIT_LINUX_TARGET` are optional when the compiler already encodes them. To execute discovered tests on the build host, set `WORMEDIT_CROSS_EMULATOR` to a compatible emulator before configuration. Otherwise copy the build tree/test executables to the Linux target and run CTest there; compilation remains a useful cross-build gate but is not reported as a runtime pass.

The full application cross-build will receive a separate preset after SDL3 GPU, audio decoder, and packaging dependencies have target-side discovery rules. This avoids accidentally linking host libraries into a Linux artifact.

## Linux AppImage release

On an x86_64 Linux host with Docker (no host linuxdeploy/appimagetool required):

```sh
git submodule update --init
scripts/linux-appimage.sh [output-dir]     # default: build/dist; at most 3 jobs
```

The script builds a Release application, Lua, both offline manuals and an LGPL
FFmpeg inside the digest-pinned Ubuntu 22.04 container in
`packaging/linux/Dockerfile`. Runtime requirements are **x86_64, glibc 2.35 or
newer**, an X11 or Wayland session, and a working Vulkan driver (the same GPU
requirements as the native application). Graphics drivers and glibc stay on the
host; linuxdeploy's excludelist plus explicit GL/Vulkan exclusions keep them out
of the image. GCC 14 provides the application’s C++23 library features; its C++ runtime is bundled.
The container uses the existing `WORMEDIT_WARNINGS_AS_ERRORS=OFF` option because
older GCC diagnoses valid vector/optional operations at `-O3`; warnings remain
visible and native GATE keeps its normal policy (see `packaging/linux/README.md`). Test older distributions separately;
building against this floor does not prove every desktop/driver combination.

CMake, linuxdeploy, appimagetool, the AppImage runtime and FFmpeg use fixed release
URLs with recorded SHA-256 checks. The runtime is supplied to appimagetool so it
cannot silently fetch a moving release. Ubuntu/PPA security updates remain
allowed; this is not a bit-reproducible package lock. All builds, including
FFmpeg and SquashFS compression, use at most three jobs. Set
`WORMEDIT_BUILD_JOBS=1|2|3` to reduce the application/compression count; the cached
FFmpeg SDK build uses three. The reusable build directory is
`build/linux-appimage`; delete it for a fresh application build.

The filename uses `WORMEDIT_VERSION` from the container's configured `config.h`,
including a pre-release suffix when configured. The output includes a SHA-256
sidecar, installed file inventory and an `ldd` dependency report. Unresolved
libraries or GPL/nonfree FFmpeg settings abort packaging. Assets, fonts,
`manual/index.html`, Lua reference and `third-party/` notices stay beside the
executable under `usr/bin/` inside the image. FFmpeg's source archive, license and
build configuration are included; keep them when redistributing. For a custom
native FFmpeg SDK install, set `WORMEDIT_FFMPEG_NOTICES_DIR` to that same notice
layout; system zlib can use `WORMEDIT_ZLIB_NOTICE_FILE`.

```sh
chmod +x wormedit-*-x86_64.AppImage
./wormedit-<version>-x86_64.AppImage
./wormedit-<version>-x86_64.AppImage --appimage-extract-and-run
```

The second launch supports systems without usable FUSE. For inspection,
`--appimage-extract` writes `squashfs-root/` in the current directory. Run release
smokes only inside isolated Xvfb: open a copied manual fixture, play it, export a
chart and open Help → User manual; inspect captures before claiming success.
`packaging/linux/wormedit.png` is the single replaceable placeholder for owner art.

## Cross-compiling the headless core for Windows from Linux

The Windows toolchain is likewise compiler-agnostic and works with a MinGW-w64 GCC toolchain or a suitably configured Clang toolchain:

```sh
export WORMEDIT_WINDOWS_C_COMPILER=/path/to/x86_64-w64-mingw32-gcc
export WORMEDIT_WINDOWS_CXX_COMPILER=/path/to/x86_64-w64-mingw32-g++
export WORMEDIT_WINDOWS_RC_COMPILER=/path/to/x86_64-w64-mingw32-windres
export WORMEDIT_WINDOWS_TARGET=x86_64-w64-windows-gnu

cmake --preset windows-cross-headless \
  -DWORMEDIT_NLOHMANN_JSON_SOURCE_DIR=/path/to/nlohmann-json-3.12.0 \
  -DWORMEDIT_CATCH2_SOURCE_DIR=/path/to/Catch2 \
  -DWORMEDIT_LUA_SOURCE_DIR=/path/to/lua-5.4.8
cmake --build --preset windows-cross-headless
```

`WORMEDIT_WINDOWS_RC_COMPILER`, `WORMEDIT_WINDOWS_SYSROOT`, and `WORMEDIT_WINDOWS_TARGET` are optional when the compiler prefix already supplies them. Set `WORMEDIT_CROSS_EMULATOR` to a Wine executable if the build host can run the generated test programs; otherwise execute CTest on Windows and treat the Linux job as a compile gate.

As with Linux cross-builds, enable the full application only after its Windows-target SDL3 GPU, decoder, and packaging dependencies are supplied as target packages/source builds. Host package discovery must never satisfy a Windows dependency.

## Cross-compiling the Windows application from Linux

The `windows-cross` preset builds the real SDL3 GPU application (`wormedit.exe`, Release, GUI subsystem) with
MinGW-w64. The SDL3 source is reused from `build/_deps` when present, zlib is built from source for the
Windows target (`cmake/dependencies/zlib.cmake`), and the FFmpeg video decoder is off because there is no
Windows-target FFmpeg. Pass `-DWORMEDIT_WINDOWS_CONSOLE=ON` to keep a console attached for debugging.

```sh
scripts/windows-install.sh <dir> [--run]             # install into a folder, optionally launch under Wine
```

The scripts source `scripts/env-mingw.sh`, which finds `x86_64-w64-mingw32-*` on `PATH` (override with
`MINGW_PREFIX`). The libstdc++/winpthread runtimes are linked statically.

For distribution, build the pinned Windows FFmpeg SDK and package the
`windows-cross-release` preset, which requires video playback and audio metadata support:

```sh
scripts/windows-ffmpeg.sh                            # isolated SDK in build/windows-ffmpeg/prefix
scripts/windows-cross-package.sh [output-dir]        # build, install, strip, zip (default build/dist)
```

The SDK script needs curl, tar, make, pkg-config and MinGW-w64. It verifies the source
archive's pinned SHA-256 and builds LGPL shared libraries without external codecs,
network protocols, encoders or command-line programs. Set `WORMEDIT_BUILD_JOBS` to
override the shared-machine default of at most eight jobs. To use another SDK built
with the same source/license layout, set `WORMEDIT_FFMPEG_ROOT` to its prefix.
Only that SDK's pkg-config directory is searched, preventing host FFmpeg discovery.

The ZIP contains `wormedit.exe`, SDL3 and FFmpeg DLLs, `assets/`, licenses, a launch
note, build revision and local tracked changes. An uncommitted build is marked
`preview` in the filename. FFmpeg's corresponding source archive, license and build
configuration are included under `third-party/ffmpeg`; keep these when redistributing.
Installation resolves DLL imports recursively and fails for missing third-party
dependencies. Extract the entire ZIP before launching; assets and DLLs belong next
to the executable. A SHA-256 sidecar is written beside the ZIP.

## Frontend event harness

See [the frontend event harness guide](frontend-event-harness.md) for windowless
input-to-action tests, owning snapshots, explicit modifier steps, and the
`headless-events` preset used by the existing Linux/GCC CI lane.

## Offline Lua API reference

The application manual includes an LDoc API reference under `manual/lua-api/`.
Build it with `cmake --build build --target lua-api --parallel 2`, or build `manual`
for both books. The `Manual` install component packages the reference, styles,
examples and license notices; `WORMEDIT_INSTALL_MANUAL=OFF` disables both books.
Generation uses pinned LDoc v1.5.0, Penlight 1.14.0, LuaFileSystem 1.8.0 and Lua
5.4.8. The native build-time runner is independent of the target toolchain, so
Windows cross-packages do not need Wine to generate documentation. Lua remains
an optional application feature; no system Lua or LuaRocks is needed for docs.

For disconnected builds, provide checkouts at the revisions in
`cmake/dependencies/ldoc.cmake` using
`FETCHCONTENT_SOURCE_DIR_WORMEDIT_LDOC`,
`FETCHCONTENT_SOURCE_DIR_WORMEDIT_PENLIGHT`, and
`FETCHCONTENT_SOURCE_DIR_WORMEDIT_LFS`, plus `WORMEDIT_LUA_SOURCE_DIR` as above.
Cross-builds may select a native C compiler with `WORMEDIT_HOST_C_COMPILER`.
