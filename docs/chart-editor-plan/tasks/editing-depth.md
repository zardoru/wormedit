# Task cards: editing depth, performance and shared timing (stage 6)

[Back to the task-card index](../tasks.md).

Stage 6 from [later work](../stages/later.md). These are independent of each
other unless a card says otherwise. Apply the stale-plan check (rule 9) first.

## T-730 Plan granular timing links with copy-on-write overrides

- **Status:** deferred (was: ready; user request 2026-10-19, later work)
- **Outcome:** A reviewed design and bounded implementation cards specify timing sharing across compatible charts/format profiles, with independently linkable categories, copy-on-write local overrides and explicit restoration of links.
- **Read first:** `docs/chart-editor-plan/document-model.md`; `include/wormedit/core/document.h`; `include/wormedit/core/timing.h`; `include/wormedit/core/timing_lane_catalog.h`; `include/wormedit/formats/native_project.h`.
- **Files owned:** `docs/chart-editor-plan/document-model.md`, `docs/chart-editor-plan/tasks/editing-depth.md`, `docs/chart-editor-plan/tasks.md`; future implementation-card files only after recording their paths here.
- **Steps:**
  1. Start only when the user explicitly picks up T-730; preserve the immediate-work queue.
  2. Audit timing ownership, category-specific edits, exact position mapping and native/interchange persistence against the deferred requirements in the document-model plan.
  3. Resolve shared-source ownership and the design questions below with the owner before assigning implementation defaults.
  4. Write bounded cards for category link/override storage, undoable edit/relink operations, category-level UI, effective-timing consumers and persistence/export, each with exact ownership and verification.
- **Invariants:** BPM, SV/scroll, SPEEDS, Kiai, stops and delays each link independently. Linked categories follow their shared source; the first local write copies only the affected category's effective data into a detached local override. Other categories and charts stay unchanged. Relinking restores the source's current category and can discard only that category's local data, with explicit loss feedback/confirmation. Linking, overriding and relinking are undoable through the shared transaction. Copy-on-write describes behavior; physical storage remains a design choice.
- **Reject/failure behavior:** Cancelling a discard confirmation, an invalid/missing source, a cyclic link or an incompatible exact mapping must preserve local data and existing links. Unsupported target-format semantics must produce the existing conversion diagnostics without altering shared timing.
- **Verify:** For this planning record, `ctest --test-dir build/release -R '^documentation\.chart_editor_state$' --output-on-failure` and `git diff --check` pass. Future cards must specify GATE plus category-isolation, source-update propagation, first-write detachment, relink/cancel/Undo, native round-trip, cross-resolution mapping and resolved preview/playback/export checks.
- **Design questions for pickup:** Are sources named shared timing sets or designated charts, and how does the user edit shared data versus make a local override? Which additional timing categories participate beyond the explicitly requested BPM, SV/scroll, SPEEDS, Kiai, stops and delays (for example offsets, warps, anchors and measure layouts)? How do differing measure IDs, divisions and format capabilities map exactly? What are source-deletion and new-chart default-link policies? Is sharing confined to one project?
- **Out of scope:** Implementing or prioritizing timing links now (explicit user deferral, 2026-10-19); event-by-event override/merge behavior is not requested by the category-level copy-on-write model. Export-cut sections remain a separate deferred request in T-729.

## T-140 Mirror and shuffle lanes of a selection

