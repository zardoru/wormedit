# Task cards: 1.0 release candidate

[Back to the task-card index](../tasks.md).

Release engineering for **1.0.0-rc1** (owner, 2026-10-26). Windows and Linux ship first; the
macOS bundle is staged here and verified later by the owner on a real Mac without a signing
certificate. Owner decisions: the native project stays a folder-form JSON file with loose assets
([open question 12](../decisions/open-questions.md)); a build refuses projects from a newer
schema; every persisted change bumps the schema (see `code-style.md`). No feature work enters
the release beyond these cards; T-58 and the other open feature cards are deferred past 1.0.

## T-760 Persist bookmark list order under native schema 24

- **Status:** partial (2026-10-26; schema 24 and guard implemented; GATE blocked by the timing cut-axis seam test)
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

- **Status:** ready
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
- **Out of scope:** AppImage and macOS names (T-763, T-764 consume the variable).

## T-762 Build and check the Windows release zip under Wine

- **Status:** ready
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
- **Out of scope:** an installer (zip only for RC1), code signing, the version string (T-761).

## T-763 Package Linux as an AppImage

- **Status:** ready
- **Outcome:** `scripts/linux-appimage.sh [output-dir]` builds `wormedit-1.0.0-rc1-x86_64.AppImage` from a Release build inside a pinned old-glibc container; the image starts on this machine in isolated Xvfb (and with `--appimage-extract-and-run`), finds its assets and offline manual, plays audio, exports a chart, and carries all third-party license notices.
- **Read first:** `CMakeLists.txt` (install rules near `install(TARGETS wormedit`), `cmake/windows-runtime.cmake` (license list), `cmake/wormedit_manual.cmake`, `docs/building.md`, `scripts/windows-cross-package.sh` (script shape to mirror).
- **Files owned:** new `scripts/linux-appimage.sh`, new `packaging/linux/` (Dockerfile, `.desktop`, AppStream metadata if added, placeholder icon), new `cmake/third-party-notices.cmake` (shared license install used by Linux and Windows; replace the duplicated list in `cmake/windows-runtime.cmake`), `CMakeLists.txt` (include it and Linux desktop-file install only), `docs/building.md` (Linux packaging section).
- **Steps:**
  1. Container: pin a base image old enough for broad glibc support (e.g. Ubuntu 22.04) with the compiler, CMake, SDL3 build deps and an LGPL FFmpeg; document the glibc floor.
  2. Install into an AppDir, add the `.desktop` file and icon, bundle libraries with pinned `linuxdeploy`/`appimagetool` releases (checksums recorded); exclude GPU/Vulkan/GL driver libraries and libc per the AppImage excludelist.
  3. Move third-party license installation into `cmake/third-party-notices.cmake` and install it on every platform.
  4. Run the Outcome checks in Xvfb; inspect screenshots; record image size and `ldd`-style bundled library list.
- **Invariants:** assets keep resolving through `SDL_GetBasePath` next to the binary; the native and Windows install layouts stay unchanged apart from the shared license step.
- **Reject/failure behavior:** a missing bundled library, unresolved dependency or GPL FFmpeg fails the script.
- **Verify:** script exit status, AppImage launch screenshots (inspected), GATE (CMake changes), the Windows package still lists the same licenses.
- **Out of scope:** Flatpak, distro packages, AppImage update information, the final icon (owner art; the placeholder lives in one path so it can be swapped).

## T-764 Stage an unsigned macOS app bundle for owner verification

