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
