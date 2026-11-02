# Storyboard screen: scope and implementation cards

[Back to the task card index](../tasks.md).

SB-0 records the owner-approved scope (2026-10-31); SB-1–SB-12 deliver v1.
SB-13 remains planned outside v1, and SB-14 is the separate OSBX-12 hybrid-Lua UI.
These mirror the existing Nightshift tasks, not new work items. OSBX-* / VID-* /
SMX-* prerequisites refer to their existing Nightshift tasks; the older T-* cards
in [OSBX](osbx.md) and [scripting/storyboard](scripting-storyboard.md) give context.
Delivery is post-1.0 on `storyboard-osbx`, in the same eventual PR as OSBX.
No implementation card bypasses the OSBX-0/1.0 release gate.

All cards follow [architecture](../../architecture.md) and
[code style](../decisions/code-style.md): shared transaction/gesture runners,
persistent C++ frontend views, one snap owner, and existing dock/resource/audio
services. Listed future paths are intended ownership, not claims they already exist.
Before starting, reconcile them with landed prerequisite files and record any
supporting-file extension with the coordinator. Common build registration paths
may be shared only with coordinated edits. `GATE` means `scripts/gate.sh`; event
harness means `ctest --test-dir build --output-on-failure -L event-harness`.
Visual checks use isolated Xvfb only, with PNGs inspected. If mockups change, edit
`docs/chart-editor-plan/mockups/generate.py`, never generated SVGs directly.

## SB-0 Decide the Storyboard screen's v1 scope

- **Status:** done (approved scope recorded; parent review accepted)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** approved v1 scope is recorded in question 9 and the OSBX draft, with implementation cards tracked by the documentation test.
- **Read first:** `docs/architecture.md`, `docs/chart-editor-plan/decisions/code-style.md`, `docs/chart-editor-plan/decisions/open-questions.md`, `docs/osbx-draft-spec.md`, `docs/chart-editor-plan/tasks.md`.
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/osbx-draft-spec.md`, `docs/chart-editor-plan/tasks/storyboard-screen.md`, `docs/chart-editor-plan/tasks.md`, `docs/chart-editor-plan/mockups/README.md`, `cmake/check_chart_editor_docs.cmake`.
- **Supporting extension:** coordinator approved `cmake/check_chart_editor_docs.cmake` to register this group and validate SB-* cards alongside T-*; existing required fields remain mandatory.
- **Steps:**
  1. Consume resolved owner approval of all seven defaults (Nightshift blocker `26590054-27dd-4ce1-b051-be6aa8bc44ab`); do not re-ask. Preserve the later SB-13 refinement and SB-14 request.
  2. Record song-level OSBX-only editing; current per-difficulty osu! export refusal for multi-chart songs; OSB conversion as a preserved-source copy; generated read-only plus Detach; left/centre/right/bottom dock; ms default and shared beat/grid snap; editing gestures pause playback.
  3. Keep simulation (SB-13), clip UI, simultaneous editing and video outside v1; mirror existing SB tasks, link mockups and extend documentation validation.
- **Done so far:** owner answers and SB-1–SB-14 recorded; supporting checker extension approved. Review checklist applied: current shared transaction/gesture, C++ view and snap owners are preserved; no C++ implementation or complexity allowances changed.
- **Verification evidence (2026-10-31):** documentation tests passed 6/6. An isolated temporary copy of docs/memory, with read-only links for other repository paths, removed SB-1's Outcome field; checker failed with `storyboard-screen.md SB-1 lacks field: Outcome`, as required. The checkout was not changed by this negative check.
- **GATE output tail:** `gate: 8 job(s), set explicitly; logs in /tmp/wormedit-gate.OrrdGW`; `pass build`; `pass ctest`; `pass whitespace`; `pass complexity`; `100% tests passed out of 5615`; `gate: passed`. Complexity reported zero changed translation units. The known test-generated `.agent/osu-stepmania-publication/song.wormedit` change was restored after the run.
- **Parent review (2026-10-31):** all six owned files reviewed against the resolved scope, current C++ frontend policy and SB-13/SB-14 requirements. Parent independently confirmed the missing-SB-field rejection; final GATE is enforced by Nightshift before task completion.
- **Remaining:** none in SB-0; implementation follows the recorded OSBX dependencies. Publication remains the coordinator's goal-level follow-up.
- **Invariants:** no implementation or video-profile decision; preserve the 1.0 freeze and separate OSBX prerequisites.
- **Reject/failure behavior:** unresolved scope choices stop only dependent work; failures remain visible to the parent.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`; isolated temporary fixture must reject a missing SB required field; `scripts/gate.sh --jobs 8`.
- **Out of scope:** implementation, commits, publication and finalization (parent-owned); video profiles remain question 16 / T-155.

## SB-1 Add storyboard edits through the shared transaction

