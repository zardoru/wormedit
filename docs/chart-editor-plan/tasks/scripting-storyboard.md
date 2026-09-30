# Task cards: storyboard, Lua, MIDI, SV recipes, video (stage 7)

[Back to the task-card index](../tasks.md).

Stage 7 from [later work](../stages/later.md) and the
[OSBX draft](../../osbx-draft-spec.md). Several items wait on open questions 9,
13, and 16; those cards are `blocked-on-user`. Apply the stale-plan check
(rule 9) first.

## T-150 Parse and emit OSB Animation objects

- **Status:** ready
- **Outcome:** `Animation,layer,origin,"path",x,y,frameCount,frameDelay,loopType` headers parse into a structured object with the same command children as Sprite, and emit back identically.
- **Read first:** `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `tests/core/storyboard_tests.cpp`.
- **Files owned:** `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `src/formats/native_project.cpp` (storyboard JSON), `tests/core/storyboard_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Tests first: parse/emit/reparse of an Animation with nested loop. 2. Model fields; `LoopForever`/`LoopOnce` enum. 3. Native JSON read/write with older-record compatibility.
- **Invariants:** unknown directives stay retained lines.
- **Reject/failure behavior:** malformed headers stay retained raw lines with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'storyboard|native_project'`.
- **Out of scope:** drawing animations (T-152).

## T-151 Parse and emit OSB Sample directives

- **Status:** ready
- **Outcome:** `Sample,time,layer,"path",volume` lines become structured objects scheduled on the preview clock and emitted back identically.
- **Read first:** as T-150, plus `include/wormedit/core/storyboard_preview.h`.
- **Files owned:** as T-150, plus `src/core/storyboard_preview.cpp`, `tests/core/storyboard_preview_tests.cpp`.
- **Steps:** 1. Tests first. 2. Model and codec. 3. Expose samples in the preview schedule (audio playback itself is a later card).
- **Invariants:** as T-150.
- **Reject/failure behavior:** as T-150.
- **Verify:** as T-150.
- **Out of scope:** mixing samples into playback.

## T-152 Draw Animation frames in the player preview

- **Status:** blocked-on T-150
- **Outcome:** preview selects the correct frame path (`name0.png`, `name1.png`, …) from time, delay, and loop type.
- **Read first:** `src/core/storyboard_preview.cpp`, `src/sys/player_preview_presenter.cpp`.
- **Files owned:** `src/core/storyboard_preview.cpp`, `tests/core/storyboard_preview_tests.cpp`, `src/sys/player_preview_presenter.cpp`.
- **Steps:** 1. Pure frame-index function with tests for LoopOnce end and LoopForever wrap. 2. Resolve frame path through the resource catalog.
- **Invariants:** exact millisecond math.
- **Reject/failure behavior:** missing frame image shows the missing placeholder.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'storyboard'`.
- **Out of scope:** trigger blocks.

## T-153 Model OSB trigger blocks

- **Status:** ready
- **Outcome:** `T,triggerName,start,end` blocks with nested commands parse structurally instead of rejecting, and emit back identically; preview ignores them unless T-154 lands.
- **Read first:** as T-150.
- **Files owned:** as T-150.
- **Steps:** 1. Tests first with a HitSound and a Passing trigger. 2. Structural node like loops.
- **Invariants:** as T-150.
- **Reject/failure behavior:** as T-150.
- **Verify:** as T-150.
- **Out of scope:** evaluating triggers.

## T-154 Simulate Pass/Fail state in storyboard preview

- **Status:** blocked-on T-153
- **Outcome:** a preview toggle chooses Pass or Fail; the Pass/Fail layers and `Passing`/`Failing` triggers follow it.
- **Read first:** `src/core/storyboard_preview.cpp`, `src/sys/player_preview_presenter.cpp`, `src/ui/nuklear/player_preview.cpp`.
- **Files owned:** those three files, `src/ui/frontend.h`, `tests/core/storyboard_preview_tests.cpp`.
- **Steps:** 1. State parameter on evaluation. 2. Toggle in the preview overlay.
- **Invariants:** default is Pass.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test.
- **Out of scope:** HitSound triggers.

## T-155 Decide the storyboard-editor and video-export scope

- **Status:** blocked-on-user
- **Outcome:** answers to open questions 9 (standalone `.osb` vs `[Events]`), 16 (video profiles), and the OSBX syntax items in `memory/chart-editor-status/open-decisions.md`; then cards for the Storyboard tab, OSBX DSL, storyboard-to-video export, and the area-selection **Export player-preview video** action.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/osbx-draft-spec.md`, `docs/chart-editor-plan/playback/player-preview.md` (video export section).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add the follow-up cards (the video exporter card is blocked on the storyboard-to-video card).
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-156 Create a sandboxed, bounded Lua state

