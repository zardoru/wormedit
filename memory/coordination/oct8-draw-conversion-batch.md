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

Root reviewed T-412 checkpoint `e79d9f3` and integrated it as `20119e4` after
its normal build, full core/event suites and configured baseline tidy gates.
T-463 may proceed in order. Draw explicitly releases `src/sys/compose_actions.cpp`
early because T-463 does not edit it; root may wire sliced app exports and warn
policy now. Shared sys regression tests remain Draw-owned until final release.
No per-frame interaction inspection is introduced for tool readiness.

Additional T-463 scope approved before edits: `src/ui/nuklear/compose_panel.cpp`
invokes cancellation of the new dock-stem drag capture on focus, inactive-tab
and modal paths. Preserve existing resize/cursor behavior and panel routing;
no manifests or other panel changes are required.

Root reviewed and integrated clean T-467 `f20582c` as `90482aa`; source ownership
is released. Its normal/decoder-OFF, CLI, capacity, strict native PCM and actual
Kick full-tail gates passed. Root app review found extension aliases (`bme`,
`bml`, `pms`) are not canonical lane-profile IDs. Sliced BMSON app requests infer
the compatible canonical layout; named output keeps its filename and checks
BMS/PMS family compatibility before any publication. Other app requests preserve
their existing profile behavior. Root adds directory/named-file warning-success
and silent-BMS atomic publication regressions after shared test release.

Additional T-463 production projection regression scope approved before edits:
`tests/sys/frame_timing_tests.cpp` exercises `populate_compose_view` with two
stems and a packed channel sharing an asset. Assert exact dock row/current
source ownership, packed eligibility and cached identity/revision invalidation;
manually populated harness rows alone cannot validate this new join. Existing
semantic event/resource links are authoritative; ambiguous empty-channel
ownership must not silently choose the first lane. Preserve the document model.

Root editor integration now uses warning loss policy, bounded result summary
and notification details. An isolated verification-only test driver linked to
normal production frame-target objects passes three cases / 86 assertions
across seven generated scenarios: both target families and directory/named
paths, missing-source refusal before music/destination writes, silent BMS staging
and incompatible named PMS rejection. Shared test source remains worker-owned;
these regressions will be appended after release. Main full checkpoint build
passes; independent main BMS Kick export into a dotted directory retains all
514 voice tuples with zero onset shift and full 7,008,868-frame PCM maximum
error 2.38419e-7 / RMS 2.05836e-9, zero samples above 2/32768. Main full batch
suite and final Draw-dependent application rebuild remain pending.

Additional T-463 supporting scope approved before edits:
`src/sys/sound_list_projection.cpp` and `tests/sys/sound_list_projection_tests.cpp`
include valid BMSON BGM stem resources in automatic Sounds lists so the actual
dock can emit typed stem placement payloads. Keep slot eligibility and generic
keysound placement unchanged; require an existing asset and preserve non-BMSON
filtering. Cover BMSON inclusion, non-BMSON exclusion and slot exclusion, then
rerun actual shared-asset projection and event regressions before release.


## Final integration and cleanup (2026-10-08)

Root reviewed and integrated T-463 `1dad8ee` as `999d3ac` after final worker
release. Root editor warning/sliced publication implementation is `3d9f2c7`.
The three permanent app regression cases cover seven generated scenarios and
pass 86 assertions in the normal frame target (`root/main-app-export-final.log`).
Configured header-inclusive tidy reports only the unchanged build_speed_graph
29 baseline; no new violations. Normal main application/all-target build passes
(`root/main-build-final.log`), and the complete suite passes **2,710/2,710**
(`root/main-tests-final.log`, 53.48 seconds).

The main-object SDL dummy/software capture (`root/kick-draw.png` and log) shows
Draw active, three ready packed waveforms and a 300 px voice stem; document
revision stays unchanged. No physical-display/GPU/cadence claim. All 15 original
chart/audio hashes match `root/source-sha256-before.json` after final capture.

Both released clean worktrees were removed without force; branches remain.
Removed 8,568,435,423 bytes of generated worker builds and four Kick WAV sets
after retaining chart bytes, artifact hashes/manifests, build configurations,
replay runtimes, logs and probes. Exact paths/sizes: `root/cleanup-report.json`.
Only main remains. T-413/T-414/T-389, T-461/T-453 and T-282 were not implemented.
