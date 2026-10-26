# Building wormedit

The headless core and its tests build without a window or GPU; the application is an SDL3 GPU program with a Nuklear frontend. Both use the same CMake project and presets.

## Release versions

The numeric CMake `PROJECT_VERSION` is `1.0.0`. `WORMEDIT_VERSION_SUFFIX` defaults
to `-rc1`; the normal CMake variable `WORMEDIT_VERSION_FULL` joins them as
`1.0.0-rc1`. The configured `config.h` exposes that full `WORMEDIT_VERSION` and
separate numeric `WORMEDIT_VERSION_MAJOR`, `MINOR` and `PATCH` macros. About and
Windows archive names use the full version. Suffix characters are limited to
`[-.0-9A-Za-z]`.

For a final release, configure with `cmake -S . -B build -DWORMEDIT_VERSION_SUFFIX=`.
For the Windows package, pass `CMAKE_ARGS=-DWORMEDIT_VERSION_SUFFIX=` to the
package script. This produces `wormedit-1.0.0-windows-x64.zip`; RC1 produces
`wormedit-1.0.0-rc1-windows-x64.zip`.

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
The container enables `WORMEDIT_WARNINGS_AS_ERRORS=ON` with the shared policy in
`cmake/warnings.cmake`: GCC's optimizer-dependent `maybe-uninitialized` and
`array-bounds` diagnostics remain visible and non-fatal. Targets using this policy
treat other enabled warnings as errors (see `packaging/linux/README.md`). Test
older distributions separately; building against this floor does not prove every
desktop/driver combination.

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
Windows target (`cmake/dependencies/zlib.cmake`). This development preset disables FFmpeg;
the release preset below uses the pinned Windows SDK. Pass `-DWORMEDIT_WINDOWS_CONSOLE=ON`
to keep a console attached for debugging.

```sh
scripts/windows-install.sh <dir> [--run]             # install into a folder, optionally launch under Wine
```

The scripts source `scripts/env-mingw.sh`, which finds `x86_64-w64-mingw32-*` on `PATH` (override with
`MINGW_PREFIX`). The executable links its C++ runtime statically. FFmpeg can also require
`libwinpthread-1.dll`; the SDK script copies that DLL and its notices into the SDK.

For distribution, build the pinned Windows FFmpeg SDK and package the
`windows-cross-release` preset, which requires video playback and audio metadata support.
For a clean release build, start with no existing `build/windows-cross-release` directory
(move an earlier build aside if needed), then run these commands from the repository root:

```sh
WORMEDIT_BUILD_JOBS=3 scripts/windows-ffmpeg.sh
WORMEDIT_BUILD_JOBS=3 scripts/windows-cross-package.sh
```

The SDK is installed into `build/windows-ffmpeg/prefix`. The package script configures
and builds `build/windows-cross-release`, stages into `build/windows-cross-stage`,
and writes `build/dist/wormedit-1.0.0-rc1-windows-x64.zip` and its SHA-256 sidecar.
Pass an output directory as the package script's first argument to change `build/dist`.
The explicit three-job limit is suitable when sharing the machine with other release lanes.

The SDK script needs curl, tar, make, pkg-config and MinGW-w64. It verifies the source
archive's pinned SHA-256 and builds LGPL shared libraries without external codecs,
network protocols, encoders or command-line programs. Set `WORMEDIT_BUILD_JOBS` to
override the shared-machine default of at most eight jobs. To use another SDK built
with the same source/license layout, set `WORMEDIT_FFMPEG_ROOT` to its prefix.
Only that SDK's pkg-config directory is searched, preventing host FFmpeg discovery.
Before configuring the application, packaging requires the SDK's corresponding source,
license and build-configuration files, verifies `CONFIG_GPL=0` and `CONFIG_NONFREE=0`,
and requires the winpthreads notice when its DLL is present. A failed preflight exits
without building or writing a ZIP.

