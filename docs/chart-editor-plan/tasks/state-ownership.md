# State ownership and lifetime consolidation

[Back to executable task cards](../tasks.md). Source of truth: [state ownership audit](../../audits/state-ownership-2026-10-12.md), including its Decision log D1–D19, findings, Pattern catalog, Necessary complexity and Rejected or downgraded sections. Owner decisions override recommendations elsewhere in the audit.

## Execution

Cards run in coordinated parallel lanes. Wave 1: **wyrm T-601–T-605**; **drake T-607–T-608**; **wyvern T-625 (core/formats/audio/visual scope), then T-613**; **hydra T-622, then T-628 (src/ui scope)**. Later waves are scheduled by Claude. Ready means available for scheduling; dependencies and explicit STOP steps still apply.

Read `AGENTS.md` and `docs/chart-editor-plan/tasks.md` before taking a card. Work only in the assigned worktree on its existing branch; never switch branches, push or rewrite commits. Commit each card or numbered large-card part separately. Implementers put stale-plan findings, behavior-based search patterns, behavior differences, changed call sites and actual gate tails in commit bodies. Claude records evidence on the cards when merging and owns plan/status updates.

Files owned below are primary exact paths (new owner/helper/test files may be created). Broad migrations require an exact caller inventory and coordinator approval/recording of supporting-file extensions before edits; coordinate overlapping ownership before proceeding. This does not override card exclusions or STOP. T-616/T-620/T-621 in particular must obtain their module/move path inventory before edits. Do not edit other lanes' files concurrently. All A claims must be re-grepped and confirmed against the executing tree, and stale claims recorded rather than implemented.

Behavior is preserved except explicit owner decisions. Every approved behavior change requires a regression that fails before the change; record all other differences and preserve them. No new per-frame/per-event heap allocation, std::function, virtual dispatch or large by-value copies. Named functions/members stay at cognitive complexity ≤25; no branch-hiding lambdas or NOLINT. Type construction and basic queries live on the type; aggregates gain static factories, never user-declared constructors. Yes/no queries use is_/has_/can_. Keep WormUi* flat with existing fields/capacities and frame-owned pointer lifetimes. D6 — No: the Nuklear UI state lifetime split is out of scope for this batch.

Keep the audit's necessary complexity: ordered explicit cancellation/join/cleanup/output close, notifications last and probe scheduler after resident loader; generation bump first; stack frame projections and local tap timestamp/viewport delta; project-owned chart-keyed maps; decoder feature blocks; captured-state, cache/workspace and per-batch-item stamps; staged scratch histories/Undo merging and distinct plan/parse/query/storage/dispatch semantics. Do not pursue rejected claims or merge boundary calls with different triggers.

Each implementation card runs the full Verify gate. After T-622 lands, add `cmake --preset linux-clang`, `cmake --build --preset linux-clang` and `ctest --preset linux-clang --output-on-failure` to each gate. libc++ is gated only after the owner installs it; record configured/not run until then. No Xvfb in implementer sandboxes. A failing gate is fixed within authorized scope or stops the card and is reported.

## T-601 Group command state by lifetime

