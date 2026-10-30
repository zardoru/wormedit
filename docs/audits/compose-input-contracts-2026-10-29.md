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

## Independent integration review

A fresh Sol-high source review of `23e113b..5e9ec8e` found no correctness or material
maintainability blockers. It traced selection entry surfaces, same-frame routes, current
producer selector values, captured paint intent, Kiai creation, all six recipe commands,
and the frontend guard. Existing application target refresh still performs invalid-target
and brush cleanup; projection no longer has that extra obligation. No tests or compiler
checks were claimed by the reviewer.

The admitted payload accessor deliberately relies on the internal event/variant pairing;
the review found no reachable empty optional, wrong alternative or reentrancy mismatch.
That invariant remains a cost for future route changes, rather than a claim that the
whole transport now makes every illegal state unrepresentable. Root added default value
equality and count ratchets for selector reads and projection repair. The lexical ratchets
do not detect aliases or a moved read that preserves the total count.

Combined root runtime verification is still pending at this checkpoint.

Root subsequently moved the unchanged query body into
`src/sys/app/application_command_state.cpp`, keeping its declaration on the session type.
The inline checkpoint above would make eligibility edits touch the broad session header.
The out-of-line owner adds one definition hop and four compile variants (app, frame timing,
event harness, and core same-sound tests). A matched Make dry-run after the full build will
compare header versus implementation edit scope; no elapsed-time gain is inferred. The
combined compile database has 1,082 first-party entries/757 unique sources and zero exact
duplicate production compilations. This is four more actions than the round-two baseline.

The first complete native runtime run found four failures. Two were diagnostic drift:
recipe and export-cut admission now retain their existing family messages (Draw-source
feedback is retained too). Two were new fixture mistakes: the first solo toggle captured
layer 102 while expecting layer 101, and chart-wide same-sound selection was incorrectly
expected to apply visible-only filters. Root corrected the captured row and retained the
chart-wide assertion, then added the real visible-input route to the same-sound sequence.
No production chart-selection semantics or assertions were weakened to make this pass.
The corrected focused family run passes 847 assertions in 58 cases; matching complexity
passes all three affected units. Final GATE is being rerun; the failed run is not acceptance
evidence. Logs: `/tmp/wormedit-round2-focused-workflows.log` and
`/tmp/wormedit-round2-corrections-complexity.log`.

## Final combined acceptance

The unchanged policy body now has one out-of-line owner. After a successful full build,
root confirmed an idle Make plan with zero compiler actions, then independently changed
only the timestamp of the session header and implementation, restoring each timestamp in
a `finally` block. The same final graph plans 181 unique first-party compiler output objects
for the header touch and four for the implementation touch. This compares edit locations
on one graph, not two historical timed builds. The header plan includes the four newly
registered owner objects. Their cold-build cost and extra definition hop are retained costs.
Raw plans: `/tmp/wormedit-round2-{idle,session-header,policy-implementation}-plan.log`;
helper/result: `/tmp/wormedit-round2-measure-edits.py` and
`/tmp/wormedit-round2-rebuild-measurements.json`. Source contents are unchanged by the plans.

Final full configuration remains native Debug/GCC, app, Lua and all media/FFmpeg enabled.
Matching Clang/clang-tidy/libclang 23.1.3 runs the complexity check. Actual final output:

```text
gate: 2 job(s), set explicitly; logs in /tmp/wormedit-gate.LyyJ9B
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 5602
gate: passed
```

Summary: `/tmp/wormedit-round2-gate-verified.log`; 10/10 changed-TU complexity checks.
Eight additional unchanged consuming units pass (`/tmp/wormedit-round2-header-complexity.log`),
so 18 distinct units cover this round's changes/headers. No baseline or limit changed.
The final focused filter `[semantic-layer],[select-same-sound],[admission],[draw],[paint],
[kiai-spans],[song-preview],[speed-spans],[speeds],[recipe-areas],[cut-drag]` passes
847 assertions in 58 cases (`/tmp/wormedit-round2-focused-workflows-final.log`).
The event harness passes 721/721 (`/tmp/wormedit-round2-events.log`). The visible same-sound
path now executes through pointer/input dispatch; its behavior was not merely source-traced.

Isolated Compose smoke passes and `/tmp/wormedit-round2-compose.png` was opened and inspected:
field, toolbar, quantize and Sounds dock render normally. It is a startup/render check;
the test families cover interaction. No window opened on the user's desktop. Generated
walkthrough output was restored after all consumers finished. Final docs/whitespace are
checked after acceptance report updates. The original failed run is retained in
`/tmp/wormedit-round2-gate-complete.log`; none of its failures is waived.
