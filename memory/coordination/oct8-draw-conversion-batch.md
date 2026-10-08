# Draw workflow and BMSON conversion batch

Authorized 2026-10-08 after T-464. Base `f47c78a`.
Root owns main, manifests, scope approvals, final review, integration and cleanup.
Two Sol 6.1 medium coordinators share one sequential Luna medium executor;
all briefs give exact exclusive file ownership and release before review/build.
No additional worktrees or executors without root coordination.

- `/root/bga_preview_next_61`: `wormedit-draw-oct8`, branch `batch/draw-oct8`.
  T-412 first, then T-463 after its dependency passes. Draw source and explicit
  right-drag erase, dock source selection, shortcuts; then empty/active stem
  points, dock stem drag and packed keysound placement. Preserve T-464 resizing,
  razor/move gestures, one undo per stroke and source-deletion cancellation.
  T-413/T-414/T-389 and T-461/T-453 are outside this batch.
- `/root/budget_recovery_61`: `wormedit-conversion-oct8`, branch
  `batch/conversion-oct8`. T-466 then T-467 share conversion implementation/tests
  and therefore remain sequential in this tree. Reuse T-465 normalized distinct
  slice spans, deterministic WAV names and transactional publication. Preserve
  unrelated conversions and round trips; over-capacity/decoder failure writes
  nothing. Verify actual reimport/preview PCM and BMS profile capacity.

Luna `/root/budget_recovery_61/budget_luna` starts loaned to Draw after its stale
audit and contract design. Conversion queues narrow briefs through root; no
concurrent Luna handoffs or same-file edits. Coordinators may implement design/
glue while Luna owns disjoint tests or a concrete implementation slice.

Read AGENTS, workflow and canonical cards; stale audit before changes. Follow
steps in order, stop on unanswered explicit decisions, and request exact root
approval for necessary supporting paths before edits. Root owns CMake source
manifests and application integration paths; provide narrow patches if needed.
Root alone edits resume/implemented/coordination status.

Scratch `wormedit-scratch-builds/oct8-draw-conversion/{draw,conversion,root}`.
Fresh normal offline optimized builds use immutable existing dependency source
overrides, explicit WORMEDIT_OTOWORM_SOURCE_DIR and disconnected FetchContent.
No temporary submodule symlinks, gitlink mutations or substituted optimized
production objects. Workers initially use two compile jobs; root later allows
four each after checking 16 host threads and about 21 GiB available memory,
with no concurrent heavy root build. Keep timing probes outside compile load;
root uses four after releases. Keep original charts/audio read-only and all
outputs in scratch. Preserve logs/configuration/probes outside removed trees.

No user-desktop windows, app restarts or Xvfb under the existing restriction.
Use the full production frontend event harness and separate SDL dummy/software
captures; state native display/GPU limitations. Run every card gate and docs
after planning edits. Check configured cognitive complexity, including changed
headers and baseline scores: new functions <=25, no NOLINT or increased old
violations. Coordinators review actual diffs/results, commit clean source and
explicitly release all writes/jobs. Root integrates serially, normally builds
and tests all main, commits planning results and removes clean worker builds.

Supporting scopes approved before edits:

- Draw: `src/ui/nuklear/compose_input_internal.h` for explicit stroke intent,
  `compose_input_panel.cpp` for right-drag/context-menu arbitration and
  `compose_panel.cpp` for source/capture lifecycle. Application state lives in
  already-owned `src/sys/compose_view.h`; `application_commands.h` is not needed.
  Concrete tests: `tests/core/interaction_tests.cpp`,
  `tests/core/command_registry_tests.cpp`,
  `tests/sys/compose_action_notification_tests.cpp`,
  `tests/ui/nuklear_event_harness_tests.cpp`,
  `tests/ui/bga_lane_event_harness_tests.cpp`. Preserve the legacy core paint
  default for unrelated callers; Draw and erase send explicit intent.