- **Status:** Ready
- **Findings closed:** LT1, LT2, LT3, LT5, OW3a (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: fresh-object resets, base slicing, survivor assignments and qualified accesses to all ten groups. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Lifetime-group `application_command_state_t`: `services` member (no inheritance), `preferences` (exactly the 11 LT1 fields), `project` (everything that resets today). The reset becomes `project = {}`. Plus D1.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/compose_view.h`, `src/sys/application_command_dispatch.cpp`, `src/sys/main.cpp`, `src/ui/nuklear/compose_state.h`.
- **Files owned:** `include/wormedit/core/preferences.h`, `src/sys/compose_view.h`, `src/sys/application_command_dispatch.cpp`, `src/sys/main.cpp`, `src/ui/nuklear/compose_state.h`, `tests/sys/application_commands_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyrm: first, before T-602–T-605 and T-615.
- **Decisions applied:**
  - D1 — Fix: T-601 keeps `scratch_on_right` derived from the display preferences and outside the project reset. Add a regression test that fails today.
  - D3 — Keep resetting: Snap toggle, Y-lock, timing focus and mask, player preview visibility and skin, and dock width go in `project`.
  - D4 — Keep separate: The Sounds-panel preview-on-click and the editor audition-on-click stay distinct. T-601 and T-612 add a comment at both declarations saying so.
  - D6 — No: The Nuklear UI state split is out of scope.
- **Steps:**
  1. Inventory the exact 11 LT1 survivors and all reset fields; demonstrate D1 with a failing project-open/recovery regression.
  2. Replace services inheritance with a member; group the 11 survivors under preferences and all current reset fields under project; keep scratch_on_right derived outside the reset.
  3. Migrate qualified accesses atomically using coordinator-approved caller ownership; make reset project = {}; add D4 comments at both declarations.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not persist D3 fields or merge the two D4 settings; metronome ownership is T-615. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-602 Unify the project session boundary

- **Status:** Ready
- **Findings closed:** PB1–PB3 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: assignments of the eight PB1 fields, source_path clearing, pending import strings and generation/reset call sequences. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** One project boundary (the pending import paths go into one `pending_import_t` owner): a `project_identity_t` with the eight PB1 fields plus the external player source path, and one `begin_project_session(...)` used by replacement and recovery. Generation bump first. Plus D2.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/main.cpp`, `src/sys/application_dispatch_context.h`, `src/sys/project_open.h`, `src/sys/project_open.cpp`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application_dispatch_context.h`, `src/sys/project_open.h`, `src/sys/project_open.cpp`, `tests/sys/project_open_tests.cpp`, `tests/sys/application_commands_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyrm: after T-601; before T-603.
- **Decisions applied:**
  - D2 — Fix: In T-602, recovery clears the external player's source path through the single project boundary. Add a regression test.
- **Steps:**
  1. Inventory both boundaries and pending audio/chart/BMS import paths; demonstrate the recovery source-path bug with a failing regression.
  2. Add project_identity_t and pending_import_t owners and one begin_project_session entry point.
  3. Route replacement and recovery through the boundary, bumping generation first and clearing the external player source path in both.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not change chart-switch resets or captured-work generation rules. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-603 Give the application an owner and ordered shutdown

- **Status:** Ready
- **Findings closed:** OW1, OW2, OW4, SD1–SD4, CB1 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: local lifetimes, stored capture lists, duplicate aggregate initializers and cancel/join/cleanup/close calls. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** `application_t` owner in `src/sys/application.h/.cpp`: subsystems in dependency order, `start()`, an idempotent `shutdown()` also called from the destructor, RAII platform and crash-marker guard. Callbacks capture `this` or a subsystem. One context factory replaces the two literals. `main()` shrinks to about 30 lines. Plus D5.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_startup.h`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_startup.h`, `src/sys/application_startup.cpp`, `src/sys/application_dispatch_context.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/sys/application_lifetime_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyrm: after T-602; before T-604/T-605, T-614, T-615 and T-619.
- **Decisions applied:**
  - D5 — Join first: In T-603, `shutdown()` joins every worker (autosave scheduler, image and video jobs, waveform scheduler) before platform and SDL teardown.
- **Steps:**
  1. Confirm OW4/SD3/SD4 leads; inventory subsystem dependencies, stored callbacks and every worker; add a regression for D5 ordering.
  2. Create a noncopyable/nonmovable application_t with subsystems in dependency order, RAII platform/crash guards and start() for side effects.
  3. Move callbacks to this/subsystem captures and one temporary context factory; implement explicit idempotent shutdown also used by the destructor, joining all workers before SDL teardown.
  4. Reduce main to startup, construction, start, loop and shutdown; leave named phase extraction to T-604.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not remove explicit cancellation or retain the borrowed dispatch mirror as the final state (T-605). Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-604 Name frame phases and close confirmation

- **Status:** Ready
- **Findings closed:** OW5 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: frame timing markers, phase ordering and close-confirmation callback branches. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Frame loop as named `run_frame` phases, and the close-confirmation callback as a member function.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_frame_services.h`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_frame_services.h`, `src/sys/application_frame_services.cpp`, `tests/sys/application_lifetime_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyrm: after T-603; before T-605.
- **Steps:**
  1. Confirm OW5 against the current loop and record reads/writes of its ten phases.
  2. Extract named run_frame phase members without moving event/action/reconcile/render order.
  3. Replace the close-confirmation callback body with a named member and exercise accepted/cancelled close behavior.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-605 Narrow handlers to subsystem inputs

- **Status:** Ready
- **Findings closed:** CB2, CB3, OW3b, OW6 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: parameter lists with seven or more arguments, audio-chain bundles, auto& parameters, Context templates and context field usage. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Narrow handler inputs: remove `application_dispatch_context_t` and the frame-context inheritance; use subsystem references, subsystem member functions or small bundles; drop `_ref`; move `external_player_runtime_t` to its own header. Remove every `auto&` parameter and `template <typename Context>` in `main.cpp`. Record the before and after count of sys functions with seven or more parameters (76 today).
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_dispatch_context.h`.
- **Files owned:** `src/sys/main.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/application_dispatch_context.h`, `src/sys/application_command_dispatch.h`, `src/sys/application_command_dispatch.cpp`, `src/sys/application_frame_services.h`, `src/sys/application_frame_services.cpp`, `src/sys/external_player_runtime.h`, `src/sys/compose_audition_requests.h`, `src/sys/compose_audition_requests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyrm: after T-604; before T-609, T-614, T-618 and the owner migrations.
- **Steps:**
  1. Record the current count of sys functions taking seven or more parameters (audit baseline 76) and confirm CB2 usage subsets.
  2. Replace mirrors and frame inheritance with subsystem references/member calls or small stack bundles; move external_player_runtime_t into its own header.
  3. Remove application_dispatch_context_t, _ref suffixes and every auto& parameter/Context template in main.cpp; record the after count and migrate callers atomically.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-606 Reset UI sessions at transitions

- **Status:** Ready
- **Findings closed:** UR1–UR3 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: four-reset sequences, picker lock/generation increments, project_loaded polls and scene key field comparisons. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Reset entry points: one Nuklear session-reset helper; the sound-import picker/session split, with the reset moved from a per-frame poll to the actual transition; a single scene-cache key check and invalidation.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/sound_import_runtime.h`, `src/ui/nuklear/sound_import_runtime.cpp`, `src/sys/compose_view.h`.
- **Files owned:** `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/sound_import_runtime.h`, `src/ui/nuklear/sound_import_runtime.cpp`, `src/sys/compose_view.h`, `src/sys/compose_view.cpp`, `src/sys/compose_input.cpp`, `src/sys/compose_minimap_navigation.cpp`, `tests/ui/sound_import_state_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Later wave, coordinated with application and UI owners; before T-621.
- **Decisions applied:**
  - D6 — No: The Nuklear UI state split is out of scope.
- **Steps:**
  1. Confirm UR3 keys and inventory the three reset sequences and sound-import lifetimes.
  2. Extract one Nuklear session reset; separate app picker, project batches and request pending state, resetting on actual transition rather than every unloaded frame.
  3. Use one scene-key check and invalidation entry point; test transition/reset and stale-key behavior.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No D6 UI state lifetime split; leave unrelated seek-record/beat-fit boundaries alone. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-607 Share the core document transaction

- **Status:** Done
- **Completion (2026-10-13):** Commit `216da16` (rebased). `document_command_transaction_t` in `command.h`: a public aggregate built with `::from(history)`; the stale check stays at apply entry points until T-609 stamps; `copy_document()` copies lazily once, `adopt_document()` hands over staged snapshots without a second copy; `reject`/`rejection`/`commit`. `command_result_with_t<Payload>` added. Initial editors migrated.
- **Gate evidence (Claude, on main with FFmpeg on):** `100% tests passed out of 3542`; event harness `500/500`; documentation `1/1` (after T-608).
- **Findings closed:** CT1–CT4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: revision result initializers, stale checks, whole-document copies, commit calls and command-plus-payload field sets. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** `document_command_transaction_t` and `command_result_with_t<Payload>` in `command.h`, with a lazy single copy. Migrate the core editors for BPM, measures, markers, metadata, chart and project, and lanes.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `include/wormedit/core/bpm_editor.h`, `src/core/bpm_editor.cpp`.
- **Files owned:** `include/wormedit/core/command.h`, `src/core/command.cpp`, `include/wormedit/core/bpm_editor.h`, `src/core/bpm_editor.cpp`, `src/core/measure_editor.cpp`, `src/core/marker_editor.cpp`, `src/core/metadata_editor.cpp`, `src/core/chart_editor.cpp`, `src/core/chart_import.cpp`, `src/core/workspace.cpp`, `src/core/lane_layout_editor.cpp`, `src/core/lane_migration.cpp`, `src/core/lane_profile_switch.cpp`, `tests/core/command_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 drake: before T-608, T-610 and T-624.
- **Steps:**
  1. Confirm CT2–CT4 and inventory no-change, pre-copy lookup, batch and prepared-document variants.
  2. Introduce document_command_transaction_t with one lazy copy and command_result_with_t<Payload>; test rejection before copy, no-change and commit/Undo behavior.
  3. Migrate BPM, measures, markers, metadata, chart/project and lane editors; retain domain diagnostics and staged orchestration.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-608 Migrate remaining document commands

- **Status:** Done (sys publication call sites left for a later wave)
- **Completion (2026-10-13):** Commits `dbf4654` (resources, sound imports, sound groups), `8203ded` (BMSON channel edits and rejection builders), `86fb597` (timing and SV), `b6e4949` (notes, BGM, gestures, clipboard), `6adaba8` (exactly the nine payload results on `command_result_with_t`; remaining editors), rebased. Local `reject`/`rejected` builders and inline `{.applied = false}` rejections removed; `sound_import`'s former `applied = false` reset was proven unobservable (no path sets `applied` before rejecting). One fewer document copy in tap-timing publication. Forced call-site edits in sys (`application_command_dispatch`, `chart_import_session`, `compose_actions`, `compose_input`, `compose_note_to_bgm`, `main`, `metadata_actions`, `project_view`). Later sys wave: BGA import publication `compose_bga_actions.cpp:88`; quantize-preset publication `compose_actions.cpp:1956/1975/2006`. Rebase onto T-625 resolved three conflicts (helper renames vs transaction rewrites).
- **Gate evidence (Claude, on main with FFmpeg on):** build exit 0; `100% tests passed out of 3542`; event harness `100% tests passed out of 500`; documentation `100% tests passed out of 1`.
- **Findings closed:** CT1–CT3, AP7 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: applied/revision/diagnostics result shapes, rejected builders under any name and inline false-applied returns. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Migrate the remaining commands: resources, BMSON, SV, timing lanes, notes and BGM. Staged edits remain orchestration around the transaction. Collapse the nine payload result types. Remove the `rejected()` builders and the inline `{.applied = false}` rejections (AP7).
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/core/resource_assignment.cpp`, `src/core/sound_groups.cpp`.
- **Files owned:** `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/core/resource_assignment.cpp`, `src/core/sound_groups.cpp`, `src/core/sound_import.cpp`, `src/core/sound_slice.cpp`, `src/core/bmson_sound_channel_editor.cpp`, `src/core/sv_recipe.cpp`, `src/core/timing_lane_controller.cpp`, `src/core/timing_anchor_editor.cpp`, `src/core/scroll_speed_editor.cpp`, `src/core/pause_warp_editor.cpp`, `src/core/note_label_editor.cpp`, `src/core/note_to_bgm.cpp`, `src/core/quantize.cpp`, `src/core/time_shift.cpp`, `src/core/bms_branch_editor.cpp`, `tests/core/command_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 drake: after T-607; before T-610, T-620 and T-624.
- **Steps:**
  1. Inventory remaining commands and the nine CT3 payload-result shapes, including differing reject applied rules.
  2. Migrate resources, BMSON, SV, timing lanes, notes and BGM through the lazy transaction; preserve scratch histories and undo merging.
  3. Collapse only command-plus-payload results; delete competing rejection builders and inline rejected results after atomic caller migration.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-609 Validate stamps once at each boundary

- **Status:** Ready
- **Findings closed:** RV1–RV5 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: project/chart/revision field sets, comparisons and live-revision command arguments along complete dispatch paths. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Stamps and boundaries: `document_stamp_t`/`chart_stamp_t` with `is_current`, one boundary per entry point (per batch item for compose input), removal of redundant handler rechecks, no stamp for synchronous core calls, stamps kept for state captured earlier. Plus D8. Runs after T-605.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`.
- **Files owned:** `include/wormedit/core/command.h`, `src/core/command.cpp`, `src/sys/compose_actions.cpp`, `src/sys/compose_input.cpp`, `src/sys/metadata_actions.cpp`, `src/sys/application_commands.cpp`, `src/sys/application_command_dispatch.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/sys/metadata_notification_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-605; before T-610 and T-618.
- **Decisions applied:**
  - D8 — Unify: In T-609, one `stale_action_message(label)` produces every message; tests that assert the old strings are updated. A missing target stays a separate error.
- **Steps:**
  1. Classify comparisons as boundary, redundant, self-comparison or legitimate later; confirm RV2/RV4/RV5 rather than trusting counts.
  2. Add document_stamp_t/chart_stamp_t with is_current and one boundary per entry point, per item in Compose batches.
  3. Remove only proven synchronous duplicate checks/stamps; unify stale_action_message(label) and update text assertions while retaining distinct missing-target errors.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-610 Share stamped proposal lifecycles

- **Status:** Ready
- **Findings closed:** SP1–SP3 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: stamp/diagnostics/valid/status fields and begin/revise/apply/cancel call sequences, including drafts and drags. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Staged proposals: a shared stamp and diagnostics in every proposal, the redundant `valid` flags removed, the verbs normalized, and each `apply_` running through the transaction with the proposal's stamp. Comes after T-607/T-608 and T-609.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/timing_lane_controller.h`, `include/wormedit/core/sv_recipe.h`, `include/wormedit/core/lane_migration.h`, `include/wormedit/core/tap_timing_apply.h`.
- **Files owned:** `include/wormedit/core/timing_lane_controller.h`, `include/wormedit/core/sv_recipe.h`, `include/wormedit/core/lane_migration.h`, `include/wormedit/core/tap_timing_apply.h`, `include/wormedit/core/chart_clipboard.h`, `include/wormedit/core/channel_map_editor.h`, `src/core/timing_lane_controller.cpp`, `src/core/sv_recipe.cpp`, `src/core/lane_migration.cpp`, `src/core/tap_timing_apply.cpp`, `src/core/chart_clipboard.cpp`, `src/core/channel_map_editor.cpp`, `src/sys/application_timing_stage.cpp`, `src/sys/compose_view.h`, `src/sys/recipe_form.cpp`, `tests/core/timing_lane_controller_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-607/T-608 and T-609; before T-624.
- **Steps:**
  1. Inventory every staged proposal/draft lifecycle and distinguish genuine status machines from redundant validity flags.
  2. Embed shared stamps and diagnostics, remove redundant valid flags and normalize lifecycle verbs.
  3. Route each apply through the transaction using its captured stamp, preserving preview IDs, scratch histories and undo merging.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not redesign core pointer gesture lifecycles; only shared stamps apply there. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-611 Drive frontend choices from core catalogs

- **Status:** Ready
- **Findings closed:** FT1–FT5 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: literal label/extension arrays, enum casts, index arithmetic, note-kind integers and selection-kind switches. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Core catalogs (also the catalog's P3/P7 items: tab names with D15, sound kind labels with D16, the note-kind table keyed by enum, step types and key ranges, extension tables with D17, `bms_line_ending_t`, and an exhaustive `editor_selection_object_kind_t`↔`timing_lane_kind_t` mapping with no `default`) (layouts, playback rates, encodings, format families, timing lanes) with ids sent through the view; actions carry ids, not list positions; static_asserts against enum counts. Plus D7 and D9.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `include/wormedit/core/command_registry.h`, `src/core/command_registry.cpp`.
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `include/wormedit/core/command_registry.h`, `src/core/command_registry.cpp`, `src/core/chart_clipboard.cpp`, `include/wormedit/audio/playback_rate.h`, `src/audio/playback_rate.cpp`, `src/sys/application_defaults.cpp`, `src/sys/main.cpp`, `src/sys/workspace_projection.cpp`, `src/sys/external_player.cpp`, `src/sys/chart_import_session.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/frontend.cpp`, `src/ui/nuklear/setup_panel.cpp`, `src/ui/nuklear/metadata_format_fields.cpp`, `src/ui/nuklear/sound_import_dialog.cpp`, `src/ui/nuklear/compose_toolbar.cpp`, `tests/sys/application_defaults_tests.cpp`, `tests/sys/external_player_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs before T-617; catalog consumers coordinated with T-612.
- **Decisions applied:**
  - D6 — No: The Nuklear UI state split is out of scope.
  - D7 — Everything core builds: In T-611, the New Project catalog offers every layout core can build: osu!mania 1–18K, the DP/double-scratch BMS profiles and the PMS profiles. Each entry has its label, profile factory and default output groups (StepMania groups only where a step type exists). Layouts wider than ten lanes start with no key bindings, as `make_osu_mania_profile` already does. Record this on the card.
  - D9 — No: In T-611, MIDI and Quaver are in the format catalog but marked not offered for output.
  - D15 — "Project" and "Audio Slices": In T-611, one tab table owns the label, id and enum. The labels are "Project" (not "Setup") and "Audio Slices" (no slash), used by both the tab strip and the commands. Command *ids* (`workspace.open_*`) keep their names.
  - D16 — "Keysound": In T-611, one sound-kind label table; the import dialog shows "Keysound".
  - D17 — Allow `.bml`: In T-611, the external player preview uses the shared BMS extension table, which includes `.bml`. Add a regression test.
- **Steps:**
  1. Confirm FT5/P3/P7 leads; inventory stable IDs, labels, offered flags and every duplicated catalog caller.
  2. Create core catalogs for layouts, rates, encodings, families, timing/note kinds, tabs, sound kinds, step types/key ranges and BMS line endings; use exhaustive mappings/static_asserts.
  3. Pass IDs and labels through the flat ABI and migrate callers; apply D7/D9/D15/D16/D17 with failing-before regressions for each approved behavior difference.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not offer MIDI/Quaver output or change command IDs; wider-than-ten layouts have no default key bindings. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-612 Apply construction and projection vocabulary

- **Status:** Ready
- **Findings closed:** NV1–NV3, TX1–TX4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: A–Z tests, tolower, +32 and a-minus-A arithmetic, lowered-string comparisons, return-only builders and read-only projections. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Note (2026-10-13):** The vocabulary and structure rules are already written in [code style](../decisions/code-style.md) (owner request). This card applies the renames and keeps that slice current; it does not re-add the table to `decisions.md`.
- **Outcome:** The vocabulary table in `decisions.md` plus the ~23 renames, done mechanically one module at a time. Also the ASCII text helpers (`ascii_lowercase`, `ascii_iequal`/`ascii_iless`, `lowercase_extension`): delete every copy, forwarder and inline lowercasing found by searching for what the helpers do, and rename `folded_name`. Plus D13: rename projections to `build_<x>_view`.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `docs/chart-editor-plan/decisions.md`, `src/core/text_util.h`, `src/core/document.cpp`, `src/core/sound_groups.cpp`.
- **Files owned:** `include/wormedit/core/preferences.h`, `docs/chart-editor-plan/decisions.md`, `src/core/text_util.h`, `src/core/document.cpp`, `src/core/sound_groups.cpp`, `src/core/sound_group_proposal.cpp`, `src/core/sound_import.cpp`, `src/core/natural_order.cpp`, `src/formats/slice_spans.cpp`, `src/sys/application_defaults.cpp`, `src/sys/project_open.cpp`, `src/sys/audio_project_import.cpp`, `src/sys/bga_list_projection.cpp`, `src/sys/compose_sound_actions.cpp`, `src/ui/nuklear/audio_panel.cpp`, `src/ui/nuklear/sound_import_state.cpp`, `src/ui/nuklear/compose_state.h`, `tests/core/natural_order_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs before T-616 and T-617; coordinate text helpers with T-625.
- **Decisions applied:**
  - D4 — Keep separate: The Sounds-panel preview-on-click and the editor audition-on-click stay distinct. T-601 and T-612 add a comment at both declarations saying so.
  - D6 — No: The Nuklear UI state split is out of scope.
  - D13 — A: In T-612, projections become `build_<x>_view`.
- **Steps:**
  1. Confirm NV2 borderline cases and distinguish side-effect operations, derived indexes and song-project noun uses.
  2. Add the vocabulary table and owner-approved type construction/query rule to decisions.md, reserving fold for Unicode and build_<x>_view for frontend views.
  3. Mechanically rename value builders and projections one module per step, including projection-only filenames and CMake paths.
  4. Add allocation-free ASCII comparisons/string_view helpers and lowercase_extension; delete every confirmed copy/forwarder/inline lowercase and rename folded_name to file_name_of; add D4 comments.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-613 Read audio metadata with FFmpeg

- **Status:** Done
- **Completion (2026-10-13):** Commit `d9f33d0`. libavformat reads `title`/`artist` (format and stream dictionaries; 1 MiB probe, 1 s analysis, RAII close, UTF-8-checked and trimmed) behind `WORMEDIT_HAS_FFMPEG`; without FFmpeg there is no prefill and no error, and the title falls back to the file stem. The ID3v2/ID3v1/Ogg parsers and their byte/UTF-16 helpers are gone. New options `WORMEDIT_ENABLE_FFMPEG` and `WORMEDIT_REQUIRE_FFMPEG`; `release` and `windows-cross-release` presets require FFmpeg (D11), dev/headless presets stay tolerant. Note: the prefill is compiled only when the video decoder is enabled too (true in both release presets). Owner follow-up: cross-build FFmpeg for Windows (`CMakePresets.json` windows-cross-release).
- **Gate evidence (Claude, on main with FFmpeg on):** build with FFmpeg found; `100% tests passed out of 3534` (includes the FFmpeg tag tests); event harness `500/500`; documentation `1/1`.
- **Findings closed:** D10, D11 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: ID3/Ogg byte parsing, text decoders, title/artist byte caps and FFmpeg capability/build definitions. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** D10: replace the hand-written tag reader with `libavformat` metadata behind the FFmpeg build flag (rename `WORMEDIT_HAS_VIDEO_DECODER` usage for this to a `WORMEDIT_HAS_FFMPEG` capability). Without FFmpeg, there is no prefill and no error. Make the tag tests conditional on FFmpeg. Add a dependency rule to `decisions.md`. Plus D11.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/audio_project_import.cpp`, `src/sys/audio_project_import.h`, `CMakeLists.txt`, `CMakePresets.json`.
- **Files owned:** `src/sys/audio_project_import.cpp`, `src/sys/audio_project_import.h`, `CMakeLists.txt`, `CMakePresets.json`, `docs/chart-editor-plan/decisions.md`, `tests/sys/audio_project_import_tests.cpp`, `tests/CMakeLists.txt`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyvern: after its core/formats/audio/visual part of T-625.
- **Decisions applied:**
  - D10 — (a) libavformat: T-613.
  - D11 — The ship preset requires FFmpeg: T-613 adds a release preset (Linux and Windows) that finds FFmpeg with `REQUIRED`; the dev, headless and test presets stay tolerant. Cross-building FFmpeg for Windows is a setup step for the owner: record it on the card, and do not attempt it in the sandbox.
- **Steps:**
  1. Inventory metadata behavior and byte limits; add conditional regression fixtures for the approved libavformat coverage change.
  2. Replace hand-written tags with avformat_open_input/find_stream_info and dictionary title/artist queries under WORMEDIT_HAS_FFMPEG; without FFmpeg return no prefill and no error.
  3. Add Linux/Windows release presets requiring FFmpeg while dev/headless/tests remain tolerant; document dependency rules and owner Windows cross-build setup.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No Windows FFmpeg cross-build in the sandbox, audio-decoder replacement or WAV writer redesign. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-614 Own project audio source reconciliation

- **Status:** Ready
- **Findings closed:** AS1–AS4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: document-plus-registry signatures, loose sync fields, reset callbacks and audio-phase/action-phase call order. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** `project_audio_sources_t` owns the registry and its sync state. Reconciliation becomes a member; consumers stop taking document and registry separately. Runs after T-603/T-605. Plus D12.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/project_audio_sources.h`, `src/sys/project_audio_sources.cpp`, `src/sys/audio_source_registration.h`, `src/sys/audio_source_registration.cpp`.
- **Files owned:** `src/sys/project_audio_sources.h`, `src/sys/project_audio_sources.cpp`, `src/sys/audio_source_registration.h`, `src/sys/audio_source_registration.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/chart_preview_state.h`, `src/sys/chart_preview_state.cpp`, `src/sys/compose_audition_requests.h`, `src/sys/compose_audition_requests.cpp`, `src/sys/bmson_waveform_projection.cpp`, `tests/sys/audio_source_registration_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-603/T-605; before T-615 and T-619. D12: test first; if reproduced, reconcile on demand (owner, 2026-10-13).
- **Decisions applied:**
  - D12 — Test first, then reconcile on demand (owner, 2026-10-13): if the same-frame window reproduces, `project_audio_sources_t` queries reconcile when the document revision has moved since the last reconcile (a revision compare per query; the reconcile itself only in the same-frame case), shipped with the regression test. No STOP.
- **Steps:**
  1. D12: add and run a same-frame asset-edit-then-audition test before changing reconciliation. If AS3 reproduces, implement on-demand reconciliation on revision mismatch and keep the test as its regression (D12, owner 2026-10-13); if it does not reproduce, record the test as coverage and change nothing.
  2. If the test does not reproduce, record why and create project_audio_sources_t owning registry and sync bookkeeping; keep decoded_source_registry_t document-free.
  3. Make reconciliation a member and replace callbacks with sibling calls; consumers take the owner and query with an explicit current document without storing invalidatable references.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion. D12 reproduction requires STOP without a fix.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not fix the same-frame stale window without a subsequent owner decision. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-615 Own metronome clicks and settings

- **Status:** Ready
- **Findings closed:** MT1–MT4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: sine/exponential click synthesis, binding vector plumbing and settings-to-bridge callbacks. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** `src/audio/metronome` click synthesis plus a `metronome_t` owner in `audio_runtime_t` (bindings, registration after a reset, settings as the single source of truth). Remove the `metronome_bindings` plumbing and the three metronome `std::function` services. Runs after T-601, T-603 and T-614.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/audio/metronome.h`, `src/audio/metronome.cpp`, `src/sys/metronome.h`, `src/sys/metronome.cpp`.
- **Files owned:** `include/wormedit/audio/metronome.h`, `src/audio/metronome.cpp`, `src/sys/metronome.h`, `src/sys/metronome.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/audio_source_registration.cpp`, `src/sys/chart_preview_state.h`, `src/sys/chart_preview_state.cpp`, `src/sys/compose_view.h`, `src/sys/transport_player_actions.cpp`, `tests/core/audio_preview_bridge_tests.cpp`, `tests/sys/timing_settings_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-601, T-603 and T-614.
- **Steps:**
  1. Inventory click waveform parameters and the four surviving metronome preferences; coordinate transfer from T-601.
  2. Extract pure audio click synthesis and a metronome_t owner under audio_runtime_t, registering bindings after each registry generation reset.
  3. Use settings as one source of truth; remove bindings plumbing and three stored std::function services while retaining timing scheduling and bridge mixing.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-616 Name yes/no queries consistently

- **Status:** Ready
- **Findings closed:** NV4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: bool-return bodies that query state rather than mutate, stale-match field comparisons and comparator use sites. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Yes/no checks: rename about 286 to `is_`/`has_`/`can_` (members too; comparators excluded); `*_matches` stale checks become `is_<x>_current`. Mechanical, one module per step. Runs after T-612.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `docs/chart-editor-plan/decisions.md`.
- **Files owned:** `docs/chart-editor-plan/decisions.md`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-612.
- **Steps:**
  1. Inventory yes/no checks across free functions and members; exclude successful actions and algorithm comparators.
  2. Rename is_/has_/can_ queries mechanically one module per step; stale *_matches checks become is_<x>_current.
  3. Migrate declarations, definitions and all consumers atomically within coordinator-approved ownership, recording before/after inventories.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-617 Move factories and row queries onto their types

- **Status:** Partly done (core; sys/UI callers and the compatibility forwarders go to wave 2)
- **Completion (2026-10-13):** Commits `5238880` (`document_history_t::from`) and `1680c3b` (27 more aggregate types on `from`/`with_defaults`, renamed through clangd; tests assert they stay aggregates). RW: `measure_index_t::entry_for`/`row_of`/`position_at` and `measure_lookup_t::from` replace the five audit map owners plus the quantize-geometry and note-to-BGM maps, only where maps already existed; the linear index gains no map and an empty lookup allocates nothing; SV chart-end anchors use an explicit measure policy. Metadata find-or-append is `format_group_for`; the BMSON builder accessor is `channel_for`. One-line `make_*` forwarders remain for sys/UI; their callers (file:line → new call) are in the lane's `*-sys-ui-followups.txt`, copied to `/tmp/wormedit-lanes/drake-factories-agent/`.
- **Gate evidence (Claude, on main with FFmpeg on):** build exit 0; `100% tests passed out of 3560`; event harness `503/503`; documentation `1/1`.
- **Findings closed:** NV5, NV6, RW1–RW5 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: single-result-type factories, find/else-emplace/reference returns, checked row sums, id-to-entry maps and inverse row conversion bodies. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Type-owned factories: each of the ~56 single-factory types gets `T::from(...)`/`T::with_defaults()` in place of its `make_` function, keeping aggregates aggregates; many-factory types stay in catalogs. Find-or-add accessors become `owner.<x>_for(key)` members (`stem_entry` → `cache.waveform.stem_for(channel)`). Runs after T-611 and T-612. Also RW: `measure_index_t::row_of`/`position_at`/`entry_for` members, one shared `measure_lookup_t::from(index)` replacing the five private maps (no new allocation), deletion of the sys copies; prove `absolute_row_of`'s inputs are within capacity or record the difference.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/snap.h`, `src/core/snap.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/snap.h`, `src/core/snap.cpp`, `src/core/interaction_internal.h`, `src/core/interaction_hit_test.cpp`, `src/core/marker_navigation.cpp`, `src/core/timing.cpp`, `src/core/time_shift.cpp`, `src/core/sv_recipe.cpp`, `src/sys/compose_bookmark_commands.h`, `src/sys/compose_bookmark_commands.cpp`, `src/sys/bookmark_chooser_projection.cpp`, `src/sys/bmson_waveform_projection.cpp`, `tests/core/document_tests.cpp`, `tests/core/snap_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-611 and T-612; before T-619, T-623 and T-626.
- **Steps:**
  1. Inventory single-factory aggregates and find-or-add accessors; confirm the five private maps and prove absolute_row_of inputs are in capacity or record/preserve its difference.
  2. Add T::from/with_defaults without user-declared aggregate constructors; keep multi-factory catalogs and use owner.<x>_for(key) accessors.
  3. Add measure_index_t row_of/position_at/entry_for and shared measure_lookup_t::from only where a map already exists; name chart-end policy and delete equivalent sys conversions.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No new maps at previously allocation-free sites; no unchecked-to-checked row behavior change without proof. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-618 Finish compose mutations and feedback once

- **Status:** Ready
- **Findings closed:** AP1–AP8 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: gesture guards, stamp checks, rebind/two-message failures, feedback field sets, hint-routing branches and severity switches. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Compose action outcomes: `compose_action_outcome_t` plus `require_no_gesture`, with `finish_compose_action` doing the rebind and the feedback once. Migrate the 49 compose action handlers, the compose input handlers and the `main.cpp` rebinds. Split `compose_marker_actions.cpp` by concept. Fold `area_action_context_t` and its helpers into it (AP4). Add `notification_sink_t::post_feedback` and `notification_sink_t::muted()`, then remove the seven copies of the hint routing and the optional sinks (AP5). Replace `bookmark_command_outcome_t`, `bookmark_rename_outcome_t`, `bms_block_outcome_t` and the sound/BGA feedback twins with the shared outcome (AP6), and add one `to_notification_severity` for the 12 hand-written mappings. Find copies by what they do, not by name (AP8). Runs after T-605 and T-609.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/compose_actions.h`, `src/sys/compose_actions.cpp`, `src/sys/compose_actions_internal.h`, `src/sys/compose_area_actions.h`.
- **Files owned:** `src/sys/compose_actions.h`, `src/sys/compose_actions.cpp`, `src/sys/compose_actions_internal.h`, `src/sys/compose_area_actions.h`, `src/sys/compose_area_actions.cpp`, `src/sys/compose_bga_actions.cpp`, `src/sys/compose_sound_actions.cpp`, `src/sys/compose_resource_actions.cpp`, `src/sys/compose_marker_actions.cpp`, `src/sys/compose_bookmark_commands.h`, `src/sys/compose_bookmark_commands.cpp`, `src/sys/bookmark_rename_commands.h`, `src/sys/bookmark_rename_commands.cpp`, `src/sys/compose_input.cpp`, `src/sys/main.cpp`, `src/sys/application.cpp`, `include/wormedit/core/notifications.h`, `src/core/notifications.cpp`, `tests/sys/compose_action_notification_tests.cpp`, `tests/core/notifications_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-605 and T-609; before T-620.
- **Steps:**
  1. Inventory all 49 handlers plus input/main rebinds, area repacking and outcome/result twins; confirm P5 leads by field sets/body equivalence.
  2. Add shared outcome, require_no_gesture and finish_compose_action; migrate all handlers, folding area context/helpers and preserving two-message rebind failure.
  3. Split marker actions into bookmark, measure/meter, recipe and transform concepts; replace all three outcome types and sound/BGA twins.
  4. Add notification_sink_t post_feedback/muted and one severity conversion; migrate optional sinks and all behavior-equivalent routing copies.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-619 Own bookmark navigation and stem display state

- **Status:** Ready
- **Findings closed:** BK1–BK2, ST1–ST2 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: seek-record/clock/service field sets and stem display/index/packed/view containers. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Concept owners: `bookmark_navigation_t` (seek record, clock, chooser/rename/seek commands; the three services become member calls; files named `bookmark_*`) and `compose_scene_cache_t::stems` (display state grouped, `stem_for`/`packed_for` members). Inventory each on the card first. Runs after T-603, T-614 and T-617.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/sys/bookmark_navigation.h`, `src/sys/bookmark_navigation.cpp`, `src/sys/compose_bookmark_commands.h`, `src/sys/compose_bookmark_commands.cpp`.
- **Files owned:** `src/sys/bookmark_navigation.h`, `src/sys/bookmark_navigation.cpp`, `src/sys/compose_bookmark_commands.h`, `src/sys/compose_bookmark_commands.cpp`, `src/sys/bookmark_rename_commands.h`, `src/sys/bookmark_rename_commands.cpp`, `src/sys/bookmark_chooser_projection.h`, `src/sys/bookmark_chooser_projection.cpp`, `src/sys/application.h`, `src/sys/application.cpp`, `src/sys/compose_view.h`, `src/sys/bmson_waveform_projection.cpp`, `src/sys/compose_draw_source_projection.cpp`, `tests/sys/compose_bookmark_commands_tests.cpp`, `tests/sys/stem_window_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-603, T-614 and T-617; before T-621.
- **Decisions applied:**
  - D13 — A: In T-612, projections become `build_<x>_view`.
- **Steps:**
  1. Record the full BK2/ST2 ownership inventory on the card before moving anything.
  2. Create bookmark_navigation_t with seek record, clock and chooser/rename/seek members; replace the three services and use bookmark_* file names.
  3. Group display caches under compose_scene_cache_t::stems with stem_for/packed_for; keep analysis, editing and audition in their current layers.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-620 Share expected and diagnosed results

- **Status:** Ready
- **Findings closed:** RS1–RS4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: status/message/diagnostics result field sets, operator bool success rules, fail builders and per-kind decode/codec twins. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Shared results: `core/result.h` with `result_t<T, E>` (an alias of `std::expected`, with shared `ok`/`fail` helpers and standard `and_then`/`transform`) and `diagnosed_t<T>` (one `ok()` rule). Migrate the status and status+message results and the diagnostic ones; delete the per-file `fail`/`failure`/`failed`/`rejected` builders; keep any differing success rule at call sites and record it (RS3). Starts with the C++23 bump (D14). Runs after T-608, T-618 and T-622; before T-621.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `CMakeLists.txt`, `CMakePresets.json`, `tests/CMakeLists.txt`, `include/wormedit/core/result.h`.
- **Files owned:** `CMakeLists.txt`, `CMakePresets.json`, `tests/CMakeLists.txt`, `include/wormedit/core/result.h`, `include/wormedit/core/command.h`, `src/core/command.cpp`, `docs/chart-editor-plan/decisions.md`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-608, T-618 and T-622; before T-621. C++23 first in its own commit.
- **Decisions applied:**
  - D14 — (a) C++23: In T-620, raise the standard first, in its own commit; `result_t` is an alias of `std::expected`; the macOS Xcode 15+ check is left for the owner.
- **Steps:**
  1. D14: raise CMAKE_CXX_STANDARD and every cxx_std_20 to 23 first, in its own gated T-620 part 1 commit; record owner Xcode 15+ verification.
  2. Inventory all current success rules and P2/P5 leads; define result_t as std::expected alias with compatible ok/fail helpers and diagnosed_t with one !has_errors rule.
  3. Migrate status/message and diagnosed results one module per step, preserving stricter warning rejection at call sites and deleting equivalent local failure builders.
  4. Leave payload-only views/plans/indexes and genuinely distinct semantic results outside the migration; run GCC/clang/mingw checks for the standard bump.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not silently change warning acceptance or invent static members on the std::expected alias; Xcode 15+ check belongs to the owner. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-621 Move subsystem files into folders

- **Status:** Ready
- **Findings closed:** FL1–FL5 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: prefix clusters, include paths, CMake source entries and capitalized header consumers. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Folders: move `src/sys` and `src/ui/nuklear` into subsystem folders (FL2), keeping file-name prefixes (FL3). Pure `git mv` plus include and CMake path edits, one commit per folder, each listed in `.git-blame-ignore-revs`. Lowercase `src/common/Chart.h`. Runs last.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `.git-blame-ignore-revs`, `src/common/Chart.h`.
- **Files owned:** `CMakeLists.txt`, `tests/CMakeLists.txt`, `.git-blame-ignore-revs`, `src/common/Chart.h`, `src/common/chart.h`, `src/common/chart.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs last, after every earlier card, including T-619 and T-620.
- **Decisions applied:**
  - D13 — A: In T-612, projections become `build_<x>_view`.
- **Steps:**
  1. Schedule last after all earlier cards; inventory exact per-folder moves from FL2 and coordinate all old/new path ownership.
  2. Use git mv plus only include/CMake path edits, keeping filename prefixes; make one gated commit per folder.
  3. Lowercase Chart.h and update consumers; list each move commit in .git-blame-ignore-revs without rewriting commits.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No semantic edits in move commits; keep filename prefixes. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-622 Add Linux clang verification presets

- **Status:** Done (linux-clang-libcxx configured but not run: libc++ is not installed)
- **Completion (2026-10-13):** Commit `cef8c4c`. Presets `linux-clang` (clang++ + libstdc++) and `linux-clang-libcxx` with separate build trees (ignored), 4 build jobs and 2 test jobs. Clang-only diagnostics fixed at the source with no suppression: braced stable-ID initializers, matching forward declarations, `nk_flags` conversion before OR, unused constants/locals/internal functions removed (including write-only tap-space state), unnamed unused parameters, an explicit preview-status fallback, and a distinct fixture directory for a parallel-test path collision. Lane gate: GCC and clang full CTest 3,542 each. Owner follow-up: install libc++ (`pacman -S libc++`) to run the libc++ preset.
- **Gate evidence (Claude, on main with FFmpeg on):** `100% tests passed out of 3555` (GCC, after T-628); event harness `503/503`; documentation `1/1`.
- **Findings closed:** CL1–CL3 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: compiler selections, warning flags, libc++ availability and clang-only diagnostics. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Linux clang presets: `linux-clang` (libstdc++), added to the per-card gate from then on, and `linux-clang-libcxx` (gated once the owner installs libc++). Fix clang-only diagnostics rather than suppressing them. **Runs before T-620.**
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `CMakePresets.json`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/decisions.md`.
- **Files owned:** `CMakePresets.json`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/chart-editor-plan/decisions.md`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 hydra: before T-628 in that lane. Runs before T-620; adds the clang gate from then on.
- **Steps:**
  1. Confirm current compiler/library availability and baseline preset configuration.
  2. Add linux-clang/libstdc++ and linux-clang-libcxx presets; build/run full CTest on linux-clang and fix diagnostics in coordinator-approved files.
  3. Record libcxx configured but not run until owner installs libc++; make linux-clang build/full CTest part of subsequent card gates.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No warning suppression; libc++ installation belongs to the owner. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-623 Put timing and index queries on their types

- **Status:** Partly done (core and audio; sys wrappers deferred)
- **Completion (2026-10-13):** Commit `206178c`. `timing_index_t` gains `seconds_at`, `row_at` (exact, plus a `chart_end_policy_t` overload), `seconds_at_approx`, `row_at_approx`, `tail_seconds_per_row` and `row_at_frame`; `chart_end_policy_t {reject, clamp_to_end, clamp_to_last_row, extrapolate_tail}`. Core/audio callers migrated. The sys wrapper → member + policy mapping is planned in the follow-up file. Lane drake/core-queries; deferred sys/UI call sites (file:line → new call) are in the lane's `02-T-623-sys-ui-followups.txt`, copied to `/tmp/wormedit-lanes/drake-core-queries-agent/`, and go to wave 2.
- **Gate evidence (Claude, on main with FFmpeg on, after all three core parts):** build exit 0; `100% tests passed out of 3550`; event harness `500/500`; documentation `1/1`.
- **Findings closed:** TI1–TI4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: rational time divisions, end clamps, tail-BPM extrapolation, frame-to-seconds conversions and copied index queries. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** `timing_index_t` query members (`seconds_at`, `row_at`, `*_approx` with `chart_end_policy_t`, `tail_seconds_per_row`, `row_at_frame`). Each sys wrapper becomes a call with its current policy. Sweep the other index types for copied queries (TI4). Runs after T-617.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/timing.h`, `src/core/timing.cpp`, `src/sys/compose_hover_timing.cpp`, `src/sys/stem_waveform_window.h`.
- **Files owned:** `include/wormedit/core/timing.h`, `src/core/timing.cpp`, `src/sys/compose_hover_timing.cpp`, `src/sys/stem_waveform_window.h`, `src/sys/compose_minimap_navigation.cpp`, `src/sys/audio_transport_frame.cpp`, `include/wormedit/core/editor_scene.h`, `include/wormedit/core/scroll_speed_editor.h`, `tests/core/timing_tests.cpp`, `tests/sys/compose_hover_timing_tests.cpp`, `tests/sys/stem_window_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-617.
- **Steps:**
  1. Inventory wrappers and record each current reject/clamp-to-end/clamp-to-last-row/extrapolate policy; sweep TI4 index query leads.
  2. Add timing_index_t exact/approximate seconds_at/row_at, tail_seconds_per_row and row_at_frame members with named chart_end_policy_t.
  3. Replace wrappers with calls using their existing policies; migrate other index queries only where equivalence is confirmed.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Do not unify differing end-of-chart policies. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-624 Share chart event edit action shapes

- **Status:** Ready
- **Findings closed:** EA1–EA3 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: insert/update/delete field sets, duplicate value payloads, event/anchor identifier fields and timing-lane construction translations. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Generic chart edit actions: `insert_edit_t`/`update_edit_t`/`delete_edit_t` over one value type per kind; the per-kind action trios, the `event`/`anchor` id spellings and the 21 timing-lane translations go away; per-kind validation and undo labels stay. Runs after T-607/T-608 and T-610.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/command.h`, `include/wormedit/core/bpm_editor.h`, `include/wormedit/core/marker_editor.h`, `include/wormedit/core/pause_warp_editor.h`.
- **Files owned:** `include/wormedit/core/command.h`, `include/wormedit/core/bpm_editor.h`, `include/wormedit/core/marker_editor.h`, `include/wormedit/core/pause_warp_editor.h`, `include/wormedit/core/scroll_speed_editor.h`, `include/wormedit/core/timing_anchor_editor.h`, `include/wormedit/core/timing_lane_controller.h`, `include/wormedit/core/lane_layout_editor.h`, `include/wormedit/core/semantic_layer_editor.h`, `src/core/timing_lane_controller.cpp`, `tests/core/timing_lane_controller_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-607/T-608 and T-610.
- **Steps:**
  1. Inventory twelve action trios, paired modules and all 21 timing translations.
  2. Define insert_edit_t/update_edit_t/delete_edit_t and one value type per kind, with id as the identifier field.
  3. Migrate editors/controllers through the shared transaction; remove duplicate payload vocabularies and translations, retaining each kind validation and Undo label.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-625 Share core value and text helpers

- **Status:** Partly done (wave 1: core, formats, audio, visual, tools; sys/UI call sites deferred)
- **Completion (2026-10-13):** Commit `e424422`. Shared helpers: `to_long_double`/`to_double` with one named zero-denominator policy (return zero), `nearest_row` with a clamp policy, wide gcd/multiply, 128-bit `subtract_exact` in `timing_arithmetic`, `text_util` UTF-8 truncation/`valid_bounded_text`/explicit-set `trim`/`ascii_upper`/`ascii_iequal`, `parse_exact_decimal` with an exponent limit, `bms36_value`, `default_maximum_chart_objects`, `default_snap_denominators`. D19 applied with regression tests (9 core cases failed 7/9 against the old implementations; the audio row-range case failed before). Recorded differences: invalid denominators convert to zero; 128-bit add/subtract accepts representable results the 64-bit copies rejected; bounded names/tags reject malformed UTF-8; BMS decimal exponent cap 38, BMSON/clipboard 18; the visible-row range rounds outward. Byte-only diagnostic clipping kept at `native_project_storage.cpp:68`, `shortcut_profile_startup.cpp:27`, `recovery_controller.cpp:48/62`. Remaining sys/UI call sites are listed in the wyvern lane's `T-625-remaining-sys-ui.txt` and go to wave 2.
- **Gate evidence (Claude, on main with FFmpeg on):** build exit 0; `100% tests passed out of 3534`; event harness `100% tests passed out of 500`; documentation `1/1`; lane clang-tidy on 61 touched files with no new or increased violations.
- **Findings closed:** Catalog P1 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: division guards, 128-bit gcd/multiply/subtract, frame/row rounding, UTF-8 byte masks, whitespace/length checks, exponent caps, base-36 bodies and branch collectors. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Core value and text helpers (P1). In `rational.h`/`checked_math.h`/`timing_arithmetic.h`: `to_long_double`/`to_double` with one named zero-denominator policy, 128-bit gcd/multiply overloads, seconds↔frames, the rows→beat-fraction reducer, `nearest_row(row, clamp policy)`, and `normalize` names that say whether they return optional. In `text_util.h`: `is_utf8_continuation`/`truncate_utf8`, `trim` with an explicit whitespace set, `valid_bounded_text`, the remaining fold copies, `ascii_upper`. In formats: route through `invalid_utf8_offset` and `parse_exact_decimal` (with an exponent-limit option); add `bms36_value`. Constants: the chart-object limit and the default snap set. D19: unify each drifted copy on the most careful variant, with a regression test per difference. Runs before T-621.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/rational.h`, `src/core/rational.cpp`, `src/core/checked_math.h`, `src/core/timing_arithmetic.h`.
- **Files owned:** `include/wormedit/core/rational.h`, `src/core/rational.cpp`, `src/core/checked_math.h`, `src/core/timing_arithmetic.h`, `src/core/wide_math.h`, `src/core/text_util.h`, `src/core/sound_slice.cpp`, `src/core/channel_map_editor.cpp`, `src/core/lane_migration.cpp`, `src/formats/bms.cpp`, `src/formats/bms_writer.cpp`, `src/formats/bmson.cpp`, `src/formats/osu_mania.cpp`, `src/formats/stepmania.cpp`, `src/audio/aligned_waveform.cpp`, `src/audio/waveform.cpp`, `src/visual/visual_asset.cpp`, `tests/core/rational_tests.cpp`, `tests/core/sound_slice_rules_tests.cpp`, `tests/core/audio_aligned_waveform_tests.cpp`, `tests/formats/bms_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 wyvern: core/formats/audio/visual scope, then T-613. Runs before T-621; remaining sys consumers coordinated in later waves.
- **Decisions applied:**
  - D19 — Unify: In T-625, T-626 and T-628, each drifted copy moves to the most careful variant, with a regression test per difference: the floor/ceil row range, the guarded rational division (with one named zero-denominator policy), one float→byte rounding rule, an explicit whitespace set for `trim`, UTF-8 validation in every bounded-text check, and 128-bit `subtract_exact`. Record each changed call site on the card.
- **Steps:**
  1. Confirm P1 leads in core/formats/audio/visual; inventory differing contracts and failing-before regressions for each D19 drift before unifying.
  2. Share rational conversion with a named zero-denominator policy, wide math, seconds/frames, beat reduction, nearest_row/clamp policy and optional-normalize names.
  3. Share UTF-8 continuation/truncation, explicit-whitespace trim, validated bounded text and ASCII upper; coordinate fold work with T-612.
  4. Migrate formats to invalid_utf8_offset/parse_exact_decimal with exponent limits and bms36_value; share branch collectors, chart-object limit and default snap constants.
  5. Apply D19 careful variants (including floor/ceil ranges and 128-bit subtract_exact), recording every changed call site; coordinate later sys consumers before editing outside Wave 1 scope.
  6. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Wave 1 excludes sys/UI migration edits; coordinate later consumers. Do not erase exponent/case/return-rule differences without D19 coverage. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-626 Move document and lane queries onto owners

- **Status:** Partly done (core, audio, formats; sys/UI lookups deferred)
- **Completion (2026-10-13):** Commit `126d88f`. Document/chart lookup members, `lane_layout_t::find`/`contains`/`has_same_shape`, `lane_definition_t::is_editable`, defaulted `operator==` on lane, channel-map, MIDI destination and `target_resource_identity_t` types, exhaustive control-kind predicates beside `bms_control_kind_t`. The free `find_chart` stays as a forwarder for sys until wave 2. Nit for T-612: `has_positive_control_value` only forwards to `has_control_value`. Lane drake/core-queries; deferred sys/UI call sites (file:line → new call) are in the lane's `03-T-626-sys-ui-followups.txt`, copied to `/tmp/wormedit-lanes/drake-core-queries-agent/`, and go to wave 2.
- **Gate evidence (Claude, on main with FFmpeg on, after all three core parts):** build exit 0; `100% tests passed out of 3550`; event harness `500/500`; documentation `1/1`.
- **Findings closed:** Catalog P4 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: ranges::find of charts/assets/notes/events by id, lane predicates/equality field sets, layout-at loops and projection maps. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Document, chart and lane queries as members (P4): `song_document_t`/`chart_document_t` `find_chart`/`find_audio_asset`/`find_note`/`find_bgm_event`/`find_measure` members, migrating the ~110 inline chart lookups and the other inline lookups; `lane_layout_t` `find`/`contains`, `lane_definition_t::is_editable`, one `layout_at` with a row overload; defaulted `operator==` on `target_resource_identity_t` and lane types, plus a named shape comparator; control-kind predicates beside `bms_control_kind_t`. The per-projection id maps become a shared index only where an index is built today (no new allocation). Runs after T-617; before T-621.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`.
- **Files owned:** `include/wormedit/core/document.h`, `src/core/document.cpp`, `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `src/core/position_validation.h`, `src/core/chart_clipboard.cpp`, `tests/core/document_tests.cpp`, `tests/core/resource_assignment_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs after T-617; before T-621.
- **Decisions applied:**
  - D19 — Unify: In T-625, T-626 and T-628, each drifted copy moves to the most careful variant, with a regression test per difference: the floor/ceil row range, the guarded rational division (with one named zero-denominator policy), one float→byte rounding rule, an explicit whitespace set for `trim`, UTF-8 validation in every bounded-text check, and 128-bit `subtract_exact`. Record each changed call site on the card.
- **Steps:**
  1. Confirm P4 leads and inventory inline lookups, equality differences and existing map construction sites.
  2. Add document/chart find members, lane find/contains/is_editable, one layout_at including row overload, defaulted lane/resource identity equality and named shape comparator.
  3. Migrate all inline callers and control-kind predicates; share indexes only at existing allocation sites.
  4. Apply applicable D19 careful-variant changes with failing-before regressions and a complete changed-call-site record.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-627 Share split helpers and projection contexts

- **Status:** Partly done (core; the compose projection context is deferred to wave 2)
- **Completion (2026-10-13):** Commit `5da6907`. One definition each of `minimum_viewport_scroll`, `normalized_position_span`, `snapped_position`, `absolute_row_at_y_pixels` and `find_active_lane` in `interaction_internal.h`; shared `valid_pause_unit`/`valid_warp_encoding` and one `next_source_order` for the timing editors. Lane drake/core-queries; deferred sys/UI call sites (file:line → new call) are in the lane's `01-T-627-sys-ui-followups.txt`, copied to `/tmp/wormedit-lanes/drake-core-queries-agent/`, and go to wave 2.
- **Gate evidence (Claude, on main with FFmpeg on, after all three core parts):** build exit 0; `100% tests passed out of 3550`; event harness `500/500`; documentation `1/1`.
- **Findings closed:** Catalog P1, P8 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: viewport/span/snap/y-pixel/lane lookup bodies, pause/warp validators, source-order scans and field-by-field frame/scope repacking. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Split leftovers and rebuilt contexts (P1/P8): one definition each of `minimum_viewport_scroll`, `normalized_position_span`, `snapped_position`, `absolute_row_at_y_pixels` and `find_active_lane` in `interaction_internal.h`; shared timing-event validators and `next_source_order`; `compose_projection_context_t {frame, scope}` plus `compose_chart_scope_t::for_view`, replacing the five rebuilds and four wrapper structs. Runs before T-621.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/core/interaction_internal.h`, `src/core/interaction_cursor.cpp`, `src/core/interaction_selection.cpp`, `src/core/interaction_hit_test.cpp`.
- **Files owned:** `src/core/interaction_internal.h`, `src/core/interaction_cursor.cpp`, `src/core/interaction_selection.cpp`, `src/core/interaction_hit_test.cpp`, `src/core/timing_lane_controller.cpp`, `src/core/pause_warp_editor.cpp`, `src/core/timing_anchor_editor.cpp`, `src/sys/compose_view.h`, `src/sys/compose_projection_internal.h`, `src/sys/compose_scene_projection.cpp`, `src/sys/compose_draw_source_projection.cpp`, `tests/core/interaction_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Runs before T-621.
- **Steps:**
  1. Confirm P1/P8 leads and record any differing viewport/snap/validation contracts.
  2. Keep one definition of each shared interaction helper, timing validator and next_source_order, migrating equivalent callers.
  3. Add compose_projection_context_t {frame, scope} and compose_chart_scope_t::for_view; remove five scope rebuilds/four wrappers and confirmed redundant nested contexts.
  4. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** Adjacent cards’ owner/factory/catalog/stamp changes unless necessary supporting scope is approved. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

## T-628 Share Nuklear text popup geometry and color helpers

- **Status:** Done
- **Completion (2026-10-13):** Commit `d0cfa55`. New UI helpers (`text_fit.h`, `text_layout.h` and four more headers): ellipsis fitting, word wrap with UTF-8-boundary splitting of overlong words (D18, regression test with a long path failed before), shared popup lifecycle/`owns_input`, `PopupContextStamp::from(view)`, shared key forwarding, rect helpers, `color.h` with one float→byte rounding rule (D19, regression test failed before); the stored `keysound_filter_popup_open` flag replaced by a query; `bookmark_rename_editor_t` → `inline_label_editor_t`. Preserved: marker-list byte clipping (`text_fit.h:44`). Existing `draw_row` complexity 28 (`compose_sounds_panel.cpp:1256`) unchanged. Lane gate: GCC and clang 3,547 each; event harness 503.
- **Gate evidence (Claude, on main with FFmpeg on):** build exit 0; `100% tests passed out of 3555`; event harness `100% tests passed out of 503`; documentation `1/1`; isolated Xvfb Compose screenshot inspected (normal).
- **Findings closed:** Catalog P1, P3, P6, P9, P10 (targets; not yet closed).
- **Stale-plan audit:** Search by behavior: ellipsis/wrap loops, long-word handling, popup stamp field sets, modal-key forwarding, rect comparisons, alpha/byte conversions and derived-popup flag writes. Confirm every A lead and current caller/body equivalence; record stale claims, stricter validation, diagnostics, rounding and policy differences before editing.
- **Outcome:** Nuklear UI helpers (P1/P3/P6/P9/P10): `text_fit.h` (`fit_text_with_ellipsis`), `text_layout.h` (`wrap_text`, plus D18), a `retained_popup_t` with `owns_input()`, `PopupContextStamp::from(view)`, a shared `forward_modal_key`, rect helpers on SDL3 functions, `color.h` (`with_alpha`, `percent_to_alpha`, one float→byte rounding rule), one UI color type, `keysound_filter_popup_open` removed in favour of a method, `bookmark_rename_editor_t` → `inline_label_editor_t`. Runs before T-621.
- **Read first:** `docs/audits/state-ownership-2026-10-12.md`, `src/ui/nuklear/text_fit.h`, `src/ui/nuklear/text_layout.h`, `src/ui/nuklear/color.h`, `src/ui/nuklear/popup_lifecycle.h`.
- **Files owned:** `src/ui/nuklear/text_fit.h`, `src/ui/nuklear/text_layout.h`, `src/ui/nuklear/color.h`, `src/ui/nuklear/popup_lifecycle.h`, `src/ui/nuklear/dialog.cpp`, `src/ui/nuklear/tooltip.cpp`, `src/ui/nuklear/components.cpp`, `src/ui/nuklear/compose_state.h`, `src/ui/nuklear/compose_state.cpp`, `src/ui/nuklear/compose_note_label_inline.h`, `src/ui/nuklear/compose_note_label_inline.cpp`, `src/ui/nuklear/compose_sound_picker.cpp`, `src/ui/nuklear/compose_panel.cpp`, `src/ui/nuklear/compose_bookmark_rename.h`, `src/ui/nuklear/compose_bookmark_rename.cpp`, `tests/ui/dialog_tests.cpp`, `tests/ui/popup_lifecycle_tests.cpp`, `tests/ui/nuklear_compose_state_tests.cpp`. Supporting caller/test/CMake extensions require exact paths and coordinator approval before editing.
- **Runs after/before:** Wave 1 hydra: after T-622, src/ui scope. Runs before T-621.
- **Decisions applied:**
  - D6 — No: The Nuklear UI state split is out of scope.
  - D18 — Fix the code: In T-628, `wrap_text` splits overlong words at UTF-8 character boundaries so a line never overflows; add a test with a long path that has no spaces.
  - D19 — Unify: In T-625, T-626 and T-628, each drifted copy moves to the most careful variant, with a regression test per difference: the floor/ceil row range, the guarded rational division (with one named zero-denominator policy), one float→byte rounding rule, an explicit whitespace set for `trim`, UTF-8 validation in every bounded-text check, and 128-bit `subtract_exact`. Record each changed call site on the card.
- **Steps:**
  1. Confirm P1/P3/P6/P9/P10 leads in src/ui and pinned SDL3 APIs; reproduce long unspaced UTF-8 path overflow and each D19 color drift with failing-before tests.
  2. Extract text_fit/text_layout; apply D18 UTF-8-boundary long-word splitting and D19 one float-to-byte rounding rule in shared color helpers/type.
  3. Add retained_popup_t with owns_input(), PopupContextStamp::from(view), shared forward_modal_key and SDL3 rectangle helpers preserving edge semantics.
  4. Replace derived keysound_filter_popup_open storage with a query and rename bookmark_rename_editor_t to inline_label_editor_t; migrate confirmed helper callers within src/ui, recording changed call sites.
  5. Run every Verify command; record search evidence, preserved/approved differences, actual output tails and supporting ownership in the commit body; commit only this card or its numbered part. Claude transfers evidence on merge.
- **Invariants:** Preserve diagnostics, ordering, Undo, atomic publication and current rejection/no-change rules except copied owner decisions. Follow Execution performance/type/ABI/complexity rules and the audit’s Necessary complexity. Keep genuine variant contracts explicit; record all changed call sites for an approved unification.
- **Reject/failure behavior:** Preserve cancellation, stale-work rejection, missing-target errors and no partial document/cache publication. A failed gate stops completion.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `ctest --test-dir build -L event-harness`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `clang-tidy -p build <source-file>` for every touched .cpp, with no new/increased violations. Let stale build configuration reconfigure. Read actual output and record command exit statuses/tails; run failing-before regressions for approved changes and existing production-path tests. Add the Linux clang gate after T-622 as specified in Execution. Skip Xvfb in the sandbox.
- **Out of scope:** No D6 UI state lifetime split; no edits outside src/ui and its tests/build wiring without coordination. Rejected/downgraded audit claims, unapproved behavior changes and unrelated work.