- **Status:** blocked-on OSBX-4b, SB-0
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** Typed OSBX storyboard edits run through the shared command transaction, with validation, data change and effect only.
- **Read first:** `docs/architecture.md`, `include/wormedit/core/osbx_document.h`, `src/core/command.cpp`, `tests/core/command_tests.cpp`.
- **Files owned:** `include/wormedit/core/osbx_edit.h`, `src/core/osbx_edit.cpp`, `tests/core/osbx_edit_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Inventory shared transaction/gesture paths. Add typed create/delete/rename storyboard, add/duplicate/delete/rename node, same-layer reparent/reorder, subtree layer move and image/origin/blend edits; choose and record explicit subtree-deletion semantics before implementing deletion.
  2. Add transform-op add/remove/reorder, op/track defaults, key add/move/value/easing/delete, multi-key delta move and image-table add/remove edits. Update references on ID rename; allocate deterministic unique IDs from parent plus counter, above current/Undo/Redo identities.
  3. Validate each proposed document with `validate_osbx_document`; return validation, data change and effect through shared runners, including staged drag proposals.
  4. Test edit behavior, invalid input, one-command Undo/Redo document equality and journal delta round trip for a key move.
- **Invariants:** One publication per command/gesture; stable identities; generator ownership and OSB-model read-only checks live at the edit boundary.
- **Reject/failure behavior:** Invalid results, cycles, cross-layer parenting, duplicate key times, OSB or generator-owned targets reject atomically; generated targets explain “detach first”.
- **Verify:** GATE.
- **Out of scope:** Conversion (SB-10), detach (SB-11), widgets (SB-2–SB-9); obtain coordinator approval for command-registration support files.

## SB-2 Activate the Storyboard tab: view model, layout shell, storyboard picker

- **Status:** blocked-on SB-1, SB-0
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** the Storyboard tab is a real screen. It is still mostly empty panels, but every later task plugs into this shell.
- **Read first:** `docs/architecture.md`, `include/wormedit/core/workspace.h`, `src/sys/project/workspace_view.cpp`, `src/ui/frontend/workspace.h`, `src/ui/nuklear/dock.cpp`.
- **Files owned:** `include/wormedit/core/workspace.h`, `src/core/workspace.cpp`, `src/sys/project/workspace_view.cpp`, `src/sys/project/workspace_view.h`, `src/ui/frontend/storyboard.h`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.h`, `tests/ui/storyboard_tab_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Replace the placeholder with storyboard IDs, names, OSB/OSBX kind, generator ownership, selected node/op/key IDs and subviews. Keep selection and viewport state in the existing per-project view-state owner.
  2. Refill persistent backend-neutral C++ views in place; cache document-derived projections by stamp, with selection-dependent invalidation. Follow current frontend policy, superseding the imported task’s stale “C ABI style” wording.
  3. Use the existing dock for left picker/hierarchy/assets, centre canvas, right inspector and bottom timeline; wire New/Delete through SB-1.
  4. Test `workspace.open_storyboard`, picker identity, reset on project switch and focus isolation from Compose lane keys in the event harness.
- **Invariants:** Frontend reads views and emits typed actions; IDs cross the boundary, never document objects or list positions.
- **Reject/failure behavior:** No selection or an empty project gives an explicit empty state; rejected create/delete leaves the document unchanged.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Out of scope:** Populated panels (SB-3–SB-8), transport (SB-9); supporting per-project state or app routing paths require coordinator approval.

## SB-3 Storyboard canvas: render, fit/zoom/pan, hit-test, selection outline

- **Status:** blocked-on OSBX-3, SB-2
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** the centre panel draws the selected storyboard at the shared transport time and lets the user select sprites by clicking.
- **Read first:** `src/sys/audio/player_preview_presenter.cpp`, `include/wormedit/core/storyboard_preview.h`, `include/wormedit/core/osbx_evaluator.h`, `src/ui/nuklear/storyboard/storyboard_panel.h`.
- **Files owned:** `src/sys/storyboard/storyboard_scene_presenter.cpp`, `src/sys/storyboard/storyboard_scene_presenter.h`, `src/sys/storyboard/storyboard_canvas_view.cpp`, `src/sys/storyboard/storyboard_canvas_view.h`, `src/sys/audio/player_preview_presenter.cpp`, `include/wormedit/core/storyboard_canvas.h`, `src/core/storyboard_canvas.cpp`, `src/ui/nuklear/storyboard/canvas.cpp`, `tests/core/storyboard_canvas_tests.cpp`, `tests/sys/storyboard_canvas_view_tests.cpp`, `tests/ui/storyboard_canvas_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Extract reusable storyboard evaluation/image resolution from the player presenter after OSBX-3; both consumers share the image-quad drawing path and capacity policy.
  2. Project the 640×480 canvas at shared transport time with fit/zoom/pan, safe-area outlines and view-only background controls. Use the shared aggregate 1024-quad budget with canvas-first reservation and explicit player overflow.
  3. Hit-test inverse world matrices against cached image dimensions; topmost wins and Alt-click cycles stacked hits. Select groups through the hierarchy; canvas hits select recorded sprite IDs.
  4. Draw transformed selection outlines/origins and group child bounds; test rotated/sheared/negative-scale hits, zoom/pan mapping and click routing, then inspect an isolated Xvfb capture.
- **Invariants:** One evaluator/presenter, union image request and complete-frame texture pin boundary; zoom/pan/selection/hidden IDs are canonical project view state. Hidden IDs clear/reconcile on storyboard/session changes and never alter authored visibility.
- **Reject/failure behavior:** Singular matrices and missing metadata cannot yield false hits; missing images use existing placeholders and overflow is visible.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths.
- **Out of scope:** Gizmo mutation (SB-7), second decoder/evaluator (none needed; reuse existing owners).

## SB-4 Hierarchy and layers panel

- **Status:** blocked-on SB-1, SB-2
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** left dock lists layers (background, fail, pass, foreground) with each layer's node tree in draw order. Selection is shared with the canvas.
- **Read first:** `src/ui/nuklear/storyboard/storyboard_panel.h`, `include/wormedit/core/osbx_edit.h`, `src/ui/frontend/storyboard.h`.
- **Files owned:** `src/sys/storyboard/storyboard_hierarchy_view.cpp`, `src/sys/storyboard/storyboard_hierarchy_view.h`, `src/ui/nuklear/storyboard/hierarchy.cpp`, `tests/sys/storyboard_hierarchy_tests.cpp`, `tests/ui/storyboard_hierarchy_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Project Background/Fail/Pass/Foreground node trees in draw order, sharing selection with the canvas; show missing-image reasons and OSBX-7 unsupported-export warnings once available.
  2. Wire add group/sprite, delete, inline rename, same-layer reorder/reparent and explicit subtree layer move through SB-1; editor visibility is view-only.
  3. Route arrows, F2, Delete and Ctrl+D within hierarchy focus; duplication uses SB-1 fresh IDs. Test projected order, select/rename/delete and drag routing.
- **Invariants:** Per-layer draw order and stable selection identities survive projection.
- **Reject/failure behavior:** Cross-layer reparent drops refuse with a reason; OSB/generated targets expose read-only reasons.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Out of scope:** Conditional simulation (SB-13), conversion/detach commands (SB-10/SB-11).

## SB-5 Inspector: node properties and the ordered transform stack

- **Status:** blocked-on SB-1, SB-2, SB-10
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** the right dock edits the selected node.
- **Read first:** `include/wormedit/core/osbx_edit.h`, `docs/osbx-draft-spec.md`, `src/ui/nuklear/storyboard/storyboard_panel.h`.
- **Files owned:** `src/sys/storyboard/storyboard_inspector_view.cpp`, `src/sys/storyboard/storyboard_inspector_view.h`, `src/ui/nuklear/storyboard/inspector.cpp`, `tests/sys/storyboard_inspector_tests.cpp`, `tests/ui/storyboard_inspector_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Expose ID, image picker (SB-8), nine origins, layer, blend, order and the ordered translate/rotate/scale/skew/matrix/pivot stack; add/remove/reorder ops and show read-only evaluated world matrix. Expose the optional inclusive lifetime range and explicit Clear action through SB-10; key edits preserve it unless the user changes it explicitly.
  2. Edit the evaluated value at the playhead: keyed ops/tracks auto-key there; unkeyed ones change the default. Apply the same rule to opacity/color.
  3. Commit numeric Enter/blur once; field-label scrubbing is one cancellable gesture. Show OSB/owner read-only reasons and Convert/Detach actions as those commands land.
  4. Test default versus auto-key, noncommuting op reorder and field commit/cancel routing.
