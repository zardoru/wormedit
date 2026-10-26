# Task cards: OSBX Lua DSL and Lua SV curves

[Back to the task-card index](../tasks.md).

> **Deferred group (user decision, 2026-09-30):** these are late-stage ambitions, not MVP work. Cards still marked `deferred` require explicit owner selection. Completed Lua/SV cards and the owner-promoted T-186 follow their individual statuses.

Turns the [OSBX draft](../../osbx-draft-spec.md) into buildable slices. Order:
the OSBX path starts with the typed document and evaluator in plain C++ (no Lua), then Lua as a
thin declaration layer over it, then chart DSL and SV curve functions, then
export/editing. Lua cards depend on the sandbox (T-156 in
[scripting-storyboard](scripting-storyboard.md)). Every Lua card must keep the
rule from the draft: Lua builds typed descriptors; it never mutates the live
document, and a failed run keeps the last materialized result.

## T-180 Answer the OSBX open questions with recommended defaults

- **Status:** deferred (stage 7–8 ambition; was: blocked-on-user)
- **Settled subset (owner, 2026-10-25):** T-186 adopts `project.lua` as the generator entry-point filename independently of OSBX. Do not ask that naming question again; the remaining storyboard/chart conventions stay deferred.
- **Outcome:** each question in the draft's "Open specification questions" has an answer written back into `docs/osbx-draft-spec.md`, so later cards need no design choices.
- **Read first:** `docs/osbx-draft-spec.md` (last section), `docs/chart-editor-plan/decisions/open-questions.md`.
- **Files owned:** `docs/osbx-draft-spec.md`, `docs/chart-editor-plan/decisions/open-questions.md`.
- **Steps:**
  1. STOP and present these recommended defaults for approval, one line each: extensions `project.lua`, `*.osbx.lua`, `*.chart.lua`; entry points are the files themselves; generated Lua uses one call per line with two-space indentation; clips/loops/triggers use the table forms `clip { id, ops, tracks }`, `loop { start, count, keys }`, `trigger { name, from, to, keys }`; matrix tracks interpolate component-wise only (no decomposed mode in 0.1); OSB bake metric = max screen-space position error 0.5 px and max 4 keys per 100 ms, no rasterization in 0.1; blend modes `normal`, `additive`; overrides stored as a per-object JSON patch keyed by stable ID, conflicts resolved manually; sampling defaults `adaptive`, tolerance 0.002, `max_points` 256, impulses rejected on every target except as an explicit finite box.
  2. Write the approved answers into the spec and mark the draft `0.2`.
- **Invariants:** no code changes.
- **Reject/failure behavior:** a rejected default stays open and blocks only the cards that name it.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** implementation.

## T-181 Define the typed OSBX storyboard document

- **Status:** deferred (stage 7–8 ambition; was: ready)
- **Outcome:** C++ types for OSBX nodes (group/sprite/animation/sample), stable IDs, parent links, layers, ordered transform stacks, property tracks, keyframes with easing, and reusable clips, plus a validator for every rule in the draft's Validation section that applies to storyboards.
- **Read first:** `docs/osbx-draft-spec.md` (Scene graph, Matrices, Keyframes, Validation), `include/wormedit/core/storyboard.h`.
- **Files owned:** new `include/wormedit/core/osbx_document.h`, new `src/core/osbx_document.cpp`, new `tests/core/osbx_document_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Types only, no Lua. 2. `validate_osbx_document` returning typed diagnostics (duplicate/missing IDs, cycles, cross-layer parenting, non-finite values, malformed matrices, wrong key value shapes, recursive clips). 3. One test per rule.
- **Invariants:** keyframe time is signed integer ms; IDs are strings.
- **Reject/failure behavior:** validation never mutates the document.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** evaluation (T-182), persistence (T-184).

## T-182 Evaluate OSBX transform stacks and keyframes

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-181)
- **Outcome:** a pure evaluator returns each visible node's world affine matrix `[a,b,c,d,tx,ty]`, opacity, and color at a millisecond time, following the draft's column-vector and declaration-order rules.
- **Read first:** `docs/osbx-draft-spec.md` (Matrices, Keyframes), `include/wormedit/core/osbx_document.h`, `src/core/storyboard.cpp` (existing easing functions).
- **Files owned:** new `include/wormedit/core/osbx_evaluator.h`, new `src/core/osbx_evaluator.cpp`, new `tests/core/osbx_evaluator_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Reuse the existing OSB easing table; add cubic Bézier and `step`. 2. Tests: translate-then-rotate differs from rotate-then-translate; pivot wrapper; parent×local order; rotation keys do not wrap; matrix keys interpolate per coefficient.
- **Invariants:** deterministic; no allocation per evaluated key beyond the output arena.
- **Reject/failure behavior:** a node with an invalid matrix at time t is skipped with a diagnostic.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** drawing.

