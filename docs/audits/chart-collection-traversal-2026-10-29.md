# Chart collection traversal audit — 2026-10-29

T-781; base `d154284`. This is a bounded structural migration, not a replacement event framework.

## Evidence and ownership

The stale-plan search found `id_ceiling.h` already owns declaration reducers (T-638/T-643),
`chart_id_visitor.h` owns cloning references, and `chart_position_storage.h` owns geometry roles.
`timing_links_internal.h` owns the independently linked categories (stop/delay share storage).
`native_project_value_codec.h:78` demonstrates typed collection visitation locally, but its
serialization fields are not a core membership contract. None of these supplied the common
flat structural membership to the seven consumers. Reusing the codec inventory would incorrectly
mix measures, recipes and layers into the object declaration domain.

`chart_collections.h` now owns the 15 flat object-ID declaration collections, with typed position
participation. Its point traversal is derived from that inventory. Allocation, labels and focus
consume declarations. Remapping consumes point IDs **and** their measure references; it retains
separate layout lane IDs, note/BGM heads, hold ends, annotations, projections and timing-link
references. Declaration scans never treat those references as declarations. Existing prepared
node/content/shared-pool and history/generator reservations remain separate, unchanged owners.

Geometry retains timing-category filtering through the timing-link traversal; only local/visual
point membership comes from the new inventory. This preserves separate stop/delay filters.
Volume/sample sets are chart-local; neither becomes a timing-link category. Range shift still
uses its existing explicit `include_timing` policy for sample controls. BGA/layout content shifts,
warp whole-span movement and marker anchoring remain domain operations. Quantize retains its
note-family selection, hold policy, marker/channel handling and recipe rules. Only recipe range
ends can use the exclusive chart boundary; bucket heads and all other endpoints cannot.

The coordinator approved `tests/core/chart_object_ids_tests.cpp` as an exact supporting extension.
No CMake, native codec, foundational document/command/interaction, sys or central plan docs changed.

## Fixed semantic extension exercise

Prompt, held constant for both snapshots: “Add a new chart-local point event shaped like volume
and sample-set controls. Locate the structural participation needed for declared-ID allocation,
reference remapping, geometry, labels, focus, quantize and range shift. Exclude format/UI/domain
validation and typed editor work.” The tool is deterministic source search and caller inspection,
not a fresh-agent latency comparison. Same lead, context, paths and search expression on both
snapshots; no claim about model navigation speed.

Query: `rg -n 'chart\.(volumes|sample_sets)'` over the seven owned consumer paths, then the new
inventory. `git show REF:PATH` supplies frozen snapshots. Both `c6eadce` and `d154284` give the
same baseline. The sample-control commit touched 68 files; most were necessary domain/format/UI
work and are explicitly excluded from the claimed reduction.

| Structural owner | Before membership edits | After membership edits |
|---|---:|---:|
| ID remapping | 1 | 0 |
| Declaration allocation | 1 | 0 |
| Free-position geometry | 1 | 0 |
| Label duplicate/cap scan | 1 | 0 |
| Workspace focus lookup | 1 | 0 |
| Quantize | 1 | 0 |
| Range shift | 1 | 0 |
| Shared typed inventory | 0 | 1 |
| Total necessary membership edit files | 7 | 1 |

This counts required production membership sites, not lines. Before: 14 member occurrences on
12 matching lines across seven files. After: two occurrences on two lines in one owner for the
two existing control families. A third analogous family requires one new typed entry rather
than seven consumer edits. Semantic review and regression coverage still span all seven consumers.
Concrete after extension: add `visit(chart.new_controls, chart_collection_role_t<local_point>{})`
to the inventory after declaring its storage/type. Allocation/labels/focus automatically enumerate
its `.id`; remapping sees `.id` and `.position.measure`; geometry visits its free point even when
timing categories are excluded; quantize transforms its point; range shift uses the existing
explicit timing-inclusion policy. No new category or timing-link registration follows from this.

A consumer-to-membership trace adds one header hop (zero before, one after); inspecting all seven
consumers now reads eight source files rather than seven. That is a navigation cost, accepted for
the explicit shared extension owner. The core-wide query shrinks from 14 files to 8; remaining
matches are codec/domain/presentation or out-of-lane owners, not silently claimed migrated.

## Build and scope measurements

A deterministic static include walk over `src` and `include` at `d154284` and this tree found:
`id_ceiling.h` reaches 40 source translation units, `chart_id_visitor.h` 2, and
`chart_position_storage.h` 9 in each snapshot. The new inventory reaches 46 source translation
units through those and direct consumers. This is a source-graph estimate, excluding tests,
conditional preprocessing and generated headers, not a Ninja rebuild timing. No reduction in
compilation count, duplicate compilation, or rebuild fanout is claimed. Editing a shared inventory
can rebuild more than editing a single old consumer. Adding the chart storage field already
requires changing `document.h`, so this work does not establish less whole-feature rebuild churn.
No new `.cpp` target or duplicate source registration is introduced. The current isolated
compile database has 1,214 first-party compilation entries, 755 unique first-party sources,
459 entries beyond one per source, and 215 multiply compiled sources. These are current graph
counts, not elapsed-time measurements. The CMake files and source registrations are unchanged
from `d154284`; D provides no mechanism for reducing those duplicate compilations.

