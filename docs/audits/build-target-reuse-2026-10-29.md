# Build target reuse — 2026-10-29

T-779, baseline `d154284`. This measures compiler actions, not moved lines or reader preference.

## Protocol

Measured tools: GCC 16.2.1 (20260810), CMake 4.4.5, Ninja 1.13.2.

Configure the native Debug application with BUILD_TESTING, Lua and decoders enabled and export `compile_commands.json`. Dependency sources are read from `/home/zardoru/devel/wormedit/build/_deps/*-src`; libotoworm uses the read-only `/home/zardoru/devel/wormedit/third_party/libotoworm` source override. All generated files live in this worktree's own `build`. No root generated output is reused or linked.

Run `python3 cmake/measure_compile_reuse.py build/compile_commands.json`; use `--root PATH` when inspecting another checkout. The script includes only root-relative `src/` and `tests/` sources. It tokenizes `command` with `shlex` (or uses `arguments`), removes exactly `-o` plus its next token, and groups by source, working directory and remaining ordered argv. It does **not** strip definitions, warnings, includes, dependency arguments, compiler paths or feature flags. Missing `-o` fails explicitly. This POSIX argument protocol is for the measured Linux database; MSVC `/Fo` is not silently normalized.

The supplied snapshot was 1213 entries /754 unique sources. The current root database independently gave 1215/756. The independently configured baseline in this worktree gave **1214/755**, with **852 production entries**. All three agree on **17 identical extra production compilations across 10 source files**. Only the local baseline is used for the comparison. Its database was captured before editing at `/tmp/lane-a-before.json`.

## Result

| Same local configuration | Before | After |
|---|---:|---:|
| First-party compiler actions | 1214 | 1075 |
| Unique source files | 755 | 755 |
| Production compiler actions | 852 | 713 |
| Identical extra production actions | 17 | 0 |
| Production sources with identical duplicates | 10 | 0 |

The 139 removed actions exceed the 17 exact duplicates: the frontend now has its own compile configuration instead of inheriting executable-wide flags that never affect most frontend sources. This is an explicit configuration ownership change, not evidence that the previous command lines were identical.

The frontend is a family of STATIC libraries beneath `wormedit_ui_frontend_common`. Mesh, interval index, field renderer, region cache, backend, arrow/theme/tooltip, frontend state, preview and notifications have lower-level compile owners. Narrow tests link those owners directly, avoiding unrelated panel compilation and core dependencies where the original test had none. Project headers, Nuklear configuration, SDL and warnings share a small CMake construction helper; core/JSON requirements are declared only where needed. No decoder or audio transport implementation is moved.

An initial broad archive design reduced full-build commands but made narrow renderer builds depend on all frontend panels and core. Coordinator review rejected that closure regression before acceptance; the lower-level graph corrects it. Source reuse alone was insufficient. Required closures were measured using **all** commands from `ninja -C build -t commands TARGET`, independent of existing built objects, on the original and corrected configurations.

| Required target closure | Original first-party / all compiler actions | Final |
|---|---:|---:|
| Mesh tests | 2 / 445 | 2 / 445 |
| Region-cache tests | 7 / 450 | 7 / 450 |
| Field-renderer tests | 13 / 456 | 13 / 456 |
| SDL backend tests | 13 / 456 | 13 / 456 |
| Frontend state tests | 160 / 603 | 160 / 603 |

The all-action counts include required third-party SDL/Catch/core dependencies. These closures are neutral, not an improvement. Repeat with `python3 cmake/measure_compile_reuse.py build/compile_commands.json --ninja-target wormedit_sdl_command_mesh_tests` (repeat `--ninja-target` for other targets). The original graph was configured from the original two CMake files without building, then the corrected files were restored.

Native preprocessor comparison (`-E -P`, SHA-256 of compiler output) matched **227 original compilations of 88 shared sources**, with zero mismatches: the corrected lower-level graph passed 225 comparisons across 87 sources, followed by the two generator-form variants against the common configuration. Repeat with `python3 cmake/measure_compile_reuse.py build/compile_commands.json --before /tmp/lane-a-before.json`. This establishes native token equivalence despite removed irrelevant executable macros; it is not cross-platform execution evidence.

The frontend source/header conditional audit found consumer-specific branches in `components.cpp` (`WORMEDIT_TEST_ICON_WIDGETS`) and `frontend.cpp` (`WORMEDIT_HAS_MACOS_MENU`), which remain executable sources. `frame_timing.cpp` retains its Debug application `WORMEDIT_FRAME_ALLOCATION_HOOK`; only the two identical renderer-test versions share a separate static library. Other sys/test/instrumentation compilations remain separate. Renderer test support inherits no application allocation hook or test-icon recording. No target-wide macros were blindly copied into a common target.

| Edit / source | Before compile actions | After |
|---|---:|---:|
| `sdl_command_mesh.cpp` | 6 | 1 |
| `theme.cpp` | 5 | 1 |
| `compose_panel.cpp` | 2 | 1 |
| `frame_timing.cpp` | 8 | 7 |
| `components.cpp` | 4 | 4 |
| `frontend.cpp` | 2 | 2 |

Source-registration occurrences across the two CMake files also change: command mesh 5→1, theme 4→1 and static interval index 7→2 (the headless core-test variant remains separate). Common `compose_panel.cpp` registration stays 1→1: the previous source-list variable already avoided registration duplication for the application/event pair. The improvement there is actual object reuse, not fewer edit sites. A literal `rg -n "sdl_command_mesh\.cpp" CMakeLists.txt tests/CMakeLists.txt` search narrows from five registrations in two files to one in one file. Following compilation settings from a test target now requires following its named support library into the root CMake file; that extra indirection is a tradeoff, not a demonstrated navigation win. These are deterministic source-reference observations, not fresh-agent navigation measurements.

