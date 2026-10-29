# Compose input contracts, 2026-10-29

Base: integration checkpoint `7863caf`, continuing PR81; round-two comparison base
`23e113b557bb5a41fa753a32bbcda5ba0661cfb2`. No timing or navigation-speed benchmark is claimed.

## T-782: effective solo selection policy

Before: four flag/target derivations in three files: pointer input installation
(`compose_input.cpp`), retained-state repair and scene request (`compose_view.cpp`),
and same-sound query (`compose_sound_selection.h`). Two sites wrote retained
`solo_semantic_layer`, including projection. Projection additionally cleared a missing
target, solo flag and brush flag. Scoped registered selection commands did not install
policy. The static-style regression explicitly expected projection to repair interaction.

After: `semantic_layer_state_t::effective_solo_layer(chart)` owns derivation and validates
existence in the current chart, including explicit No layer. Projection reads it into
its detached interaction view and scene request, without clearing session policy or
writing retained solo state. Three selection entry surfaces install policy: pointer
batch, Compose view-action dispatch and registered scoped-selection commands.
Same-sound queries read it directly. Note-label hit selection now reads its fresh
interaction view rather than a cached projected policy.

Maintenance exercise: changing solo eligibility previously required checking all four
derivations and both retained writes; it now changes one query, then checks the three
installation surfaces. Changing target while solo, disabling solo and entering a chart
without the target no longer require a projection between action and selection. Tests
exercise these sequences with and without projection, and same-sound target changes
without projection. A real Compose toggle → registered selection → target action →
selection → toggle-off sequence additionally exercises same-frame dispatch wiring.
Explicit selection commands retain their own selection semantics.
Chart-local target IDs continue to resolve against the current chart; this does not
introduce a new persisted or chart-stamped session target model.

Neutral costs: retained core interaction still holds a policy snapshot; sys must install
it at dispatch. There are now three explicit installation sites instead of two, but no
projection repair obligation. The session header already has broad frontend-consumer
fanout; adding the query does not reduce that fanout and requires consumer verification.
The sound-query supporting header extension was approved by the coordinator and recorded
on T-782 before editing. Shared command, lifecycle, stamp and transaction runners are unchanged.

## T-783: bounded family admission

The frame action bag remains transport and supports simultaneous events. The new internal
`compose_input_admission.h` constructs bounded payloads using `T::from`; the event boundary
admits once, then scopes the admitted variant to that event. There is no typed mirror in
the frontend action bag. The existing shared result/outcome runners finish refusals.

The selected families are complete: note-paint begin (toggle/draw/erase), semantic-layer
paint begin (add/remove), BGA/BGM paint stroke selectors, Draw source role/resource,
Kiai handles including create, Speed handles, preview handles, export-cut handles,
recipe range handles, and recipe commands through `later`. Updates/releases of note and
semantic paint use their captured gesture intent; their unused raw intent field remains
ignored. BGA updates/releases validate the wire selector but retain the begin intent,
including modifier changes. Unsupported selectors stop before handler dispatch.

Reproducible search benchmark against `7863caf`:
`git grep -n 'input.paint_intent' 7863caf -- src/sys/compose/input` produces five lines
(six member accesses): three BGA proposal lines, semantic paint begin and note paint begin.
`git grep -n 'input.span_edge' 7863caf -- src/sys/compose/input` produces nine lines:
Kiai two, preview one, Speed two, recipe range two, recipe command one, export cut one.
Those values were also reinterpreted in raw-parameter helpers: Kiai mapping, preview
mapping, export-cut switch and recipe-command validation/casts.
Afterward, `rg 'input\.(paint_intent|span_edge)' src/sys/compose/input` has five reads,
all in admission: one paint read and four span/recipe reads. Selected handlers contain
zero wire-selector reads; their semantic enum operations remain at their domain owners.

Extension exercise: a new span family previously needed a producer, family route and a
handler-specific raw mapping/fallback. It now needs a producer, family route and admission
mapping to a bounded payload, followed by a typed handler. A new recipe operation still
needs the existing UI enum, recipe producers/menu and typed handler switch; admission's
upper bound follows `later`. This is a smaller raw-interpretation search scope, not fewer
feature edits. Producer files are unchanged; no navigation-time or build-time gain was
measured. The extra internal header and variant/context dependency increase header surface
and consumer rebuild fanout. BGA's proposal accepts an already admitted erase bool to avoid
making the widely included session header depend on the admission header.

