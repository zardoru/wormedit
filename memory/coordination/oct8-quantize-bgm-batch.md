# Quantize previews, lane-key workflow and BGM conversion

Authorized 2026-10-08 by the user. Base `840a4a2`; normal main build and
2,764/2,764 tests pass. Root owns main, scope/handoffs, manifests, shared task
card changes, status records, final checks/captures and cleanup.

## Worktrees and agents

- Feedback: `/home/zardoru/devel/wormedit-quantize-keyboard-oct8`,
  `batch/quantize-keyboard-oct8`, T-437 then T-422.
- BGM: `/home/zardoru/devel/wormedit-bgm-conversion-oct8`,
  `batch/bgm-conversion-oct8`, T-423.
- Evidence/builds: `/home/zardoru/devel/wormedit-scratch-builds/oct8-quantize-bgm`.

Two existing Sol 6.1 medium coordinators prepare and review narrow written
briefs. Luna medium is now available as `/root/luna_quantize_bgm_oct8`. Root
serializes this shared executor: a coordinator requests its slot with an exact
brief; no other source writer runs in that worktree during execution. The other
coordinator may audit/review or prepare immutable baseline checks independently.
Root will integrate reviewed checkpoints serially. Keep only these two workers
and main, then remove released clean workers/builds while retaining branches,
configuration, ordinary runtime and evidence.

## Ownership and phase handoffs

Feedback initially owns T-437's existing quantize preview source/header,
Compose projection, frontend contract, Compose canvas, existing quantize preview
tests and `tests/ui/nuklear_event_harness_tests.cpp`, plus its
`editing-feedback.md` card. T-422 follows its checkpoint and owns the existing
event harness, note-menu hint and identified keyboard-reference documentation.
Support extensions need root approval and an exact card record before edits.

BGM initially audits T-423 read-only and proposes exact existing
`note_to_bgm` source/header/tests for reuse. The simple existing core command and
application routing must be extended rather than duplicated under a parallel
API. Its pure allocator/command phase can follow the first Feedback executor
checkpoint; shared frontend files stay exclusive to Feedback. Document schema
changes must not occur under a Feedback build reading those headers. Native
migration, validation, hold restoration, tail projection and BMS loss reporting
need exact supporting-file approval and recorded phase handoffs before editing.

Root exclusively owns `docs/chart-editor-plan/tasks/bms-editing.md`, since both
coordinators have tasks in that file, and both CMake manifests. Coordinators send
exact required changes rather than editing these paths. Root records additions
before executor work. No new source/test file is needed merely to reproduce an
existing command; request any necessary manifest changes explicitly.

## Acceptance

T-437 previews remain non-mutating and bounded by the retained 256-row report.
Settings changes, halfway choices, hold-tail modes and collisions project on the
next frame, with correct visibility/clipping and lifecycle invalidation. Preserve
warm preview memoization and avoid full chart scans each frame.

T-437's original step/failure hid all blocked previews despite requesting red
collisions. Root resolves this within its intended outcome: collision-only
command rejection retains valid red target geometry while Apply stays disabled;
invalid settings, unsupported geometry and Block-halfway rejection draw none.
No quantize command policy changes. Approved supporting paths:
`src/sys/compose_view.h`, `tests/sys/frame_timing_tests.cpp`,
`src/ui/nuklear/compose_panel.cpp` and narrow inspector source/header visibility
accessors. Feedback records exact ownership/purposes on its card before edits.
`src/ui/nuklear/compose_canvas.h` is additionally approved for a post-dock overlay
entry so visibility uses the current section clip/scroll state. Retained paint
context is cleared each frame/hidden lifecycle; dynamic overlays stay outside
static field caches and inside the field scissor.

T-422 covers actual key event through document changes for three successive
notes and undo, and makes the existing gesture discoverable. Plain lane keys
keep their current whole-selection behavior; rejection preserves selection and
document. Audit partial existing coverage before adding tests or changing code.
Its note menu has no lane submenu; the contextual hint belongs in Note actions,
without confusing semantic Layer with lane assignment. The keyboard reference
belongs in the existing `docs/chart-editor-plan/editing/interactions.md`.
The missing native-event/application/undo chain must exercise the real dispatcher;
any minimal target/helper extension is requested explicitly from root.