## T-183 Draw OSBX scenes in the player preview

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-182)
- **Outcome:** OSBX sprites render through the existing storyboard draw path (same quads, layers, and image cache as OSB sprites).
- **Read first:** `include/wormedit/core/storyboard_preview.h`, `src/sys/audio/player_preview_presenter.cpp`, `include/wormedit/core/osbx_evaluator.h`.
- **Files owned:** `src/core/storyboard_preview.cpp`, `include/wormedit/core/storyboard_preview.h`, `src/sys/audio/player_preview_presenter.cpp`, `tests/core/storyboard_preview_tests.cpp`.
- **Steps:** 1. Convert evaluated matrices into the existing draw-instance form (add shear support if the instance lacks it). 2. Test one sheared sprite end to end.
- **Invariants:** OSB sprites render unchanged.
- **Reject/failure behavior:** missing images use the missing placeholder.
- **Verify:** standard build/test; smoke PNG with a test OSBX document.
- **Out of scope:** Lua.

## T-184 Persist OSBX documents in the native project

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-181, T-122)
- **Outcome:** native projects store OSBX documents plus their materialized-output provenance (API version, source file hashes, seed); older projects load unchanged.
- **Read first:** `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `include/wormedit/core/osbx_document.h`.
- **Files owned:** `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** standard JSON codec with bounded validation; write/read/write test.
- **Invariants:** unknown JSON fields reject, like the rest of the codec.
- **Reject/failure behavior:** invalid documents publish nothing.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'native_project'`.
- **Out of scope:** Lua source storage beyond references and hashes.

## T-185 Build OSBX storyboards from Lua declarations

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-181, T-156, T-180)
- **Outcome:** a `*.osbx.lua` file using the draft's Premake-style calls (`storyboard`, `image`, `group`, `sprite`, `layer`, `parent`, `origin`, `transform.*`, `opacity`, `key`) evaluates in the sandbox into one `osbx_document_t`.
- **Read first:** `docs/osbx-draft-spec.md` (Storyboard declaration example), `include/wormedit/scripting/lua_sandbox.h`, `include/wormedit/core/osbx_document.h`.
- **Files owned:** new `src/scripting/osbx_lua.cpp/.h`, new `tests/lua/osbx_lua_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. A "current object" context in C++; each call validates and appends to a builder, recording file/line provenance.
  2. After the script returns, run `validate_osbx_document`; publish only if valid.
  3. Golden test: the draft's example evaluates to a fixed document (compare field by field).
- **Invariants:** same source + seed → identical document on every platform.
- **Reject/failure behavior:** any error returns diagnostics with file:line and no document.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Out of scope:** includes (T-186), chart modules (T-187).

## T-186 Resolve `project.lua` includes and asset declarations