- **Status:** ready
- **Outcome:** `scripts/macos-package.sh` on a Mac produces `Wormedit-1.0.0-rc1-macos-<arch>.zip` holding `Wormedit.app` with assets and manual in `Contents/Resources`, non-system dylibs in `Contents/Frameworks` with rewritten install names, an `Info.plist` built from the numeric version, and an ad-hoc signature; a checklist in `docs/building.md` lets the owner verify it.
- **Read first:** `CMakeLists.txt` (`APPLE` blocks, install rules), `src/sys/app/macos_menu.mm`, the `SDL_GetBasePath` callers in `src/sys/app/application.cpp` and `src/ui/nuklear/app/frontend.cpp`, `docs/building.md`, `.github/workflows/ci.yml` (macOS job).
- **Files owned:** new `cmake/macos-bundle.cmake`, new `packaging/macos/` (`Info.plist.in`, entitlements if needed, placeholder `.icns` or its generator), new `scripts/macos-package.sh`, `CMakeLists.txt` (include the bundle file inside `if(APPLE)` only), `docs/building.md` (macOS packaging and Gatekeeper section).
- **Steps:**
  1. Make `wormedit` a `MACOSX_BUNDLE` with install rules placing assets and the manual where `SDL_GetBasePath` resolves inside a bundle (`Contents/Resources`).
  2. Bundle dylibs (prefer CMake `RUNTIME_DEPENDENCY_SET` like the Windows runtime) and fix install names; ad-hoc sign with `codesign --force --deep -s -`.
  3. Write the owner checklist: build, launch, Gatekeeper first-open (right-click Open or `xattr -dr com.apple.quarantine`), audio, export, manual, Retina scaling, menu bar.
  4. Prove what can be proved off-Mac: CMake configure of the bundle logic stays inert on Linux and Windows, shell lint of the script.
- **Invariants:** Linux and Windows builds and install layouts are unchanged.
- **Reject/failure behavior:** the script fails if any bundled binary still references a Homebrew/absolute non-system path.
- **Verify:** GATE on Linux; `bash -n scripts/macos-package.sh`; then STOP and hand the checklist to the owner, who runs it on the Mac.
- **Out of scope:** notarization and Developer ID signing (no certificate), a DMG, universal binaries unless trivial.

## T-765 Write the 1.0.0-rc1 release notes and known limitations

- **Status:** blocked-on T-762, T-763
- **Outcome:** `docs/release-notes/1.0.0-rc1.md` summarizes what Wormedit does, supported formats, downloads per platform with system requirements, the project-format promise (folder form; newer schemas are refused), the Lua API v1 status, known limitations taken from the deferred cards (storyboards/OSBX, Quaver, StepMania rolls and tick counts, MIDI, multivoice sounds, CJK UI, 48K stage focus), the unsigned macOS first-open steps, and how to report bugs.
- **Read first:** `README.md`, `docs/manual/index.adoc`, this card file, the Status lines of deferred cards, `docs/building.md`.
- **Files owned:** new `docs/release-notes/1.0.0-rc1.md`, `README.md` (link to the notes), `docs/manual/chapters/getting-started.adoc` (install section only).
- **Steps:** 1. Draft from the sources above. 2. Check every claim against the RC build or a passing test. 3. Link it from the README.
- **Invariants:** no claim about a platform that T-762–T-764 did not verify; macOS is labelled owner-verified only after the T-764 checklist passes.
- **Reject/failure behavior:** an unverifiable claim is removed, not softened.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; manual build passes.
- **Out of scope:** the website or store pages.

## T-766 Run the release gate and prepare the rc1 tag

- **Status:** blocked-on T-760, T-761, T-762, T-763, T-765
- **Outcome:** on one release commit the full GATE passes, the Windows zip and AppImage are built from that commit and pass their smoke checks, the documentation and manual checks pass, and an annotated `v1.0.0-rc1` tag is prepared locally for the owner to push.
- **Read first:** `scripts/gate.sh`, `docs/building.md`, this card file.
- **Files owned:** `memory/chart-editor-status/resume.md`, `memory/chart-editor-status/current-milestone.md`, this card file.
- **Steps:** 1. Confirm main is frozen (owner). 2. GATE. 3. Build both artifacts and record SHA-256 sums. 4. Create the annotated tag locally. 5. STOP: the owner pushes the tag and publishes the release.
- **Invariants:** artifacts and the tag name the same commit.
- **Reject/failure behavior:** any failure blocks the tag; nothing is pushed by an agent.
- **Verify:** gate summary, artifact checksums, `git describe` on the release commit.
- **Out of scope:** the macOS artifact (owner builds it on the Mac after T-764).
