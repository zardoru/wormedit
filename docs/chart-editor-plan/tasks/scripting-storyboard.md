# Task cards: storyboard, Lua, MIDI, SV recipes, video (stage 7)

[Back to the task-card index](../tasks.md).

> **Deferred group (user decision, 2026-09-30):** these are late-stage ambitions, not MVP work. Every card here is `deferred`; do not start one unless the user explicitly asks for it by ID.

Stage 7 from [later work](../stages/later.md) and the
[OSBX draft](../../osbx-draft-spec.md). Several items wait on open questions 9,
13, and 16; those cards are `blocked-on-user`. Apply the stale-plan check
(rule 9) first.

## T-150 Parse and emit OSB Animation objects

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** `Animation,layer,origin,"path",x,y,frameCount,frameDelay,loopType` headers parse into a structured object with the same command children as Sprite, and emit back identically.
- **Read first:** `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `tests/core/storyboard_tests.cpp`.
- **Files owned:** `include/wormedit/core/storyboard.h`, `src/core/storyboard.cpp`, `src/formats/native_project.cpp` (storyboard JSON), `tests/core/storyboard_tests.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. Tests first: parse/emit/reparse of an Animation with nested loop. 2. Model fields; `LoopForever`/`LoopOnce` enum. 3. Native JSON read/write with older-record compatibility.
- **Invariants:** unknown directives stay retained lines.
- **Reject/failure behavior:** malformed headers stay retained raw lines with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'storyboard|native_project'`.
- **Out of scope:** drawing animations (T-152).

## T-151 Parse and emit OSB Sample directives

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** `Sample,time,layer,"path",volume` lines become structured objects scheduled on the preview clock and emitted back identically.
- **Read first:** as T-150, plus `include/wormedit/core/storyboard_preview.h`.
- **Files owned:** as T-150, plus `src/core/storyboard_preview.cpp`, `tests/core/storyboard_preview_tests.cpp`.
- **Steps:** 1. Tests first. 2. Model and codec. 3. Expose samples in the preview schedule (audio playback itself is a later card).
- **Invariants:** as T-150.
- **Reject/failure behavior:** as T-150.
- **Verify:** as T-150.
- **Out of scope:** mixing samples into playback.

## T-152 Draw Animation frames in the player preview

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-150)
- **Outcome:** preview selects the correct frame path (`name0.png`, `name1.png`, …) from time, delay, and loop type.
- **Read first:** `src/core/storyboard_preview.cpp`, `src/sys/audio/player_preview_presenter.cpp`.
- **Files owned:** `src/core/storyboard_preview.cpp`, `tests/core/storyboard_preview_tests.cpp`, `src/sys/audio/player_preview_presenter.cpp`.
- **Steps:** 1. Pure frame-index function with tests for LoopOnce end and LoopForever wrap. 2. Resolve frame path through the resource catalog.
- **Invariants:** exact millisecond math.
- **Reject/failure behavior:** missing frame image shows the missing placeholder.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'storyboard'`.
- **Out of scope:** trigger blocks.

## T-153 Model OSB trigger blocks

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** `T,triggerName,start,end` blocks with nested commands parse structurally instead of rejecting, and emit back identically; preview ignores them unless T-154 lands.
- **Read first:** as T-150.
- **Files owned:** as T-150.
- **Steps:** 1. Tests first with a HitSound and a Passing trigger. 2. Structural node like loops.
- **Invariants:** as T-150.
- **Reject/failure behavior:** as T-150.
- **Verify:** as T-150.
- **Out of scope:** evaluating triggers.

## T-154 Simulate Pass/Fail state in storyboard preview

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-153)
- **Outcome:** a preview toggle chooses Pass or Fail; the Pass/Fail layers and `Passing`/`Failing` triggers follow it.
- **Read first:** `src/core/storyboard_preview.cpp`, `src/sys/audio/player_preview_presenter.cpp`, `src/ui/nuklear/audio/player_preview.cpp`.
- **Files owned:** those three files, `src/ui/frontend.h`, `tests/core/storyboard_preview_tests.cpp`.
- **Steps:** 1. State parameter on evaluation. 2. Toggle in the preview overlay.
- **Invariants:** default is Pass.
- **Reject/failure behavior:** none.
- **Verify:** standard build/test.
- **Out of scope:** HitSound triggers.

## T-155 Decide the storyboard-editor and video-export scope