- **Invariants:** Transform order remains visible and authoritative; shared transaction/gesture owners publish edits.
- **Reject/failure behavior:** Invalid values reject atomically with diagnostics; read-only targets cannot mutate via numeric fields.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Out of scope:** Lua-link controls (SB-14), gizmos (SB-7).

## SB-6 Keyframe timeline dock

- **Status:** blocked-on SB-1, SB-2
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** the bottom dock shows rows for the selected node(s): one per transform op (nested for pivots) and per property track, with keys as markers, easing shown between keys, the shared playhead, and measure/beat lines from the chart timing.
- **Read first:** `include/wormedit/core/snap.h`, `include/wormedit/core/osbx_edit.h`, `src/ui/frontend/storyboard.h`, `docs/frontend-event-harness.md`.
- **Files owned:** `src/sys/storyboard/storyboard_timeline_view.cpp`, `src/sys/storyboard/storyboard_timeline_view.h`, `src/ui/nuklear/storyboard/timeline.cpp`, `tests/sys/storyboard_timeline_tests.cpp`, `tests/ui/storyboard_timeline_event_harness_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Project one row per selected node op/track, nested pivots, key markers, easing segments, shared playhead and chart measure/beat lines; support horizontal zoom/scroll.
  2. Wire ruler seek, click/box selection, K/add from evaluated value, Delete and easing picker (step/linear/OSB/cubic Bézier with curve previews).
  3. Stage multi-key time drags with start ghosts and target readouts: ms default, beat/grid via `snap.h`, Shift exact, one Undo, Escape restores. Refuse duplicate times within a track.
  4. Cache by stamp/selection and reuse warm storage; test row order, time mapping, snapping, refused drops, warm allocations and drag/cancel/delete/shortcut routing; inspect isolated capture.
- **Invariants:** Signed integer-ms keys; selection IDs and the shared transport/snap owner remain authoritative.
- **Reject/failure behavior:** Occupied destinations or invalid times refuse the whole drag with a reason and retain the original keys.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths.
- **Out of scope:** Frame/MIDI/anchor snapping and clip UI (later scope); scripted bands (SB-14).

## SB-7 Canvas gizmos: move, rotate, scale, pivot

- **Status:** done (2026-11-02; parent SOURCE48 acceptance)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** direct manipulation of the selected sprite or group on the canvas.
- **Read first:** `include/wormedit/core/osbx_edit.h`, `src/sys/storyboard/storyboard_canvas_view.h`, `src/sys/storyboard/storyboard_inspector_view.h`, `docs/architecture.md`.
- **Files owned:** `include/wormedit/core/osbx_edit.h`, `include/wormedit/core/osbx_evaluator.h`, `include/wormedit/core/osbx_lua_link.h`, `include/wormedit/core/storyboard_gizmo.h`, `src/core/osbx_edit.cpp`, `src/core/osbx_evaluator.cpp`, `src/core/osbx_lua_link.cpp`, `src/core/storyboard_gizmo.cpp`, `src/sys/audio/player_preview_presenter.cpp`, `src/sys/storyboard/storyboard_gizmo_view.cpp`, `src/sys/storyboard/storyboard_gizmo_view.h`, `src/sys/storyboard/storyboard_scene_presenter.cpp`, `src/sys/storyboard/storyboard_scene_presenter.h`, `src/ui/frontend/storyboard.h`, `src/ui/frontend/storyboard_gizmo.h`, `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/storyboard/canvas.cpp`, `src/ui/nuklear/storyboard/canvas.h`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `tests/core/osbx_edit_tests.cpp`, `tests/core/osbx_evaluator_tests.cpp`, `tests/core/storyboard_gizmo_tests.cpp`, `tests/sys/storyboard_gizmo_tests.cpp`, `tests/ui/storyboard_gizmo_event_harness_tests.cpp`. Coordinator-only common registration: `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Released supporting scope:** The coordinator admitted the listed core, projection/runtime, frontend and regression support for this outcome. All source slots are released; a later task needs a new named reservation. Common lists and coherent verification remain coordinator-owned.
- **Steps:**
  1. Target the first matching translate/rotate/scale op; if absent, offer to append it with status feedback. Draw pivot handles for pivot ops.
  2. Map drag deltas through inverse(parent world × preceding ops); use SB-5 auto-key/default rules and shared gesture dispatch. Shift constrains axis, 15° rotation or uniform scale.
  3. Test rotated/sheared parent mapping, constraints, one-step Undo and exact Escape cancellation, then inspect an isolated gizmo capture.
- **Invariants:** A gizmo changes one specific op; starting a drag pauses playback via SB-9.
- **Reject/failure behavior:** Noninvertible transforms refuse safely; cancellation restores the exact starting state.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths.
- **Verification (2026-11-02):** Its distinct SOURCE48 service GATE passed build, all 6,332 tests, whitespace and 215 changed-C++ complexity comparisons, with unchanged allowances. The cohort also passed raw checks on 220 units, five extra header consumers and all 836 event-harness cases. Current isolated Translate preview changes the canvas and Escape restores exact canvas pixels; rotated/sheared-parent constraints, Undo/Redo, stale/arena/media cases pass. Evidence: `/tmp/sb7-source48-acceptance.json`, `/tmp/cohort48-gizmo-preview-pixel-review.json`. Actual GATE logs: `/tmp/wormedit-gate.gwYOQX`.
- **Out of scope:** Raw matrix/skew gizmos (inspector-only in v1); lifecycle plumbing remains in shared runners.

## SB-8 Storyboard image assets panel