- **Status:** done (2026-10-25; owner promoted and implemented independently of OSBX)
- **Decision (owner, 2026-10-25):** decouple project-level scripting from OSBX. Depend on completed T-156/T-158/T-160 and the existing generator worker/preview path, not T-185. T-187 remains the separate chart-declaration DSL; T-185 later adds storyboard declarations to the same host-controlled loading model.
- **Outcome:** select `project.lua` in the existing Lua generator inspector. `worm_project`, ordered relative `include`, and `audio`/`image`/`midi` declarations freeze a bounded project-local source graph and asset hashes into an API-v1 generator recipe. Included API-v1 note/layer output is merged into one current-chart preview and Apply transaction; helper modules may return ordinary Lua values.
- **Read first:** `docs/osbx-draft-spec.md` (File roles, Language execution), `docs/architecture.md`, `src/scripting/lua_generator.cpp`, `src/sys/compose/generator_runtime.cpp`.
- **Files owned:** new `src/scripting/lua_project.cpp/.h`, new `tests/lua/lua_project_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`; coordinator-approved support: `include/wormedit/scripting/lua_sandbox.h`, `src/scripting/lua_sandbox.cpp`, `src/scripting/lua_generator.cpp/.h` (bounded host reader capability), `src/sys/compose/generator_runtime.cpp`, `tests/sys/generator_runtime_tests.cpp`, `docs/lua-api/`, `scripts/check-lua-api-docs.py`, `docs/manual/chapters/placing-notes.adoc`, `docs/architecture.md`, `docs/osbx-draft-spec.md`, this card and `memory/chart-editor-status/resume.md`.
- **Owner-approved gate follow-up (2026-10-25):** after HEAD advanced to `c715daf`, include status-bar fixes in this batch: `src/ui/nuklear/app/frontend.cpp`, `tests/ui/playback_status_event_harness_tests.cpp`, `tests/ui/compose_status_event_harness_tests.cpp`. Preserve millisecond/row/musical readouts, update the old clock expectation, reserve notification width consistently and keep selection text visible from 800 through 1280 px with a compact layout. Focused status coverage passes 75 assertions / eight cases; no status feature is reverted.
- **Steps:**
  1. Resolve includes relative to their including file; reject cycles, missing files and canonical/symlink paths outside the project. Cache each canonical source per build and evaluate includes once in stated order, preserving helper return values.
  2. Hash every source and declared asset. Asset declarations expose project-relative paths, kinds, hashes and an existing imported asset ID when matched; importing assets and MIDI input selection remain explicit existing actions.
  3. Freeze the graph into bounded Lua source with per-file SHA256 hashes. Saved-source runs and load/save/export never read source files. Explicit live regeneration rebuilds the graph; changed dependencies require the existing captured-source confirmation before an applicable preview.
  4. Preserve generator atomicity, BMS target pinning, cancellation and last good output. Split the normal total instruction budget between discovery/replay and frozen evaluation/replay; cap 64 files, 256 path aliases, aggregate source 64 KiB, final bundle 64 KiB and aggregate asset reads 64 MiB.
  5. Verify relative/nested includes, ordered modules, cycles/escapes including symlinks, declared assets/hashes, saved frozen execution without files, changed-dependency confirmation, limits, cancellation, save/load and one Undo step.
- **Invariants:** ordinary Lua sessions have no host reader. Only explicit project preparation grants project-local host reads; the frozen recipe has none. A failed run keeps the previous materialized result.
- **Reject/failure behavior:** malformed declarations, missing files, escapes, cycles, incompatible output, budget/cancellation or replay failure publish nothing and identify the failing file where available.
- **Verify:** GATE; focused Lua/project generator worker tests; documentation and packaged manual checks.
- **Verification (2026-10-25):** final GATE passes 5,294/5,294, whitespace and 11 changed-TU complexity checks without increased allowances; four additional sandbox-header consumers pass complexity. Focused project-loader coverage passes 85 assertions / eight cases, generator-worker coverage 90 / seven, and status UI coverage 75 / eight. This includes later-called helper closures retaining their defining file, malformed declarations remaining rejected when scripts replace standard globals, save/load, captured changed-source confirmation and frozen runs without source files. Native and Windows Manual-component installs pass all local links, anchors, raw sources, styles and license checks across ten HTML pages. Lua-disabled worker syntax passes. Isolated native 800×700 and 1280×720 windows were inspected; the 800 px capture shows the full selected-object summary alongside millisecond, row and musical readouts. Logs: `/tmp/wormedit-project-final-review.log`, `/tmp/wormedit-gate.H5NTgv`; captures: `/home/zardoru/.cache/wormedit-verification/project-lua-visual/final-status.png` and `final-status-wide.png`.
- **Out of scope:** glob includes, OSBX declarations, T-187's measure/chart DSL, multiple-chart creation, automatic asset import, implicit execution on opening a project, and source-to-visual override editing.

## T-187 Build chart difficulties from `measure { … }` descriptors

