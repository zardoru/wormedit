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
without projection. Explicit selection commands retain their own selection semantics.
Chart-local target IDs continue to resolve against the current chart; this does not
introduce a new persisted or chart-stamped session target model.

Neutral costs: retained core interaction still holds a policy snapshot; sys must install
it at dispatch. There are now three explicit installation sites instead of two, but no
projection repair obligation. The session header already has broad frontend-consumer
fanout; adding the query does not reduce that fanout and requires consumer verification.
The sound-query supporting header extension was approved by the coordinator and recorded
on T-782 before editing. Shared command, lifecycle, stamp and transaction runners are unchanged.

## Verification checkpoint

Executed with no local build configuration and no root output mutations:

- Bounded syntax-only GCC checks using root compilation flags, remapping first-party
  include/source paths to this checkout and retaining root generated/dependency paths:
  `compose_view.cpp`, `application_command_dispatch.cpp`, `compose_actions.cpp` passed.
- Initial `frame_timing_tests.cpp` syntax check encountered the concurrent unfinished
  T-783 admission header; this is not recorded as passing.
- `cmake -DWORMEDIT_SOURCE_ROOT=$PWD -P cmake/check_chart_editor_docs.cmake` passed.
- `git diff --check` passed at this checkpoint.

Deferred to root after integration: focused workflows and event harness execution,
matching Clang 23.1.3 complexity comparison, explicit changed-header consumers, and
combined warm LuaON GATE (`--jobs 2 --complexity-base 23e113b`). No test executable or
GATE was run in this lane. Both cards remain partial until root supplies that evidence.