Precisely outside this selector migration: `selection_mode` (pointer, BGM/BGA/timing,
bar-line, area/space/latched selections), `navigation_action` (navigation steps),
`note_kind` (arm-note-kind), `timing_lane` (timing-marker selection/move), and
`bga_lane_kind`/`bga_lane_kind_valid` (BGA lane reveal/move/place). These retain their existing
typed vocabulary and route-level checks. Identity fields (`lane_id`, `object_id`, resource
and timing IDs), coordinate/numeric payloads, viewport inputs and gesture-kind dispatch
also remain outside family-selector admission. No remaining handler interprets
`paint_intent` or `span_edge` raw values.

New boundary regressions reject malformed selectors for all selected families without
revision/history change or paint gesture admission, compare unchanged BGA/note content,
preserve an active stroke on malformed release, and preserve note-toggle zero. Existing
valid recipe, span, paint and harness workflows are retained for root execution.

## Verification checkpoint

Executed with no local build configuration and no root output mutations:

- Bounded syntax-only GCC checks using root compilation flags, remapping first-party
  include/source paths to this checkout and retaining root generated/dependency paths:
  `compose_view.cpp`, `application_command_dispatch.cpp`, `compose_actions.cpp` passed.
- Final bounded GCC syntax-only checks passed for `compose_input.cpp`,
  `compose_timing_input.cpp`, `compose_export_cut_input.cpp`, `frame_timing_tests.cpp`
  and `compose_action_notification_tests.cpp`. Initial test-TU checks encountered the
  unfinished admission header; final successful checks supersede those failures.
- `cmake -DWORMEDIT_SOURCE_ROOT=$PWD -P cmake/check_chart_editor_docs.cmake` passed.
- `git diff --check` passed at this checkpoint.
- Matching Clang/clang-tidy 23.1.3 complexity comparison passed for the above three
  production TUs with `--jobs 2` and a deduplicated, explicitly remapped temporary
  compilation database. Final output: `complexity: checked 3/3` / `complexity: passed
  (3 translation units)`. Earlier attempts failed due an incomplete include-root
  remap and a subsequently fixed in-progress BGA signature; neither is a passing check.
- Matching Clang 23.1.3 complexity checked all five additional source/test TUs.
  The first run rejected only the two new solo tests (Catch macros expanded inside
  loops, scores 80/48). They now use this file's existing assertion helper, with the
  same checks; the final targeted rerun reports `complexity: checked 1/1` /
  `complexity: passed (1 translation units)`. No allowance or baseline changed.
  The four other TUs had no new/increased findings in the five-TU check.
- Direct documentation checks for ceremony ratchet and architecture names passed
  alongside chart-editor-state. These are script executions, not a local CTest/GATE.

Deferred to root after integration: focused workflows and event harness execution,
combined matching Clang 23.1.3 complexity comparison, remaining header consumers, and
combined warm LuaON GATE (`--jobs 2 --complexity-base 23e113b`). No test executable or
GATE was run in this lane. Both cards remain partial until root supplies that evidence.

## Root integration requests

Cherry-pick this lane's three checkpoints in order. Run the warm LuaON combined GATE
with `--jobs 2 --complexity-base 23e113b`; do not substitute the lane's partial checks.
Exercise `[semantic-layer]`, `[select-same-sound]`, `[admission]`, recipe/Kiai/preview/
Speed/export-cut and valid note/semantic/BGA/BGM paint workflows, plus the event harness.
Explicit header consumers for combined verification include `compose_input.cpp`,
`compose_timing_input.cpp`, `compose_export_cut_input.cpp`, `compose_view.cpp`,
`compose_actions.cpp`, `application_command_dispatch.cpp`, `frame_timing_tests.cpp`
and `compose_action_notification_tests.cpp`; root's full build covers additional session
and frontend-action-header consumers. Re-run documentation CTest checks after integration.
No root build output/source or tracked fixture was changed. All lane compiler processes
completed before handoff. Root owns final card acceptance and PR81 updates.
