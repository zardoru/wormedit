# Compose workflow ownership — 2026-10-29

T-780, base `d154284`, branch `refactor/compose-workflow-ownership`.
[Task](../chart-editor-plan/tasks/compose-workflow-ownership.md).

## Owner correction

Previously `application_command_state_t`, including application services and all project
workflow groups, was declared among scene caches in `compose_view.h`. Paste admission,
proposal revision, clipboard decoding and commit lived in `compose_view.cpp`. Quantize
settings/preset operations lived in `compose_actions.cpp`, while typed application lived
in `compose_quantize.cpp` (T-722).

Now `sys/app/application_command_state.h` owns the application aggregate and services.
The existing lifetime groups and reset expressions remain intact. Dedicated
`compose_clipboard_workflow_t` and `compose_quantize_state_t` retain their existing project
fields; compatibility type aliases keep existing callers valid without duplicated storage.
`compose_view.h` remains a compatibility consumer of application state, not its owner.

`compose_clipboard_workflow.cpp` owns paste admission, decoding, proposal preparation,
stale checks, revision and application. It takes the clipboard workflow rather than the
application aggregate. Public application adapters in `application_command_dispatch.cpp`
read the existing clipboard service and coordinate paint modes. They do not mutate the
chart. Detached preview geometry and indexing stay in `compose_view.cpp`, behind
`build_compose_paste_geometry`. Geometry/index storage remains in the retained paste
session, preserving lifetime, reuse and reset behavior. This is an explicit remaining
value-type dependency on editor scene types, not a second cache or a new renderer.

Quantize editor/report types live in `compose_quantize_state.h`; its public operation
header no longer includes `compose_view.h`. Six settings/preset/preview routes moved to
an immutable module table in the existing `compose_quantize.cpp`. Their operation and
policy fields were compared mechanically with base and match exactly. Typed command and
frontend paths continue to call the T-722 selection application function. Successful
preset transactions use the existing outcome mapper with `rejected_only`, retaining
fixed success text/severity and diagnostics. The preexisting failure branches stay intact.

Borrowed edit-input bundles forward-declare application state and scene cache instead
of importing rendering. The command-dispatch contract imports application state;
its implementation still explicitly imports rendering for the navigation operations that
use it. This is deliberately not a relocation of all scene declarations to an umbrella.

## Behavior review

- Missing-chart begin clears paste and reports the same error. The application adapter
  does not consult the clipboard service for a missing chart or pending gesture.
- Internal clipboard wins over unchanged system text; newly supplied BMSE/iBMSC text
  is decoded against the cursor lane layout. Decode/admission failure preserves an
  existing session and paint modes, as before.
- Begin clears all three paint modes only on admission. Proposal/warp checks and
  detached geometry construction use the same inputs and algorithms.
- Revision mismatch or missing chart clears the session; ordinary invalid placement
  and commit refusal retain it. The unchanged-anchor/index fast path remains.
- Apply still revises first, respects unavailable anchors, uses core clipboard application
  and its shared transaction, and clears the session only on success.
- Quantize still normalizes collision rejection and disallows resolution promotion for
  application while retaining preview policies. Preset names, duplicate detection,
  indexes, Undo labels, snap sharing and feedback text remain unchanged.
- Shared input outcome/feedback finishing and document transaction ownership are unchanged.
  No frontend payload, file format, schema or core operation changed.

Code-style checklist reviewed: one concrete storage owner per workflow, existing parser
and typed query APIs, existing staged transaction/result/feedback owners, one module table
per route fact, and unchanged lifetime/reset groups. Compatibility aliases denote the
same underlying state types. No new/increased complexity violation remains.

Existing regressions cover pending/stale/failed/successful paste, detached ghosts,
quantize settings and presets, invalid stored settings, mutation/report retention,
feedback severity and rebind failure. Verification results are recorded below, not inferred
from their presence.

## Dependency and modification measurements