- **Status:** done (2026-10-01; `include/wormedit/core/lane_transform.h`, `src/core/lane_transform.cpp`, `tests/core/lane_transform_tests.cpp`, five tests)
- **Implementation notes:** `apply_lane_mirror` reverses the playable lanes of each stage of the layout active at a note (scratch, free-zone and pedal lanes stay); `apply_lane_remap` takes an explicit bijection. Both go through one assignment routine that checks the layout at the note's head and tail, rejects any same-lane overlap, and commits through `commit_document_command`. A transform that moves nothing creates no history entry. The lane-move collision check in `interaction.cpp` is private, so the overlap check is a small local copy.
- **Outcome:** **Mirror** reverses the lane order of selected playable notes within their stage; **Remap…** applies an explicit lane permutation; both are one command each.
- **Read first:** `include/wormedit/core/interaction.h` (lane-move proposal), `src/core/interaction.cpp`, `include/wormedit/core/lane_profiles.h`.
- **Files owned:** new `include/wormedit/core/lane_transform.h`, new `src/core/lane_transform.cpp`, new `tests/core/lane_transform_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Core proposal reusing lane-move collision validation. 2. Tests: 7K mirror keeps scratch lane fixed; permutation with collision rejects; undo. 3. STOP before adding UI; UI is T-141.
- **Invariants:** rows, durations, sounds, labels, layers unchanged.
- **Reject/failure behavior:** notes whose target lane is missing reject all.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** random shuffle (needs a seed policy).

## T-141 Add a Transform menu for selections

- **Status:** done (2026-10-02; `src/sys/compose_selection_transform.cpp/.h`, `tests/sys/compose_selection_transform_tests.cpp`)
- **Implementation notes (2026-10-02, Claude):** a collapsible **Transform** section in the Compose inspector (not a popup) with Mirror lanes, Rotate lanes left/right (a one-place cyclic remap per stage, built by `rotate_lane_permutation`) and Shift earlier/later by one active-snap step. A custom remap editor is not part of this card. Disabled with an empty selection; core messages go to the status line.
- **Outcome:** Compose has a Transform popup with Mirror, Remap, and Shift rows (T-142) acting on the selection.
- **Read first:** `include/wormedit/core/lane_transform.h`, `src/ui/nuklear/compose/compose_toolbar.cpp`, `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose/compose_toolbar.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `src/ui/frontend.h`.
- **Steps:** standard pattern; one menu entry per available transform.
- **Invariants:** disabled with an empty selection.
- **Reject/failure behavior:** core messages in the status line.
- **Verify:** standard build/test.
- **Out of scope:** new transforms.

## T-142 Shift a selection or everything after the cursor by N rows

