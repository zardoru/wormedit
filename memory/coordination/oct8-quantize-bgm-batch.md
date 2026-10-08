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
