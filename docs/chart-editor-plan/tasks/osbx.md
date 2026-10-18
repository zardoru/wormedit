# Task cards: OSBX Lua DSL and Lua SV curves

[Back to the task-card index](../tasks.md).

> **Deferred group (user decision, 2026-09-30):** these are late-stage ambitions, not MVP work. Every card here is `deferred`; do not start one unless the user explicitly asks for it by ID.

Turns the [OSBX draft](../../osbx-draft-spec.md) into buildable slices. Order:
first the typed document and evaluator in plain C++ (no Lua), then Lua as a
thin declaration layer over it, then chart DSL and SV curve functions, then
export/editing. Lua cards depend on the sandbox (T-156 in
[scripting-storyboard](scripting-storyboard.md)). Every Lua card must keep the
rule from the draft: Lua builds typed descriptors; it never mutates the live
document, and a failed run keeps the last materialized result.

## T-180 Answer the OSBX open questions with recommended defaults

- **Status:** deferred (stage 7–8 ambition; was: blocked-on-user)
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
- **Read first:** `include/wormedit/core/storyboard_preview.h`, `src/sys/player_preview_presenter.cpp`, `include/wormedit/core/osbx_evaluator.h`.
- **Files owned:** `src/core/storyboard_preview.cpp`, `include/wormedit/core/storyboard_preview.h`, `src/sys/player_preview_presenter.cpp`, `tests/core/storyboard_preview_tests.cpp`.
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

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-185)
- **Outcome:** `worm_project`, `include`, `audio`, `image`, and `midi` declarations load modules in stated order relative to the including file, hash every source and asset, and refuse paths outside the project.
- **Read first:** `docs/osbx-draft-spec.md` (File roles, Language execution), `src/scripting/osbx_lua.cpp`.
- **Files owned:** new `src/scripting/lua_project.cpp/.h`, new `tests/lua/lua_project_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:** 1. Include resolution with cycle detection. 2. SHA-256 per file. 3. Tests: relative include, cycle, escaping path.
- **Invariants:** no filesystem access from Lua itself; the host reads files.
- **Reject/failure behavior:** any failure keeps the previous materialized result.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Out of scope:** glob includes.

## T-187 Build chart difficulties from `measure { … }` descriptors

- **Status:** deferred (stage 7–8 ambition; was: blocked-on T-159, T-186)
- **Outcome:** `*.chart.lua` modules with `difficulty`, `resolution`, `offset`, `columns`, `measure { … }`, `measures(seq)`, `note`, `hold`, `bgm`, and chart-level `bpm`/`stop`/`warp`/`scroll` produce a generator transaction previewed and applied as one command.
- **Read first:** `docs/osbx-draft-spec.md` (Measure-based chart declarations), `src/scripting/lua_generator.cpp`, `include/wormedit/core/rational.h` (exact float→rational helper).
- **Files owned:** new `src/scripting/chart_lua.cpp/.h`, new `tests/lua/chart_lua_tests.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Steps:**
  1. Exact helpers `measure_length "0.75"`, `ratio(3,4)`, `meter(6,8)`, `measure_time "0.125"`, `rows(n)`, `at_measure(id, row)`.
  2. Bare Lua numbers go through the existing bounded IEEE-754→rational conversion; report the chosen fraction.
  3. One test per validation rule in the draft that concerns charts.
- **Invariants:** `row = capacity` is rejected (write row 0 of the next measure).
- **Reject/failure behavior:** the whole transaction rejects on the first error.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.'`.
- **Out of scope:** sample references (T-188).

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