The package install includes `wormedit.exe`, required runtime DLLs, `assets/`, the offline
manual, license notices and a launch note. Its filename uses `WORMEDIT_VERSION_FULL`;
`BUILD-REVISION.txt` and `BUILD-CHANGES.patch` record the revision and tracked local changes.
Only the `Wormedit` and `Manual` install components are selected: dependency SDK
headers, static libraries and absolute install paths stay out of the release.
The shared notice installer supplies the complete `third-party/` tree, including
the embedded SDL/miniaudio/libotoworm dependencies and static MinGW/GCC runtimes.
FFmpeg's corresponding source archive, license and build configuration are included under
`third-party/ffmpeg`; keep these when redistributing.
Installation resolves DLL imports recursively and fails for missing third-party
dependencies. Before writing a ZIP, packaging rejects host/shared libraries,
static SDK libraries, debug DLLs/PDBs and executables/DLLs that are not x64 PE.
The `.zip.files.txt` sidecar records the installed file inventory.
Extract the entire ZIP before launching; assets and DLLs belong next to the executable.

Release verification is recorded on [T-762](chart-editor-plan/tasks/release-1-0.md#t-762-build-and-check-the-windows-release-zip-under-wine).
Check the archive's DLLs and notices, then use a fresh temporary Wine prefix inside isolated
Xvfb for the packaged tutorial project, audio, osu!mania export and offline-manual checks.
Inspect the screenshots before recording a pass; Wine windows must stay off the desktop.
For a manual runtime session (requires Wine, Xvfb and xauth), extract outside the
build tree and launch with a disposable prefix; this command keeps the app and
Wine's helper windows on the isolated display:

```sh
wine_work=$(mktemp -d /tmp/wormedit-wine-release.XXXXXX)
unzip -q build/dist/wormedit-1.0.0-rc1-windows-x64.zip -d "$wine_work"
package="$wine_work/wormedit-1.0.0-rc1-windows-x64"
cp -R "$package/manual/fixtures/tutorial" "$wine_work/ascii"
cp -R "$wine_work/ascii" "$wine_work/曲"
xvfb-run -a -s '-screen 0 1440x1000x24 -nolisten tcp' \
  env WINEPREFIX="$wine_work/prefix" WINEARCH=win64 WINEDEBUG=-all \
  bash -c 'wine "$1" "$(winepath -w "$2")"' _ \
    "$package/wormedit.exe" "$wine_work/ascii/tutorial.wormedit"
# After the isolated session and screenshot inspection:
WINEPREFIX="$wine_work/prefix" wineserver -k
rm -rf "$wine_work"
```

Generate the Windows project path inside the same temporary prefix if using
custom drive mappings. Check open, Save As/reopen and export in the `曲` folder
as well as an ASCII path. Playback must produce real nonzero audio; recording
Wine's output from a null-sink monitor verifies PCM without playing it on the
desktop. Test the tutorial's osu!mania output group explicitly and
record any application rejection with its full message. Use Help → User manual
to open the packaged local HTML, keeping its browser on the isolated display.
For the tutorial, create an osu!mania export target in Project and enable
**Flatten BMS branches to the active outcome** before saving the target. This
choice is saved per output group in native schema 25. Export from Compose, then
reimport the resulting `.osu` and check its 16 notes, eight columns and all four
keysounds. Confirm that every referenced Ogg file is present beside the exported
chart; round-3 Wine verification found that this tutorial export publishes the
chart and manifest but omits its audio assets, so reimport currently reports
missing audio. T-762 remains partial pending that application fix.
Unicode startup arguments and all four Export footer buttons pass at `3229cfe`;
include a direct command-line launch and an Explorer-style wide-character shell
launch with a `曲` path when repeating the release smoke.
For the Help check, set `HKCU\Software\Wine\WineBrowser`'s `Browsers` value in
the temporary prefix to a browser wrapper that uses its own temporary profile
on the same Xvfb display. This avoids the desktop browser's profile/IPC routing.

## macOS owner-verification bundle (unsigned)

Run this on a real Mac, in a native arm64 terminal on Apple Silicon or x86_64
terminal on Intel. This lane stages the build; macOS launch/audio/export remain
unverified until the owner completes the checklist below. CI's macOS job builds
only the headless preset; commit `ba2ce77` fixes native C++ compatibility but does
not establish that the GUI bundle works.

Install current Xcode Command Line Tools with `xcode-select --install` (finish
the installer before continuing), install [Homebrew](https://brew.sh/), then:

```sh
brew install cmake ninja pkgconf asciidoctor python ffmpeg
git submodule update --init --recursive
scripts/macos-package.sh
# Optional output directory and CMake version override, after T-761 is merged:
# scripts/macos-package.sh build/dist -DWORMEDIT_VERSION_SUFFIX=-rc1
```

CMake must be at least 4.2 (libotoworm's requirement). The script probes the
selected Apple compiler/SDK for C++23 `std::expected`; update Xcode/CLT if it fails.
It uses `xcrun` for C, C++ and Objective-C++, and needs network access for pinned
FetchContent dependencies on the first build. SDL3 is compiled from this project's
pinned source with Metal support, not Homebrew SDL. Lua 5.4.8 and Ogg/Vorbis are
pinned source builds; miniaudio/stb are pinned dependencies. Lua/LuaRocks, SDL3,
and libiconv Homebrew packages are unnecessary: zlib and iconv resolve from the
macOS SDK. FFmpeg is discovered using pkg-config with `brew --prefix ffmpeg` and
`brew --prefix` (both `/opt/homebrew` and `/usr/local` work).

The build lives in `build/macos-release-<arch>`, uses three jobs by default
(`WORMEDIT_BUILD_JOBS` overrides), and enables Lua, audio, images, FFmpeg video
and the offline manual. Installation selects the `Wormedit` and `Manual`
components, excluding dependency SDK headers/libraries and their absolute paths.
The minimum deployment target defaults to the build
Mac's macOS major/minor version. `WORMEDIT_MACOS_DEPLOYMENT_TARGET` can override
it, but an older target requires compatible SDK and Homebrew libraries and needs
separate verification on that OS. This is a single-architecture build, with no
claim of compatibility with older Macs or macOS versions.

Output is `build/dist/Wormedit-<full-version>-macos-<arch>.zip` and a SHA-256
sidecar. The full version uses `WORMEDIT_VERSION_FULL` from T-761, falling back to
numeric `PROJECT_VERSION` before that merge; both plist version fields stay
numeric. The archive contains `Wormedit.app`; it has an ad-hoc signature, with no
Developer ID, notarization, hardened runtime or entitlements. The script signs
nested libraries after rewriting paths and verifies the complete signature.
It writes revision, tracked local changes, macOS/compiler details and a complete
`otool -L` listing into `Contents/Resources/BUILD-*.txt` (changes use `.patch`).

The bundle uses `Contents/MacOS/Wormedit`, `Contents/Frameworks` for recursive
non-system dylibs, and `Contents/Resources` for `assets/`, `manual/` and notices.
[SDL_GetBasePath](https://wiki.libsdl.org/SDL3/SDL_GetBasePath) returns Resources
inside an app bundle; the plist explicitly selects that behavior. Fonts and UI
themes in `frontend.cpp`, field themes loaded from that same theme root,
hitsound playback/export in `application.cpp`, and the offline Help menu all
use this base. Project media stays relative to the project; preferences use
SDL's user preference directory. External-player `<apppath>` expansion also uses
SDL's base (Resources in this bundle); configure an external player's executable
with its own absolute path. Nothing depends on the Finder working directory.

[BundleUtilities](https://cmake.org/cmake/help/latest/module/BundleUtilities.html)
copies dependencies recursively and rewrites install names. A separate audit
rejects residual rpaths, unresolved references, absolute non-system dependencies,
escaping symlinks and missing CPU slices before making the ZIP. Do not replace
system `/usr/lib` or `/System/Library` dependencies with Homebrew copies.
The placeholder W icon is generated with the macOS SDK by
`packaging/macos/generate-icon.swift`; replace that one generator with owner art,
then delete `build/macos-release-<arch>/macos-icon` and rebuild to refresh it.

**Merge prerequisite (T-763/T-764):** `cmake/third-party-notices.cmake` is supplied
by the Linux lane. Its shared destination must be
`Wormedit.app/Contents/Resources/third-party` before the notices include runs;
`cmake/macos-bundle.cmake` records the integration TODO. Notices should use the
application's default `Wormedit` install component. The package script
refuses to archive without this directory; no second license list is maintained
here. Homebrew's [FFmpeg formula](https://formulae.brew.sh/formula/ffmpeg) enables
GPL components. This command produces a local owner-verification build, not an
LGPL-only release artifact. Public redistribution needs the matching notices,
source/compliance materials for its actual recursive dependency set, or a
separately prepared LGPL FFmpeg SDK; the Windows SDK is not a macOS SDK.

### macOS owner checklist and Gatekeeper first open

Record macOS version, CPU, build revision, archive SHA-256, and pass/fail for each
item. Keep `build/macos-release-<arch>/macos-dependencies.txt` with the report.

1. Run the commands above from the integrated RC checkout. Confirm the archive's
   full RC version and numeric plist version, no missing-resource/path audit
   errors, and a successful `codesign --verify --deep --strict` in the log.
2. Extract the ZIP into a fresh folder whose name contains spaces (for example
   `~/Desktop/Wormedit RC Test`). Inspect Contents/Resources for fonts, UI and
   field themes, hitsounds, manual (including Lua API), license notices and build
   records. Recheck the extracted signature:
   `codesign --verify --deep --strict --verbose=2 "$HOME/Desktop/Wormedit RC Test/Wormedit.app"`.
3. Move the extracted app to `/Applications` or another directory outside the
   checkout. Launch it from Finder with no terminal environment. Prefer a Mac
   without Homebrew for this relocation check; do not rename/remove Homebrew on
   your working machine. Verify no missing-dylib or missing-font/theme errors.
4. A local build may have no quarantine attribute. For a downloaded/quarantined
   copy, Gatekeeper can block this ad-hoc signed app. Try right-click **Open**;
   if current macOS requires it, use Apple's
   [System Settings → Privacy & Security → Open Anyway](https://support.apple.com/en-us/102445)
   flow, then confirm Open. For this trusted owner build only, an
   alternative is `xattr -dr com.apple.quarantine "/Applications/Wormedit.app"`.
   Do not disable Gatekeeper globally. Rejection by `spctl` is expected without
   notarization and is not a signature-integrity failure.
5. Open a copied manual fixture project from
   `Wormedit.app/Contents/Resources/manual/fixtures` (copy the whole fixture
   directory to a writable location first). Check Compose, switch UI/field
   themes, and verify readable Latin/Japanese fonts without fallback warnings.
6. Play and pause a chart with audio; check keysounds, bundled default hitsounds
   and the metronome. Import an MP3/Ogg and a small video to exercise FFmpeg;
   record any audio/device/decoder error. Save/reopen a native project copy.
7. Export an osu!mania output group to a writable folder. Reopen the `.osu`
   output and confirm its chart and copied audio resources. Export preview audio
   with default hitsounds and confirm they are audible.
8. Use **Help → User Manual** while offline. Confirm the local HTML opens in the
   browser, screenshots/fixture links work, and the Lua API reference opens.
9. On a Retina display, resize/maximize the window, change UI scale, and move it
   between displays if available. Check sharp text, pointer alignment, scrolling
   and dialogs. Exercise native menu bar File/Edit/View/Playback/Help, Command
   shortcuts, enabled states, window close and Command-Q; check About's version.
   To compare the fallback menu, fully quit and run from a terminal with
   `WORMEDIT_IN_WINDOW_MENU=1 "/Applications/Wormedit.app/Contents/MacOS/Wormedit"`.
10. **STOP:** return the completed checklist and build records to the release
    coordinator. Until these checks pass, label macOS as staged, not verified.

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