The prior [workflow exercise](workflow-navigation-baseline-2026-10-19.md) uses fresh Luna-medium
agents and different workflow prompts. Its noisy file counts are not comparable to this fixed
semantic source exercise. T-722 and T-637 preserve the typed workflow/transaction boundaries;
T-638 supplies the declaration-versus-reference/capacity regression baseline, extended here for
volume and sample-set declarations at ordinary and UINT64_MAX ceilings.

Read-only PR evidence: [75](https://github.com/zardoru/wormedit/pull/75) retains prepared/latched
selection semantics; [65](https://github.com/zardoru/wormedit/pull/65) adds browser build concerns;
[64](https://github.com/zardoru/wormedit/pull/64) concerns dependency removal;
[63](https://github.com/zardoru/wormedit/pull/63) adds Quaver adapter/domain work.
None authorizes changing category membership or merging those branches. Their broader touched-file
counts are not a denominator for this bounded structural improvement. Recent `4c5fade`, `c596924`
and `72f342d` are selection/prepared sound-layer semantics, not flat structural membership changes.
[PR 58](https://github.com/zardoru/wormedit/pull/58) supplies the external seeded measure geometry
regression exercise; its source is read at head `e15c2cb457221cf2d316efde56ac8b4ee5b45f29` into
`/tmp`, without merging, registering or modifying the feature branch.

## Verification

Configuration: own isolated application build, `CMAKE_BUILD_TYPE=Debug`,
`WORMEDIT_BUILD_APP=ON`, `WORMEDIT_ENABLE_LUA=ON`, `/usr/bin/clang++` 23.1.4,
original `-g` debug flags, two jobs. Read-only source overrides point to existing FetchContent
sources and libotoworm; generated outputs live only in this worktree's `build`. Unlike B/C's
Lua-OFF configuration, D retains Lua ON. Matching Clang/clang-tidy/libclang 23.1.3 are selected
from `/tmp/wormedit-clang-23.1.3` for complexity; no baseline or system-tool changes.

- `cmake --build build --parallel 2`: passed.
- Focused `[chart-object-ids],[measure-editor],[quantize],[time-shift],[note-label-editor],
  [workspace],[bms-program-edit],[bms-program-authoring-view]`: **182 cases, 2,747 assertions passed**.
  These include ordinary and prepared recorded-owner regressions. The 50k label case emitted its
  existing 125 ms target warning; no latency improvement or timing-target pass is claimed.
- Pinned external PR58 source, compiled with the existing measure-editor test compilation flags
  and linked with this build's core-test libraries into `/tmp/d-measure-property-tests`:
  **2 cases, 193,484 assertions passed**, including the 40-edit fast and 6,400-edit extended corpora.
  No feature-branch merge or test registration; this is regression execution, not a before/after
  speed claim. Reproduction source/script: `/tmp/d-measure_geometry_property_tests.cpp`,
  `/tmp/d-run-pr58.py`; run `python3 /tmp/d-run-pr58.py` from this worktree after building.
- Static include closure against the actual compilation database identifies **55 header-consuming
  translation units** (46 source, nine test). Matching-toolchain explicit checker passed all 55;
  list `/tmp/d-header-consumers.txt`, output `/tmp/d-complexity-all-consumers.log`.
- Final `scripts/gate.sh --jobs 2 --complexity-base d154284`, matching toolchain on PATH and
  LD_LIBRARY_PATH: **build, every test, whitespace and changed-file complexity passed**.
- Explicit documentation run: **6/6 passed**. New task file mandatory fields also checked directly;
  central task-index/checker registration remains coordinator-owned.

Actual output tails:

```text
All tests passed (2747 assertions in 182 test cases)
All tests passed (193484 assertions in 2 test cases)
complexity: checked 55/55
complexity: passed (55 translation units)
gate: 2 job(s), set explicitly; logs in /tmp/wormedit-gate.MlL5ay
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 5591
gate: passed
100% tests passed out of 6
```

The initial full gate passed all tests but caught a new shift-test cognitive score of 32.
Moving expected-row arithmetic outside Catch macros fixed that test; final changed-file check
passed ten translation units. No baseline allowance changes, skips or waivers. The earlier
contended cold-build segment was interrupted for coordinator resource scheduling; completed
objects were preserved and no elapsed-time comparison is drawn from it. All heavy jobs drained
and the exclusive slot was explicitly released before report/commit/PR work.

## Review checklist

Reviewed the code-style checklist against the final source slice: one structural membership owner;
no local outcomes, transactions, lifecycle/stamp plumbing, UI seam or schema change; no parser,
cache, forwarder or temporary scaffold. Typed identity domains stay separate. New tests assert
behavior at the allocation/remap/position/query boundary. Existing prepared-owner, typed editor,
transaction and generator tests are exercised without editing their other-lane fixtures. Callback
short-circuiting remains the capacity boundary; no reference reserves a flat declaration ID.


Coordinator-confirmed remaining scope (2026-10-29): root owns follow-up review of
`src/core/document.cpp:1925` (diagnostic-location inventory), `src/core/document.cpp:2099`
(resolution point transforms), and `src/core/semantic_layer_editor.cpp:155` (flat declaration
identity/cap scan). These are explicitly deferred outside D. Root will migrate eligible common
membership or ratchet the retained form with domain-specific exclusions. The seven-consumer
migration is complete in scope; this is not a whole-core sweep.