- **Status:** done (2026-11-02; parent SOURCE48 acceptance)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** a panel (tab in the left dock) lists the storyboard's OSBX images table with thumbnails and usage counts, and the song's importable image resources.
- **Read first:** `src/sys/resources/bga_dock_runtime.cpp`, `src/ui/nuklear/compose/compose_bga_panel.cpp`, `include/wormedit/core/osbx_edit.h`, `src/ui/frontend/storyboard.h`.
- **Files owned:** `include/wormedit/core/osbx_edit.h`, `include/wormedit/core/resource_file_copy.h`, `src/core/osbx_edit.cpp`, `src/core/resource_assignment.cpp`, `src/core/resource_assignment_internal.h`, `src/core/resource_file_copy.cpp`, `src/sys/app/application.cpp`, `src/sys/app/application_command_state.h`, `src/sys/app/application_frame_services.cpp`, `src/sys/app/application_frame_services.h`, `src/sys/audio/player_preview_presenter.cpp`, `src/sys/resources/image_import_probe.cpp`, `src/sys/resources/image_import_probe.h`, `src/sys/resources/resource_job_state.cpp`, `src/sys/resources/resource_job_state.h`, `src/sys/storyboard/storyboard_assets_view.cpp`, `src/sys/storyboard/storyboard_assets_view.h`, `src/sys/storyboard/storyboard_runtime.cpp`, `src/sys/storyboard/storyboard_runtime.h`, `src/sys/storyboard/storyboard_scene_presenter.cpp`, `src/ui/frontend/storyboard.h`, `src/ui/frontend/storyboard_assets.h`, `src/ui/frontend/timing_preview.h`, `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/audio/player_preview_texture.cpp`, `src/ui/nuklear/audio/player_preview_texture.h`, `src/ui/nuklear/compose/compose_bga_panel.cpp`, `src/ui/nuklear/compose/compose_bga_panel.h`, `src/ui/nuklear/storyboard/assets.cpp`, `src/ui/nuklear/storyboard/assets.h`, `src/ui/nuklear/storyboard/canvas.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.h`, `tests/core/osbx_edit_tests.cpp`, `tests/sys/player_preview_presenter_tests.cpp`, `tests/sys/resource_notification_tests.cpp`, `tests/sys/storyboard_assets_tests.cpp`, `tests/ui/nuklear_player_preview_texture_tests.cpp`, `tests/ui/storyboard_assets_event_harness_tests.cpp`. Coordinator-only common registration: `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Released supporting scope:** The coordinator admitted the listed core, projection/runtime, frontend and regression support for this outcome. All source slots are released; a later task needs a new named reservation. Common lists and coherent verification remain coordinator-owned.
- **Steps:**
  1. List OSBX images, cached thumbnails, usage counts, missing files and importable song images in the left Assets tab, following BGA/resource list style.
  2. Add existing image entries or import through the existing resource-import path; drag onto canvas or Add sprite creates a sprite at the drop in one Undo step.
  3. Remove only unused images, listing users on refusal; test usage/removal behavior and drag-to-canvas routing.
- **Invariants:** Reuse the resource importer and thumbnail/texture cache; no second decoder.
- **Reject/failure behavior:** Used-image removal rejects with user IDs; failed import publishes no partial storyboard edit.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Verification (2026-11-02):** Its distinct SOURCE48 service GATE passed build, all 6,332 tests, whitespace and 215 changed-C++ complexity comparisons, with unchanged allowances. The cohort also passed raw checks on 220 units, five extra header consumers and all 836 event-harness cases. Actual 1280×720 and 800×720 captures show thumbnails and usage counts. Import/relink/image/sprite transactions and the real 256 MiB pin-pressure fixture pass. Historical 39 owned plus 14 consumed paths retain their separate provenance. Evidence: `/tmp/sb8-source48-acceptance.json`. Actual GATE logs: `/tmp/wormedit-gate.533loj`.
- **Out of scope:** Obtain coordinator approval for resource-import entry-point support; no replacement importer is needed.

## SB-9 Transport and preview controls on the Storyboard tab

- **Status:** done (2026-11-02; parent SOURCE48 acceptance)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** play/pause, scrub, loop region, playback rate and audio work on the Storyboard tab through the shared transport (the same clock Compose and the player preview use: "one deterministic scene evaluator supplies both interactive preview and offline frames").
- **Read first:** `src/sys/audio/transport_player_actions.cpp`, `src/sys/audio/audio_transport_frame.cpp`, `src/sys/storyboard/storyboard_canvas_view.h`, `src/ui/frontend/workspace.h`.
- **Files owned:** `src/sys/app/application.cpp`, `src/sys/app/application_command_state.h`, `src/sys/app/application_frame_view.cpp`, `src/sys/audio/chart_preview_state.cpp`, `src/sys/audio/chart_preview_state.h`, `src/sys/storyboard/storyboard_runtime.cpp`, `src/sys/storyboard/storyboard_runtime.h`, `src/sys/storyboard/storyboard_transport_view.cpp`, `src/sys/storyboard/storyboard_transport_view.h`, `src/ui/frontend/storyboard.h`, `src/ui/frontend/storyboard_transport.h`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/transport.cpp`, `src/ui/nuklear/storyboard/transport.h`, `tests/sys/storyboard_transport_tests.cpp`, `tests/ui/storyboard_transport_event_harness_tests.cpp`. Coordinator-only common registration: `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Released supporting scope:** The coordinator admitted the listed core, projection/runtime, frontend and regression support for this outcome. All source slots are released; a later task needs a new named reservation. Common lists and coherent verification remain coordinator-owned.
- **Steps:**
  1. Wire existing play/pause, ruler scrub, timeline-selection loop, rate and audio controls to shared transport; expose time and bar/beat readouts.
  2. Pause playback when an edit gesture starts; canvas time always follows the shared transport. Optionally reuse the player note layer for an overlay.
  3. Test Space/transport shortcuts and focus isolation plus equality of canvas/transport time.
- **Invariants:** One clock and audio path across Compose, player preview and Storyboard.
- **Reject/failure behavior:** Invalid seek/loop inputs retain transport state and diagnose; edit gestures follow shared rejection/cancel rules.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`.
- **Verification (2026-11-02):** Its distinct SOURCE48 service GATE passed build, all 6,332 tests, whitespace and 215 changed-C++ complexity comparisons, with unchanged allowances. The cohort also passed raw checks on 220 units, five extra header consumers and all 836 event-harness cases. Shared-clock, selection-loop, rate, seek, audio and gesture-pause workflows pass; current 16-path hashes were rechecked. Evidence: `/tmp/sb9-source48-acceptance.json`, `/tmp/cohort48-sb9-owned-source-ack.json`. Actual GATE logs: `/tmp/wormedit-gate.dDa55b`.
- **Out of scope:** Simulation (SB-13), video/frame snapping (video tasks); shared-transport support needs coordinator approval.

