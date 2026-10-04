# Notifications pilot: reviewed readiness and implementation batches

Date: 2026-10-04. Base: `f2b35bd`; branch: `coord/notifications`.
Coordinator: **GPT-6.1 Sol, medium** (user preference recorded in
`memory/agent-workflow.md`). Executor: **GPT-6 Luna,
medium**. The readiness pilot was documentation only. The user subsequently
authorized full T-221/T-222/T-223 implementation; root approved the N1–N7 paths.

## Readiness and review

- T-218, T-219 and T-220 are done in the cards and resume checklist.
- T-221 is ready for scoped implementation briefs; its T-219 block was stale.
- T-222 stayed **blocked-on T-221** during the documentation-only pilot;
  implementation scheduling is recorded below after global source review.
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

## Approved batch ownership and review order

Root approved N1–N7 supporting paths below; later exact producer extensions
are recorded on the owning cards before editing. Medium Luna gets an exclusive
subset and positive/negative brief; Sol reviews actual source and evidence.

- N1 sink: `include/wormedit/core/notifications.h`, `src/core/notifications.cpp`,
  `tests/core/notifications_tests.cpp`. Ordered exact bytes, empty/exception
  suppression, thread drain timing, join-before-center lifetime.
- N2 dispatch: `src/sys/application_command_dispatch.h/.cpp`, proposed
  `tests/sys/application_command_notification_tests.cpp`, `tests/CMakeLists.txt`.
  Typed diagnostic/outcome metadata, no callback duplicates or lost rejection.
- N3 globals: `src/sys/main.cpp`, `src/sys/metadata_actions.h/.cpp`,
  `src/sys/options_runtime.h/.cpp`, proposed `tests/sys/application_command_notification_tests.cpp`,
  and exact additional producer/bridge paths and tests
  approved on T-221. Review every producer, invalid/stale unchanged-state case,
  exact dynamic diagnostic and call order before claiming global completion.
- N4 input/actions/projection/dialog: `src/sys/compose_input.h/.cpp`,
  `src/sys/compose_actions.h/.cpp`, `src/sys/compose_view.h/.cpp`,
  `src/sys/timing_view.h/.cpp`, `src/ui/nuklear/frontend.cpp`, `src/ui/frontend.h`,
  plus approved main/dispatch integration, `tests/core/notifications_tests.cpp`,
  `tests/ui/nuklear_notification_state_tests.cpp`, new
  `tests/sys/compose_notification_tests.cpp`, `tests/CMakeLists.txt`.
  Typed success plus later navigation hint keeps success; picker posts once;
  projection suppression uses identity/revision/producer edges, not timed dedupe.
- N5 autosave/recovery: `src/core/autosave_loop.cpp`,
  `include/wormedit/core/autosave_loop.h`, `tests/core/autosave_loop_tests.cpp`,
  `src/sys/platform_recovery.h/.cpp`, `src/sys/application_frame_services.h/.cpp`,
  `src/sys/main.cpp`. Failure/fallback/retry once; idle quiet; safe worker join.
- N6 resource/export: `src/sys/compose_actions.cpp`, `src/sys/resource_job_state.h/.cpp`,
  `src/sys/metadata_asset_runtime.h/.cpp`, `src/sys/application_frame_services.h/.cpp`,
  `src/sys/main.cpp`, new `tests/sys/resource_notification_tests.cpp`, `tests/CMakeLists.txt`.
  Typed task failure/cancel/publication, stale never relinks, terminal poll once.
- N7 audio/decode: `src/sys/audio_transport_frame.h/.cpp`,
  `src/sys/audio_source_registration.h/.cpp`, `src/sys/waveform_analysis_runtime.h/.cpp`,
  `src/sys/main.cpp`, new `tests/sys/audio_notification_tests.cpp`, `tests/CMakeLists.txt`.
  Device/source/waveform failures typed, unchanged progress quiet, cleanup preserved.

Initial order was N1 → N2 → full globals review → N4 → N5 → N6 → N7.
Root permitted independent N5, N6a and isolated N7 after reviewed sink/lifetime
prerequisites; T-223's internal construction/post/test order stayed intact.
Local file ownership is exclusive; different isolated trees may edit shared
logical paths, with root reviewing conflicts and serializing integration. Root
owns main integration, resume and shared status. Manual Xvfb remains unavailable.

## Verification and root decisions

Implementation gates: focused positive/negative tests, configured clang-tidy
(new functions ≤25), build, documentation check and diffcheck. Card integration
adds full suite and allowed headless event workflows; Xvfb is unavailable.
T-221's final grep excludes Compose/view/worker/history/close scoped seams until
the full batch. Every producer retains exact text and call order; existing bounded
UTF-8 notification policy applies.

Root settled sink lifetime: non-owning handle, center declared before all posting
schedulers/runtimes; scheduler joins on normal and exceptional exits before
center destruction. Noexcept posting catches exceptions, ignores failure, and
suppresses empty messages. Producers supply explicit classification metadata;
text/order remain unchanged. Final main grep is a full-batch gate; staged exclusions
are recorded on T-221. Further exact scope extensions can be coordinator-approved.
Typed policies still need producer-by-producer review, and N4 will be subdivided
before execution. Sol owns main/Options glue; N1 Luna owns only core notification
files. `tests/ui/nuklear_options_state_tests.cpp` was added to T-221 ownership for
typed outcome and repeat suppression tests. The independent Debug Ninja app build
uses offline dependency sources and `-j4`; evidence goes under
`/tmp/wormedit-batch-implementation/notifications-*.log`.

