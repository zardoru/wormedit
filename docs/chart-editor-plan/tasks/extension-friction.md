# Extension friction: SV composition, specialized lanes and UI gestures

[Back to task cards](../tasks.md). Owner direction, 2026-10-18: prioritize refactoring by pending feature work; use sv-tools and blackhole-studio as SV capability references; make specialized lanes and UI gestures easier to add. Cross-cycle state is desired. The checkpoints below are proposed implementation scopes, not completed work or blanket approval of reference-tool parity. Import/export preparation and diagnostic provenance from the preceding reevaluation remain later candidates.

## Bounded reference review

Read source at sv-tools `0f1dfd03999d4e6fde183095e9de519377f82297` and blackhole-studio `c76aae9d3851ed35a5af7f06a16373024cfdec09`, fetched 2026-10-18. Reference behavior informs requirements; Wormedit retains its typed model, bounded Lua runtime, exact coordinates and atomic commands.

| Capability observed | Source evidence | Consequence for Wormedit's extension contract |
|---|---|---|
| BPM-compensating SV generation; linear/quadratic ramps; repeated sampled functions with repeated-value suppression; timestamp shifting; inherited-to-BPM conversion; two-region average-rate calculation | [sv-tools svc.js](https://github.com/zardoru/sv-tools/blob/0f1dfd03999d4e6fde183095e9de519377f82297/svc.js) | Separate curve values, placement, emission policy and target encoding. BPM compensation differs from T-190's displacement normalization. The `sv_reverse` body is a stub, not an implemented capability. |
| Cycle spans by duration, end time, beats, beat fraction or note timings | [cycle-time-emitter.ts](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/cycle-time-emitter.ts) | Distinguish musical/chart/audio domains, note-derived anchors and cycle schedule from values. Variable cycle duration is a meaningful requirement. |
| Divisions by count, beat fraction or time span; time deformation; no endpoint, last-cycle endpoint or every-cycle endpoint | [divisor-emitter.ts](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/divisor-emitter.ts) | Placement and endpoint ownership need explicit contracts before adding samplers. Do not copy the JavaScript falsy-zero fallback as intended time-warp semantics. |
| SV/BPM emission, cycle/divisor context, optional repeated-value suppression and intentional skipped emissions | [osu-timing-point-emitter.ts](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/osu-timing-point-emitter.ts) | Keep emission policy separate from value evaluation and validation. BPM is a distinct potential output capability, not silently added to today's SV/SCROLLS/SPEEDS enum. Explicit skip and invalid nonfinite values must remain distinguishable if skip support is introduced. |
| A generator creates a function whose closure state survives cycles; whole-timing-point-batch transforms are a separate function type | [sv-functions.ts](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/sv-functions.ts), [emitter integration](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/osu-timing-point-emitter.ts) | Owner accepts cross-cycle state: one fresh session per recipe build, with persistent state within that ordered build and no ambient VM state between builds. Stateless adaptive evaluation and ordered stateful emission are different contracts. |
| Easing, sine/bounce, teleport plateaus and rate jitter; whole-batch shift/rebase, SV multiply and repeat-with-gap | [builtin-sv-functions.ts](https://github.com/zardoru/blackhole-studio/blob/c76aae9d3851ed35a5af7f06a16373024cfdec09/src/app/blackhole-classes/builtin-sv-functions.ts) | Value transforms and topology/placement transforms cannot all be implemented as another scalar curve branch. T-190's existing initial stages remain the first consumer; further features need their own cards. |

No reference JavaScript was executed. Its dynamic execution, legacy numeric truncation and osu-specific limits are not adopted. No numerical parity claim or full repository/UI audit is made.

## Current extension costs and acceptance criteria

- **SV:** `sv_recipe_samples_t::from` samples values; `collect_recipe_samples` retains row positions and recipe pointers; `compose_recipe_point` calls `curve_value` again. This is safe only under the present stateless contract and would bypass arbitrary batch post-processing or advance a stateful evaluator again. T-681 must preserve existing built-in composition values, half-open ranges, adaptive tolerance, SPEEDS transition ownership and partial diagnostic evidence while giving evaluated/transformed batches a clear owner.
- **Lanes:** timing layout partitions live in `src/ui/compose_timing_geometry.h`; specialized views/renderers and sys area projections already exist. T-679 found BGA/timing/bar-line geometry already shared, with intentional BGM head/full-tail and bookmark band/line differences. A new lane needs a documented route through data/validation, projection, layout, drawing, input claims, selection/area geometry, commands and tests. The goal is fewer duplicated facts and lifecycle rules, not fewer domain-specific functions.
- **Gestures:** the core `editor_gesture` runner and proposal transaction already own core begin/revise/apply and terminal state. UI `compose_drag_capture_t` owns stamp/threshold/continuation decisions; the cancellation inventory owns reset hooks. UI blocking/hover/paint participation still names captures separately in `compose_input_queries_t`, while pointer routing has ordered named consumers. T-683 should consolidate only facts that actually repeat and preserve distinct consumer policies.
- **Proof of utility:** exercise existing contrasting cases before introducing a new abstraction: SPEEDS transitions, BGA resource-backed points, editable measure handles, waveform audio-offset cancellation, and paint release fences. Future lane/gesture cards must list their exact extension sites and add production-emitter interruption coverage. No synthetic demonstration lane or gesture is required merely to justify a framework.
- **Constraints:** retain named typed render/dispatch calls, shared transactions and current frontend lifetimes. No stored callbacks, virtual lane framework, type-erased renderer registry, universal hit rectangle or universal release rule. New contained presentation/action surfaces use C++ types per code style. T-653 remains pending revision; T-644 stays at bounded later checkpoints; folder moves remain last.

## Shared execution verification

On each scheduled checkpoint, run `scripts/gate.sh --jobs 4`, `cmake --preset linux-clang`, `scripts/gate.sh build-clang --jobs 4`, `clang-tidy -p build <each affected .cpp>`, and `ctest --test-dir build -R '^documentation\.' --output-on-failure`. Record focused regression commands after the exact supporting ownership inventory. T-682 also uses `scripts/smoke-compose-virtual-x.sh /tmp/t682-compose.png compose` and inspects the image; specific lane fixtures/actions must be listed before execution.

## T-681 Prepare SV evaluation and composition for feature extensions

- **Status:** Proposed (2026-10-18; owner capability direction recorded; implementation scope awaits scheduling)
- **Outcome:** Sampling, composition, draft preview and export share a prepared recipe boundary that preserves built-in semantics and can consume ordered stateful evaluation and T-190 post-processing without re-running their raw expressions.
- **Read first:** `docs/osbx-draft-spec.md` (Parametric scroll and speed generation), `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`, the pinned references above.
- **Files owned:** `include/wormedit/core/sv_recipe.h`, `src/core/sv_recipe.cpp`, `tests/core/sv_recipe_tests.cpp`; exact supporting/new owner paths must be inventoried and coordinator-recorded before edits. T-189/T-190 retain Lua/persistence feature ownership.
- **Steps:**
  1. Inventory every sampling/composition/preview/bake caller and characterize existing built-in values, boundary rows, overlap multiplication, adaptive tolerance and partial diagnostics.
  2. Define prepared ownership for schedule, evaluation session, raw/transformed values and composition. Distinguish pure random-access queries from ordered stateful emission; specify behavior at composition rows absent from an emitted batch before implementing it.
  3. Introduce only the seam needed by T-190 and T-189, keeping existing built-in output identical. A stateful session advances only for its declared ordered emissions; composition, inspection and export consume its prepared data without advancing it again.
  4. Verify preview/export agreement and rebuild determinism with a bounded test evaluator, then implement T-190 as the first real consuming feature in a separately reviewable checkpoint.
- **Invariants:** Cross-cycle state remains inside one recipe evaluation. Each independent build starts fresh from stored source/parameters/input hashes/seed; no opaque VM state is persisted. Partial results, diagnostics and target transition rules survive. Existing pure adaptive behavior stays.
- **Reject/failure behavior:** Budget, cancellation, invalid values or incompatible sampling reject without document mutation. Initially, stateful functions require an explicit ordered sampling plan; arbitrary adaptive probing must reject clearly until a defined compatible strategy is implemented.
- **Verify:** Focused existing SV recipe/preview/export and new prepared-session regressions; GCC and linux-clang GATE; affected .cpp clang-tidy with no new/increased complexity; final documentation checks. Record exact commands/logs on execution.
- **Out of scope:** Full reference-tool parity, new BPM recipes, time deformation/repeat UI, opaque state persistence, whole timing-adapter cleanup and unrelated file splitting.

## T-682 Establish the specialized-lane extension boundary

- **Status:** Proposed (2026-10-18; owner requested easy specialized-lane additions; exact extraction follows inventory)
- **Outcome:** Adding a specialized lane has an explicit bounded set of extension sites, with common lane facts owned once and domain-specific view, geometry, targeting and editing policies retained.
- **Read first:** `src/ui/compose_timing_geometry.h`, `src/sys/compose_view.cpp`, `src/ui/nuklear/compose_canvas.cpp`, `src/ui/nuklear/compose_input_panel.cpp`, T-660/T-679 evidence.
- **Files owned:** `docs/architecture.md`, this card for initial inventory; production/test extraction paths require exact coordinator-recorded ownership after step 1. No whole-file split is pre-authorized.
- **Steps:**
  1. Trace contrasting lanes (BPM point, SPEEDS transition, BGA resource event, measure handle and waveform/audio source) from durable data to projection/layout/render/pick/selection/action/gesture/test; list each fact and its current owner.
  2. Identify duplicated lane metadata and missing typed participation boundaries. Publish an exact extraction/test inventory and coordinate T-683 overlaps before any production edits.
  3. Share proven common facts and routing setup at their current owner; keep typed named per-family render/target/command calls and explicit special policies. Preserve layout, priorities and existing caches.
  4. Document the resulting add-a-lane path and verify existing contrasting lanes with relevant projection, event and isolated visual checks.
- **Invariants:** Point/span/handle shapes, musical/audio coordinates, resource resolution, snapping/locks, overlap stack behavior and pointer-versus-area geometry remain explicit. No forced common payload or callback lifetime.
- **Reject/failure behavior:** Invalid/missing targets and hidden/locked lanes retain current behavior; a policy difference blocks automatic unification of that part.
- **Verify:** Record exact focused projection/input/selection tests after ownership inventory; GCC and linux-clang GATE; clang-tidy affected sources; isolated Compose visual checks; documentation tests.
- **Out of scope:** Runtime lane plugins, renderer type erasure, notes hit-index redesign, blanket C ABI conversion, D6 UI lifetime split and folder moves. T-660's old universal-picker proposal needs revision using this inventory, not automatic execution.

## T-683 Make UI gesture participation an explicit extension contract

- **Status:** Proposed (2026-10-18; owner requested easy UI-gesture additions; preserve T-634/T-641/T-680 work)
- **Outcome:** A gesture declares its repeated capture-participation facts at one owner, while shared lifecycle/cancellation and named per-domain emitters handle the existing routing and transaction contract.
- **Read first:** `src/ui/nuklear/compose_drag_capture.h`, `src/ui/nuklear/compose_input_queries.h`, `src/ui/nuklear/compose_input_panel.cpp`, `src/core/interaction_gesture_runner.h`, `tests/ui/gesture_lifecycle_event_harness_tests.cpp`.
- **Files owned:** `docs/architecture.md`, this card for initial inventory; production/test extraction paths require exact coordinator-recorded ownership after step 1, coordinated with T-682.
- **Steps:**
  1. Inventory one existing gesture through capture admission/priority, hover/paint/modal participation, begin/update/click/release, cancellation/reset, core proposal dispatch and production-emitter tests. Compare paint, BGA move and waveform audio-offset cases.
  2. Record which repeated facts can share declarations and which policies differ by consumer; name exact source/test paths before editing. Prefer the existing query, capture and cancellation owners over a new runner.
  3. Derive only matching participation queries from the shared declaration; retain live queries after handlers acquire input, ordered named pointer handlers, source validity, distinct release rules and stamped deferred cancellation.
  4. Verify every affected family across Escape, focus/revision/modal/tab/input-loss and late release; document the add-a-gesture path so a new family reuses the runner and contributes its focused cases to the existing interruption matrix.
- **Invariants:** Domain gestures own only planning/payload/feedback; existing runners own stamp, transaction, terminal cleanup and cancellation. View-only captures and separately stamped audio-offset edits retain their differing effects. Test each actual production emitter.
- **Reject/failure behavior:** Preserve one core cancel and late-release fences, stale rejection and no partial mutation. Never clear capture evidence before registry hooks observe it; do not infer that every capture blocks the same consumers.
- **Verify:** Exact affected production interruption/event tests plus focused core proposal tests; GCC and linux-clang GATE; affected-source clang-tidy; final documentation tests. No desktop test windows.
- **Out of scope:** Another core gesture runner, a universal release policy, stored callback dispatch, new gesture features, D6 lifetime regrouping and broad test pruning.

## Planning verification (2026-10-18)

Documentation-only update: no production implementation or reference scripts executed. The prior reevaluation of pending work remains in scope: the prepared SV boundary leads; cancellable preparation/explicit publication for T-146/T-147/T-107 and structured diagnostics for T-224 remain later candidates, with T-110 already having a basic asynchronous summary. Feature-sized extractions precede optional hot-file splits. Final documentation and whitespace checks are recorded in the commit body; no full runtime-suite rerun is claimed.