- **Status:** done (2026-10-25; owner-promoted implementation)
- **Outcome:** `*.chart.lua` modules with `difficulty`, `resolution`, `offset`, `columns`, `measure { … }`, `measures(seq)`, `note`, `hold`, `bgm`, and chart-level `bpm`/`stop`/`warp`/`scroll` produce a generator transaction previewed and applied as one command.
- **Read first:** `docs/osbx-draft-spec.md` (Measure-based chart declarations), `src/scripting/lua_generator.cpp`, `include/wormedit/core/rational.h` (exact float→rational helper).
- **Files owned:** new `src/scripting/chart_lua.cpp/.h`, new `tests/lua/chart_lua_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Owner decision (2026-10-25):** declarations create owned difficulties matched by stable_id; the selected chart is launch context, not an implicit replacement target. Preserve unrelated charts and reject edits to generated ownership.
- **Coordinated support (2026-10-25):** coordinator ed9e39b4-85d7-459f-95f5-1ed4c5dacd3f approves generator-only extensions in `include/wormedit/core/document.h`, `generator.h`, `src/core/generator.cpp`, new `generator_chart.*`, native codec/migrations and necessary validation/identity consumers, CMake/tests. Shared ID traversal extraction from `src/core/chart_editor.cpp` into an internal header is approved where reused. Shared SV owns its definition/placement fields and ceiling reservations; preserve their union with generator witness reservations. Both unreleased branches use one additive schema23 (22→23), with absent generator additions defaulting safely even on already-version23 documents. No 23→24 branch-order migration and no SV codec edits. Supporting scope also includes the existing generator worker/UI preview, project loader, API/manual docs, this card and resume state.
- **Supporting extraction:** existing chart ID traversal and membership cleanup are shared through `src/core/chart_id_visitor.h` and `chart_memberships.h`; the StepMania layout factory moves from the importer to `lane_profiles` so DSL modes reuse its lane/arrow catalog. Generator view/runtime and trusted sandbox adapter preserve source diagnostics and use the existing worker/proposal pipeline.
- **Grid safety:** authored positional data is preserved on compatible same-grid regeneration; changing resolution while such data remains rejects atomically. Removing a referenced measure rejects through full document validation. No authored SV fields or global definitions are rewritten.
- **Integration support:** shared SV integration extends `tests/lua/chart_lua_tests.cpp`, `tests/formats/native_project_generator_tests.cpp` and `native_project_lua_recipe_tests.cpp` for compatible regeneration, atomic rejection, retained definition IDs and the combined additive migration. Native screenshot review exposed a recovery-path bug: `src/core/autosave_loop.cpp` anchored generator reload origins as media paths, invalidating autosave and journal serialization. Coordinator-approved supporting changes in that shared owner, `include/wormedit/core/autosave_loop.h`, and the existing autosave/journal tests preserve frozen project-relative origins. Both pipeline regressions failed before the fix and pass afterwards; media anchoring remains covered.
- **Steps:**
  1. Exact helpers `measure_length "0.75"`, `ratio(3,4)`, `meter(6,8)`, `measure_time "0.125"`, `rows(n)`, `at_measure(id, row)`.
  2. Bare Lua numbers go through the existing bounded IEEE-754→rational conversion; report the chosen fraction.
  3. One test per validation rule in the draft that concerns charts.
- **Invariants:** `row = capacity` is rejected (write row 0 of the next measure).
- **Reject/failure behavior:** the whole transaction rejects on the first error.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Verification at handoff (2026-10-25):** `scripts/gate.sh --jobs 4` passes all **5,308/5,308** tests, whitespace and **24 changed-TU complexity checks** with no allowance increases (logs `/tmp/wormedit-gate.Mh5CXZ`). Focused chart tests pass 137 assertions/13 cases; worker tests pass 101 assertions/8 cases; all 91 Lua CTests pass. Native isolated Run/Apply/Save confirms the explicitly saved owned Easy difficulty with two measures/three notes, unchanged launch chart and schema23 persistence; screenshot review later exposed recovery warnings, fixed at integration below. Native and Windows staged offline books pass links/anchors/assets across 12 API HTML pages. Additional `linux-clang` core/scripting compilation was blocked by the existing export-cut initializer, resolved by SV parent `1ecb78c` at integration.
- **Combined integration verification (2026-10-25):** `scripts/gate.sh build --jobs 4 --complexity-base c715daf` passes **5,329/5,329** tests, build, whitespace and **45 changed-TU complexity checks**; eight explicit header consumers also pass, with no allowance increases. Both additive schema23 migrations and ID reservations are retained. Combined chart/SV tests pass **72 assertions / three cases** and schema tests **20 / two**. The corrected autosave/journal tests pass **19 / two** after reproducing both failures before the fix. Clang core/scripting builds pass, including the incremental recovery fix. Fresh isolated Regenerate/Apply/Save preserves two charts, one owned binding, three placements sharing one definition, frozen source/hash and relative reload origin in both the saved file and a published journal record. Both fresh native captures are free of recovery errors. Logs: `/tmp/wormedit-integration-final-gate.log`, `/tmp/wormedit-gate.vInK43`, `/tmp/wormedit-integration-clang.log`, `/tmp/wormedit-integration-clang-recovery.log`; native evidence: `/tmp/wormedit-integration-native/`.
- **Out of scope:** sample references (T-188), storyboard output and new `song`/`target` directives; existing Project output groups select exports.

## T-188 Accept typed sample references from Lua

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-187)
- **Outcome:** `osu_sample { … }`, `bms_wav "0A"`, and `audio_file "…"` attach typed references to notes/BGM; export runs a `resource_plan_t` pass that reuses or allocates BMS slots / BMSON channels / osu! filenames and reports conflicts before writing.
- **Read first:** `docs/osbx-draft-spec.md` (Audio sample references and writer allocation), `include/wormedit/core/resource_assignment.h`, `include/wormedit/formats/conversion.h`.
- **Files owned:** `src/scripting/chart_lua.cpp`, new `include/wormedit/formats/resource_plan.h`, new `src/formats/resource_plan.cpp`, new `tests/formats/resource_plan_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** follow the six numbered steps in the draft section, one test each.
- **Invariants:** two explicit BMS refs with the same index and different audio are an error.
- **Reject/failure behavior:** conflicts reject output without touching files.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'resource_plan|^lua\.'`.
- **Out of scope:** osu! hitsound UI (T-216).

## T-189 Evaluate Lua curve functions in SV recipes

- **Status:** done (2026-10-24)
- **Outcome:** an SV/`SCROLLS`/`SPEEDS` recipe can use `values(function(p, ctx) … end)` (or a module entry point returning such a function); preview and export evaluate it in a fresh sandbox with the recipe's sampling policy and store only source reference, API version, entry point, named parameters, input hashes, seed, execution mode and declared emission/cycle plan.
- **Read first:** `docs/osbx-draft-spec.md` (Parametric scroll and speed generation), `include/wormedit/core/sv_recipe.h` (`sample_sv_recipe`, `compile_sv_recipes`), `include/wormedit/scripting/lua_sandbox.h`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, new `src/scripting/lua_curve.cpp/.h`, new `tests/lua/lua_curve_tests.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/formats/native_project.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`. Supporting ownership: `src/core/document.cpp` (coordinator-approved song-level Lua input identity validation), `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/formats/native_project_migrations.cpp`, `tests/formats/native_project_lua_recipe_tests.cpp`, `include/wormedit/scripting/lua_sandbox.h`, `src/scripting/lua_sandbox.cpp`, `tests/lua/lua_sandbox_tests.cpp`, `docs/architecture.md`, `memory/chart-editor-status/resume.md`, `docs/chart-editor-plan/tasks/osbx.md`, `src/sys/project/output_group_export.cpp`, `src/tools/wormedit_convert.cpp`, `docs/osbx-draft-spec.md`.
- **Owner direction (2026-10-18):** Cross-cycle state is desired, based on the [pinned SV references and proposed prepared-evaluation checkpoint](extension-friction.md). Pure curves remain supported; ordered stateful sessions replace the old blanket stateful-closure rejection. T-681 must define prepared composition semantics before this evaluator integration; recheck the current native schema version before selecting its additive migration.
- **Parallel ownership (coordinator, 2026-10-24):** storage agent owns document/vocabulary descriptor declarations, schema 17 codec/migration and its dedicated native tests. Runtime agent owns sandbox protected invocation and Lua-curve implementation/tests after the core contract is published. Storage agent subsequently owns core preparation/composition and its tests after completing schema work; coordinator owns build wiring, docs/status and export integration. No simultaneous builds in shared directories; no file overlap without handoff. T-191 implementation follows the backend contract; T-162 overlaps core/storage and is serialized.
- **Durable contract:** `sv_recipe_t::lua` is an optional API-v1 descriptor; inline source bytes remain `expression_source` even with a project-relative reload origin/hash. Ordered cycle emission counts partition the fixed ascending-row sampling schedule (including the endpoint inspection query), while `p` remains normalized over the whole interval. Context indices are 1-based. Pure mode has no cycle plan. No sampled points or VM state persist.
- **Steps:**
  1. The curve kind already exists as `sv_recipe_curve_t::lua_expression` with an inline `expression_source` (native schema 2, T-319; stored, validated and round-tripped, but sampling rejects it as unsupported). Extend it with the remaining stored fields above (API version, entry point, named parameters, input hashes, seed, optional project-relative source reference, execution mode and declared emission/cycle plan) using the next additive native schema migration selected from the executing tree. Replace the unsupported rejection in `sample_sv_recipe` with the prepared evaluation backend (no bytecode).
  2. A prepared evaluation/session interface in core; Lua implementation lives in `src/scripting` so core stays Lua-free. Instantiate once per recipe build, retain closure state across its cycles, and restart from declared inputs/seed on each independent preview/export build.
  3. Pure mode retains order-independence checks. Stateful mode requires a declared stable cycle/emission sequence and replay determinism from a fresh session; do not double-evaluate an emission or advance its state during composition. Initially reject incompatible adaptive probing clearly.
  4. `ctx` exposes read-only interval/baseline/parameters and cycle/emission indices; seeded RNG and mutable closure state belong to the bounded evaluation session. Document index bases, evaluation order and endpoint ownership.
  5. Tests: `function(p) return 1 + p end` matches `curve.linear{1,2}`; a counter closure persists across cycles but resets on a fresh build; identical emission plans/seeds reproduce preview/export; post-processing and composition do not advance state; non-finite output rejects; instruction budget stops an infinite loop.
- **Invariants:** identical inputs/seed/ordered emission plan → identical samples and diagnostics; pure built-ins retain existing behavior; save/load stores recipes and execution metadata, never sampled points or opaque VM state.
- **Reject/failure behavior:** any failure rejects the preview/export batch and keeps the recipe unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.|sv_recipe'`.
- **Verification (2026-10-24):** Lua-enabled GATE passes 4,928/4,928, including the integrated inspector work; changed complexity passes 20 TUs without increased allowances. Focused Lua coverage passes 350 assertions / 41 cases. ASan/LSan/UBSan pass the sandbox and pinned Lua runtime (250 assertions / 31 cases). Lua-disabled syntax checks pass the worker/export/converter/form consumers. Logs: `/tmp/wormedit-lua-gate.log`, `/tmp/wormedit-lua-focused.log`, `/tmp/wormedit-lua-sandbox-asan-2.log`, `/tmp/wormedit-lua-disabled-syntax.log`.
- **Bounded review:** fixed pure-state verification to include globals independently of captured `_ENV`, charged context construction and retained-state inspection against the cumulative budget, preserved full unsigned seed/identity values, checked frozen-source hashes, and verified the exclusive endpoint restores the authored baseline without another emission. Lua `pairs`/`next` iteration order and platform libm rounding retain the sandbox's documented portability limits; replay rejects observed inconsistent output.
- **Out of scope:** editor UI (T-191).