The static include inventory resolves literal local includes recursively (quoted includes
relative to their containing file, then `src/`/`include/`; `wormedit/` includes under
`include/`), deduplicating paths. Before reads `git show d154284:<path>`; after reads the
worktree. External headers and generated includes are excluded; conditionals are not
evaluated. Counts are conservative source reach, not compiler dependency counts or timings.

| Implementation/input owner | Before local-file reach | After local-file reach | Concrete exclusion |
|---|---:|---:|---|
| Paste lifecycle | 211 (`compose_view.cpp`) | 43 (`compose_clipboard_workflow.cpp`) | Scene header, recipe form and stem-waveform headers absent |
| Quantize public contract | 171 | 40 | Scene, recipe form and stem-waveform headers absent |
| Quantize module | 173 | 160 | Scene and stem-waveform headers absent; application recipe state remains reachable |
| Borrowed edit-input header | 178 | 52 | Scene, recipe form and stem-waveform headers absent |

The paste row compares the old lifecycle's actual translation unit with its new compiled
owner, not an allegedly equivalent whole application. Projection remains expensive;
the extracted module still links with projection for ghost construction. Quantize still
reaches application services/recipe state through its implementation aggregate. Do not
claim that every quantize dependency was removed.

Read-only observation of the root `build/compile_commands.json` and its matching `.o.d`
files at root HEAD `d154284`: `compose_view.h` appears in **190 compile objects / 83 unique
source files**. Only objects named by that compile database were counted; an earlier
unrestricted recursive scan mixed nested configurations and is discarded. This is
observational baseline evidence; the same-config comparison below independently verifies it.

A second structural scan uses this worktree's exact compile database as the target
universe, applies the same literal-include algorithm to base/current source, and weights
each source by its compile entry count. Scene-header reach falls from **83 sources /
194 compile entries** to **72 sources / 163 entries**. Eleven consumers leave it: frame
services, action outcome, area actions, BGA actions, quantize, metadata actions/view,
notification-history runtime, and quantize-preview/global-drop/notification-history tests.
No source enters scene-header reach. These conditional-inclusive counts intentionally
overestimate compiler dependencies and are separate from the compiler observations.

The same-config compiler comparison then confirmed **190 → 159 compile objects**,
**83 → 72 unique sources**, using normal C++ compiler `-MM` scans with two workers.
Before used an immutable `git archive d154284 src include tests` snapshot; after used this
worktree. Both used this worktree's compile commands and generated headers, with identical
compiler/feature flags; only project source/include roots changed. External dependency
source overrides stayed fixed. No root generated output was reused. The scans cover the
union of before/after candidate consumers, and presence of each actual header is counted
once per compile entry. Script and raw results for inspection: `/tmp/t780-deps.py`,
`/tmp/t780-compiler-deps/results.json`, `/tmp/t780-compiler-deps.log`.

| Declaration edited | Before compile objects / sources | After compile objects / sources |
|---|---:|---:|
| Compose scene cache/header | 190 / 83 | 159 / 72 |
| Application workflow aggregate | 190 / 83 (embedded in scene header) | 177 / 78 |
| Clipboard workflow state | 190 / 83 (embedded in scene header) | 180 / 79 |
| Quantize workflow state | 190 / 83 (embedded in scene header) | 177 / 78 |

Compiler-observed local-file reach for the paste lifecycle is **211 → 43** in each of its
three compile entries. Quantize implementation reach is **173 → 160** for app/harness/frame
entries and **152 → 137** for the core-test entry. These are exact dependency inventories,
not wall-time timings or claims that every edit gets faster.
New clipboard compilation adds three entries; target duplication is unchanged.
Application state and the extracted state headers remain broadly consumed, so this is a
scene-versus-workflow invalidation boundary, not a promise of minimal rebuilds for every
state field.