- **Status:** deferred (stage 7–8 ambition; was: blocked-on-user)
- **Outcome:** answers to open questions 9 (standalone `.osb` vs `[Events]`), 16 (video profiles), and the OSBX syntax items in `memory/chart-editor-status/open-decisions.md`; then cards for the Storyboard tab, OSBX DSL, storyboard-to-video export, and the area-selection **Export player-preview video** action.
- **Read first:** `docs/chart-editor-plan/decisions/open-questions.md`, `docs/osbx-draft-spec.md`, `docs/chart-editor-plan/playback/player-preview.md` (video export section).
- **Files owned:** `docs/chart-editor-plan/decisions/open-questions.md`, this file.
- **Steps:** 1. STOP and ask. 2. Record; add the follow-up cards (the video exporter card is blocked on the storyboard-to-video card).
- **Invariants:** none.
- **Reject/failure behavior:** none.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-156 Create a sandboxed, bounded Lua state

- **Status:** done (2026-10-24)
- **Outcome:** a core wrapper creates a Lua 5.4 state with only `base` (minus `dofile`/`loadfile`/`load` of binary chunks), `string`, `table`, `math`, `utf8`; enforces an instruction budget and memory cap; returns typed errors.
- **Read first:** `tests/lua/lua_runtime_tests.cpp`, `cmake/dependencies/lua.cmake`, `tests/CMakeLists.txt` (`wormedit_lua_tests`).
- **Files owned:** new `include/wormedit/scripting/lua_sandbox.h`, new `src/scripting/lua_sandbox.cpp`, new `tests/lua/lua_sandbox_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/architecture.md`, `memory/chart-editor-status/resume.md`, `docs/chart-editor-plan/tasks/osbx.md`, this file.
- **Implementation reconciliation:** the wrapper owns one move-only evaluation session. Allocated bytes, VM instructions and cancellation cover setup and every subsequent evaluation; Lua globals/closures may survive successful calls but any failure closes the session. Independent sessions start from their configured seed. Only scalar results cross the wrapper; document bindings and recipe integration remain later cards. In addition to file loading, omit host output (`print`/`warn`), GC control and metatable access/installation: user finalizers can execute while Lua disables instruction hooks. Dynamic `load` accepts bounded text strings only. A protected `xpcall` adapter invokes error handlers after hook delivery is restored, rather than inside an exhausted count hook. The RNG uses explicit portable integer arithmetic; VM instruction limits are not a wall-clock timeout for standard-library C functions.
- **Steps:** 1. Custom allocator with a byte cap. 2. `lua_sethook` count hook for the instruction budget. 3. Tests: `io`/`os` absent, infinite loop stops with budget error, 100 MB allocation fails cleanly, `math.random` replaced by a seeded deterministic generator.
- **Session direction (owner, 2026-10-18):** T-189 may retain closure state across cycles of one recipe build. Sandbox memory/instruction/cancellation bounds cover that complete evaluation session; independent builds start fresh. See [extension checkpoints](extension-friction.md).
- **Invariants:** deterministic RNG across platforms for a fixed seed; successful evaluations share one bounded session, and fresh sessions start independently. As specified by Lua, `next`/`pairs` iteration order is unspecified; reproducible scripts use ordered iteration. Standard-library transcendental math retains the platform libm precision contract, not a new bit-exact math implementation.
- **Reject/failure behavior:** every failure closes the state and returns an error; no longjmp across C++ frames outside `lua_pcall`.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Implementation:** `wormedit::scripting` is built only with `WORMEDIT_ENABLE_LUA=ON`. The opaque move-only session has no public raw-state access. Its default limits are 8 MiB allocated capacity (including allocator headers), one million VM instructions and 64 KiB source/result text. All setup, parsing, calls and error handlers use protected Lua boundaries; callbacks contain only trivial automatic values. Seeded unsigned SplitMix64 arithmetic supplies floats and unbiased inclusive integer ranges, including the full signed domain. Failures close the VM and retain typed diagnostics/usage; successful calls retain globals and closures without resetting budgets.
- **Verification:** Lua-enabled GCC GATE passes **4,887/4,887**, including **20 Lua cases**. The focused sandbox suite passes **160 assertions / 18 cases**. AddressSanitizer, LeakSanitizer and UndefinedBehaviorSanitizer pass **167 assertions / 20 cases**, with both the wrapper and pinned Lua C runtime instrumented. The Linux Clang preset builds the affected Lua target and passes the same **167 / 20**; its linker retains the upstream `tmpnam` warning from Lua’s unopened OS library. Changed-C++ complexity passes for both new TUs; a separate three-TU pass includes the existing pinned-runtime witness. No complexity or execution allowances increased. Logs: `/tmp/t156-gate-1.log` (details `/tmp/wormedit-gate.dhywRW`), `/tmp/t156-focused-2.log`, `/tmp/t156-asan.log`, `/tmp/t156-complexity-2.log`, `/tmp/t156-clang-build.log`, `/tmp/t156-clang-tests.log`.
- **Bounded test review:** reviewed all 18 new sandbox bodies against `c5227ad`: library/host isolation, scalar results, plain/caught infinite loops, both xpcall handler paths and ordinary results, direct/caught memory-cap failure, partial startup cleanup, cumulative closure sessions, RNG golden values/ranges/independence, dynamic/top-level binary rejection and text environments, typed syntax/nonstring errors, diagnostic/result caps, source/config limits, cooperative cancellation and move-only callback lifetime. The two raw pinned-runtime bodies remain independent witnesses and are retained, not newly reviewed for suite-wide value. Adversarial Lua tests have a ten-second CTest timeout.
- **Handoff:** T-189 is ready after this sandbox and the completed T-681 prepared-session work; no recipe evaluator, document API or UI is activated here.
- **Out of scope:** document API (T-158).