## T-190 Add the recipe post-process pipeline

- **Status:** done (2026-10-22)
- **Outcome:** recipes carry an ordered `postprocess` list with `multiply { value }`, `normalize { mode = preserve_displacement | preserve_distance | preserve_average, value }`, and `balance_bump { solve = x | y }`, applied to a copy of the sampled batch exactly as the draft's formulas say.
- **Read first:** `docs/osbx-draft-spec.md` (Post-process pipeline, Finite bumps), `include/wormedit/core/sv_recipe.h`, `tests/core/sv_recipe_tests.cpp`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`.
- **Supporting ownership approved (coordinator, 2026-10-22):** `include/wormedit/core/document.h`, `include/wormedit/core/vocabulary.h`, `src/formats/native_project_migrations.cpp` (durable stage values and schema migration), `src/sys/compose/views/compose_recipe_areas.cpp`, `src/sys/compose/views/compose_marker_view.cpp`, `src/ui/frontend/compose.h`, `src/ui/nuklear/compose/compose_recipe_form.cpp` (processed projection and ordered stage labels), `docs/manual/chapters/timing.adoc`, this card and `memory/chart-editor-status/resume.md`. Keep raw and transformed batches transient at the prepared owner; existing forms preserve the stored pipeline when editing other parameters.
- **Prepared-output dependency:** Review T-681 in [extension friction](extension-friction.md) before implementation. Transform the prepared raw batch once; effective preview, overlap composition and export must consume the transformed semantics rather than call the original curve again. Keep existing built-in numerical behavior characterized first.
- **Projection test ownership (coordinator, 2026-10-22):** `tests/sys/compose_recipe_areas_tests.cpp` and `tests/sys/recipe_form_tests.cpp` verify transformed projection and preservation of stages during ordinary form edits.
- **Implementation (2026-10-22):** recipes persist up to 32 ordered multiply, normalize (displacement/distance/average) and weighted bump-balance stages. Schema 11 migrates older recipes to an empty pipeline. The prepared owner retains raw inspection and transforms a copy of long-double emissions once; composition, draft preview, projection and export consume the final half-open step batch. Empty pipelines retain existing analytic built-in behavior. Normalization integrates the same emitted values against the authored baseline; balancing can solve X or Y, including varying baselines and exact bump boundaries. Zero/non-finite denominators, invalid stages, bounds and target-forbidden output reject. Ordinary form edits preserve the pipeline, and the inspector lists its order; pipeline editing controls and Lua evaluation remain separate work.
- **Clang/native evidence (2026-10-22):** Linux Clang configure/build and full CTest pass **4,467/4,467** (`/tmp/t190-clang-final.log`). Tests cover the requested normalization/bump formulas, raw inspection, stage order, weighted signed/distance integrals, both bump solve directions, target/budget rejection, schema migration, form preservation, processed projection, preview/export agreement and consuming emitted transformations once. Isolated Xvfb shows **Normalize displacement → Multiply** without clipping; parameter Apply/save then one Undo/save preserves both stages and schema 11 while restoring the original value (`/tmp/t190-inspector-stages.png`, `/tmp/t190-native-{applied,undone}.png` and `.wormedit` snapshots). The first GCC gate caught three new Catch tests above the complexity limit; cohesive assertion helpers corrected them without changing allowances. libc++ remains unavailable.
- **Final GCC evidence (2026-10-22):** GATE passes build, **4,467/4,467**, whitespace and **10-TU** changed-C++ complexity (`/tmp/t190-commit-gate.log`, `/tmp/wormedit-gate.SIYMkp`), including both final edge cases and the completion notes. No execution or complexity allowance increased.
- **Steps:**
  1. Tests first: linear 0.75→2.0 normalized to displacement 1 integrates to the baseline; bump with `L_A = L/4`, `X = 2` solves `Y = 2/3`; zero denominator rejects.
  2. Implement stages; persist the list; older projects load with an empty list.
- **Invariants:** stage order is preserved and shown; recipes never store sampled points.
- **Reject/failure behavior:** non-finite or target-forbidden outputs reject the batch.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sv_recipe|native_project'`.
- **Out of scope:** Dirac impulses (T-162).

