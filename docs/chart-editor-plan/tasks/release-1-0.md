# Task cards: 1.0 release candidate

[Back to the task-card index](../tasks.md).

Release engineering for **1.0.0-rc1** (owner, 2026-10-26). Windows and Linux ship first; the
macOS bundle is staged here and verified later by the owner on a real Mac without a signing
certificate. Owner decisions: the native project stays a folder-form JSON file with loose assets
([open question 12](../decisions/open-questions.md)); a build refuses projects from a newer
schema; every persisted change bumps the schema (see `code-style.md`). No feature work enters
the release beyond these cards; T-58 and the other open feature cards are deferred past 1.0.

## T-760 Persist bookmark list order under native schema 24

- **Status:** done (2026-10-26; the seam-test blocker was fixed on main in `75dc758`; main GATE at `03dfce8` passes 5,426/5,426 tests, build and whitespace, and the changed-TU complexity check passes after the script fix in the next commit)
- **Outcome:** projects with `bookmark_orders` (added in `a18480e` without a schema bump) are written as schema 24, schema 23 files migrate with no order, and a schema 23 build refuses a schema 24 file instead of silently dropping the order.
- **Read first:** `include/wormedit/core/document.h` (`current_document_schema_version`, `bookmark_list_order_t`), `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `docs/chart-editor-plan/decisions/code-style.md` (persisted schema rule).
- **Files owned:** `include/wormedit/core/document.h` (version constant only), `include/wormedit/formats/native_project.h` if it repeats the version, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`, `tests/formats/native_project_lua_recipe_tests.cpp`, checked-in schema/fixture files that pin the current version, `docs/architecture.md`, `docs/lua-api/README.md`, this card's status and evidence.
- **Supporting-file extensions (2026-10-26):** lane coordinator approves the architecture and Lua API text to identify schema 24 as current while retaining schema 23 introduction history; pin the existing Lua recipe compatibility regression explicitly to schema 23 so the version bump does not silently turn it into a schema 24 test. Card edits record ownership and verification only.
- **Steps:**
  1. Bump the document and native-project schema constant to 24 and add an additive 23→24 migration step (no data change; `bookmark_orders` absent means default order).
  2. Add regressions: a schema 23 document migrates to 24 with empty orders; a schema 24 document round-trips its orders; a schema 25 document is refused with `unsupported_version` and leaves no output.
  3. Add a guard test that pins the set of top-level `song_document_t` JSON keys written for the current schema, with a failure message telling the author to bump the schema and add a migration.
  4. Update every fixture or doc string that names schema 23 as current.
