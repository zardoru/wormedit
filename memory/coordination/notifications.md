# Notifications pilot: reviewed readiness and implementation batches

Date: 2026-10-04. Base: `f2b35bd`; branch: `coord/notifications`.
Coordinator: **GPT-6.1 Sol, medium** (user preference recorded in
`memory/agent-workflow.md`). Executor: **GPT-6 Luna,
medium**. This pilot contains documentation only; no feature implementation.

## Readiness and review

- T-218, T-219 and T-220 are done in the cards and resume checklist.
- T-221 is ready for scoped implementation briefs; its T-219 block was stale.
- T-222 stays **blocked-on T-221**. Do not execute it during this pilot.
- T-223's T-219 prerequisite is satisfied; execution must wait for the sink
  contract and serialized ownership below. Ready does not mean independent.
- Only the T-221/T-223 statuses were reconciled in the card file. T-224 and
  the root-owned resume checklist remain outside this pilot's ownership.

Reviewed the actual Luna report `/tmp/wormedit-coordination-notifications-luna.md`
against current sources, rather than treating lexical counts as message counts.
Read AGENTS, workflow, card rules, notification cards and their Read first
sources. Important verified boundaries:

| Evidence | Consequence |
|---|---|
| `include/wormedit/core/notifications.h:110-127` has center post/drain, no sink | A sink API is a proposed ownership extension, not existing infrastructure. |
| `src/sys/main.cpp:3520,3817-3819` owns center and begins frame drain | Posts later in a frame appear after the next drain; preserve this timing. |
| `src/sys/application_command_dispatch.cpp:73-98` carries untyped strings | Use outcome/diagnostic metadata, never severity inferred by prose matching. |
| `src/sys/timing_edit_runtime.cpp` has no status-string boundary | No edit needed there solely because the card lists it. |
| `src/sys/metadata_actions.cpp:180-195` returns optional strings | Local `auto status` variables are results, not a global status slot. |
| `src/sys/application_frame_services.cpp:152-168` consumes job results | This omitted bridge must be included in T-223 scope before implementation. |
| `src/sys/main.cpp:3845-3865` consumes probe/resident/waveform results | These are T-223, not T-221, despite living in main. |
| `src/sys/timing_view.cpp:228-251` projects staged diagnostics | Post on a result/state transition, not on every projection frame. |
| `src/ui/nuklear/frontend.cpp:237,260,1647` supplies picker errors/override | Replace error precedence at callback/result drain, preserving callback safety. |

Luna correctly identified missing sink and duplicate-post hazards. Its raw search
counts include clears, comparisons, model enums and projections; they are not
acceptance totals. Additional bridge/lifetime findings above refine its proposal.
`main.cpp` declares the autosave scheduler before the center: reverse destruction
alone is insufficient once an autosave worker can post. Shutdown/join must finish
before center destruction on every exit, including exceptional exits.

## Classification ledger

| Message family | Reviewed classification / unresolved detail |
|---|---|
| Preferences warnings | `warning/system`; preserve each original warning and order, not separator spaces used to concatenate a line. |
| Undo/redo applied; timing committed; semantic layer added | `success/edit`; no-op undo/redo `info/edit`. |
| Routine navigation release/cancel; staged gesture guidance | `hint/input` for navigation, `hint/edit` for staged edit guidance. |
| Invalid bounds, stale target, unavailable command, pending gesture | `warning`; source follows producer (`edit`, `input`, `import_`). |
| Structured diagnostics | Preserve their explicit severity and original text; source comes from operation, not diagnostic wording. |
| SDL device/mix/cache failure; missing decoded source | `error/audio` for stopped playback failures; unavailable asset counts `warning/audio`. |
| Playback mode fallback, silenced late voices | `warning/audio`; document-selection stop `info/audio`. |
| Autosave/backup/publication failure | `error/recovery` for autosave/backup; export publication `error/export_`. |
| Resource copy/import success, published output | `success/import_` or `success/export_` according to operation. |
| Resource finished with stale document/source | `warning/import_`; retain successful copy and absence of relink. |
| Empty return/clear; repeat suppression; unchanged projection | No post. Retain any state clear required by existing UI behavior. |

