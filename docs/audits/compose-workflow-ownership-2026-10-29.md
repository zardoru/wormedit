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
observational baseline evidence; local before/after compiled fanout remains pending.

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

## Verification status

Registration commit `25ebcfa` uses exactly lane A's approved three CMake additions.
Direct documentation schema check and `git diff --check` passed during source review.
Initial isolated configure passed. An early sequential syntax check of `compose_view.cpp`
passed; clipboard compilation identified two missing direct includes, now supplied.
These preliminary checks predate later edits and are not a final build result.

Coordinator paused heavy verification because concurrent cold builds exhausted memory
and swap. No cold build/GATE was started by this lane; configure and early syntax checks
had already drained when the pause arrived. Full build, GATE, event harness, matching
clang-tidy/header-consuming complexity, and local compiled fanout are pending the build
slot. No wall-time benchmarks from the contended environment will be reported.