- **Status:** done (2026-10-01; `include/wormedit/core/time_shift.h`, `src/core/time_shift.cpp`, `tests/core/time_shift_tests.cpp`, five tests)
- **Implementation notes:** `apply_shift_selection` moves named notes and BGM events; `apply_shift_from_cursor` moves everything beat-anchored at or after the cursor (notes, BGM, BGA, lane-layout segments, beat bookmarks and sections) and, with `include_timing`, BPM, pause, warp, scroll, speed, kiai and timing-anchor events. Rows are the chart's own division grid counted across measures, so a shift across a measure-length change is exact; a target outside the materialized measures rejects (it never creates measures). Audio-time bookmarks and sections stay and are counted in `audio_anchored_skipped`. Charts with retained BMS control flow or projections, objects linked to BMSON sound-channel events, and SV recipes with `include_timing` reject with `unsupported_feature`.
- **Outcome:** a core command shifts selected notes/BGM (or every object after the cursor, optionally including timing events) by an exact signed row count.
- **Read first:** `include/wormedit/core/interaction.h`, `src/core/interaction.cpp`, `docs/chart-editor-plan/document-model.md` (positioning-domain paragraph).
- **Files owned:** new `include/wormedit/core/time_shift.h`, new `src/core/time_shift.cpp`, new `tests/core/time_shift_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Proposal with exact cross-measure arithmetic. 2. Tests: shift past chart start rejects; shift across a measure-length change is exact; timing-included variant moves BPM events too.
- **Invariants:** audio-time-anchored objects are not moved (report their count).
- **Reject/failure behavior:** any collision rejects.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** UI (in T-141).

## T-143 Save quantization presets and show a report

- **Status:** done
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Outcome:** named quantization settings (grid, tie policy, hold scope) persist in the native project; applying one shows a per-row report of moved and colliding objects.
- **Read first:** `include/wormedit/core/quantize.h`, `src/formats/native_project.cpp`, `src/ui/nuklear/compose/compose_inspector.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/ui/nuklear/compose/compose_inspector.cpp`, `src/sys/compose/actions/compose_actions.cpp`, `tests/formats/native_project_tests.cpp`; user-approved preset/report extensions (2026-10-03): `src/ui/frontend.h`, `src/sys/compose/views/compose_view.cpp`, `tests/sys/compose_quantize_preview_tests.cpp`, `src/sys/compose_quantize_preview.h/.cpp`, `src/sys/compose/views/compose_view.h`, `src/sys/compose/actions/compose_area_actions.cpp`; user-approved recovery/autosave extension (2026-10-03): `src/formats/native_project_storage.cpp`, `tests/formats/native_project_storage_tests.cpp`.
- **Implementation audit (2026-10-03):** project-owned presets and schema-3 migration, settings-aware dry runs, exact-request Apply, bounded 256-object reports, and Save/Delete are implemented. The application build and all 1,158 core tests passed; after rebase and final UI polish, 110 frontend-state tests and the complete 1,690-test suite passed again. Isolated saved-project comparisons confirm preset reload, duplicate-name rejection, exact two-note Apply, endpoint movement, undo and deletion while preserving unrelated fields. The final report is readable at 1280×720 with ASCII arrows and separate absolute-row lines. The earlier real-window check also exposed an autosave compatibility gap: `src/formats/native_project_storage.cpp` accepts only schemas 1 and 2, while preset documents use schema 3; its tests explicitly construct schema-2 documents. The user approved this storage/test extension on 2026-10-03; it is now implemented and verified.
- **Completion (2026-10-03):** Recovery envelopes now accept schema 3 alongside legacy schemas 1 and 2. Storage tests use the current document schema; new regressions preserve non-default presets through autosave/recovery and reject unsupported envelope versions. The preset autosave regression was observed failing before the fix. Application build and full 1,698/1,698 tests passed; configured clang-tidy reports no new-function violations (existing dependency/test warnings remain). Logs: `/tmp/wormedit-schema3-build.log`, `/tmp/wormedit-schema3-ctest.log`, `/tmp/wormedit-schema3-tidy.log`.
- **Steps:** Schema-3 recovery/autosave extension approved by the user (2026-10-03). 1. Add preset list to the song document (schema migration via T-122). 2. Preset picker + Save/Delete. 3. Report list (bounded 256 rows).
- **Invariants:** older projects load with an empty list.
- **Reject/failure behavior:** duplicate preset names reject.
- **Verify:** standard build/test.
- **Out of scope:** global presets outside the project.

## T-144 Search notes by label

- **Status:** done (2026-10-02; `src/sys/compose_label_search.cpp/.h`, `tests/sys/compose_label_search_tests.cpp`)
- **Implementation notes (2026-10-02, Claude):** inspector **Find label** field with Select (selects every match and jumps to the earliest), Previous and Next (move the cursor to the match before/after it, wrapping). No match gives a status message. Search text travels in `compose_note_label`.
- **Implementation notes (2026-10-01):** step 1 is done: `find_notes_by_label(chart, text)` in `include/wormedit/core/note_label_editor.h` (ASCII case-insensitive, chart order, empty text matches nothing) with a test in `tests/core/note_label_editor_tests.cpp`. Step 2 (field, Next/Previous, status message) remains and touches the compose files.
- **Outcome:** a search field selects every note whose label contains the text (case-insensitive) and jumps to the first; Next/Previous cycle.
- **Read first:** `include/wormedit/core/note_label_editor.h`, `src/ui/nuklear/compose/compose_inspector.cpp` (note label field), `src/sys/compose/actions/compose_actions.cpp`.
- **Files owned:** `src/sys/compose/actions/compose_actions.cpp`, `src/sys/compose/views/compose_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose/compose_inspector.cpp`, `tests/core/note_label_editor_tests.cpp`.
- **Steps:** 1. Core helper `find_notes_by_label(chart, text) -> ids` with tests. 2. Wire field and buttons.
- **Invariants:** no document mutation.
- **Reject/failure behavior:** no match → status message.
- **Verify:** standard build/test.
- **Out of scope:** regex.

## T-145 Add large-document performance benchmarks

- **Status:** done (2026-10-02; opt-in Release benchmark in `tests/core/performance_budget_tests.cpp`)
- **Outcome:** a benchmark test builds a synthetic 50,000-note, 400-measure chart with a 10-minute generated audio source and asserts budgets for scene composition, hit-test index build, timing index build, and waveform pyramid completion.
- **Read first:** `docs/references/ui-backend-boundary.md` (performance paragraph), `tests/core/editor_scene_tests.cpp`, `tests/core/audio_waveform_tests.cpp`.
- **Files owned:** new `tests/core/performance_budget_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Generate data in-test (no fixture files). 2. Time each phase with `std::chrono::steady_clock`; budgets start at 4× the measured Release value and are listed in the test. 3. Tag `[.performance]` so it is opt-in.
- **Invariants:** tests only.
- **Reject/failure behavior:** a missed budget becomes a new card.
- **Verify:** `cmake -S . -B build/performance -G Ninja -DCMAKE_BUILD_TYPE=Release -DWORMEDIT_BUILD_APP=OFF -DWORMEDIT_ENABLE_LUA=ON -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5`; `cmake --build build/performance --target wormedit_performance_tests -j 4`; `build/performance/tests/wormedit_performance_tests '[.performance]'` (93 assertions, all pass). The separate target is intentionally not registered with CTest; ordinary suites do not run the benchmark.
- **Out of scope:** optimizations; scheduler priorities/work stealing (only if this card shows need).