Unclear cases to resolve from typed producers before conversion: `options.status`,
project-chart results/rebind failures, external launch errors and source choice,
tap timing results, import staging failures, resource relink diagnostic severity,
export cancellation, publication warnings appended to success, resident decode
versus budget reports, waveform task errors. Do not invent an all-warning or
all-error fallback. Mixed success plus warning must retain both outcomes and
original complete text; the exact split/detail policy needs coordinator review.

## Proposed exact ownership extensions and ordered Luna briefs

Extensions below are **proposals for root approval**, not edits approved by this
pilot. Before each implementation task, record approved paths on its card,
assign exclusive ownership and read the producer sources for that brief. Every
brief uses medium Luna, the workflow's full outcome/context/invariants/report
format, and the positive/negative checks below. Never overlap shared files.

1. **N1 / T-221 sink contract.** Own proposed additions to
   `include/wormedit/core/notifications.h`, `src/core/notifications.cpp`,
   `tests/core/notifications_tests.cpp`. Specify explicit severity/source posts,
   empty suppression, exception containment for worker posting and lifetime.
   Prefer an application-owned non-owning handle only with provable join-before-
   destruction; otherwise propose safe shared state explicitly. No UI pointers.
   Positive: ordered exact-byte posts through handle; thread post visible only
   after drain. Negative: empty value, posting failure, stopped-worker teardown;
   document/history unchanged. Review API and teardown before N2/N5.

2. **N2 / T-221 command dispatch.** Own
   `src/sys/application_command_dispatch.h`,
   `src/sys/application_command_dispatch.cpp`, `src/sys/main.cpp`, proposed
   `tests/sys/application_command_notification_tests.cpp`, `tests/CMakeLists.txt`.
   Replace command-context status seam with sink; use structured result metadata.
   Keep Compose-owned result conversion behind an explicit temporary bridge for
   N4; do not double-post diagnostics at shortcut and command boundaries.
   Positive: undo, redo, semantic success and transport result ordering.
   Negative: nothing-to-undo, disabled/rejected command, stale rebind, empty
   external action; preserve rejection atomicity and exact message text.

3. **N3 / T-221 application/global producers.** Own `src/sys/main.cpp`,
   `src/sys/metadata_actions.h`, `src/sys/metadata_actions.cpp`, proposed
   `src/sys/options_runtime.h`, `src/sys/options_runtime.cpp`,
   `tests/sys/application_command_notification_tests.cpp`. Convert preferences,
   options, shortcuts, timing edits and metadata dispatch function by function.
   Return typed outcomes where producer information would otherwise be lost.
   Before further edits, inventory remaining main helpers and propose their
   exact missing producer/bridge/test paths; project-chart, external-player,
   dropped-file and startup paths are not permission for arbitrary file edits.
   Positive: multiple ordered same-frame outcomes, exact dynamic diagnostics.
   Negative: invalid/stale input leaves document/history unchanged, no empty
   posts, no worker/Compose migration. T-221 completion waits for all global
   producers; N3 alone is not a claim that the whole card is done.

4. **N4 / T-222 after reviewed T-221.** Own `src/sys/compose_input.cpp`,
   `src/sys/compose_input.h`, `src/sys/compose_actions.cpp`,
   `src/sys/compose_actions.h`, `src/sys/compose_view.cpp`,
   `src/sys/compose_view.h`, `src/sys/timing_view.cpp`, `src/sys/timing_view.h`,
   `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`; proposed integration
   `src/sys/main.cpp`, `src/sys/application_command_dispatch.cpp`,
   `tests/core/notifications_tests.cpp`, `tests/ui/nuklear_notification_state_tests.cpp`,
   new `tests/sys/compose_notification_tests.cpp`, `tests/CMakeLists.txt`.
   Split into sequential input/actions, projection, and dialog briefs before
   execution; each sub-brief gets only its actual paths. Keep projection side
   effects edge-triggered by producer identity/revision, not timed dedupe alone.
   Positive: semantic success then cancel hint keeps success; dialog error posts
   once, changing a projection diagnostic emits once. Negative: empty/canceled
   picker and repeated unchanged projection emit nothing; chart/input focus,
   navigation and history unchanged. Preserve exact text and callback handoff.