## SB-10 Convert an imported OSB storyboard to OSBX

- **Status:** partial (implementation and focused verification complete; parent GATE/review pending)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** an explicit command turns an OSB-model storyboard (imported from .osu/.osb) into an equivalent OSBX document so it can be edited. It is one Undo step and creates a new OSBX storyboard; the original stays until the user deletes it (owner decision, SB-0 #2).
- **Read first:** `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `include/wormedit/core/osbx_document.h`, `include/wormedit/core/osbx_edit.h`.
- **Files owned:** `include/wormedit/core/osb_to_osbx.h` (sole public conversion boundary), `src/core/osb_to_osbx.cpp`, `tests/core/osb_to_osbx_tests.cpp`, `src/core/osbx_edit.cpp`, `include/wormedit/core/osbx_edit.h`, `include/wormedit/core/osbx_document.h`, `src/core/osbx_document.cpp`, `src/core/osbx_evaluator.cpp`, `include/wormedit/core/document.h`, `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_osbx_tests.cpp`, `tests/formats/native_project_tests.cpp` (parent schema-guard correction only), `src/core/osbx_osb_export.cpp`, `tests/core/osbx_osb_export_tests.cpp`, `tests/core/osbx_document_tests.cpp`, `tests/core/osbx_evaluator_tests.cpp`, `src/core/storyboard_preview.cpp`, `docs/osbx-draft-spec.md`, this card, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Supporting scope (coordinator, 2026-11-01):** exact fractional lifetime requires a node-owned optional range, shared evaluator gate, schema-30 codec/migration and finite flat export clipping. The extra paths above cover that fidelity gap and the single OSB/display-title layer catalog, not a second clock/evaluator or new Lua language tokens. Common CMake edits/builds still require the shared slot.
- **Steps:**
  1. Implement explicit Convert to OSBX as one transaction creating a new storyboard and preserving the original; map sprite layer/origin/image and M/MX/MY, S/V, R, F, C families to ops/tracks.
  2. Preserve scale → rotate → translate semantics, command start/end times, easing and hold gaps. Record a lossless P blend/flip mapping before enabling it.
  3. For loops use OSBX-1c when available; otherwise bounded unrolling or explicit refusal with sprite IDs. Refuse animations and retained unknown lines with line numbers.
  4. Compare original `evaluate_storyboard_property` with converted OSBX at sampled fixture times within float tolerance; test copy retention, failures and Undo.
- **Done so far:** core converter, identity-bearing shared command result, explicit lifetime set/clear edit and schema/evaluator/export integration. Loops and retained P/trigger/animation directives refuse; overlapping interrupted curves and discontinuous shared endpoints refuse rather than approximate. `apply_osbx_edit_with_result` returns the new reserved ID only after successful create/convert; the original remains unchanged.
- **Remaining:** parent full GATE and independent review/finalization. SB-5 provides inspector controls for the explicit lifetime; no commit or publication was made by this worker.
- **Focused evidence (2026-11-01):** admitted `cmake --build build --target wormedit_core_tests wormedit_osbx_evaluator_tests wormedit_osbx_osb_export_tests --parallel 4` passed; subsequent test-helper rebuild of the core/export targets also passed. `build/tests/wormedit_core_tests '[osb-to-osbx],[osbx],[osbx-edit],[storyboard],[schema-guard]' --reporter compact`: 141 cases / 7042 assertions passed. Dedicated evaluator: 23 / 336; exporter: 28 / 1642. Logs: `/tmp/sb10-focused-build.log`, `/tmp/sb10-core-focused.log`, `/tmp/sb10-evaluator-focused.log`, `/tmp/sb10-export-focused.log`.
- **Analysis evidence:** raw `clang-tidy -p build <cpp> --extra-arg=-Wno-unknown-warning-option` completed successfully for all 17 affected/explicit header-consuming translation units; the compatibility argument matches the existing checker and suppresses only unknown GCC warning options. `scripts/check-complexity.py --build-dir build --jobs 4 <same 17 cpp paths>` passed 17/17, with no baseline change. Both used `PATH=/tmp/wormedit-llvm-23.1.3/usr/bin:$PATH` and `LD_LIBRARY_PATH=/tmp/wormedit-llvm-23.1.3/usr/lib`. Paths/commands: `/tmp/sb10-tidy/results.json`, `/tmp/sb10-complexity-command.json`; strict log `/tmp/sb10-complexity.log`, detailed logs `/tmp/wormedit-complexity.t94kzv_v`.
- **Verified fixes:** finite constant-lifetime export exposed the bake metric's dense-array assumption; timestamp lookup now also handles endpoint-only samples. Direct native delta replay retains lifetime; the journal may legitimately reseed small string-ID scenes, so only the new recovery fixture allows that existing policy while prior require-delta checks remain mandatory. New lifetime containment and reparse/activity test helpers preserve assertions while keeping each new function below the cognitive-complexity limit.
- **Consumer audit:** prepared Lua scenes copy whole nodes and compare whole source values (`src/core/osbx_lua_link.cpp`); native storyboard deltas use the canonical codec (`src/formats/native_project_delta.cpp`). Lifetime is retained without additional consumer edits; journal roundtrip coverage exercises the latter.
- **Invariants:** Conversion is explicit, loss-aware and keeps the source until separately deleted.
- **Reject/failure behavior:** Unsupported content or exhausted unrolling limits rejects conversion without publishing a partial copy.
- **Verify:** GATE.
- **Out of scope:** implicit conversion during selection; general native OSBX persistence remains OSBX-4b, with only the exact lifetime extension above owned here. Broader interrupted-curve mapping and P/loop/animation support require later lossless policies. Inspector controls belong to SB-5.

## SB-11 Detach a generator-owned storyboard

- **Status:** done (2026-11-02; parent SOURCE56 acceptance)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** "Detach from generator" clears the OSBX-4a owner reference and removes the matching generator_storyboard_binding_t from the recipe in one Undo step, making the storyboard editable. A later regeneration then creates a new storyboard instead of overwriting the detached one (or rejects with a key clash; decide and record).
- **Read first:** `include/wormedit/core/osbx_edit.h`, `include/wormedit/core/generator.h`, `src/core/generator.cpp`, `docs/osbx-draft-spec.md`.
- **Files owned:** `include/wormedit/core/osbx_edit.h`, `src/core/osbx_edit.cpp`, `src/ui/frontend/storyboard.h`, `src/sys/project/workspace_view.cpp`, `src/sys/storyboard/storyboard_runtime.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.h`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/inspector.cpp`, `src/ui/nuklear/storyboard/hierarchy.cpp`, `src/ui/nuklear/storyboard/timeline.cpp`, `tests/core/osbx_edit_tests.cpp`, `tests/core/generator_tests.cpp`, `tests/formats/native_project_generator_tests.cpp`, `tests/ui/storyboard_tab_event_harness_tests.cpp`.
- **Current ownership:** All 14 source writers are released and acknowledged in `/tmp/sb11-source49-parent-ack.json`; clips and SB-13 later received separate serialized successor slots. One typed per-board transaction clears only the captured owner and matching binding, and one shared banner/action serves the three docks. Existing generator allocation and native persistence supply the fresh regeneration identity. No continuing source admission is granted.
- **Regeneration decision:** The next generation creates a new history-reserved storyboard identity and preserves the detached authored storyboard, including after Undo/Redo and save/reload.
- **Steps:**
  1. Clear the OSBX-4a owner reference and matching `generator_storyboard_binding_t` together through one SB-1 transaction.
  2. Record regeneration policy before implementation: new output must create a fresh storyboard or explicitly reject a key clash; it cannot overwrite detached edits.
  3. Expose owner/reason and Detach from inspector, hierarchy and timeline using the existing projected action seam; coordinate supporting UI paths with their owners.
  4. Test detach/Undo, regeneration after detach and save/reload.
- **Invariants:** Detach preserves materialized nodes/keys and clears both ownership records atomically.
- **Reject/failure behavior:** Missing/stale binding rejects atomically; regeneration never silently reclaims detached output.
- **Verify:** GATE.
- **Out of scope:** Override patches (outside v1); generator Apply itself belongs to OSBX-6.
- **Verification (2026-11-02):** Its distinct SOURCE56 service GATE passed build, all 6,425 tests, whitespace and 218 changed-C++ complexity comparisons without raised allowances. The coherent cohort also passed all 5,595 tests with all four codecs disabled and complete 841-unit header-consumer complexity coverage. Original SOURCE55 complexity failed 14 test functions; eight test-only helper refactors preserve all assertions and pass, while 833 unchanged units retain exact-input evidence. Source and protected fixture hashes stayed fixed. See `/tmp/cohort56-service-acceptance.json` and `/tmp/cohort56-parent-verification.json`. Core/native and all-three-dock event cases cover stale/ambiguous refusal, one Undo/Redo, history-reserved regeneration and save/reload. Real isolated 1280/800 Detach/Undo/Redo captures were inspected; current binary equivalence is `/tmp/cohort56-visual-equivalence.json`. Long-module clipping in the narrow hierarchy banner remains an explicit SB-12 final-layout follow-up, not a claim of final screen acceptance. Logs: `/tmp/wormedit-gate.ObtAHT`; receipt: `/tmp/sb11-source56-finalization.json`.

## SB-12 Storyboard screen acceptance, captures and manual chapter

- **Status:** done locally (2026-11-02; SOURCE71 GATE, workflow, matrix and manual verified; formal final receipts are in Nightshift)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **Outcome:** proof that the screen works end to end and user documentation for it.
- **Read first:** `docs/frontend-event-harness.md`, `docs/chart-editor-plan/tasks/storyboard-screen.md`, `docs/manual/index.adoc`, `scripts/smoke-compose-virtual-x.sh`.
- **Files owned:** `tests/ui/storyboard_workflow_event_harness_tests.cpp`, `tests/sys/storyboard_workflow_tests.cpp`, `docs/manual/chapters/storyboards.adoc`, `docs/manual/index.adoc`, `docs/manual/shots/storyboards.sh`, `docs/chart-editor-plan/tasks/storyboard-screen.md`, `docs/chart-editor-plan/stages/later.md`, `memory/chart-editor-status/implemented.md`, `memory/chart-editor-status/resume.md`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Run event-harness/sys workflow: new storyboard → image → group/sprite → reparent → rotate keys at 0/2000 ms with easing → gizmo move → Undo/Redo → save/load → osu! bake report; cover read-only conversion and generator detach.
  2. Capture populated screen, selected gizmo and keyed timeline at 1280×720 and 800 px width using isolated Xvfb and the smoke script STEPS hook. Inspect images, compare mockup hierarchy/banners/rows, and record paths/deviations.
  3. Add the Storyboards manual chapter with revision-stamped captures using existing manual tooling; build it without warnings.
  4. Update card/stage/status evidence after acceptance; restore only the tracked `.agent` song fixture changes made by ctest.
- **Invariants:** Acceptance proves the integrated behavior with shared audio transport and one-step Undo/cancel.
- **Reject/failure behavior:** Any missing test binary, failed gate, visual defect or manual warning leaves acceptance incomplete.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths; `ctest --test-dir build --output-on-failure -R '^documentation\.'`; `cmake --build build --target manual --parallel 8` must finish without warnings.
- **Out of scope:** SB-13 simulation does not block this card; SB-14 links and video export are separate follow-ups.
- **Current coordination:** Begin a fresh managed read-only preflight and request exact source/test ownership before writes. Include the observed 800 px hierarchy generator-module clipping at the shared `src/ui/nuklear/storyboard/storyboard_panel.cpp` owner, reusing existing text wrapping/measurement and one shared action. Add a meaningful UI bounds regression and fresh 1280/800 captures. `/tmp/sb12-parent-narrow-banner-preflight.json` records the finding. Parent retains CMake/status/common docs unless explicitly handed over. Preserve the unrelated dirty `.agent` fixture at its saved parent hash; never restore HEAD over it.
- **Current source-only reservation (2026-11-02):** Current managed attempt `318d47fb` owns exactly existing `src/ui/nuklear/storyboard/storyboard_panel.cpp` and new `tests/ui/storyboard_workflow_event_harness_tests.cpp` plus `tests/sys/storyboard_workflow_tests.cpp`, with current before hashes/new-path absence in `/tmp/sb12-source3-admission-before.json`. These three paths are disjoint from both active OSBX source grants. Reuse measured wrapping in the one shared ownership banner, preserving captured Detach/scenario/Lua routes, and author independent bounds plus production workflow tests first. No header/API, CMake/target, docs/status or execution permission is granted. Parent retains registration, manual/captures and final coherent verification. Existing warmed-frame allocation behavior and project/module/width/font layout validity must be preserved.

- **Parent integration evidence (2026-11-02):** All three source slots are released and two test files registered in existing targets. SOURCE67 full build and all6,515 tests pass, including the real renderer/runtime workflow through reparent,0/2000ms keys,Out easing,gizmo,Undo/Redo,native save/load and actual bake report. Actual720px selected-track capture exposed clipping after the Lua-link toolbar appeared; a new800/1280 event regression failed at both widths before the narrow timeline-minimum fix and passes afterwards. Parent supporting `src/ui/nuklear/storyboard/timeline.cpp` captures the grid tooltip's own bounds so its disabled tooltip does not occlude the Easing popup; all guards remain. Four test-only complexity helper refactors preserve assertions, and fresh six-TU raw/strict checks pass without raised limits. Inspected `/tmp/cohort70-storyboard-selected-1280.png` and `-800.png` show selected gizmo and keyed timeline; generated owner/module/Detach captures at both widths are `/tmp/cohort64-generated-1280.png` and `-800.png`. Compared with the mockup: same pane hierarchy, existing global transport replaces the duplicate toolbar, fixed Neither occupies the scenario slot, narrow pane labels are compact and inspector/hierarchy scroll normally. The schema39 canonical-transaction teaching fixture uses original art and actual referenced audio; its real atomic CLI export succeeds with a baked-object diagnostic and reimport validation (`/tmp/cohort68-fixture-export-teaching.json`). The initial dense fixture refused within unchanged density limits and is retained as a failed attempt. Standard manual tooling produced `docs/manual/images/storyboards.png` stamped064fd9f+dirty; SOURCE70 six documentation tests and manual/LDoc build pass without warnings. Final matrix/GATE/task/goal acceptance remain pending.

- **Local acceptance verified (2026-11-02):** The complete SOURCE71 GATE passes build, all 6,515 tests, whitespace and all 221 changed-C++ complexity comparisons (`/tmp/cohort71-osbx1c-service-gate.json`, `/tmp/wormedit-gate.0fb7O1`). Both configurations are verified: the all-disabled Lua/four-codec build passes all 5,490 tests and a full 724-TU raw/strict complexity scan. Enabled header coverage is 837 unchanged passing units plus six fresh rechecks after four test-only helper refactors; no allowance changed. Actual 1280/800 captures, 853 event-harness cases, six documentation checks, warning-free manual/LDoc and real atomic tutorial/media export proofs pass. All source writers are released and the unrelated dirty fixture is unchanged. SOURCE72 changes only these acceptance records; feature C++/headers and manual/API content remain identical. Independent managed task GATEs, formal goal transitions and publication receipts are recorded in Nightshift; this source entry does not itself complete a task or publish a change.

## SB-13 Pass/fail and trigger simulation in the storyboard preview

- **Status:** done for required fixed choices (2026-11-02; parent SOURCE56 acceptance); optional Custom remains deferred
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** the Storyboard tab and the player preview can show the conditional layers the way a player would see them. One typed choice drives preview, video export (VID-0 #7) and the StepMania adapters (SMX-2/3/4), so every output agrees.
- **Read first:** `docs/osbx-draft-spec.md`, `include/wormedit/core/storyboard.h`, `include/wormedit/core/osbx_evaluator.h`, `src/sys/audio/player_preview_presenter.cpp`.
- **Files owned:** `src/ui/frontend/storyboard.h`, `src/ui/frontend/storyboard_hierarchy.h`, `src/sys/storyboard/storyboard_runtime.h`, `src/sys/storyboard/storyboard_runtime.cpp`, `src/sys/storyboard/storyboard_scene_presenter.h`, `src/sys/storyboard/storyboard_scene_presenter.cpp`, `src/sys/storyboard/storyboard_canvas_view.h`, `src/sys/storyboard/storyboard_canvas_view.cpp`, `src/sys/storyboard/storyboard_hierarchy_view.cpp`, `src/sys/audio/player_preview_presenter.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/hierarchy.cpp`, `src/ui/nuklear/audio/player_preview.h`, `src/ui/nuklear/audio/player_preview.cpp`, `src/ui/nuklear/app/frontend.cpp`; new `src/ui/nuklear/storyboard/scenario.h`, new `src/ui/nuklear/storyboard/scenario.cpp`, new `tests/sys/storyboard_scenario_tests.cpp`, new `tests/ui/storyboard_scenario_event_harness_tests.cpp`.
- **Current ownership:** The actual 19-path source continuation is released and accepted; the earlier read-only preflight was interrupted, never counted as implementation. Parent registered the shared scenario widget and two test units in existing targets. One project runtime owns transient default-Pass state; both selectors share a widget/action route and hierarchy flags are derived per frame. Explicit video/SM export policies remain unchanged on the same core enum. No released source slot reopens.
- **Steps:**
  1. Required after SB-9/T-154: reuse or extend the core conditional-layer choice Pass/Fail/Neither (default Pass) as an explicit OSB/OSBX evaluator input shared with VID-1 and SMX-2/3/4. Neither hides both conditional layers.
  2. Required: add Storyboard/player selectors as view state and mark inactive hierarchy layers “not shown in this state”. Coordinate core/evaluator ownership with T-153/T-154; obtain approval for any additional shared-choice header.
  3. Optional later, after T-153 and OSBX-1c: add Custom scenario switch markers and Passing/Failing/HitSound* trigger firings. Video/SM output still uses a single pick.
  4. Test all three required visibility states in both evaluators, selector routing and inspected same-frame Pass/Fail Xvfb captures; if Custom lands, test a Passing trigger at its switch time.
- **Invariants:** One core choice across preview and exports; required pick-one behavior precedes optional Custom.
- **Reject/failure behavior:** Invalid scenarios diagnose without corrupting documents or replacing the last valid preview state.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths.
- **Out of scope:** Outside v1 and never a prerequisite of SB-12; video encoding/profiles stay with VID/T-155.
- **Verification (2026-11-02):** Its distinct SOURCE56 service GATE passed build, all 6,425 tests, whitespace and 218 changed-C++ complexity comparisons without raised allowances. The coherent cohort also passed all 5,595 tests with all four codecs disabled and complete 841-unit header-consumer complexity coverage. Original SOURCE55 complexity failed 14 test functions; eight test-only helper refactors preserve all assertions and pass, while 833 unchanged units retain exact-input evidence. Source and protected fixture hashes stayed fixed. See `/tmp/cohort56-service-acceptance.json` and `/tmp/cohort56-parent-verification.json`. All 17 new sys/event cases pass. Actual same-time Pass/Fail art at 1280/800, overlay Fail, and hidden-tab Neither persistence were inspected in `/tmp/cohort55-scenario-visual-review.json`; current executable bytes match exactly. Logs: `/tmp/wormedit-gate.gTCXGj`; receipt: `/tmp/sb13-source56-finalization.json`. Custom/trigger timelines are not claimed.

## SB-14 Link to Lua from the inspector and timeline

- **Status:** done (2026-11-02; parent SOURCE48 acceptance)
- **Mockup:** [storyboard-screen.svg](../mockups/storyboard-screen.svg) (reference: match its structure and hierarchy)
- **States:** [storyboard-states.svg](../mockups/storyboard-states.svg) (read-only, key drag, conditional layers and Lua links as applicable).
- **Outcome:** Inspector, timeline and hierarchy expose OSBX-12 hybrid Lua links with explicit bake/unlink and read-only scripted output.
- **Read first:** `docs/osbx-draft-spec.md`, `src/sys/storyboard/storyboard_inspector_view.h`, `src/sys/storyboard/storyboard_timeline_view.h`, `docs/chart-editor-plan/tasks/osbx.md`.
- **Files owned:** `include/wormedit/core/lua_source.h`, `include/wormedit/core/lua_vocabulary.h`, `include/wormedit/core/osbx_document.h`, `include/wormedit/core/osbx_edit.h`, `include/wormedit/core/osbx_lua_link.h`, `src/core/osbx_edit.cpp`, `src/core/osbx_lua_link.cpp`, `src/sys/app/application.cpp`, `src/sys/app/application_command_state.h`, `src/sys/app/application_frame_view.cpp`, `src/sys/audio/player_preview_presenter.cpp`, `src/sys/storyboard/osbx_lua_link_runtime.cpp`, `src/sys/storyboard/osbx_lua_link_runtime.h`, `src/sys/storyboard/storyboard_gizmo_view.cpp`, `src/sys/storyboard/storyboard_hierarchy_view.cpp`, `src/sys/storyboard/storyboard_inspector_view.cpp`, `src/sys/storyboard/storyboard_inspector_view.h`, `src/sys/storyboard/storyboard_runtime.cpp`, `src/sys/storyboard/storyboard_runtime.h`, `src/sys/storyboard/storyboard_timeline_view.cpp`, `src/ui/frontend/storyboard.h`, `src/ui/frontend/storyboard_hierarchy.h`, `src/ui/frontend/storyboard_inspector.h`, `src/ui/frontend/storyboard_lua_links.h`, `src/ui/frontend/storyboard_timeline.h`, `src/ui/nuklear/app/frontend.cpp`, `src/ui/nuklear/storyboard/hierarchy.cpp`, `src/ui/nuklear/storyboard/inspector.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.cpp`, `src/ui/nuklear/storyboard/storyboard_panel.h`, `src/ui/nuklear/storyboard/timeline.cpp`, `tests/core/osbx_edit_tests.cpp`, `tests/lua/osbx_lua_link_tests.cpp`, `tests/sys/osbx_lua_link_runtime_tests.cpp`, `tests/sys/storyboard_gizmo_tests.cpp`, `tests/sys/storyboard_lua_links_tests.cpp`, `tests/ui/storyboard_lua_links_event_harness_tests.cpp`. Coordinator-only common registration: `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Released supporting scope:** The coordinator admitted the listed core, projection/runtime, frontend and regression support for this outcome. All source slots are released; a later task needs a new named reservation. Common lists and coherent verification remain coordinator-owned.
- **Steps:**
  1. After OSBX-12, add Link to Lua per op/track: module/entry, named parameters, seed and range (selected key span or whole storyboard); show ƒ, module:entry, Reload frozen source/hash, file:line errors, Bake to keys and Unlink.
  2. Add Fill from Lua for groups and Detach children; mark scripted children read-only with ƒ and a reason.
  3. Draw linked timeline ranges as hatched bands with sampled dots; refuse dragging samples with “bake or unlink to edit”, preserving hand keys outside the range.
  4. Test projected rows/bands and link/reload/bake/unlink routing, last-good-result retention and worker-only Lua execution following T-191; inspect isolated capture.
- **Invariants:** Hybrid links use OSBX-12 ownership; Lua never runs on the frame thread and errors retain the last good result.
- **Reject/failure behavior:** Failed reload/evaluation keeps prior output; linked ranges require explicit bake/unlink before direct editing.
- **Verify:** GATE; `ctest --test-dir build --output-on-failure -L event-harness`; inspect isolated Xvfb capture(s) and record paths.
- **Verification (2026-11-02):** Its distinct SOURCE48 service GATE passed build, all 6,332 tests, whitespace and 215 changed-C++ complexity comparisons, with unchanged allowances. The cohort also passed raw checks on 220 units, five extra header consumers and all 836 event-harness cases. Range/last-good/source/metadata/rebase and Link/Reload/Bake/Unlink/Fill/Detach workflows pass. Actual current form and Escape cancellation were inspected; one async shell and poll own evaluation and bounded source reload. Evidence: `/tmp/sb14-source48-acceptance.json`, `/tmp/cohort48-lua-visual-review.json`. Actual GATE logs: `/tmp/wormedit-gate.lzuO3R`.
- **Out of scope:** Generator-owned override patches (outside v1); core link execution belongs to OSBX-12. Mockup generator edits require coordinator approval.