- **Status:** blocked-on T-156 (un-deferred, user decision 2026-09-30: Lua SV recipes are near-term)
- **Outcome:** an SV/`SCROLLS`/`SPEEDS` recipe can use `values(function(p, ctx) … end)` (or a module entry point returning such a function); preview and export evaluate it in a fresh sandbox with the recipe's sampling policy and store only source reference, API version, entry point, named parameters, input hashes, seed, execution mode and declared emission/cycle plan.
- **Read first:** `docs/osbx-draft-spec.md` (Parametric scroll and speed generation), `include/wormedit/core/sv_recipe.h` (`sample_sv_recipe`, `compile_sv_recipes`), `include/wormedit/scripting/lua_sandbox.h`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, new `src/scripting/lua_curve.cpp/.h`, new `tests/lua/lua_curve_tests.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/formats/native_project.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`.
- **Owner direction (2026-10-18):** Cross-cycle state is desired, based on the [pinned SV references and proposed prepared-evaluation checkpoint](extension-friction.md). Pure curves remain supported; ordered stateful sessions replace the old blanket stateful-closure rejection. T-681 must define prepared composition semantics before this evaluator integration; recheck the current native schema version before selecting its additive migration.
- **Steps:**
  1. The curve kind already exists as `sv_recipe_curve_t::lua_expression` with an inline `expression_source` (native schema 2, T-319; stored, validated and round-tripped, but sampling rejects it as unsupported). Extend it with the remaining stored fields above (API version, entry point, named parameters, input hashes, seed, optional project-relative source reference, execution mode and declared emission/cycle plan) using the next additive native schema migration selected from the executing tree. Replace the unsupported rejection in `sample_sv_recipe` with the prepared evaluation backend (no bytecode).
  2. A prepared evaluation/session interface in core; Lua implementation lives in `src/scripting` so core stays Lua-free. Instantiate once per recipe build, retain closure state across its cycles, and restart from declared inputs/seed on each independent preview/export build.
  3. Pure mode retains order-independence checks. Stateful mode requires a declared stable cycle/emission sequence and replay determinism from a fresh session; do not double-evaluate an emission or advance its state during composition. Initially reject incompatible adaptive probing clearly.
  4. `ctx` exposes read-only interval/baseline/parameters and cycle/emission indices; seeded RNG and mutable closure state belong to the bounded evaluation session. Document index bases, evaluation order and endpoint ownership.
  5. Tests: `function(p) return 1 + p end` matches `curve.linear{1,2}`; a counter closure persists across cycles but resets on a fresh build; identical emission plans/seeds reproduce preview/export; post-processing and composition do not advance state; non-finite output rejects; instruction budget stops an infinite loop.
- **Invariants:** identical inputs/seed/ordered emission plan → identical samples and diagnostics; pure built-ins retain existing behavior; save/load stores recipes and execution metadata, never sampled points or opaque VM state.
- **Reject/failure behavior:** any failure rejects the preview/export batch and keeps the recipe unchanged.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^lua\.|sv_recipe'`.
- **Out of scope:** editor UI (T-191).

## T-190 Add the recipe post-process pipeline

- **Status:** ready (un-deferred, user decision 2026-09-30: Lua SV recipes are near-term)
- **Outcome:** recipes carry an ordered `postprocess` list with `multiply { value }`, `normalize { mode = preserve_displacement | preserve_distance | preserve_average, value }`, and `balance_bump { solve = x | y }`, applied to a copy of the sampled batch exactly as the draft's formulas say.
- **Read first:** `docs/osbx-draft-spec.md` (Post-process pipeline, Finite bumps), `include/wormedit/core/sv_recipe.h`, `tests/core/sv_recipe_tests.cpp`.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`, `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`.
- **Prepared-output dependency:** Review T-681 in [extension friction](extension-friction.md) before implementation. Transform the prepared raw batch once; effective preview, overlap composition and export must consume the transformed semantics rather than call the original curve again. Keep existing built-in numerical behavior characterized first.
- **Steps:**
  1. Tests first: linear 0.75→2.0 normalized to displacement 1 integrates to the baseline; bump with `L_A = L/4`, `X = 2` solves `Y = 2/3`; zero denominator rejects.
  2. Implement stages; persist the list; older projects load with an empty list.
- **Invariants:** stage order is preserved and shown; recipes never store sampled points.
- **Reject/failure behavior:** non-finite or target-forbidden outputs reject the batch.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'sv_recipe|native_project'`.
- **Out of scope:** Dirac impulses (T-162).

## T-191 Edit Lua curve recipes in the SV recipe inspector

- **Status:** blocked-on T-189, T-201 (un-deferred, user decision 2026-09-30: Lua SV recipes are near-term)
- **Outcome:** the SV recipe inspector section (T-201) can switch a recipe's curve to "Lua function", pick a module file and entry point, edit named parameters and seed, and shows sampled points, errors with file:line, and the composed curve.
- **Read first:** `src/scripting/lua_curve.h`, the T-201 inspector section, `docs/osbx-draft-spec.md` (Composition, preview, and recipe editing).
- **Files owned:** the T-201 inspector unit, `src/sys/compose_actions.cpp`, `src/ui/frontend.h`.
- **Steps:** standard pattern; evaluation off the frame thread with cancellation.
- **Invariants:** editing never runs Lua on the frame thread.
- **Reject/failure behavior:** errors show inline; the last good preview stays.
- **Verify:** standard build/test.
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