## T-191 Edit Lua curve recipes in the SV recipe inspector

- **Status:** done (2026-10-24)
- **Outcome:** the SV recipe inspector section (T-201) can switch a recipe's curve to "Lua function", pick a module file and entry point, edit named parameters and seed, and shows sampled points, errors with file:line, and the composed curve.
- **Read first:** `src/scripting/lua_curve.h`, the T-201 inspector section, `docs/osbx-draft-spec.md` (Composition, preview, and recipe editing).
- **Files owned:** `src/ui/frontend/compose.h`, `src/ui/frontend/actions.h`, `src/ui/nuklear/compose/compose_recipe_form.cpp`, `src/ui/nuklear/compose/compose_state.h/.cpp`, `src/sys/compose/views/recipe_form.h/.cpp`, `src/sys/compose/views/compose_marker_view.cpp`, `src/sys/compose/views/compose_recipe_areas.cpp`, `src/sys/compose/views/compose_view.h/.cpp`, `src/sys/compose/actions/compose_recipe_actions.cpp`, `src/sys/compose/actions/compose_actions_internal.h`, new `src/sys/compose/recipe_evaluation_runtime.h/.cpp`, `src/sys/project/workspace_view.h/.cpp` (persistent form/graph scratch), application environment/frame-services plumbing, focused frontend/event tests, `tests/lua/lua_curve_tests.cpp` (pure built-in equivalence integration), `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/architecture.md`, `docs/manual/chapters/timing.adoc`, this card and `memory/chart-editor-status/resume.md`. Coordinator-approved expansion replaces obsolete pre-split paths and covers asynchronous owned-snapshot integration.
- **Parallel ownership:** inspector agent owns UI/sys/app and focused tests; coordinator owns CMake/docs. Core agent supplies an explicit immutable prepared-result scope for frame-thread Apply, while the sandbox backend is scoped only inside asynchronous workers. No simultaneous builds.
- **Steps:** standard pattern; evaluation off the frame thread with cancellation.
- **Invariants:** editing never runs Lua on the frame thread.
- **Reject/failure behavior:** errors show inline; the last good preview stays.
- **Verify:** standard build/test.
- **Verification (2026-10-24):** final Lua-enabled GATE passes 4,933/4,933; complexity passes all 22 changed TUs against the pre-batch base and eight explicit header consumers, without increased allowances. Focused Lua passes 353 assertions / 42 cases; warmed Lua inspector/graph allocation coverage passes 10 assertions / 1 case. Lua-disabled worker links without Lua and returns the expected unavailable-evaluation diagnostic. Asciidoctor builds the manual without warnings. Inspected the real isolated 1280×720 window with a loaded Lua recipe, module/parameter controls, composed graph and readable sample/composition counts. Logs: `/tmp/wormedit-lua-layout-gate.log`, `/tmp/wormedit-lua-headers-complexity.log`, `/tmp/wormedit-lua-focused-final.log`, `/tmp/wormedit-lua-allocation.log`, `/tmp/wormedit-lua-disabled-worker-final.log`; capture `/tmp/wormedit-lua-inspector-labels.png`.
- **Bounded review:** worker admission retains only the newest pending draft; cancellation, project/chart/revision/selection changes and stale picker callbacks discard captured work. Errors preserve display data but cannot authorize Apply. Apply requires the exact successful requested form and immutable prepared snapshots. Reload alone reads files and freezes UTF-8 source/hash; legacy inline sources and clearing a reload origin do not trigger file reads. Ordered→Pure clears the stored cycle plan. Persistent form/graph buffers and identity-based projection caching avoid warmed-frame allocations. The manual documents factories, parameters, pure/ordered execution and explicit Reload.
- **Out of scope:** a code editor widget (edit the file externally; add a Reload button).

## T-192 Classify and bake OSBX for standard `.osb` export

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-182, T-180)
- **Outcome:** export classifies each object as exact, baked, or unsupported (rasterized waits for a later card) and emits standard OSB for exact/baked, with a report listing every baked or blocked ID.
- **Read first:** `docs/osbx-draft-spec.md` (Standard OSB compatibility export), `src/core/storyboard.cpp` (OSB emission), `include/wormedit/core/osbx_evaluator.h`.
- **Files owned:** new `src/core/osbx_osb_export.cpp/.h`, new `tests/core/osbx_osb_export_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Exact mapping for translate/rotate/uniform scale/vector scale/opacity/color. 2. Baking by sampling with the T-180 error metric. 3. Shear → unsupported unless baked within tolerance.
- **Invariants:** export never silently drops an effect.
- **Reject/failure behavior:** any unsupported object blocks export with its ID.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^core\.'`.
- **Out of scope:** rasterization, video.