## T-157 Import Standard MIDI files as assets

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** a bounded SMF type 0/1 reader produces exact tick-positioned notes, tempo, meter, markers, and controller events stored as a project MIDI asset.
- **Read first:** `include/wormedit/core/document.h` (MIDI extension records), `docs/chart-editor-plan/formats-persistence.md` (MIDI paragraph).
- **Files owned:** new `include/wormedit/formats/midi.h`, new `src/formats/midi.cpp`, new `tests/formats/midi_tests.cpp`, new fixtures under `tests/fixtures/midi/` (generated in-test or tiny hand-written bytes), `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Tests first with byte arrays in the test file. 2. Parse header, tracks, running status, meta events. 3. Limits on tracks/events/bytes.
- **Invariants:** tick positions exact; unknown meta events retained.
- **Reject/failure behavior:** truncated files reject transactionally.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'midi'`.
- **Out of scope:** pitch bend/aftertouch/SysEx exposure (open question 13).

## T-158 Give Lua read-only access to the document

- **Status:** done (2026-10-24)
- **Outcome:** a versioned `wormedit` Lua table exposes selection, measures, timing, lane profile, and MIDI asset data read-only (API version 1).
- **Read first:** `include/wormedit/scripting/lua_sandbox.h`, `docs/chart-editor-plan/formats-persistence.md` (Lua paragraphs), `docs/chart-editor-plan/decisions/open-questions.md` (question 13).
- **Files owned:** new `src/scripting/lua_document_api.cpp/.h`, new `tests/lua/lua_document_api_tests.cpp`, `include/wormedit/scripting/lua_sandbox.h`, `src/scripting/lua_sandbox.cpp`, `tests/lua/lua_sandbox_tests.cpp`, `src/scripting/lua_curve.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Parallel ownership (2026-10-24):** document API agent owns the new API, sandbox protected snapshot/structured-result support and Lua curve snapshot-factory reuse. Coordinator owns build/docs wiring. Baseline MIDI only; unanswered question 13 continues to exclude extra MIDI categories.
- **Steps:** 1. Userdata/table snapshots built before the script runs (no live pointers). 2. Exact rationals as `{num, den}` tables. 3. Tests for each accessor.
- **Invariants:** scripts cannot mutate the live document.
- **Reject/failure behavior:** accessing beyond limits raises a Lua error.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Implementation:** owned recursively immutable snapshots preserve exact IDs/rationals and bounded baseline MIDI access. Structured extraction rejects unsupported/cyclic/nonfinite results within the shared sandbox budget. Focused Lua and ASan/LSan/UBSan pass 497 assertions / 68 cases across the integrated batch; T-158 complexity passes five explicit translation units.
- **Out of scope:** emitting edits (T-159).

## T-159 Run a Lua generator as a previewable command batch

- **Status:** done (2026-10-24)
- **Completion:** Keyed note/layer proposals use the shared proposal and staging runners, normal placement/layer validation and history-safe IDs. Explicit asynchronous Run produces ordinary scene-index ghosts; Apply is one Undo step and Cancel/stale results cannot publish. Worker-owned BMS caches are isolated and chart views are reacquired after validation; the queued regression fails with isolation removed. Integrated GATE passes 5,008/5,008, complexity passes 42 changed TUs plus six explicit header consumers, and the linux-clang preset builds. Runtime coverage passes 54 assertions / 6 cases; real generator event workflows pass 48 / 3. No complexity allowances increased; two reviewed limits decreased.
- **Outcome:** a script returns note/layer proposals; the editor shows them as ghosts; Apply commits one command, Cancel discards.
- **Read first:** `src/scripting/lua_document_api.h`, `src/core/interaction.cpp` (ghost proposals).
- **Files owned:** new `include/wormedit/core/generator.h`, new `include/wormedit/core/generator_types.h` (lightweight shared limits/counts), `src/core/generator.cpp`, `tests/core/generator_tests.cpp`, new `src/scripting/lua_generator.cpp/.h`, new `tests/lua/lua_generator_tests.cpp`, new `src/sys/compose/generator_runtime.h/.cpp`, new `src/sys/compose/actions/compose_generator_actions.cpp`, new `src/sys/compose/views/generator_view.cpp`, new `src/ui/nuklear/compose/compose_generator_form.cpp/.h`, `src/core/id_ceiling.h`, `src/core/chart_editor.cpp` (shared history identity maximum), `src/core/interaction_internal.h` (shared hold-lane availability), `include/wormedit/core/command.h`, `src/core/command.cpp` (shared const snapshot inspection and semantic-target-preserving command preview), `src/core/bms_program_edit.cpp` (history-safe explicit insertion identities through shared native/layer adapters), `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, Compose action route/header and panel/ghost projection/render owners, `src/sys/app/application.cpp`, `src/ui/nuklear/app/frontend.cpp` (project-session draft/picker reset), `include/wormedit/core/semantic_layer_editor.h`, `src/core/semantic_layer_editor.cpp` (shared definition validation), `src/sys/project/workspace_view.h/.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `cmake/complexity_baseline.txt` (reviewed decreases only), focused frontend/event regressions and manual/docs. Coordinator-approved supporting ownership replaces obsolete pre-split paths.
- **Parallel ownership (2026-10-24):** generator agent owns core/script/runtime/UI and dedicated tests. Root owns schema/model/native integration and build/docs. Document API agent owns sandbox; shared source/header freezes coordinate checks. Single selected chart and exact BMS scope/Chart target; general multi-difficulty DSL stays deferred.
- **Steps:** 1. Validate every proposal like normal placement. 2. Golden test: fixed script + seed → identical batch. 3. UI: script path field, Run, Apply, Cancel.
- **Invariants:** fixed seed → identical output on all platforms.
- **Reject/failure behavior:** any invalid proposal rejects the batch with the first error.
- **Verify:** GATE
- **Out of scope:** recipe persistence (T-160).

## T-160 Persist generator recipes

- **Status:** done (2026-10-24)
- **Completion:** Native schema19 retains frozen source/hash, typed input hashes, parameters/seed, exact scoped target and stable keyed historical witnesses. Explicit regeneration previews added/changed/removed counts and rejects edited/deleted outputs; Detach keeps ordinary objects. Legacy schema18 metadata migrates without evaluation or field loss. Persistence, shared Apply/Undo/Redo, hash acknowledgement and session-reset coverage pass in the integrated 5,008-test GATE; Lua and ASan/LSan/UBSan pass 497 assertions / 68 cases. Lua-disabled runtime links without Lua and passes 9 / 2; four disabled consumers compile. The manual documents the full flow.
- **Outcome:** a generator run can be saved as a recipe (script hash, inputs, parameters, seed, materialized output IDs); re-running is explicit and previewed.
- **Read first:** `include/wormedit/core/document.h` (generator-recipe records), `src/formats/native_project.cpp`.
- **Files owned:** `src/scripting/lua_generator.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`, `tests/lua/lua_generator_tests.cpp`. Supporting ownership: `include/wormedit/core/document.h`, `src/core/document.cpp`, `src/formats/native_project_migrations.cpp`, new `tests/formats/native_project_generator_tests.cpp`, T-159 typed core/runtime/inspector units and tests, `src/sys/project/project_save_paths.cpp` if needed for explicit reload origins, `CMakeLists.txt`, `tests/CMakeLists.txt`, docs/manual/status.
- **Storage contract (2026-10-24):** schema19 follows impulse schema18. Input identities/hashes share one ordered record array. Frozen source, target chart/optional scoped owner and keyed historical note/layer materialization witnesses persist; live notes remain editable. Explicit regenerate compares the witness, previews counts and rejects edited/deleted outputs rather than silently overwriting them. Detach removes recipe ownership while retaining ordinary objects. Legacy records migrate as unrunnable metadata without losing source path/parameters/seed/hash; no load/save evaluation.
- **Steps:** 1. Fill the existing record type. 2. Regenerate shows a diff preview (added/removed counts).
- **Invariants:** never regenerates automatically.
- **Reject/failure behavior:** script hash mismatch warns before running.
- **Verify:** GATE
- **Out of scope:** storyboard generators.

## T-161 Manage SV recipes in a list

- **Status:** done (2026-10-23)
- **Done so far:** Recipe spans in Compose have an Enable/Disable action and Delete action, each
  routed through the shared edit transaction with stale identity. Selection and editing live in the
  Compose SV lane and inspector; the old Timing pane no longer exists.
- **Implementation scope (2026-10-23):** coordinator extends ownership to `src/sys/compose/input/compose_timing_input.cpp`, `src/sys/compose/views/compose_marker_view.cpp`, `src/sys/compose/views/compose_scene_view.cpp`, and `memory/chart-editor-status/resume.md` for existing input routing, full-chart ordered selector projection/reset and completion evidence. The Compose inspector selector lists stored evaluation order including overlapping/offscreen recipes, with Earlier/Later controls; no separate Timing pane.
- **Completion (2026-10-23):** the inspector lists all chart recipes in stored evaluation order, including overlapping/offscreen spans; Duplicate preserves the complete definition beside its source with a fresh global ID, and Earlier/Later publish one undoable move with disabled boundary buttons. The context menu also offers Duplicate. Existing stamped input and edit runners reject stale, unknown, missing, full-capacity and exhausted-ID requests. GATE passes **4,577/4,577**, whitespace and **9-TU** complexity without increased allowances (`/tmp/t161-gate3.log`). Focused core/input/frontend families pass **702/107/34 assertions** in **70/6/2 cases**. Isolated native Duplicate/save and Undo/save prove adjacent IDs 7001/7003/7002 then 7001/7002, complete definitions and unchanged eight-note/BPM content (`/tmp/t161-native-duplicated.png`). The shared imported-source edit flag changes normally. Eight new test bodies were reviewed within this card; broader test review remains T-644.
- **Outcome:** the Compose recipe workflow exposes Enable, Duplicate, Delete and explicit
  composition-order controls for the chart's SV/SCROLLS/SPEEDS recipes, each as one command.
- **Read first:** `include/wormedit/core/sv_recipe.h`, `src/sys/compose/actions/compose_recipe_actions.cpp`,
  `src/ui/nuklear/compose/compose_recipe_menu.cpp`, `src/ui/nuklear/compose/compose_recipe_form.cpp`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`,
  `tests/core/sv_recipe_tests.cpp`, `src/sys/compose/actions/compose_recipe_actions.cpp`,
  `src/ui/frontend/compose.h`, `src/ui/nuklear/compose/compose_recipe_menu.cpp`,
  `src/ui/nuklear/compose/compose_recipe_form.cpp`, `tests/sys/frame_timing_tests.cpp`,
  `tests/sys/compose_action_notification_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`,
  `src/sys/compose/input/compose_timing_input.cpp`, `src/sys/compose/views/compose_marker_view.cpp`,
  `src/sys/compose/views/compose_scene_view.cpp`, `src/ui/nuklear/compose/compose_recipe_form.h`,
  `memory/chart-editor-status/resume.md`, this card.