Reviewed increments (2026-10-04): N1 sink and long-detail correction are committed
and root-integrated. N2 dispatch plus Sol global glue covers Options/preferences,
project chart/session save/open/close producers, timing staging, external player,
shortcut startup/rejections, and play/pause/start/cancel typed bridges. Rejection
diagnostic severity and cursor-restore callback behavior remain unchanged.
N5 executor completed core autosave/platform recovery; Sol review preserved the
original autosave prefix/path, eliminated duplicate frame failure posts, and kept
successful autosave confirmation. Center precedes posting schedulers on all exits.
Own app build passes; focused changed behavior suite passes 120/120. Configured
tidy reports only existing main/refresh/build_speed_graph and old test violations;
new registered chart helper is extracted to remain at or below 25. Actual logs:
`notifications-global-increment-ctest.log`, `notifications-global-review-build.log`,
`notifications-global-final-tidy.log` under the temporary evidence directory.
Remaining T-221: selection/metadata/import/drop and seek bookmark producer seams.
T-222 input/projection/dialog and T-223 resource/export/audio/recovery scan remain.
No card is marked done. Visual verification remains pending due unavailable Xvfb;
root forbids escalation. Headless event harness remains the allowed workflow gate.

N6a Luna runtime and Sol metadata edit/selection/transport setting producers are
reviewed. Runtime posts starts and consumed completion once; frame consumers no
longer repost metadata results. Actual invalid-audio worker decode failure posts
one error/import, never links an asset, and quiet poll stays silent. New resource
fixtures use unique directories to support isolated concurrent integration tests.
Own app build, 24 focused CTest cases and 2 transport direct cases/15 assertions
pass; configured tidy has no new violation, docs/diff gates pass. N7 is delegated
to the timing Sol coordinator in a separate isolated tree; local ownership excludes
its audio/decode/waveform files. Remaining globals still require producer review.

Reviewed next slice: sound/BGA import and edit producers retain string contracts
with optional sink publication; main review/drop bridges post exact text once and
keep only scoped dialog feedback. Audio offset drag and sound/validation navigation
use explicit branch or diagnostic outcomes. App build passes, focused import/BGA/
offset suite passes 20/20, direct sound producer case passes 3 assertions. Tidy
reports only existing main/refresh/build_speed_graph and old command-test functions.
Remaining T-221: event drop collection, startup/clipboard bridge, and seek/bookmark
publication. N4 and N6b remain pending; no card is marked done.

Collector/startup/clipboard increment: event drop validation/guidance posts at
its typed import producer; actual frontend metadata drop rejection/staging and
unrelated/idle events pass. Startup uses explicit recovery/document branch metadata;
clipboard skips producer-owned duplicate errors. User-authorized expanded import
warnings now use the generic prefix, retain first text/count and all diagnostics
in one explicit detail payload (existing 4 KiB UTF-8 bound). Formatter/sink positive
and empty-warning negative checks pass: 4/4 focused cases. Offline app/harness
build and direct docs/whitespace checks pass; tidy reports only the three existing
main/refresh/build_speed_graph violations. Evidence: `notifications-drop-build.log`,
`notifications-drop-tests.log`, `notifications-drop-main-test-tidy.log`. Remaining
T-221 globals: authoritative rebind failure edge suppression and seek/bookmark.

Final global follow-up: startup context omits the already producer-posted platform
failure; document load/failure and recovery discovery retain explicit classification.
Seek callbacks post producer metadata, and bookmark outcomes carry publication
ownership without altering callback/acceptance behavior. Authoritative rebind retries
each frame but suppresses the same identity/revision/history failure until success
or stamp change. App/core/frontend/frame targets build; 6/6 focused regressions
pass (`notifications-final-global-target-build.log`, `notifications-final-global-tests.log`).
Configured source/test tidy reports only prior violations. Docs/diff gates pass.
Full target build additionally exposed missing N6b resource-runtime test wiring,
which remains in the active executor increment. T-221 implementation review is
complete for globals; its manual visual gate remains pending, so card stays ready.
Root authorizes N4 after this implementation review without falsely marking T-221
done. N6b owns compose_actions.cpp exclusively until its reviewed boundary.

Reviewed N6b/projection/dialog: optional-sink resource/export runtimes post starts
and consumed terminals once; worker cancellation uses stop/task metadata, and
successful publication warnings remain explicit. Sol wires constructors and removes
frame reposts. Real filesystem copy links once; failure/stale and cancellation
fallback checks preserve state/text. Projection records carry producer/object keys
and typed diagnostics; identity/revision edges reset on repair. Picker callback
stages typed metadata under mutex and posts after UI consumption/unlock; scoped
noexcept adapter registers in main and unregisters on shutdown/destruction.
Actual null/long/canceled/stale callback and shutdown tests pass. App and relevant
targets build, 9/9 focused checks pass; source/test tidy has only prior violations.
Logs: `notifications-projection-dialog-build.log`,
`notifications-projection-dialog-resource-tests.log`, `notifications-picker-tidy.log`.
N4 input Luna now owns input sources/new tests; actions move to a root-created
isolated coordinator tree using `/tmp/wormedit-batch-implementation/compose-actions-brief.txt`.
Sol retains final main/history/recovery glue. Manual gates remain pending.

Reviewed N4 input: typed per-input/navigation/viewport publication preserves the
revision guard, string compatibility and explicit callback publication ownership.
Main passes the sink and removes aggregate input/navigation reposts. Real command
edit success followed by navigation cancel retains the success line; stale/no-op,
idle, and legacy/owned playhead and wheel seek checks pass: 6/6. App/frame build
passes. Logs: `notifications-n4-main-build.log`, `notifications-n4-reviewed-build.log`,
`notifications-n4-reviewed-tests.log`, `notifications-n4-tidy.log`.
Actions remain separately owned; final recovery/history/audio bridges are pending.
