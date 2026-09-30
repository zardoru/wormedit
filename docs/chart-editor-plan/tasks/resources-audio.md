# Task cards: resources and audio

[Back to the task-card index](../tasks.md).

Stage 3 resource and stage 4 audio/BMSON leftovers from
[authoring/export](../stages/authoring-export.md). Placement, assignment,
replacement, auto-increment, asset relink, BMSON restart/continue, razor cuts,
and source-registry reconciliation already exist; do not rebuild them. Apply the
stale-plan check (rule 9) first.

## T-100 Add a protected resource-delete command

- **Status:** ready
- **Outcome:** a core command deletes one catalog resource only when no note, BGM, BGA, or sound-channel event references it; otherwise it rejects with the reference count.
- **Read first:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `include/wormedit/core/document.h` (resource catalog), `tests/core/resource_assignment_tests.cpp`.
- **Files owned:** `include/wormedit/core/resource_assignment.h`, `src/core/resource_assignment.cpp`, `tests/core/resource_assignment_tests.cpp`.
- **Steps:**
  1. `apply_resource_deletion(history, revision, resource_id)`; count references in one pass.
  2. Tests: unused deletes and undo restores; used rejects with the exact count; stale revision rejects.
- **Invariants:** the underlying asset file and asset record are untouched; BMS identifiers of other resources are not renumbered.
- **Reject/failure behavior:** any reference rejects with no history entry.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** cascading delete, UI (T-101).

## T-101 Add Delete to the resource catalog

- **Status:** blocked-on T-100
- **Outcome:** each Compose catalog row has a Delete button, enabled only for unreferenced resources; referenced rows show their use count in the tooltip.
- **Read first:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp`, `src/sys/compose_actions.cpp`.
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/sys/compose_resource_presenter.cpp/.h`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `tests/core/compose_resource_presenter_tests.cpp`.
- **Steps:** 1. Project a per-row reference count. 2. Wire Delete through the standard pattern.
- **Invariants:** an armed resource that is deleted is disarmed.
- **Reject/failure behavior:** core rejection shown in the status line.
- **Verify:** standard build/test.
- **Out of scope:** usage navigation (T-102).

## T-102 Navigate to a resource's uses

- **Status:** blocked-on T-101
- **Outcome:** a "Find uses" button on a catalog row selects every note/BGM/BGA event using it and moves the cursor to the first; Next/Previous cycle through them.
- **Read first:** `src/sys/compose_resource_presenter.cpp`, `include/wormedit/core/interaction.h` (typed selection), `src/sys/compose_actions.cpp`.
- **Files owned:** `src/sys/compose_actions.cpp`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/frontend.h`.
- **Steps:** 1. Collect uses in chart order (bounded to 50,000). 2. Replace the selection; jump cursor. 3. Next/Previous buttons.
- **Invariants:** no document change.
- **Reject/failure behavior:** zero uses → button disabled.
- **Verify:** standard build/test.
- **Out of scope:** uses in other charts (show count only).

## T-103 Filter the catalog by use state and name

- **Status:** ready
- **Outcome:** the catalog has a text filter (case-insensitive substring on name/path/BMS ID) and a Used/Unused/Missing/All selector in addition to the existing role tabs.
- **Read first:** `src/ui/nuklear/compose_resources.cpp` (`filter_role`, `filter_name`), `src/sys/compose_resource_presenter.cpp`, `docs/chart-editor-plan/document-model.md` (resource list paragraph).
- **Files owned:** `src/ui/nuklear/compose_resources.cpp`, `src/ui/nuklear/compose_state.cpp/.h`, `tests/ui/nuklear_compose_state_tests.cpp`.
- **Steps:** 1. Add filter text and use-state to frontend state. 2. Apply both when iterating rows. 3. State tests for the filter predicate.
- **Invariants:** armed resource stays armed even when filtered out.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^frontend-state\.'`.
- **Out of scope:** sorting.

## T-104 Show BMS audio fallback candidates for missing files

- **Status:** ready
- **Outcome:** a missing `#WAV` row shows which fallback file was chosen, or lists ambiguous candidates and lets the user pick one through the existing relink command.
- **Read first:** `docs/references/bms-audio-fallback.md`, `src/sys/audio_source_registration.cpp`, `include/wormedit/core/resource_assignment.h` (`apply_resource_asset_relink`), `src/ui/nuklear/compose_resources.cpp`.
- **Files owned:** `src/sys/audio_source_registration.cpp/.h`, `src/sys/compose_resource_presenter.cpp/.h`, `src/ui/nuklear/compose_resources.cpp`, `src/ui/frontend.h`, `tests/sys/audio_source_registration_tests.cpp`.
- **Steps:** 1. Expose the resolution outcome (exact, fallback path, ambiguous list, corrupt) per resource. 2. Show it in the row tooltip; ambiguous rows get a picker that calls relink.
- **Invariants:** the authored `#WAV` token never changes unless the user picks.
- **Reject/failure behavior:** corrupt exact files are shown as corrupt, not replaced.
- **Verify:** standard build/test.
- **Out of scope:** image fallback.

## T-105 Audit BMSON slice collision and final-slice rules

- **Status:** ready
- **Outcome:** a test file proves each rule in the BMSON slicing reference (same-pulse restart/continue conflict, collision layering, open-ended final slice) or the report lists the rule that fails.
- **Read first:** `docs/references/bmsone-slicing.md`, `include/wormedit/core/sound_slice.h`, `tests/core/bmson_sound_channel_editor_tests.cpp`, `tests/fixtures/bmson/`.
- **Files owned:** new `tests/core/sound_slice_rules_tests.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. One `TEST_CASE` per numbered rule in the reference. 2. Do not change production code; if a rule fails, STOP and report it with the failing expectation.
- **Invariants:** tests only.
- **Reject/failure behavior:** a failing rule becomes a new card, not a fix here.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sound_slice'`.
- **Out of scope:** fixing failures.

## T-106 Render a selected interval to PCM offline

- **Status:** ready
- **Outcome:** a core function mixes the exact selected chart interval (music + BGM + keysounds) into an in-memory float32 stereo buffer using the same preview schedule and mixer as live playback, deterministically.
- **Read first:** `include/wormedit/audio/preview_session.h`, `include/wormedit/audio/preview_bridge.h`, `docs/timing-preview-scheduling.md`, `tests/core/audio_preview_session_tests.cpp`.
- **Files owned:** new `include/wormedit/audio/offline_mix.h`, new `src/audio/offline_mix.cpp`, new `tests/core/audio_offline_mix_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Drive a preview session with a synthetic callback loop from `begin_frame` to `end_frame`; decode synchronously (no realtime thread).
  2. Apply a linear fade-out over the last N ms (parameter).
  3. Tests: identical output on two runs; length equals the frame span; fade reaches zero.
- **Invariants:** a byte/frame cap rejects oversized intervals; supports a stop token for cancellation.
- **Reject/failure behavior:** missing sources are reported, not silently skipped.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^audio\.'`.
- **Out of scope:** encoding (T-107).

## T-107 Choose the preview-audio encoder

- **Status:** blocked-on-user
- **Outcome:** a recorded choice of encoder for `preview.ogg` (for example libvorbis via FetchContent, or shelling out to `ffmpeg`), then a follow-up card for encode + atomic publish + Compose menu action.
- **Read first:** `cmake/dependencies/`, `docs/chart-editor-plan/stages/authoring-export.md` (stage 4 `preview.ogg` bullet).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP: no Ogg encoder is vendored; ask which dependency is acceptable. 2. Record the answer and add the follow-up card.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.