- Conversion: `include/wormedit/formats/conversion.h`,
  `include/wormedit/formats/slice_spans.h`,
  `src/formats/slice_span_render.cpp`,
  `include/wormedit/formats/conversion_publication.h`,
  `src/formats/conversion_publication.cpp`,
  `src/formats/conversion_publication_storage.cpp`,
  `src/tools/wormedit_convert.cpp`,
  `tests/formats/conversion_publication_tests.cpp`,
  `tests/formats/slice_span_render_tests.cpp`: explicit geometry/plans and
  generated WAVs inside the existing atomic publication boundary. Pure core
  conversion stays detached and decoder-free; decoder-OFF builds remain valid.
  Generic conversions and existing overwrite/ownership policies stay intact.

Cross-tree exception: Draw retains exclusive worker edits to
`src/sys/compose_actions.cpp`. Conversion supplies narrow app caller plumbing
to root as a patch/snippet; root integrates it serially after Draw releases the
file. Conversion may not edit that worker file, avoiding two evolving copies.
Root edits CMake manifests only after receiving exact dependency/source hunks.

Additional Draw scope approved before edits: `src/sys/application_commands.h`,
`src/sys/application_commands.cpp`, `src/sys/application_command_dispatch.cpp`
for Draw/Layer tool commands, `src/ui/nuklear/compose_note_menu.cpp` for Draw
right-click release versus right-drag arbitration, and
`tests/sys/application_commands_tests.cpp`. Select context menus and the
existing Layer semantics stay intact; T-413 remains outside the batch.

Root adds two guarded CLI dependency lines to conversion's `CMakeLists.txt`:
link `wormedit::audio_decoder` and define `WORMEDIT_HAS_AUDIO_DECODER=1` only
inside the enabled decoder block. Core remains decoder-free and CLI decoder-OFF
behavior stays supported. This root-owned hunk travels with conversion's commit.
Conversion has queued an appended `tests/formats/conversion_tests.cpp` Luna
brief covering deduplicated stem/packed mappings, explicit silence, rejection,
determinism and immutable input, after its API is ready and Draw releases Luna.

Draw's second exclusive Luna brief is approved before edits:
`src/core/resource_assignment.cpp`,
`include/wormedit/core/resource_assignment.h` and
`tests/core/resource_assignment_tests.cpp` implement one atomic BGA stroke
through existing validation/commit infrastructure. Repeated per-event commits
would violate T-412's one-undo invariant. Preserve the 50,000-target cap,
event kinds/layers, source/revision checks and unchanged rejection state.
The explicit note-intent slice was reviewed and released first; erased holds
leave the active sweep to avoid repeated scans on later rows.

Root's `src/sys/main.cpp` readiness hunk enables `edit.tool_draw` and
`edit.tool_layer` beside `edit.tool_select` using existing Compose readiness.
The existing note/tool helper receives application tool state so Select, Draw
and Layer checkmarks stay mutually consistent and Draw/Layer are unavailable
in Timing focus. Conversion's app patch must branch before old
music staging/discovery so failure cannot publish music ahead of chart/WAVs.

Luna handoff: Draw's explicit note-intent and atomic BGA slices were released
after exclusive edits/tests. Root transferred the single Luna to conversion's
appended conversion tests once the concrete request/result API was ready.
Draw retains all its review/source/build paths; another Luna handoff requires
root coordination. Conversion deduplicates shared normalized spans across
selected charts and preflights generated ID capacity before allocation.

Additional Draw scope approved before edits: `src/ui/nuklear/compose_area_menu.cpp`
and `src/ui/nuklear/compose_recipe_menu.cpp`, opening predicates only. These
menus otherwise open on right press before Draw captures erase. Share the
no-movement release predicate through owned Compose state helpers, use one
screen-coordinate drag threshold, and preserve Select press behavior and menu
contents.

Sequential Luna return: conversion's appended pure tests were released and
reviewed; Draw now loans Luna exclusively `tests/ui/nuklear_event_harness_tests.cpp`
for source selection/clear, right-click versus erase drag at scaled viewports,
and cancellation regressions. Sol keeps production and all build writes;
release the test file before its review/build. Conversion retains its tests.

Additional Draw scope approved before edits: `src/core/interaction_hit_test.cpp`
for a narrow position-at-Y overload accepting the retained measure index, with
its declaration in owned `interaction.h`. BGA strokes retain validated geometry
once instead of rebuilding an index for every pointer update; exact placement
and standard absolute snap semantics remain unchanged.