- **Steps:** 1. Stale-plan check: choose a compact order affordance for overlapping Compose spans.
  2. Add core Duplicate and move commands with tests. 3. Wire the actions through the existing
  recipe command path and cover stale identity, one Undo and disabled boundary moves.
- **Invariants:** The displayed order equals stored evaluation order; commands preserve recipe
  contents, spans and generated-event semantics outside the requested operation.
- **Reject/failure behavior:** Stale revision or missing identity rejects without changing order.
- **Verify:** standard build/test.
- **Out of scope:** impulses (T-162).

## T-162 Add SV impulses as finite boxes

- **Status:** done (2026-10-24)
- **Completion:** Typed structural impulses persist in schema18 and compile to exact-grid, right-owned boxes with configurable width (default 1 ms), additive coincident sources and exposed area/error. Built-in continuous factors retain analytic evaluation; prepared Lua batches are not reevaluated. Minimum-width rejection reports an exact editable fraction. Integrated GATE passes 5,008/5,008 and 42-TU complexity, with six additional header consumers and a successful linux-clang build. Focused generator/impulse/core persistence coverage passes 363 assertions / 44 cases. The real isolated inspector shows readable width, generated rows, area and error; the manual describes representability and composition.
- **Outcome:** `curve.dirac { at, area }` adds a zero-width scroll impulse to a recipe; export compiles it to a finite constant box 1 ms wide by default (width configurable per recipe) whose integrated area equals the impulse, and the preview shows the generated points and approximation error (user decision 2026-09-30).
- **Read first:** `docs/osbx-draft-spec.md` (Dirac impulses), `include/wormedit/core/sv_recipe.h` (`compile_sv_recipes`, `bake_sv_recipes_for_export`), `tests/core/sv_recipe_tests.cpp`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`. Supporting ownership: `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/formats/native_project_migrations.cpp`, `src/core/document.cpp`, `src/core/text_util.h` (shared digest validation), new `tests/core/sv_recipe_impulse_tests.cpp`, new `tests/formats/native_project_impulse_tests.cpp`, cohesive impulse preparation helpers if needed, inspector projection/controls and manual/docs integration (coordinator), `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Parallel ownership (2026-10-24):** impulse agent owns core/model/native schema 18 and dedicated tests. Schema/model ownership hands back to coordinator before T-160 schema 19 changes; document API and generator agents do not edit these shared files concurrently.
- **Implementation contract:** the declarative `curve.dirac` primitive is stored as typed `sv_recipe_t::impulses` and authored through the inspector; the numeric Lua function API remains scalar-only. Literal Lua `curve.dirac(...)` syntax belongs to the broader curve DSL. Coincident sources add before weighting by nonparticipating continuous recipes, including standalone zero-baseline impulses.
- **Steps:** 1. Impulse primitive in the recipe model (not sampled). 2. Box compilation at export with exact area and boundary ownership rules. 3. Coincident impulses add their areas. 4. Tests: area preservation, width setting, target resolution limits.
- **Invariants:** recipes still store no sampled points.
- **Reject/failure behavior:** a box narrower than the target's time resolution rejects with the minimum width.
- **Verify:** GATE
- **Out of scope:** triangular pulses.