T-423 allocation is deterministic by group/resource key, preferred existing
lanes, row/document order and same-row spill. Conversion preserves position,
sound, identity/provenance/branch/layers and hold tails in the native project,
commits one undo, and rejects atomically. Moving BGM back restores the hold.
BMS exports only the head and reports tail loss without blocking publication.
Existing BMSON stem and simple BGM moves must keep their behavior.

T-423 first executor phase owns exactly existing `note_to_bgm.h`,
`note_to_bgm.cpp` and `tests/core/note_to_bgm_tests.cpp`. Automatic placement uses
the canonical group rule; explicit preferred-lane moves remain manual placement
with their current right/left fallback. Capacity is explicit planner policy plus
existing native limits, without invented source-family restrictions. Plan IDs
remain original event IDs and the command result/routing API stays compatible.
Later editor tail-loss reporting must not weaken a generic/CLI strict conversion
policy. Imported BMS provenance preservation needs an export regression showing
the old play-channel head is removed and the BGM head retained without duplication.
Luna first executes T-437 report metadata; BGM pure allocator follows its reviewed
checkpoint/release. No other implementation writer runs during each executor slice.

Ordinary app/core/event/frame builds and all card gates are required. New C++
functions stay at cognitive complexity 25 or below with configured header-inclusive
clang-tidy; compare existing identities/scores, including stable Catch titles.
Run documentation consistency after each card/status edit and diff checks before
commits. No production object swaps. Initial builds use at most two jobs per
coordinator; root grants higher limits only after the other job releases.
No user desktop windows or app restarts. Event harness and hidden SDL software
captures are allowed; isolated Xvfb checks require a working isolated runtime.
Report native/display/older-hardware coverage precisely. Root finishes with
normal main build, full suite, main-object captures, committed records and cleanup.

## T-422 real dispatcher target support

Root approved the ordinary production object symbol closure for the existing
event harness, rather than a separate executable or dispatcher substitute.
The existing conditional audio/image/video links remain sufficient. Exact added
production sources in root-owned `tests/CMakeLists.txt`:

- `src/sys/bookmark_chooser_projection.cpp`
- `src/sys/compose_area_actions.cpp`
- `src/sys/compose_bgm_bar.cpp`
- `src/sys/compose_bookmark_commands.cpp`
- `src/sys/compose_dead_spans.cpp`
- `src/sys/compose_input.cpp`
- `src/sys/compose_kiai_spans.cpp`
- `src/sys/compose_minimap_navigation.cpp`
- `src/sys/compose_note_to_bgm.cpp`
- `src/sys/compose_playhead_skip.cpp`
- `src/sys/compose_quantize_preview.cpp`
- `src/sys/compose_recipe_areas.cpp`
- `src/sys/compose_resource_presenter.cpp`
- `src/sys/compose_speed_spans.cpp`
- `src/sys/compose_sv_graph.cpp`
- `src/sys/compose_view.cpp`
- `src/sys/recipe_form.cpp`
- `src/sys/sound_list_projection.cpp`
- `src/sys/sound_usage_projection.cpp`
- `src/sys/workspace_projection.cpp`

## T-423 later-phase support scope, not an executor grant

The allocator remains the initial three-file phase. These supporting paths are
approved for subsequent exclusive slices after source/build ownership release:

- `include/wormedit/core/document.h`
- `src/core/document.cpp`
- `src/formats/native_project.cpp`
- `src/formats/native_project_migrations.cpp`
- `tests/formats/native_project_tests.cpp`
- `src/formats/native_project_storage.cpp`
- `tests/formats/native_project_storage_tests.cpp`
- `src/core/resource_assignment.cpp`
- `tests/core/resource_assignment_tests.cpp`
- `src/formats/bms_writer.cpp`
- `tests/formats/bms_writer_tests.cpp`
- `tests/formats/conversion_loss_matrix_tests.cpp`
- `tests/sys/compose_action_notification_tests.cpp`
- `src/formats/bmson_writer.cpp`
- `tests/formats/bmson_writer_tests.cpp`
- `src/formats/osu_mania.cpp`
- `tests/formats/osu_mania_tests.cpp`