User decision during execution: editor exports report conversion losses and
continue instead of rejecting solely for a loss. Root changes the application
request to `loss_policy_t::warn` and presents conversion loss warnings on success
after Draw releases `compose_actions.cpp`. Decode, invalid target, capacity and
publication failures still reject atomically; CLI explicit loss policies remain.

Additional Draw scope approved before edits: `src/ui/nuklear/frontend.cpp`
for narrow right-button/motion press-origin tracking in existing Compose state.
Nuklear overwrites `clicked_pos` on release, so a batched press/motion/release
must retain the actual press origin to distinguish erase from a context click.
Preserve native cursor and other frontend routing; verify the batched path and
baseline cognitive complexity.

Additional conversion scope approved before edits:
`include/wormedit/audio/slice_render.h`, `src/audio/slice_render.cpp`,
`tests/core/audio_slice_render_tests.cpp`. Actual Kick voice tuples match, but
PCM16 materialization clips decoded Ogg overshoots (kick_bass peak 1.11281).
Add an explicit float32 WAV option used by conversion to preserve decoded PCM;
keep existing PCM16 slicing defaults unchanged. Verify finite overshoot/native
rate roundtrip through the production decoder and rerun actual Kick PCM before
claiming T-466's audio gate.

Luna's read-only PCM diagnosis is released. Conversion now loans it exclusively
appended helpers/cases in `tests/core/audio_slice_render_tests.cpp` for float32
overshoot/native-rate roundtrip and pre-write invalid-input rejection. Sol owns
the encoder/runtime and every build; no other Luna edits, and release before
Sol review/build.

Root application reporting scope: `src/sys/resource_job_state.h` and
`src/sys/resource_job_state.cpp`, plus already-owned `compose_actions.cpp` and
`tests/sys/compose_action_notification_tests.cpp` after Draw release. Export
results carry loss detail into existing notification detail (4 KiB), because
the 200-byte message field alone would truncate the report. Preserve background
snapshot/export semantics and test warning-success plus pre-write rejection.

T-467 clarification approved before its implementation, after the reviewed
T-466 checkpoint: BMS requires a WAV binding for silent gameplay. Preserve
soundless/EOF playable notes through one explicitly declared shared one-frame
silent WAV/resource, generated only inside private staging. It is separate from
source slice spans; capacity counts distinct audible spans, retained referenced
WAV resources and one silent resource iff needed. Verify deterministic safe
names, reimport/PCM, just-fit and atomic over-capacity refusal.

T-463 supporting scopes approved before edits, after the reviewed T-412
checkpoint: `compose_sounds_panel.cpp`, `compose_state.h/.cpp`,
`compose_input_internal.h` and `src/sys/compose_view.h` for dock stem drag,
source capture and explicit decoded geometry plumbing. Concrete tests are
`tests/core/bmson_sound_channel_editor_tests.cpp`,
`tests/sys/compose_action_notification_tests.cpp`,
`tests/ui/bga_lane_event_harness_tests.cpp`. The pure core receives asset-bound
native sample rate/frame count; sys checks retained binding identity/revision.
Unknown ordinary span classification reports pending geometry; an empty first
point or forced restart remains deterministic. Imported metadata is not mutated
to fill decoder geometry. Root app export integration waits final Draw release.

Conversion CLI follow-up scope: explicit publication directory semantics in
already-owned API/options keep dotted output-directory names working for single
and multiple charts while preserving default application single-file semantics.

Additional T-412 regression scope approved: `tests/ui/bga_advance_workflow_tests.cpp`
and `tests/ui/inspector_resource_workflow_tests.cpp` update actual toolbar and
source-selection coordinates while preserving semantic and undo assertions.
The current toolbar contains eight controls; retain the matching eight-slot
layout after an actual recount. Autosave terminal-failure isolated rerun passes.

Additional T-467 regression scope approved before edits:
`tests/tools/wormedit_convert_tests.cpp` appends a real CLI multi-chart export
into a dotted output directory. Existing target and manifests remain unchanged.