- **Invariants:** every schema 16–23 fixture still loads; save output is byte-identical for documents without custom bookmark order apart from the version number.
- **Reject/failure behavior:** a refused newer file leaves the open project and the file on disk untouched.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'native|migration|bookmark'`, then GATE.
- **Out of scope:** recovery-journal format changes (it embeds the same document schema; only follow the constant), new bookmark features.
- **Done so far:** schema 24 follows the existing native-codec alias; the additive 23→24 step changes no data and preserves orders from the unbumped schema23 build. The guard pins all 20 top-level keys with every optional field populated. The existing future-schema regression now exercises schema 25 and its `unsupported_version`/empty-result contract. Recovery journals delegate to the native codec; recovery envelopes already follow the shared constant. Checked-in native/manual fixtures are schema 6 migration inputs, with no current-version 23 pins; historical schema23 introduction references remain accurate.
- **Remaining:** resolve or separately disposition the timing cut-axis failure below and rerun GATE before marking this card done. Timing geometry is outside this card's owned files; no timing application/test changes were made here.
- **Verification (2026-10-26):** bookmark/schema cases pass **48 assertions / 6 cases** (`/tmp/wormedit-lanes/rc1/wyrm/bookmark-schema-tests.log`); focused CTest passes **254/254** (`focused-ctest.log`); clang-tidy plus baseline comparison passes **6/6 translation units**, including codec, recovery storage and journal header consumers, with no allowance increases (`complexity.log`, detail directory `wormedit-complexity.v5e4f_o7`). All logs are under `/tmp/wormedit-lanes/rc1/wyrm/`. Initial guard-fixture validation and test-complexity findings were corrected before these final focused checks. Real Verify tails:

  ```text
  100% tests passed out of 254
  Total Test time (real) =  41.00 sec
  complexity: checked 6/6
  complexity: passed (6 translation units)
  gate: 3 job(s), set explicitly; logs in /tmp/wormedit-lanes/rc1/wyrm/wormedit-gate.gGqLt6
    pass  build
    FAIL  ctest  (/tmp/wormedit-lanes/rc1/wyrm/wormedit-gate.gGqLt6/ctest.log)
    pass  whitespace
    pass  complexity
    99% tests passed, 1 tests failed out of 5426
  gate: FAILED (ctest)
  ```

- **Compatibility artifacts:** `schema-compatibility.log` records **13 passing converter checks**: BMSON (35,070 bytes), scoped BMS (27,632 bytes) and the manual tutorial (54,108 bytes) are byte-identical to captured schema23 output apart from the version; schemas 16–23 load/save as 24; the captured schema23 converter refuses 24, and the current converter refuses 25, leaving source SHA-256 unchanged and no native output. Inputs/results are in `schema23/`, `schema24/`, `schema23-refuses-24/` and `schema24-refuses-25/`; the reproducer is `verify-schema.py` in the lane log directory.
- **Application finding / gate blocker:** `ctest --test-dir build -j 3 --output-on-failure -R '^frontend-state\.Timing stage preserves fine row targets from the drawn cut axis$'` reproduces **70/72 assertions passing, two failures** at `tests/ui/nuklear_event_harness_tests.cpp:7489`: with `cuts=true`, source row **9120** stages at measure **10**, row **0**, and differs from `indices.measures->position_at(9120)` in both upscroll and downscroll. Logs: `timing-stage-repro.log`, `timing-stage-debug.log` (windowless GDB inspection), `gate-initial.log`; the first gate passes the other **5,425/5,426** tests. This test and its timing implementation are unchanged in this lane; no claim that the failure predates the branch has been verified.

## T-761 Carry a pre-release suffix in the version

- **Status:** done (2026-10-26; same main GATE evidence as T-760: 5,426/5,426 tests after `75dc758`)
- **Outcome:** the build reports `1.0.0-rc1` in About, in `wormedit-convert` output where it reports a version, and in package file names, from a numeric CMake `project(VERSION 1.0.0)` plus one `WORMEDIT_VERSION_SUFFIX` cache variable (default `-rc1`; empty for a final release).
- **Read first:** `CMakeLists.txt` (`project(` and `configure_file`), `config.h.in`, `src/ui/nuklear/app/frontend.cpp` (About dialog), `scripts/windows-cross-package.sh`, `README.md` (status line).
- **Files owned:** `CMakeLists.txt` (project version, suffix variable, configure step), `config.h.in`, `README.md` (status line only), `docs/building.md` (a short "Release versions" section), `scripts/windows-cross-package.sh` (archive name only), a focused test only if one already checks the version string.
- **Steps:**
  1. Set `project(... VERSION 1.0.0)`; add `WORMEDIT_VERSION_SUFFIX` and a derived `WORMEDIT_VERSION_FULL`; make `config.h.in` define `WORMEDIT_VERSION` from the full string and expose the numeric parts separately.
  2. Name the Windows archive `wormedit-<full version>-windows-x64.zip`.
  3. Replace the README "pre-release (0.1.0)" status with a release-candidate status naming `1.0.0-rc1`.
  4. Document how to cut a final release (empty suffix) in `docs/building.md`.
- **Invariants:** `PROJECT_VERSION` stays purely numeric (Windows resource and macOS bundle versions need it).
- **Reject/failure behavior:** a suffix containing characters outside `[-.0-9A-Za-z]` fails configuration with a message.
- **Verify:** configure `build`, rebuild, `grep WORMEDIT_VERSION build/config.h`, GATE; isolated Xvfb About-dialog capture showing `1.0.0-rc1`.
- **Done so far:** stale-plan check found numeric `0.1.0` and source-parsed Windows names. `PROJECT_VERSION` is now numeric `1.0.0`; cache suffix defaults to `-rc1`; normal CMake variable `WORMEDIT_VERSION_FULL` is `1.0.0-rc1`. Both `build/config.h` and the application include expose the full string and numeric macros. The converter currently has no application-version output to update. Empty-suffix configuration succeeds; `bad_suffix` is rejected with `WORMEDIT_VERSION_SUFFIX may contain only [-.0-9A-Za-z]`.
- **Evidence (2026-10-26):** native configure/rebuild exit 0; `grep WORMEDIT_VERSION build/config.h` prints `"1.0.0-rc1"` plus numeric `1`, `0`, `0`; final-suffix configure exits 0 with `"1.0.0"`; invalid suffix fails as required. Logs: `/tmp/wormedit-lanes/rc1/drake/t761-{configure,build,version-checks}.log`. `STEPS=/tmp/wormedit-lanes/rc1/drake/about-steps.sh scripts/smoke-compose-virtual-x.sh /tmp/wormedit-lanes/rc1/drake/t761-about.png landing` exits 0; both its Help-menu screenshot and About screenshot were inspected, and About visibly reads `WormEdit 1.0.0-rc1`. Code-style review checklist: no new C++ functions, copied helpers or parallel version owner.
- **GATE evidence:** `scripts/gate.sh --jobs 3` exits 1; full logs `/tmp/wormedit-gate.QVRSOh/`, summary `/tmp/wormedit-lanes/rc1/drake/t761-gate.log`:

  ```text
  pass  build
  FAIL  ctest  (/tmp/wormedit-gate.QVRSOh/ctest.log)
  pass  whitespace
  pass  complexity
  99% tests passed, 1 tests failed out of 5292
  gate: FAILED (ctest)
  ```

- **Documentation evidence:** `ctest --test-dir build -j 3 --output-on-failure -R '^documentation\.'` exits 0; tail `100% tests passed out of 6` (log `/tmp/wormedit-lanes/rc1/drake/t761-documentation-final.log`).
- **Remaining:** `frontend-state.Timing stage preserves fine row targets from the drawn cut axis` fails at `tests/ui/nuklear_event_harness_tests.cpp:7489` for `cuts=true`, `row=9120`, both scroll directions (70/72 assertions pass). Reproduce: `ctest --test-dir build -j 3 --output-on-failure -R '^frontend-state\.Timing stage preserves fine row targets from the drawn cut axis$'`; the isolated rerun also fails (log `/tmp/wormedit-lanes/rc1/drake/t761-timing-recheck.log`). These C++ files are unchanged by T-761. Resolve this timing-stage finding in its owning lane and rerun GATE before marking T-761 done; version work is committed before T-762 starts.
- **Out of scope:** AppImage and macOS names (T-763, T-764 consume the variable).

## T-762 Build and check the Windows release zip under Wine

- **Status:** partial (2026-10-26; Windows zip and notice audit pass; Wine finds Unicode startup-path and tutorial conversion defects)
- **Outcome:** `scripts/windows-ffmpeg.sh` then `scripts/windows-cross-package.sh` produce `wormedit-1.0.0-rc1-windows-x64.zip`; unpacked into a fresh Wine prefix inside isolated Xvfb it starts, opens a manual fixture project, plays audio without error, exports an osu!mania output group and opens the offline manual, and the zip contains every third-party license notice.
- **Read first:** `docs/building.md` (Windows sections), `scripts/windows-cross-package.sh`, `scripts/windows-cross-wine-test.sh`, `scripts/windows-install.sh`, `cmake/windows-runtime.cmake`.
- **Files owned:** `scripts/windows-cross-package.sh`, `scripts/windows-cross-wine-test.sh`, `scripts/windows-install.sh`, `scripts/windows-ffmpeg.sh`, `cmake/windows-runtime.cmake`, `cmake/toolchains/`, `docs/building.md` (Windows sections), a new `scripts/windows-release-smoke.sh` if the checks are worth keeping.
- **Steps:**
  1. Build the pinned FFmpeg SDK and the release package from a clean `build/windows-cross-release`; record the zip size and file list.
  2. Check the zip: no host (Linux) libraries, no debug-only DLLs, licenses for SDL3, nuklear, nlohmann-json, miniaudio, zlib, FFmpeg, Lua, fonts and icons; the FFmpeg configuration is LGPL (no `--enable-gpl`/`--enable-nonfree`).
  3. Run the Wine checks from the Outcome inside Xvfb only; capture screenshots. Fix packaging defects in owned files; report application defects as findings with reproduction steps.
  4. Write the exact release commands into `docs/building.md`.
- **Invariants:** never open windows on the owner's desktop; the Wine prefix is temporary.
- **Reject/failure behavior:** a missing DLL or license fails the package script instead of producing a zip.
- **Verify:** the package script exit status and file list, the Wine screenshots (inspect them), `ctest --test-dir build -R '^documentation\.'`.
- **Done so far:** round 2 reuses the retained FFmpeg SDK and warm release build as authorized. Main `75dc758` resolves both round-1 compile findings; the real Windows application now builds with MinGW-w64 GCC 16.2.0. Round-1 SDK/source evidence remains in `/tmp/wormedit-lanes/rc1/drake/REPORT.txt`. Coordinator fix `e0c049b` is cherry-picked as `d4bc31c`; T-760/T-761 completion evidence is preserved.
- **Windows runtime integration (owner-authorized, 2026-10-26):** `cmake/windows-runtime.cmake` assigns application installs and the later shared notice rules to `Wormedit`, with SDL archives in `Development`. Both Windows installers select `Wormedit` and `Manual`, avoiding dependency SDKs and Vorbis's absolute `/usr/local/include` installs. The shared T-763 module supplies FFmpeg redistribution files without a second copy step. Packaging rejects host/static/debug libraries and non-x64 PE binaries and writes an inventory sidecar. No application C++ or shared notice module changes; no supporting-file ownership extensions. Packaging commit: `3450ad3`.
- **Package evidence:** `WORMEDIT_BUILD_JOBS=3 scripts/windows-cross-package.sh /tmp/wormedit-lanes/rc1/drake-2/dist` exits 0 (`package-committed.log`), producing `wormedit-1.0.0-rc1-windows-x64.zip`, **54,130,365 bytes**, **153 files**, SHA-256 `7c17bf2559809f90287aae412b8fd47e5d672a0de28ec69aaf590bd0166f039f`. Its build revision is `3450ad3` and `BUILD-CHANGES.patch` is empty. Inventory and SHA-256 sidecars accompany the zip. Full audit in `audit.log`: 7 x64 PE binaries; every recursive import is bundled or a Windows system/API-set DLL; no ELF/Linux libraries, SDK archives or debug symbols; all 21 dependency notice groups and both offline manuals present. FFmpeg source SHA-256 remains `8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e`, with `CONFIG_GPL=0`, `CONFIG_NONFREE=0`, `CONFIG_VERSION3=0` and LGPL-2.1-or-later redistribution files.
- **Reject behavior verified:** real release-install copies fail with exit 1 for an absent `avutil-61.dll` and an absent nuklear license; SDK/source files remain untouched (`install-rejections.log`, `missing-{dll,notice}.log`). SDK preflight rerun: `7/7 package SDK preflight checks passed (1 accepted, 6 rejected)` (`preflight.log`). The initial package audit also caught and refused a staged `libSDL3.a` before the final component correction; no zip was written from that staging tree.
- **Wine evidence:** Wine 11.20 Staging, fresh temporary win64 prefix, all windows/browser helpers confined to isolated Xvfb; **28 screenshots inspected**. The unpacked zip opens the packaged tutorial (16 notes, four keysounds), plays finite nonzero F32LE stereo/44.1-kHz audio (ASCII peak `0.334175825`; Unicode native-picker open peak `0.334130824`), saves and reopens native projects under both ASCII and `曲` folders. The comparison manual fixture exports a 4K osu!mania group under both paths; both outputs are 525-byte, byte-identical `.osu` files with eight HitObjects (SHA-256 `3c8b78e7c4a87ac77ebbe10a63c2a1589ad45544741d80a41203e015542997d6`), and the Unicode output reimports with all eight notes. Help → User manual opens the packaged local `manual/index.html` in a separate temporary Chromium profile (`manual-browser.png`, `manual-url.txt`). Runtime witnesses are in `runtime-results.txt` and `evidence-projects/`; the temporary Wine prefix is removed after capture.
- **Application findings (reported, not fixed):**
  1. Identical fresh tutorial copies launched as startup arguments load under ASCII but fail under `曲`: the recovery path shows `controls/?`, then “Primary project was not loaded; review the completed recovery scan.” File → Open on the Unicode path, Save As/reopen and audio work. Reproduction and suspected narrow-argument boundary (`src/sys/app/application_startup.cpp:22`) are recorded in `findings.txt`; captures `cli-{ascii,unicode}.png`.
  2. The tutorial's osu!mania group refuses publication on both ASCII and Unicode paths: “The target cannot preserve a BMS branch tree; select active projection explicitly.” The export dialog has no such choice; no output is published. This is the T-767 application workflow, independently reproduced in Wine (`tutorial-export-ascii-{dialog,result}.png`, `export-unicode-result.png`). The compatible comparison export above passes.
- **Verification evidence:** all round-2 artifacts/logs are under `/tmp/wormedit-lanes/rc1/drake-2/`. Final package tail: `/tmp/wormedit-lanes/rc1/drake-2/dist/wormedit-1.0.0-rc1-windows-x64.zip`; audit tails: `PASS: 7 x64 PE binaries; all recursive DLL imports bundled or Windows system DLLs; no ELF/host libraries, static SDKs or debug symbols` and `PASS: all 21 dependency notice groups plus FFmpeg corresponding source/configuration and both offline manuals`. `bash -n scripts/windows-cross-package.sh scripts/windows-install.sh` and `git diff --check` pass. Documentation command `ctest --test-dir build -j 3 --output-on-failure -R '^documentation\.'` passes 6/6 (`documentation-final.log`).
- **Remaining:** the application owner resolves Unicode startup arguments and T-767's tutorial projection workflow, then reruns those Wine cases before marking the card done. Packaging, native-picker Unicode open/save/audio/export and offline manual checks pass; there are no remaining packaging defects found in this lane.
- **Out of scope:** an installer (zip only for RC1), code signing, the version string (T-761).


## T-763 Package Linux as an AppImage

- **Status:** partial (2026-10-26; package/runtime checks pass; required GATE has one unchanged application-test failure)
- **Outcome:** `scripts/linux-appimage.sh [output-dir]` builds `wormedit-1.0.0-rc1-x86_64.AppImage` from a Release build inside a pinned old-glibc container; the image starts on this machine in isolated Xvfb (and with `--appimage-extract-and-run`), finds its assets and offline manual, plays audio, exports a chart, and carries all third-party license notices.
- **Read first:** `CMakeLists.txt` (install rules near `install(TARGETS wormedit`), `cmake/windows-runtime.cmake` (license list), `cmake/wormedit_manual.cmake`, `docs/building.md`, `scripts/windows-cross-package.sh` (script shape to mirror).
- **Files owned:** new `scripts/linux-appimage.sh`, new `packaging/linux/` (Dockerfile, `.desktop`, AppStream metadata if added, placeholder icon), new `cmake/third-party-notices.cmake` (shared license install used by Linux and Windows; replace the duplicated list in `cmake/windows-runtime.cmake`), new `cmake/notices/` (owner-authorized libotoworm MIT terms and upstream SHA-256, GCC and SDL/Khronos notices; supporting-file extension for complete redistribution notices), `CMakeLists.txt` (include it and Linux desktop-file install only), `docs/building.md` (Linux packaging section).
- **Steps:**
  1. Container: pin a base image old enough for broad glibc support (e.g. Ubuntu 22.04) with the compiler, CMake, SDL3 build deps and an LGPL FFmpeg; document the glibc floor.
  2. Install into an AppDir, add the `.desktop` file and icon, bundle libraries with pinned `linuxdeploy`/`appimagetool` releases (checksums recorded); exclude GPU/Vulkan/GL driver libraries and libc per the AppImage excludelist.
  3. Move third-party license installation into `cmake/third-party-notices.cmake` and install it on every platform.
  4. Run the Outcome checks in Xvfb; inspect screenshots; record image size and `ldd`-style bundled library list.
- **Invariants:** assets keep resolving through `SDL_GetBasePath` next to the binary; the native and Windows install layouts stay unchanged apart from the shared license step.
- **Reject/failure behavior:** a missing bundled library, unresolved dependency or GPL FFmpeg fails the script.
- **Verify:** script exit status, AppImage launch screenshots (inspected), GATE (CMake changes), the Windows package still lists the same licenses.
- **Done so far:** implementation committed as `5d6c29e`; the pinned Ubuntu 22.04/GCC 14 Release build, common notices and all Linux runtime/package checks pass. No application/test source changed from `538f511`.
- **Remaining:** owner resolves the timing-stage failure below and reruns GATE; T-762 confirms the actual Windows zip after integrating the shared installer; T-766 rebuilds the merged T-761 version as `1.0.0-rc1` (this isolated pre-T-761 branch configures `0.1.0`). Application defects are reported, not fixed in this lane.
- **Verification evidence (2026-10-26):** logs, captures, reproductions and artifact sidecars are under `/tmp/wormedit-lanes/rc1/hydra/`; `findings.md` records the GCC optimizer findings, unavailable BMS-to-osu UI projection choice and the deterministic timing-stage failure.
  - `scripts/linux-appimage.sh /tmp/wormedit-lanes/rc1/hydra/dist` exited 0: `wormedit-0.1.0-x86_64.AppImage`, 57,252,344 bytes; SHA-256 `e4dab88f865c00365f2a83722e6b8269eaf46d28042d351a36ad7dc28662af82`. Real tail: `Highest required GLIBC: 2.35`, `Success`, `/out/wormedit-0.1.0-x86_64.AppImage: 57252344 bytes`.
  - `dist/wormedit-0.1.0-x86_64.AppImage.libraries.txt` records resolved dependencies: `libavcodec.so.63`, `libavformat.so.63`, `libavutil.so.61`, `libswresample.so.7`, `libswscale.so.10`, `libstdc++.so.6`, `libgcc_s.so.1`; no libc/GPU-driver libraries or development SDKs. FFmpeg `CONFIG_GPL`, `CONFIG_NONFREE`, `CONFIG_VERSION3` are 0.
  - Inspected `normal-compose.png`, `extract-compose.png`, `export-bms-complete.png`, `manual-browser.png`: normal and explicit `--appimage-extract-and-run` launches render assets and play the packaged tutorial; both captured finite F32LE stereo/44.1 kHz PCM with peak 0.334113. BMS export writes a chart plus 4 byte-identical Ogg assets. Help opens the mounted offline manual. Packaged Lua reference check: `Lua API: 13 HTML pages, local links/anchors and offline assets passed`.
  - Windows-layout notice-only CMake install passes: 46 files, original five notice paths preserved, FFmpeg SDK auto-discovered, SDL embedded notices/source snapshot checked (`windows-notices.log`, `windows-notices.files.txt`); this is not a Windows zip runtime check. Shell syntax, desktop-file validation and whitespace pass.
  - Documentation checks after the card update: `100% tests passed out of 6` (`documentation-final.log`).
  - Full GATE output (copied to `gate-native/`); isolated test 3984 rerun also fails: row 9120 at the removed interval seam, cuts enabled, both scroll directions; 70 assertions pass and 2 fail. Reproduce: `ctest --test-dir build --output-on-failure -j 1 -R '^frontend-state\.Timing stage preserves fine row targets from the drawn cut axis$'`.

    ```text
    gate: 3 job(s), set explicitly; logs in /tmp/wormedit-gate.rdUjnT
      pass  build
      FAIL  ctest  (/tmp/wormedit-gate.rdUjnT/ctest.log)
      pass  whitespace
      pass  complexity
      99% tests passed, 1 tests failed out of 5304
    gate: FAILED (ctest)
    ```

- **Out of scope:** Flatpak, distro packages, AppImage update information, the final icon (owner art; the placeholder lives in one path so it can be swapped).

## T-764 Stage an unsigned macOS app bundle for owner verification

- **Status:** partial (2026-10-26; macOS staging implemented; Linux GATE has an unchanged timing-placement test failure)
- **Outcome:** `scripts/macos-package.sh` on a Mac produces `Wormedit-1.0.0-rc1-macos-<arch>.zip` holding `Wormedit.app` with assets and manual in `Contents/Resources`, non-system dylibs in `Contents/Frameworks` with rewritten install names, an `Info.plist` built from the numeric version, and an ad-hoc signature; a checklist in `docs/building.md` lets the owner verify it.
- **Read first:** `CMakeLists.txt` (`APPLE` blocks, install rules), `src/sys/app/macos_menu.mm`, the `SDL_GetBasePath` callers in `src/sys/app/application.cpp` and `src/ui/nuklear/app/frontend.cpp`, `docs/building.md`, `.github/workflows/ci.yml` (macOS job).
- **Files owned:** new `cmake/macos-bundle.cmake`, new `packaging/macos/` (`Info.plist.in`, entitlements if needed, placeholder `.icns` or its generator), new `scripts/macos-package.sh`, `CMakeLists.txt` (include the bundle file inside `if(APPLE)` only), `docs/building.md` (macOS packaging and Gatekeeper section), this card's status/evidence only (supporting-file extension approved by lane coordinator, 2026-10-26).
- **Steps:**
  1. Make `wormedit` a `MACOSX_BUNDLE` with install rules placing assets and the manual where `SDL_GetBasePath` resolves inside a bundle (`Contents/Resources`).
  2. Bundle dylibs (prefer CMake `RUNTIME_DEPENDENCY_SET` like the Windows runtime) and fix install names; ad-hoc sign with `codesign --force --deep -s -`.
  3. Write the owner checklist: build, launch, Gatekeeper first-open (right-click Open or `xattr -dr com.apple.quarantine`), audio, export, manual, Retina scaling, menu bar.
  4. Prove what can be proved off-Mac: CMake configure of the bundle logic stays inert on Linux and Windows, shell lint of the script.
- **Invariants:** Linux and Windows builds and install layouts are unchanged.
- **Reject/failure behavior:** the script fails if any bundled binary still references a Homebrew/absolute non-system path.
- **Verify:** GATE on Linux; `bash -n scripts/macos-package.sh`; then STOP and hand the checklist to the owner, who runs it on the Mac.
- **Done so far:** native package script, bundle configuration, placeholder icon generator, dependency audit, prerequisites and owner checklist are implemented. Application/manual install components exclude dependency SDK installs (including Vorbis's absolute `/usr/local/include` headers and unrelated unbuilt libraries).
- **Remaining:** release coordinator handles the unchanged export-cut timing test failure outside this packaging card, then reruns GATE; merge T-763's shared notices destination/component wiring; owner runs the Mac checklist after lane integration. No macOS execution is possible in this Linux lane.
- **Implementation notes (2026-10-26):** `BundleUtilities` performs recursive dylib copying/install-name rewriting; `packaging/macos/verify-bundle.py` audits non-system references, residual rpaths, symlinks and CPU slices before ad-hoc signing. Existing SDL resource-relative callers need no code changes. `WORMEDIT_VERSION_FULL` falls back to `PROJECT_VERSION`; plist versions remain numeric. SDK-generated W icon is replaceable owner art. T-763 merge TODO: align the shared notices destination variable with `Contents/Resources/third-party` and default `Wormedit` component before its include; the script refuses an absent notices directory. No duplicate notice list was added. Homebrew FFmpeg enables GPL components: the documented build is for local owner verification, with public redistribution materials left to release integration.
- **Off-Mac evidence:** logs and generation-only fixtures live in `/tmp/wormedit-lanes/rc1/tiamat/`. `bash -n scripts/macos-package.sh` exited 0; Linux execution rejects with `macos-package: run this script on a real Mac (native build only)` (exit 1). `cmake-inert.log`: `-- Linux: bundle module inert`, `-- Windows: bundle module inert`. Darwin CMake fixtures (compiler checks bypassed, no macOS compilation) generate `1.2.3-rc1`/`1.2.3` archive versions with numeric `1.2.3` plist versions. `component-install.log` confirms app/assets/manual installation into a path with spaces while excluding an absolute dependency SDK destination. Generated Linux build/install rules contain no macOS bundle/icon actions and retain `bin/assets` and `bin/manual`. `bundle-audit-tests.log`: `Ran 9 tests in 0.005s`, `OK`.
- **Linux GATE finding:** initial `scripts/gate.sh --jobs 3` built successfully and ran all 5304 tests: `99% tests passed, 1 tests failed out of 5304`; whitespace passed. Complexity initially reported missing `build/compile_commands.json`; configuring with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` repaired that build-tree prerequisite. Final `scripts/gate.sh --jobs 3` output (`gate.log`, full details in `wormedit-gate.3OqqIW/`): `pass build`, `FAIL ctest`, `pass whitespace`, `pass complexity`, `99% tests passed, 1 tests failed out of 5304`, `gate: FAILED (ctest)`. The only failure is the repeatable application defect below. Initial logs are retained in `gate-initial.log` and `gate-initial-details/`. The standalone documentation check after building passed: `100% tests passed out of 6` (`documentation.log`); its initial run before generated manual/reference outputs existed failed those two checks (`documentation-before-build.log`).
- **Application defect reproduction:** `ctest --test-dir build -j 3 --output-on-failure -R '^frontend-state\.Timing stage preserves fine row targets from the drawn cut axis$'` fails consistently (`timing-repro.log`): 70/72 assertions pass; row 9120 at the export-cut seam does not retain the expected timing position for either scroll direction (`cuts := true`, `T422 expectation at 7489`, `tests/ui/nuklear_event_harness_tests.cpp`). `git diff 538f511 -- src tests include` is empty; no timing/application/test changes were made in this lane.
- **Owner handoff / STOP:** follow the numbered **macOS owner checklist and Gatekeeper first open** in `docs/building.md`; record build/checksum, relocated Finder launch, first-open, themes/fonts, audio/video/hitsounds, native save/reopen, osu!mania/preview-audio exports, offline manual/Lua API, Retina scaling and menu bar/Command shortcuts. No macOS archive, signature or runtime verification is claimed from this Linux lane.
- **Out of scope:** notarization and Developer ID signing (no certificate), a DMG, universal binaries unless trivial.

## T-765 Write the 1.0.0-rc1 release notes and known limitations

- **Status:** blocked-on T-762, T-763, T-767
- **Outcome:** `docs/release-notes/1.0.0-rc1.md` summarizes what Wormedit does, supported formats, downloads per platform with system requirements, the project-format promise (folder form; newer schemas are refused), the Lua API v1 status, known limitations taken from the deferred cards (storyboards/OSBX, Quaver, StepMania rolls and tick counts, MIDI, multivoice sounds, CJK UI, 48K stage focus), the unsigned macOS first-open steps, and how to report bugs.
- **Read first:** `README.md`, `docs/manual/index.adoc`, this card file, the Status lines of deferred cards, `docs/building.md`.
- **Files owned:** new `docs/release-notes/1.0.0-rc1.md`, `README.md` (link to the notes), `docs/manual/chapters/getting-started.adoc` (install section only).
- **Steps:** 1. Draft from the sources above. 2. Check every claim against the RC build or a passing test. 3. Link it from the README.
- **Invariants:** no claim about a platform that T-762–T-764 did not verify; macOS is labelled owner-verified only after the T-764 checklist passes.
- **Reject/failure behavior:** an unverifiable claim is removed, not softened.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; manual build passes.
- **Out of scope:** the website or store pages.

## T-766 Run the release gate and prepare the rc1 tag

- **Status:** blocked-on T-760, T-761, T-762, T-763, T-765, T-767
- **Outcome:** on one release commit the full GATE passes, the Windows zip and AppImage are built from that commit and pass their smoke checks, the documentation and manual checks pass, and an annotated `v1.0.0-rc1` tag is prepared locally for the owner to push.
- **Read first:** `scripts/gate.sh`, `docs/building.md`, this card file.
- **Files owned:** `memory/chart-editor-status/resume.md`, `memory/chart-editor-status/current-milestone.md`, this card file.
- **Steps:** 1. Confirm main is frozen (owner). 2. GATE. 3. Build both artifacts and record SHA-256 sums. 4. Create the annotated tag locally. 5. STOP: the owner pushes the tag and publishes the release.
- **Invariants:** artifacts and the tag name the same commit.
- **Reject/failure behavior:** any failure blocks the tag; nothing is pushed by an agent.
- **Verify:** gate summary, artifact checksums, `git describe` on the release commit.
- **Out of scope:** the macOS artifact (owner builds it on the Mac after T-764).

## T-767 Flatten BMS branches per output group

- **Status:** ready
- **Outcome:** a non-BMS output group (osu!mania, StepMania, BMSON) has a saved "Flatten BMS branches to the active outcome" option; with it on, Compose export of a chart with `#RANDOM`/`#IF`/`#SWITCH` branches publishes the active outcome with the existing flattening warning, and with it off the refusal names the option. The packaged tutorial project exports its BMS chart to osu!mania from the UI.
- **Owner decision (2026-10-26):** the choice lives on the output group and is saved with the project, like the StepMania policy (option 1 of 3). Found by the T-763 smoke check: `wormedit-convert` could set `branches = active_projection`, but no UI path could.
- **Read first:** `include/wormedit/core/document.h` (`output_group_t`, `branch_projection_t`), `src/sys/project/output_group_export.h` (`output_group_export_plan_t::from`), `src/formats/conversion.cpp` (`validate_selected_target`), `src/ui/nuklear/project/setup_panel.cpp` (StepMania group policy controls), `docs/manual/chapters/bms-branches.adoc` ("Export and compare").
- **Files owned:** `include/wormedit/core/document.h` (the output-group field and schema constant), `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/sys/project/output_group_export.h/.cpp`, `src/formats/conversion.cpp` (message text only), the output-group edit command/projection/action files that already carry the StepMania policy (trace them from `group_stepmania_file` before editing and record them here), `src/ui/nuklear/project/setup_panel.cpp`, `docs/manual/chapters/bms-branches.adoc`, `docs/manual/chapters/walkthrough-osu-stepmania.adoc` (export step only), focused tests beside the existing output-group, migration and conversion tests, and one event-harness workflow test.
- **Steps:**
  1. Add the per-group branch policy to `output_group_t` (default: preserve, so existing projects keep refusing until the owner opts in); bump the native schema to 25 with an additive 24→25 migration per `code-style.md`, and extend T-760's key-set guard.
  2. Map it into `conversion_request_t::branches` in `output_group_export_plan_t::from` for non-BMS families; BMS and native groups ignore it.
  3. Edit it through the same document command, projection and Setup controls as the StepMania policy; hide or disable it for BMS and native groups.
  4. Make the refusal text name the option ("enable Flatten BMS branches on this output group").
  5. Tests: migration 24→25, round trip, plan mapping, refusal without the option, successful osu!mania export of the tutorial fixture with it (event harness through Setup and Ctrl+E).
  6. Update the two manual sections; refresh an affected screenshot only if its controls changed.
- **Invariants:** BMS export still preserves the complete program; saved projects without the field load as "preserve"; Undo covers the option change.
- **Reject/failure behavior:** a refused export writes nothing and leaves the project unchanged.
- **Verify:** focused tests, `ctest --test-dir build -R '^documentation\.'`, GATE; isolated Xvfb capture of the Setup control and a successful tutorial export.
- **Out of scope:** choosing a specific outcome per export (the active outcome comes from the branch panel), the CLI (already supports `branches`), BMS repair proposals (T-287).