These are bounded incremental source-edit compiler actions from the graph. Relinks remain necessary. A shared frontend registration has one implementation list and consumers link one library; behavior-specific units still register separately. The benchmark does not claim header edits rebuild fewer unique sources, or that a large common archive reduces linker work. No wall-time speedup is claimed: four active lanes and substantial swap use make these cold builds contention-affected.

## Real change samples and compatibility

For each commit, intersect `git diff-tree --no-commit-id --name-only -r REF` with the baseline and final compile databases; sum entries for changed production `.cpp` files. These counts measure source-action fanout, not transitive header rebuilds.

| Existing change | Before | After | Interpretation |
|---|---:|---:|---|
| `c6eadce`, sample-control lanes (68 changed files) | 58 | 46 | Frontend portion benefits |
| `4c5fade`, solo-layer selection | 9 | 9 | Neutral |
| `c596924`, prepared lane-key moves | 1 | 1 | Neutral |
| `72f342d`, prepared sound layers | 1 | 1 | Neutral |

Read-only PR evidence: [PR75](https://github.com/zardoru/wormedit/pull/75) changes sys application/dispatch, a header and an event test: no direct `.cpp` reuse improvement is expected. [PR63](https://github.com/zardoru/wormedit/pull/63) adds core codec sources/tests: neutral in this frontend slice. [PR64](https://github.com/zardoru/wormedit/pull/64) removes unused otoworm links; this change neither removes that dependency nor adds it to the common target, so its dependency removal remains semantically independent. Its event-harness link line overlaps textually: retain `wormedit_ui_frontend_common` while deleting `otoworm::otoworm` during integration. [PR65](https://github.com/zardoru/wormedit/pull/65) changes SDL/browser application configuration and frontend platform branches: its diff adds directory-wide `-fwasm-exceptions -pthread` compile/link options before target construction, so these libraries inherit those flags. Executable-specific Emscripten runtime/export/offscreen options remain on `wormedit`. This is structural compatibility evidence only: no Wasm, Windows or macOS build, or pending-branch merge, is claimed. Native and event-only guards still use `WORMEDIT_BUILD_APP OR (BUILD_TESTING AND WORMEDIT_BUILD_EVENT_HARNESS)`; headless-only creates no frontend target.

T-722's fixed fresh-agent exercises in `workflow-navigation-baseline-2026-10-19.md` were reviewed as a warning about noisy navigation claims. T-637 transaction and T-638 ID-traversal ownership are preserved: this lane changes no C++ mutation/gesture implementation. No navigation trial was run and no general reader-efficiency improvement is claimed. The measured benefit is fewer compilation owners/actions.

## Verification

All required local checks passed, with two build/test jobs:

| Configuration/check | Result | Evidence |
|---|---|---|
| Native `scripts/gate.sh --jobs 2` | build, **5585/5585 tests**, whitespace, complexity passed | `/tmp/wormedit-gate.Fuung0` |
| Headless (`APP=OFF`, `EVENT_HARNESS=OFF`) | cached build, **3572/3572 tests** passed | `/tmp/lane-a-headless-{config,build,tests}.log` |
| Event-only (`APP=OFF`, `EVENT_HARNESS=ON`) | cached build, **4407/4407 tests** passed | `/tmp/lane-a-events-{config,build,tests}.log` |
| Restored native configuration | cached build; compile-command counter exactly matches measured final native database | `/tmp/lane-a-restored-{config,build}.log` |
| Final documentation suite | **6/6** passed | `/tmp/lane-a-final-docs.log` |
| Isolated real-window Compose smoke | passed; PNG inspected: grid, toolbar, inspector and sound dock render normally | `/tmp/lane-a-smoke.log`, `/tmp/lane-a-compose.png` |

The native gate used `PATH=/tmp/wormedit-clang-23.1.3/usr/bin:$PATH` and `LD_LIBRARY_PATH=/tmp/wormedit-clang-23.1.3/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}`. The normal build compiler stayed GCC. No C++ source/header changed, so complexity correctly reports **0 affected translation units**; no header-consuming run is applicable and no allowances changed. All test binaries listed successfully. There were **no baseline failures to waive or reproduce**.

The same isolated `build` directory was reconfigured sequentially for the two additional modes, using `cmake -S . -B build -DWORMEDIT_BUILD_APP=OFF -DWORMEDIT_BUILD_EVENT_HARNESS=OFF`, then `-DWORMEDIT_BUILD_EVENT_HARNESS=ON`, with `cmake --build build --parallel 2` and `ctest --test-dir build -j 2 --output-on-failure` after each. Native mode was restored with `-DWORMEDIT_BUILD_APP=ON -DWORMEDIT_BUILD_EVENT_HARNESS=OFF`. These are exact option configurations, not claims that the separately located presets were cold-built. No real desktop window was opened.

The test-mutated tracked `.agent/osu-stepmania-publication/song.wormedit` was restored only after all runtime consumers exited and is excluded from the change. Final documentation checks and staged whitespace were rerun before commit. Heavy build ownership was released to the coordinator after the cached configuration and smoke checks completed.

The coordinator approved Lane C's separate source registration patch for `compose_clipboard_workflow.cpp` in app/frame-timing/event targets. C owns those exact hunks in its commit; they are absent here because its source is absent. Central index/architecture/resume edits remain coordinator-owned.