## T-292 Carry storyboards into SM/SSC exports

- **Status:** deferred (stage 7–8 ambition, user idea 2026-09-30; was: blocked-on T-155)
- **Outcome:** exporting a song with an OSB/OSBX storyboard to SM/SSC can also emit a non-interactive version of it: by default a rendered video (offline storyboard renderer from T-155's follow-up cards) referenced through `#BGCHANGES` (background layers) and `#FGCHANGES` (foreground layers); optionally, a small bundled Lua OSB interpreter as a StepMania BGAnimation that reads the `.osb` and draws sprites with the game engine.
- **Read first:** `docs/osbx-draft-spec.md` (Video export, Standard OSB compatibility export), `src/formats/stepmania.cpp` (retained `#BGCHANGES`/`#FGCHANGES` fields), `include/wormedit/core/storyboard_preview.h`.
- **Files owned:** decided when un-deferred (split into a video-path card and an interpreter card first).
- **Steps:**
  1. Video path: render Background/Fail/Pass and Foreground layers to two videos (or one, flattened) over the song length; write `#BGCHANGES`/`#FGCHANGES` entries with exact beat or time starts; loss-diagnose trigger-dependent content (Pass/Fail chooses one state).
  2. Interpreter path (optional): a Lua BGAnimation that parses the OSB subset wormedit emits (Sprite, Animation, commands, loops), evaluated per frame from the song time; per-profile notes on which forks allow song Lua (Etterna restricts it; SM5 and ITGmania allow BGAnimations).
  3. Fixture: a small OSB rendered both ways; re-import keeps `#BGCHANGES`/`#FGCHANGES` round-trippable.
- **Invariants:** the gameplay chart is unchanged; storyboard export never blocks chart export (it is an extra artifact).
- **Reject/failure behavior:** unsupported storyboard features are listed as classified losses for the chosen path.
- **Verify:** defined by the split cards.
- **Out of scope:** interactive (input-reactive) storyboards in StepMania.