- **Status:** ready
- **Outcome:** a core wrapper creates a Lua 5.4 state with only `base` (minus `dofile`/`loadfile`/`load` of binary chunks), `string`, `table`, `math`, `utf8`; enforces an instruction budget and memory cap; returns typed errors.
- **Read first:** `tests/lua/lua_runtime_tests.cpp`, `cmake/dependencies/lua.cmake`, `tests/CMakeLists.txt` (`wormedit_lua_tests`).
- **Files owned:** new `include/wormedit/scripting/lua_sandbox.h`, new `src/scripting/lua_sandbox.cpp`, new `tests/lua/lua_sandbox_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Custom allocator with a byte cap. 2. `lua_sethook` count hook for the instruction budget. 3. Tests: `io`/`os` absent, infinite loop stops with budget error, 100 MB allocation fails cleanly, `math.random` replaced by a seeded deterministic generator.
- **Invariants:** deterministic across platforms for a fixed seed.
- **Reject/failure behavior:** every failure closes the state and returns an error; no longjmp across C++ frames outside `lua_pcall`.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Out of scope:** document API (T-158).

## T-157 Import Standard MIDI files as assets

- **Status:** ready
- **Outcome:** a bounded SMF type 0/1 reader produces exact tick-positioned notes, tempo, meter, markers, and controller events stored as a project MIDI asset.
- **Read first:** `include/wormedit/core/document.h` (MIDI extension records), `docs/chart-editor-plan/formats-persistence.md` (MIDI paragraph).
- **Files owned:** new `include/wormedit/formats/midi.h`, new `src/formats/midi.cpp`, new `tests/formats/midi_tests.cpp`, new fixtures under `tests/fixtures/midi/` (generated in-test or tiny hand-written bytes), `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Tests first with byte arrays in the test file. 2. Parse header, tracks, running status, meta events. 3. Limits on tracks/events/bytes.
- **Invariants:** tick positions exact; unknown meta events retained.
- **Reject/failure behavior:** truncated files reject transactionally.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'midi'`.
- **Out of scope:** pitch bend/aftertouch/SysEx exposure (open question 13).

## T-158 Give Lua read-only access to the document

- **Status:** blocked-on T-156
- **Outcome:** a versioned `wormedit` Lua table exposes selection, measures, timing, lane profile, and MIDI asset data read-only (API version 1).
- **Read first:** `include/wormedit/scripting/lua_sandbox.h`, `docs/chart-editor-plan/formats-persistence.md` (Lua paragraphs), `docs/chart-editor-plan/decisions/open-questions.md` (question 13).
- **Files owned:** new `src/scripting/lua_document_api.cpp/.h`, new `tests/lua/lua_document_api_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Userdata/table snapshots built before the script runs (no live pointers). 2. Exact rationals as `{num, den}` tables. 3. Tests for each accessor.
- **Invariants:** scripts cannot mutate the live document.
- **Reject/failure behavior:** accessing beyond limits raises a Lua error.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Out of scope:** emitting edits (T-159).

## T-159 Run a Lua generator as a previewable command batch

- **Status:** blocked-on T-158
- **Outcome:** a script returns note/layer proposals; the editor shows them as ghosts; Apply commits one command, Cancel discards.
- **Read first:** `src/scripting/lua_document_api.h`, `src/core/interaction.cpp` (ghost proposals).
- **Files owned:** new `src/scripting/lua_generator.cpp/.h`, new `tests/lua/lua_generator_tests.cpp`, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/compose_inspector.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Validate every proposal like normal placement. 2. Golden test: fixed script + seed → identical batch. 3. UI: script path field, Run, Apply, Cancel.
- **Invariants:** fixed seed → identical output on all platforms.
- **Reject/failure behavior:** any invalid proposal rejects the batch with the first error.
- **Verify:** standard build/test.
- **Out of scope:** recipe persistence (T-160).

## T-160 Persist generator recipes

- **Status:** blocked-on T-159
- **Outcome:** a generator run can be saved as a recipe (script hash, inputs, parameters, seed, materialized output IDs); re-running is explicit and previewed.
- **Read first:** `include/wormedit/core/document.h` (generator-recipe records), `src/formats/native_project.cpp`.
- **Files owned:** `src/scripting/lua_generator.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`, `tests/lua/lua_generator_tests.cpp`.
- **Steps:** 1. Fill the existing record type. 2. Regenerate shows a diff preview (added/removed counts).
- **Invariants:** never regenerates automatically.
- **Reject/failure behavior:** script hash mismatch warns before running.
- **Verify:** standard build/test.
- **Out of scope:** storyboard generators.

## T-161 Manage SV recipes in a list

- **Status:** ready
- **Outcome:** the Timing pane lists the chart's SV/SCROLLS/SPEEDS recipes with Enable toggle, Duplicate, Delete, and Up/Down reorder, each one command.
- **Read first:** `include/wormedit/core/sv_recipe.h`, `src/ui/nuklear/timing_panel.cpp` (recipe form), `src/sys/timing_edit_runtime.cpp`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/sys/timing_edit_runtime.cpp`, `src/sys/timing_view.cpp`, `src/ui/frontend.h`, `src/ui/nuklear/timing_panel.cpp`.
- **Steps:** 1. Core commands (enable, duplicate, delete, move) with tests. 2. List UI.
- **Invariants:** composition order equals list order.
- **Reject/failure behavior:** stale revision rejects.
- **Verify:** standard build/test.
- **Out of scope:** impulses (T-162).

## T-162 Decide SV impulse policies

- **Status:** blocked-on-user
- **Outcome:** answers for per-target finite-pulse policy for zero-width impulses (sampling defaults are proposed in T-180; normalization is T-190), then a card for `curve.dirac`.
- **Read first:** `memory/chart-editor-status/open-decisions.md`, `docs/chart-editor-plan/editing/interactions.md` (SV recipe paragraphs).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add follow-up cards.
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.