Schema 6 acceptance includes autosave recovery, not just serialization. Native
end-bearing silent BGM events survive legacy normalization. Exact checked tail
translation accompanies a moved head. BMSON uses its existing representable
length; osu! Sample and BMS write the head with explicit typed tail-loss reports.
Existing editor warn publication and generic strict rejection remain unchanged.
Conditional BMS conversion claims and clears retained playable endpoints and
emits channel 01 in that same branch, without deleting native provenance,
flattening the branch tree or replaying old notes. Frontend tail geometry support
will be audited and recorded separately after the feedback handoff.

## T-423 linked hold and retained scene support

Read-only audits showed linked BMSON holds would reject and x=0 lengths would
reimport without semantic ends. The exact parser/linked-assignment and retained
scene paths below are approved within tail preservation, pending exclusive
executor/source-build handoff (not a grant to edit shared feedback files):

- `src/formats/bmson.cpp`
- `tests/formats/bmson_tests.cpp`
- `src/core/bmson_sound_channel_editor.cpp`
- `tests/core/bmson_sound_channel_editor_tests.cpp`
- `include/wormedit/core/editor_scene.h`
- `src/core/editor_scene.cpp`
- `tests/core/editor_scene_bgm_layers_tests.cpp`
- `src/sys/compose_view.cpp`
- `src/ui/frontend.h`
- `src/ui/nuklear/compose_canvas.cpp`
- `tests/sys/frame_timing_tests.cpp`
- `tests/ui/nuklear_event_harness_tests.cpp`

Retain established stem/slice behavior and branch restrictions. Index BGM tail
intervals once per document revision so a visible tail with an offscreen head
still renders without a per-frame chart scan. Keep head and body bounds distinct
for labels/hits; tile hashes/window validity include ends and clipped bars.
Frontend size budgets remain enforced. `compose_view.h` is not additionally
approved for this phase absent a demonstrated cache need.

## T-423 ordering clarification

Automatic allocation orders keyed groups by first selected appearance in document
measure/note traversal. Each silent note is a singleton cohort at its own
document ordinal, rather than a shared silent group or an unconditional last
pass. Keyed groups sort their members by absolute row then document ordinal.
Caller-ID permutations cannot change automatic allocation. Explicit manual
placement preserves the legacy caller-ID processing order as well as the
requested-lane/right/left/new fallback; it ignores group preferences.

The public pure allocator explicitly covers ordinary notes and rejects retained-
linked BMSON selections with a typed diagnostic directing callers to the
existing stem reassignment command. Unlinked BMSON notes remain ordinary. The
command restores linked markers before planning ordinary occupancy and keeps
mixed operations atomic, so it preserves stem ownership instead of publishing a
misleading pure assignment. No new source-family restriction is introduced.

## Implementation checkpoints

Quantize metadata worker `2b0f1c6` is independently reviewed and integrated as
main `1f91016`. Preview 33 cases/189 assertions and filtered CTest 35/35 pass;
configured header-inclusive complexity emits no new findings. The real native
harness target closure links normally on main and its event label passes
242/242. The card remains ready until renderer/visibility gates pass.

Matched ABI-safe annotation probes pass eight dense fixtures and 240 warm hits
per fixture with zero core calls. Sequential cold differences are noisy and do
not support a speedup or frame-cadence claim. Logs/manifests are in feedback
scratch; earlier colocated-hold wall measurements are superseded.

BGM allocator source is frozen at exactly its three initial files. Current tidy
passes with zero core findings and the four existing test title/score identities
unchanged. Sol BGM owns its ordinary build/test readers while Luna writes the
ten renderer files in the independent feedback tree. This overlap permits no
BGM source mutation during readers; any repair returns the writer only after
readers exit. No schema or shared tail frontend edits have been granted yet.

Renderer review confirmed a report-starvation edge: 256 earlier-stored unselected
collision occupants fill the retained table before any of 256 selected moved
notes. Root loans only `compose_quantize_preview.cpp` and its existing preview
tests back to this slice. Counts remain full; selected qualifying rows receive
retention priority, spare capacity holds occupant evidence, and final table
sorting stays deterministic. No doubled buffers, header change, per-frame scan
or core call is allowed. This extends the ten-file renderer grant to twelve.