Compile-database counts expose a tradeoff: the original settings owner
`compose_actions.cpp` has three compile entries, while `compose_quantize.cpp` has four
(the extra consumer is core tests). An isolated settings/preset implementation edit now
rebuilds **four rather than three** objects. A change spanning settings and typed selection
previously touched both translation units (**seven** entries), now one (**four**). Paste
lifecycle edits retain **three** compile entries while narrowing their dependency reach.
These are structural edit-impact counts, not measured build-time speedups. The extra
core-test settings compilation is a negative result; lane A's target work may address
reuse separately, but this PR does not claim that unlanded benefit.

Necessary implementation edit sites for a paste admission-policy change become one
narrow compiled workflow, replacing one large projection translation unit. That is a
dependency reduction, not a reduction from multiple policy owners. A change spanning
quantize settings and typed application previously required two implementation files;
those operations now share one module. Request decoding remains a separate header;
frontend/schema changes would still require the established frontend owners. The new
paste source is registered in three existing consumers (application, frame-timing and
event-harness); this lane does not remove duplicate compilations. Lane A owns target reuse.

## Fixed HIGH navigation pilot

Both fresh agents used `gpt-6-luna`, HIGH, shared read-only repository access and the same
starting prompt and tool restriction. Neither received implementation history. The prompt:

> Read-only source navigation benchmark. Work in the current repository. Begin from source
> and tests only. Do not read architecture, audits, status memory, plan/task files, or another
> agent report. Use only shell rg and sed/cat tools; do not edit. Exercises: (1) "Find where
> paste is admitted, revised, rejected and committed, including the owner of its retained
> preview." (2) "Find where keyboard quantize settings become a core request and where failure
> reaches feedback." Report each correct trace with mutation, failure/completion and preview
> owner; unique source/test files inspected in order, wrong guesses, and total search match
> counts for initial searches. Do not use elapsed time as a success metric. Keep searches
> targeted; report exactly what you inspected.

Before inspected, in order: `compose_view.h`, `compose_view.cpp`, `compose_input.cpp`,
`compose_notification_tests.cpp`, `application_command_dispatch.cpp`,
`compose_quantize_request.h`, `compose_quantize.cpp` (**7 unique files**). It found paste
admission, retained session, stale clearing, failure retention and successful commit,
but explicitly failed to complete quantize settings-to-feedback. Its first combined
paste/quantize search reported 2,326 displayed lines and truncation; this is censored,
not a reliable total. Targeted search output counts were 76, 41, 30 and 34. It reported
an overly broad first search and a wrong keyboard-flow search in the input file.

After inspected: `compose_clipboard_workflow.cpp`, `compose_input.cpp`,
`application_command_dispatch.cpp`, `compose_quantize_request.h`, `compose_quantize.cpp`,
`frame_timing_tests.cpp`, `compose_action_notification_tests.cpp` (**7 unique files**).
It found paste lifecycle/session and core application, plus quantize command, request,
core result and report/feedback outcomes. Initial separate searches reported 628 paste
and 1,709 quantize lines. No wrong path guesses were recorded. It did not inspect an
explicit key binding or the shared completion implementation. Its narrative conflated
settings action decoding with the registered command's stored typed request; the lead
review confirms the command reads the editor and never re-encodes a frontend action.
It also did not name the detached geometry helper's projection owner explicitly.

The post reader used `head`/`wc` for display/counts despite the command restriction;
therefore the pilot also has a tool-use protocol deviation, and is not a strict command-level
paired experiment. Available tools, model, effort and starting prompt were held fixed.

Verdict: neutral/inconclusive navigation result. Equal total file counts, different
search choices, incomplete traces and a single sample do not establish improved search
scope or correctness. The narrower source owner is independently checkable; moved lines
and agent confidence are not improvement evidence. Historical T-722 medium observations
in [the fixed baseline](workflow-navigation-baseline-2026-10-19.md) are context only,
not an absolute HIGH comparison. No elapsed-time result is used.

## Comparison evidence and exclusions