Implementation evidence: 400 measures, 125 distinct notes per measure, eight lanes and one 160 BPM event per measure (600 seconds total). The viewport shows four measures in the middle of the chart. A generated 44.1 kHz stereo square wave supplies 600 seconds of PCM without fixtures or filesystem writes. Each phase takes the maximum of five runs; fresh waveform caches prevent cache-hit measurements. Setup and result assertions are outside the timed intervals; scene-index preparation is outside the scene-composition budget. Correctness checks cover note counts, exact duration, visible geometry, generated status and complete pyramid coverage. PCM setup temporarily holds about 404 MiB in two copies.

Release reference (2026-10-02, GCC 14.2, AMD EPYC 9V74 cloud VM, five vCPUs, no concurrent build): hit-test index 6.54204 ms → 26.16816 ms budget; timing index 0.145229 ms → 0.580916 ms; scene composition 0.099119 ms → 0.396476 ms; waveform pyramid 126.289 ms → 505.156 ms. These are machine-specific CPU regression budgets, not UI, GPU, codec throughput or physical-device validation. No production optimization is included.

## T-146 Report progress and allow cancel for chart import

- **Status:** Done (2026-10-18)
- **Supporting scope (coordinator, 2026-10-18):** The files below extend the historical main/status slice to the current application owner, shared advisory counters, C++ UI controls and regression fixtures. Stale check confirmed synchronous opening; parser calls remain non-interruptible, with cancellation checked between reads, after parsing and before publication.
- **Scope approval (user, 2026-10-03):** All supporting-file ownership extensions documented in this card’s audit and approval step are approved, including their integration/test linkage. This extends **Files owned** only for the existing outcome and supersedes the historical pending-approval statements below.
- **Stale-plan audit (2026-10-03):** Project opening is synchronous (`src/sys/project/project_open.cpp:427` and `src/sys/app/main.cpp:2381`); the existing Cancel only dismisses BMS encoding selection. Worker and frontend changes fit their owned files, but the owned main slice permits only status wiring. Actual progress/cancel needs job submission, persistent per-open state, cancel dispatch, frame polling, and success-only document publication in main/application services. Do not hide lifecycle/publication changes behind a global callback to evade that qualifier.
- **Preparation checkpoint (T-684, 2026-10-18):** `application.cpp` now separates `open_project_session` (synchronous read), `finish_project_open` (encoding retry/error completion), and `publish_opened_project` (successful UI-thread session replacement and feedback). Put current-generation/cancellation admission before completion when adding asynchronous opening; no job or progress UI was added here. Historical main.cpp references below are superseded by these application methods.
- **Outcome:** opening a large chart shows a progress bar and a Cancel button; cancel leaves the previous document open.
- **Read first:** `src/sys/project/project_open.cpp`, `include/wormedit/core/async_job.h`, `docs/async-task-scheduling.md`.
- **Files owned:** `include/wormedit/core/job_progress.h`, `src/ui/frontend/file_jobs.h`, `src/ui/frontend/workspace.h`, `src/ui/frontend/actions.h`, `src/sys/app/application.cpp`, `src/sys/project/project_open.h`, `src/sys/project/project_open.cpp`, `src/ui/nuklear/frontend.cpp`, `tests/sys/project_open_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `docs/architecture.md`, `docs/async-task-scheduling.md`, `memory/chart-editor-status/resume.md`, this card.
- **Steps:** 0. Scope extension approved by the user (2026-10-03). 1. Run parse off the frame thread with a stop token and coarse progress (bytes read / total). 2. Publish only on success for the current generation. 3. Test that a cancelled open publishes nothing.
- **Invariants:** current document untouched until publication.
- **Reject/failure behavior:** errors shown; previous document kept.
- **Verify:** `scripts/gate.sh --jobs 4`; `cmake --preset linux-clang`; `scripts/gate.sh build-clang --jobs 4`; `git diff --name-only -z -- '*.cpp' | xargs -0 clang-tidy -p build`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `build/tests/wormedit_core_tests '[project-open]'`; `build/tests/wormedit_frontend_event_harness_tests '[file-jobs]'`; `scripts/smoke-compose-virtual-x.sh /tmp/t146-t147-compose.png compose`, inspect PNG. The focused binary/tag command replaces the stale `project_open` CTest name filter.
- **Out of scope:** export/validation progress (T-147).

- **Implementation (2026-10-18):** `project_open_runtime_t` uses the shared async shell with owned path/encoding/progress inputs. The worker reads bounded chunks, parses and validates off the frame thread. Cancellation stays busy through collection and rejects even a successful payload collected after Cancel. The UI admits only the captured document stamp/session, then retains encoding retry, imported identity, recent paths and warnings at the existing publication boundary. A detached unique result transfers its document without a UI-thread copy; autosave ticks pause while opening. The previous document survives cancellation and rejection.
- **Focused evidence:** `[project-open]` **1,128 assertions / 49 cases**, `/tmp/t146-final-open.log`; shared file-job event tests **18 assertions / 2 cases**, `/tmp/t146-t147-final-ui.log`. Covers cancellation, retry, byte progress, captured identity and actual Cancel emission on loaded/empty workspaces. Individual parser calls finish before cancellation is collected.
- **Final batch verification (2026-10-18):** GCC GATE `/tmp/t146-t147-reviewed-gcc-gate.log` (`/tmp/wormedit-gate.XGVK2l`) and Clang GATE `/tmp/t146-t147-reviewed-clang-gate.log` (`/tmp/wormedit-gate.1fK2T6`) each report `100% tests passed out of 4158` and `gate: passed`; build, whitespace and all 16 affected translation-unit complexity comparisons pass. Configured clang-tidy exits 0 (`/tmp/t146-t147-clang-tidy.log`, with final application/copy checks in `/tmp/t146-final-application-tidy.log` and `/tmp/t147-final-copy-tidy.log`); baseline allowances and ceremony maxima are unchanged. Isolated Xvfb Compose capture `/tmp/t146-t147-compose.png` inspected; job controls are verified through the headless production frontend emitter. Code-style checklist reviewed: owned requests/state, shared async shell, one filename calculation across copy/discard/planning, existing parsers, C++ view/action surface, no temporary adapters or transaction changes.

## T-147 Report progress and allow cancel for export

- **Status:** Done (2026-10-18)
- **Supporting scope (coordinator, 2026-10-18):** The files below extend the UI-only historical scope to conversion execution controls, atomic pending-asset planning/copying, runtime progress and preview cancellation. The resource-copy request owns its existing destination filename calculation. Artifact writers and filesystem calls remain non-preemptible; completed directory exchange retains its publication receipt.
- **Preparation checkpoint (T-685, 2026-10-18):** `output_group_export_t` in `src/sys/resource_job_state.h/.cpp` owns admission, snapshot capture, task submission and terminal polling; cancellation retains busy state and a domain receipt until collection. `src/sys/output_group_export.h/.cpp` owns shared request policy and the worker. Preserve that receipt: the scheduler replaces task outcomes on cancellation even when files were published. Artifact-level stop checks and progress/cancel UI remain undone.
- **Outcome:** Export and Preview export (T-110) show progress per artifact and can be cancelled without leaving partial output.
- **Read first:** `src/sys/output_group_export.h/.cpp` (plan and export worker), `src/sys/resource_job_state.h/.cpp` (runtime), `include/wormedit/formats/conversion_publication.h`.
- **Files owned:** `include/wormedit/core/job_progress.h`, `include/wormedit/core/resource_file_copy.h`, `src/core/resource_file_copy.cpp`, `include/wormedit/formats/conversion.h`, `src/formats/conversion.cpp`, `include/wormedit/formats/conversion_publication.h`, `src/formats/conversion_publication.cpp`, `src/formats/conversion_publication_storage.cpp`, `src/sys/project/output_group_export.h`, `src/sys/project/output_group_export.cpp`, `src/sys/resources/resource_job_state.h`, `src/sys/resources/resource_job_state.cpp`, `src/sys/project/project_view.h`, `src/sys/project/project_view.cpp`, `src/sys/app/application.cpp`, `src/ui/frontend/file_jobs.h`, `src/ui/nuklear/frontend.cpp`, `tests/formats/conversion_tests.cpp`, `tests/formats/conversion_publication_tests.cpp`, `tests/sys/resource_notification_tests.cpp`, `tests/sys/project_view_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`, `docs/architecture.md`, `docs/async-task-scheduling.md`, this card.
- **Steps:** 1. Thread a stop token through conversion and publication (check between artifacts). 2. UI progress/cancel.
- **Invariants:** publication stays atomic; cancel before swap leaves the destination unchanged.
- **Reject/failure behavior:** cancel after swap reports "completed".
- **Verify:** `scripts/gate.sh --jobs 4`; `cmake --preset linux-clang`; `scripts/gate.sh build-clang --jobs 4`; `git diff --name-only -z -- '*.cpp' | xargs -0 clang-tidy -p build`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `build/tests/wormedit_core_tests '[publication],[conversion-progress],[project-view]'`; `build/tests/wormedit_frame_projection_arena_tests '[resource-notification]'`; `build/tests/wormedit_frontend_event_harness_tests '[file-jobs]'`.
- **Out of scope:** validation progress; the full artifact/loss preview viewer remains T-110. This card adds progress/cancellation to the existing conversion preview.
- **Implementation (2026-10-18):** Conversion shares stop/progress controls across export and preview, checks between serializers/reimports and clears rejected artifacts. Publication checks between existing files, pending copies and generated outputs, and immediately before exchange. Progress changes from preparation to publication counters. Export retains its existing completion receipt through cancellation; preview discards results collected after Cancel. Shared UI controls retain busy/cancelling feedback, disable repeat Cancel and reserve pointer input through the existing modeless-window owner using the same bounds as rendering.
- **Recorded behavior difference:** Automatic osu!/StepMania music copying formerly created destination files before conversion/publication. It now plans the same `assets/<ID>-<filename>` mapping and copies into private atomic staging; failure/cancellation leaves the destination unchanged. Existing identical copies and explicit pre-staged resources keep their previous handling.
- **Focused evidence:** Publication/conversion/preview **300 assertions / 47 cases**, `/tmp/t147-final-core.log`; runtime/receipt/music **178 assertions / 15 cases**, `/tmp/t147-final-runtime.log`; shared frontend controls **18 assertions / 2 cases**, `/tmp/t146-t147-final-ui.log`. Cancellation after the first staged artifact and before exchange preserves the old destination and removes private staging; automatic music cancellation leaves no new destination, while successful publication contains chart and audio. Controlled workers retain after-publication receipts and reject busy admission. Writers and filesystem operations finish their current call before stopping.
- **Final batch verification (2026-10-18):** GCC GATE `/tmp/t146-t147-reviewed-gcc-gate.log` (`/tmp/wormedit-gate.XGVK2l`) and Clang GATE `/tmp/t146-t147-reviewed-clang-gate.log` (`/tmp/wormedit-gate.1fK2T6`) each report `100% tests passed out of 4158` and `gate: passed`; build, whitespace and all 16 affected translation-unit complexity comparisons pass. Configured clang-tidy exits 0 (`/tmp/t146-t147-clang-tidy.log`, with final application/copy checks in `/tmp/t146-final-application-tidy.log` and `/tmp/t147-final-copy-tidy.log`); baseline allowances and ceremony maxima are unchanged. Isolated Xvfb Compose capture `/tmp/t146-t147-compose.png` inspected; job controls are verified through the headless production frontend emitter. Code-style checklist reviewed: owned requests/state, shared async shell, one filename calculation across copy/discard/planning, existing parsers, C++ view/action surface, no temporary adapters or transaction changes.