Allocator worker `bb6f713` is independently reviewed and integrated as main
`62bcb0c`: ordinary app/core build, focused 23 cases/191 assertions, full core
1,632/1,632, docs and unchanged configured tidy baseline pass. Root repeats the
focused tests independently. This remains phase 1; native hold/schema/export
and indexed tail rendering are pending. No final main gate has been claimed.

## Reverse hold validation clarification

The existing reverse move rejects only same-head occupancy; generic document
validation does not reject overlapping note spans. The native-hold slice must
therefore check the restored hold's full absolute interval in the already-owned
`note_to_bgm.cpp`, including notes starting before it and previous restores in
the same transaction. Follow interaction duration/move conventions: endpoints
are inclusive, and the lane must remain available across intervening layout
bands as well as at both endpoints. Preserve ordinary tap behavior and avoid a
global document collision-policy change. These cases supplement the approved
phase 02a acceptance, without extending file ownership.

## Runtime recovery

The server restarted during the renderer's first ordinary affected-target build.
The log reached event-harness object compilation but contains no successful exit,
so it is not a passing gate. Sources and evidence survived. Recovered Sol
coordinators are `feedback_recovery_61` and `bgm_recovery_61`, with sole executor
`luna_recovery_oct8`. Their source-reader audit found no remaining compiler/tidy
jobs. The expanded existing Quantize UI test has a new complexity finding at 28;
extract its cohesive overlay assertions before renewed tidy and ordinary gates.
The other 15 configured baseline finding identities/scores remain unchanged.

Recovered BGM brief audit orders the remaining executor slices as native
schema/holds (02a), linked BMSON import/assignment (02d), remaining codec tails
(02c), conditional BMS loss/publication (02b), then retained scene bars (03).
The parser change precedes the codec roundtrip gate so BMSON reimport preserves
semantic ends. Exact approved files and shared-source handoffs still apply;
this readiness record is not a schema/frontend executor grant.

## Renderer acceptance and lane-key grant

Worker renderer `2bd1270` is reviewed and integrated as main `c3b959d`; a card
checkpoint conflict was resolved by retaining the completed renderer evidence
and root's selected-retention scope. Root independently repeats 7 projection
cases/90 assertions and 35 preview cases/204 assertions and reviews corrected
full-width arrow captures. Ordinary worker targets, full frame 329/329, event
243/243, quantize filter 46/46, docs and unchanged 15-finding tidy baseline pass.
All renderer source readers are released. Final ordinary configured-main gates remain
pending for the completed batch.

The feedback worker merges main cleanly before the next slice. Root now grants
T-422 to the sequential Luna executor, coordinated by recovered Feedback Sol:
exactly `tests/ui/nuklear_event_harness_tests.cpp`, the hint-only
`src/ui/nuklear/compose_note_menu.cpp`, and
`docs/chart-editor-plan/editing/interactions.md`. Use the finalized concrete
scratch brief and existing production dispatcher closure. No additional
implementation files, schema edits or BGM frontend work are granted yet.

Root independently starts the ordinary main build after renderer integration.
The observed existing main configuration is Debug, Lua OFF and video ON;
workers are RelWithDebInfo with Lua/video OFF. Earlier "all-features main"
wording was inaccurate. Preserve the existing configuration and finish its
complete ordinary target set and test suite, reporting this feature coverage
without an all-features claim. Main sources remain frozen during this reader;
worker builds use at most two jobs until root releases its four-job build.

The ordinary configured-main build completes successfully at `aa5408e`; its
durable log is `root/T437-main-ordinary-build.log` in the batch evidence tree.
Main is clean and this reader is released, so the exclusive feedback worker may
use four build jobs. This is the renderer integration checkpoint, not the final
completed-batch test or capture gate. T-422 remains the sole implementation
slice; native BGM/schema mutation still waits for its source/build handoff.

T-422 verification follows the existing undo implementation: restoring
`record.before` restores that document's historical revision as well as its
contents. The draft test's monotonic revision increment expectation was wrong;
root approves correcting it without changing command policy. Native events
must also follow an initial composed frame, and manually constructed Undo input
uses the complete existing dispatcher envelope, including viewport width.