[PR75](https://github.com/zardoru/wormedit/pull/75) was read without modification/merge.
Its four paths (application command availability, command dispatch, clipboard-selection
policy and frontend event regression) remain four paths with this refactor. The latched
range versus pending gesture rule does not belong to paste admission, and its frontend
regression remains necessary. No smaller PR75 edit-site claim is made.

T-722's typed quantize owner is preserved, extending its settings/preset coherence.
T-637's shared typed transaction remains untouched. T-638 traversal and its current
collection-lane successor are unrelated to workflow state. [PR65](https://github.com/zardoru/wormedit/pull/65)
(Wasm), [PR64](https://github.com/zardoru/wormedit/pull/64) (dependency removal) and
[PR63](https://github.com/zardoru/wormedit/pull/63) (Quaver) were inspected for path-scope
context; no claim that this lane reduces their build-target or format edit sites.
Likewise `c6eadce` spans 68 sample-control files; `4c5fade` spans ten solo-layer files;
`c596924` and `72f342d` concern prepared BMS core edits. This extraction neither removes
those semantics nor collapses their legitimately separate UI/core/tests.

## Verification

Final isolated configuration: GCC `/usr/bin/c++`, Debug, application ON, Lua OFF,
audio/image/video decoders and FFmpeg ON. App configuration supplies the frontend event
harness despite the separate headless-harness option being OFF. The existing compiler
cache launcher was enabled without changing compiler or feature settings; completed
objects were retained. The coordinator's combined Lua-ON build covers scripting consumers.
Source-only FetchContent overrides and `WORMEDIT_OTOWORM_SOURCE_DIR` refer to existing
read-only dependency sources; all generated outputs and linked artifacts use this worktree.
Preliminary syntax probes were excluded from final isolation evidence.

The coordinator initially paused heavy verification to avoid concurrent cold-build memory
and swap pressure. Final verification ran in C's exclusive slot with two jobs, then C
explicitly released it before documentation/commit/PR completion. No contended wall-time
benchmark is reported.

Real failures fixed before completion:

- Narrowing includes exposed missing direct state includes in area/outcome consumers and
  a standalone area test fixture. Each now names its actual state dependency.
- First GATE passed 5,464/5,465 tests but rejected the relocated three direct working-document
  accesses for quantize presets. All three operations now use existing `transaction.stage`
  and `finish_execution`; the old allowance was deleted with explicit coordinator approval
  and A's overlap clearance. Both old/new files have zero working-document accesses.
  Validation, default no-op policy, commit labels, diagnostics and post-commit-only editor
  updates are retained. No new/transferred/increased allowance or complexity limit was used.
- Documentation checks attempted before build completion found missing generated manuals/tool;
  the final post-build documentation run passed without source changes for those failures.

Final output tails:

```text
GATE --jobs 2 --complexity-base d154284
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 5465
gate: passed
```

Logs: `/tmp/t780-gate-final.log`, `/tmp/wormedit-gate.bCwARZ`.
Changed-file complexity: matching Clang/clang-tidy/libclang **23.1.3**, **7/7 units passed**
(`/tmp/wormedit-complexity.qnehzf7g`). No new or increased cognitive-complexity findings.

```text
Focused paste/clipboard/quantize: 100% tests passed out of 207
Event harness:                  100% tests passed out of 718
Documentation:                  100% tests passed out of 6
Explicit consuming-header complexity: checked 8/8; passed (8 translation units)
architecture.edit_execution:   100% tests passed out of 1
```

Focused tests were repeated after the preset staging change; final GATE re-ran all event
and documentation cases on that source. Explicit header consumers include application,
Compose input, frame tests and application-command tests, plus changed clipboard,
quantize, scene and command modules. The checker invokes `clang-tidy -p build` for each;
logs `/tmp/t780-complexity-headers.log`, `/tmp/wormedit-complexity.k9d8eef6`.

`scripts/smoke-compose-virtual-x.sh /tmp/t780-compose-smoke.png compose` passed. The PNG
was opened and inspected: real Compose field, lane/timing headers, inspector quantize
controls and toolbar render normally. No test window opened on the user's desktop.
Walkthrough test output in `.agent/osu-stepmania-publication/song.wormedit` was restored
after testing. `git diff d154284 --check` passes, including committed changes.