5. **N5 / T-223 autosave/recovery.** Own `src/core/autosave_loop.cpp`,
   proposed `include/wormedit/core/autosave_loop.h`,
   `tests/core/autosave_loop_tests.cpp`, `src/sys/platform_recovery.cpp`,
   `src/sys/platform_recovery.h`, proposed `src/sys/application_frame_services.cpp`,
   `src/sys/application_frame_services.h`, `src/sys/main.cpp`.
   Post once per terminal result; join scheduler while sink remains alive.
   Positive: failed publication emits one `error/recovery`; scheduler queue
   failure also reports. Negative: idle/canceled/successful tick emits no failure,
   retries retain existing policy, forced worker teardown cannot access UI or
   destroyed center. Resolve startup scan bridges before marking T-223 complete.

6. **N6 / T-223 export/resource completion, after N4/N5.** Own
   `src/sys/compose_actions.cpp`, `src/sys/resource_job_state.cpp`,
   `src/sys/resource_job_state.h`, proposed `src/sys/metadata_asset_runtime.cpp`,
   `src/sys/metadata_asset_runtime.h`, `src/sys/application_frame_services.cpp`,
   `src/sys/application_frame_services.h`, `src/sys/main.cpp`, new
   `tests/sys/resource_notification_tests.cpp`, `tests/CMakeLists.txt`.
   Carry typed failure/cancel/publication metadata out of tasks; post at one
   completion consumer. Positive: copy/import/publish exact text and source.
   Negative: stale revision never relinks, failed atomic publish changes no
   published artifact, cancel semantics preserved, poll twice never reposts.

7. **N7 / T-223 audio/decode, after N6.** Own
   `src/sys/audio_transport_frame.cpp`, `src/sys/audio_transport_frame.h`, proposed
   `src/sys/audio_source_registration.cpp`, `src/sys/audio_source_registration.h`,
   `src/sys/waveform_analysis_runtime.cpp`, `src/sys/waveform_analysis_runtime.h`,
   `src/sys/main.cpp`, new `tests/sys/audio_notification_tests.cpp`,
   `tests/CMakeLists.txt`. Positive: identical underrun repeats merge within 2 s;
   device/source/waveform failures carry explicit source/severity. Negative:
   different message/source/severity does not merge; unchanged progress/idle
   emits nothing; stop/join and device close preserve playback cleanup.

Ordering: N1 → N2 → N3/full T-221 review → N4 → N5 → N6 → N7.
No concurrent main, Compose, notifications core or test-wiring edits. Independent
inventory reads may run in parallel; root owns integration and resume updates.

## Verification and root decisions

For each implementation brief: build after each source file; run its focused
positive/negative tests plus card Verify; run configured clang-tidy on changed
C++ (complexity ≤25, no NOLINT escape). At card integration run full build/test,
documentation check and isolated Xvfb when UI changes. T-221's zero-status-ref
grep is a **final batch gate**: current main worker signatures and history/close
fallback contexts are excluded from T-221 by its scope. Root must reconcile that
contradiction before claiming the gate satisfied; do not widen scope silently.
Full no-message-loss acceptance compares producer inventory with posts, retaining
original call order and exact message bytes (existing center UTF-8 bounds apply).

Root decisions: approve exact extensions per brief; settle typed producer
policies listed above; approve sink lifetime/exception policy and staged legacy
bridges; finalize remaining N3 ownership and N4 subdivision before execution.
No feature build/test was run by this documentation-only pilot. Direct worktree
documentation validation and diff checks are recorded in the coordinator handoff.
