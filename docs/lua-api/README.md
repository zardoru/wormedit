# Scripting in Wormedit

[Back to the user manual](../../index.html).
These pages describe **API version 1**, available in builds configured with
`WORMEDIT_ENABLE_LUA=ON` (Lua 5.4.8). Documentation is included even in builds
without the optional runtime. The tables here describe values supplied to scripts;
these files are documentation schemas, not Lua modules to `require`.

## Choose the right context

* **Note generators** run a chunk with immutable globals `wormedit` (document
  snapshot) and `generator` (recipe configuration), and return note/layer proposals.
  See the **snapshot** and **generator** modules.
* **SV recipes** return a function `function(p, ctx) ... end`, or a module of
  factories selected by the recipe's entry point. They receive **curve** contexts,
  not the generator globals. `values(function(p, ctx) ... end)` is also accepted.

The examples section contains complete source files that are exercised by the
Lua tests. Copy the generator into the generator source file picker and use
**Run**, then **Apply**. The curve example is a returning chunk for an
SV recipe; use pure mode. The factory example requires entry point `rise` and a
numeric parameter `height`.

## Project entry point and modules

For several scripts, select `project.lua` in the same generator inspector. It declares
`worm_project "Name"`, ordered `include "path.lua"` calls, and optional named
`audio`, `image` and `midi` assets. See the **project** module for the declaration API.
The name labels the script group. Loose note/layer proposals target the selected chart.
Modules using the **chart** declarations instead create owned difficulties; do not mix
those two output forms in one run.

Copy [project.lua](../sources/project.lua) and [generator.lua](../sources/generator.lua)
into the same directory inside your native project, choose `project.lua`, then **Run**
and **Apply**. Includes resolve relative to each including file. Helper return values
are preserved, modules execute once in order, and returned note/layer proposals merge
into one transaction. Give outputs unique stable keys across all modules.

Named assets expose paths, hashes and imported IDs via `project.assets`; declarations
do not import files. `asset_id` identifies media; note `sound` fields take separate sound
resource IDs. Import assets and choose MIDI inputs using the existing UI.
All files must resolve inside the project directory, including through symlinks.
No absolute module paths or globs are accepted.

Live Run discovers/replays the graph and then evaluates/replays a frozen source bundle,
sharing the normal instruction budget equally between these phases. Discovery itself
executes bounded Lua before changed-dependency confirmation, without publishing edits.
Confirming a change uses the captured bundle. **Run saved source snapshot**, opening,
saving and exporting never reread scripts/assets. A failed run preserves materialized
output. Per-file hashes and asset metadata are stored inside the normal source snapshot.
Limits: 64 kind/file entries, 256 path aliases, 64 KiB aggregate Lua source, 64 KiB final
bundle including wrappers/escaping, and 64 MiB aggregate asset bytes. Cancellation is
checked between host read chunks. An oversized graph must be reduced or split into
separate recipes.

## Generated difficulties

Run [easy.chart.lua](../sources/easy.chart.lua) directly, or include it from project.lua.
The **chart** module documents measures, exact helpers and timing declarations.
The inspector shows per-difficulty counts rather than overlaying new charts on the
launch chart. Apply creates the batch in one Undo step; use Project output groups
to select its export destinations.

Keep difficulty `stable_id`, measure `id` and event `key` values stable to retain
native identities on regeneration. The recipe owns the name, resolution, offset,
measures and their notes/BGM, lane layout/input, BPM, pauses, warps and scroll.
Manual changes in those fields reject regeneration. Other authored content is
preserved, but removing its referenced measures rejects the entire batch. A grid
resolution change is rejected while authored positioned content remains. Omitted
difficulties are removed only when they contain no extra authored content.
Detach the recipe to release ownership while keeping its charts.

Bare Lua numbers are rationalized with absolute error at most 1e-9; the preview
reports the selected fraction and error bound. Exact helpers avoid that conversion.
No conversion rounds a position onto a grid: fractional rows still reject.
Limits include 32 difficulties and 4,096 measures per difficulty, plus the existing
output and VM budgets. Project entries and `.chart.lua` files allow inspected
results up to the 8 MiB VM memory cap; ordinary loose generators retain 64 KiB.

## Sandbox and standard libraries

Scripts have Lua's base, `string`, `table`, `math` and `utf8` libraries.
`io`, `os`, `package`, `require`, `debug`, and `coroutine` are unavailable.
`dofile`, `loadfile`, `print`, `warn`, `collectgarbage`, `getmetatable`, and
`setmetatable` are removed. `load` accepts bounded **text strings** only, with no
binary chunks or reader functions. `pcall` and `xpcall` cannot evade cancellation
or instruction limits. There is no general host filesystem, clock, network, or console API. The project loader
above grants only the explicit, bounded project-local include/asset preparation capability.

Snapshots are recursively immutable proxies: indexing, `#`, `pairs` and `ipairs`
work; assignments (including raw/table-library mutation attempts) fail.
Ordinary tables created by the script remain editable. Optional fields are absent
(`nil`), and arrays start at **1**. No snapshot contains live document pointers.

`math.random()` returns a number in [0, 1); `math.random(n)` returns an integer
in [1, n]; `math.random(lo, hi)` returns an integer in that inclusive interval.
`math.random(0)` draws a full-width signed integer. Wormedit replaces Lua's RNG
with seeded SplitMix64. `math.randomseed(first[, second])` explicitly reseeds; calling it without arguments
restores the recipe seed.
there is no clock-derived default seed. A fixed recipe seed reproduces each fresh
run. Keep output keys independent of table/function addresses or iteration order.
Pure SV query functions must not consume RNG or mutate retained state; ordered
mode permits both and replays the same fixed schedule.

Default session limits are **8 MiB** of Lua memory, **1,000,000** VM instructions,
and **64 KiB** each for source and inspected result size. Instruction and
cancellation budgets include setup and replay; they do not reset for each call.
These are VM budgets, not wall-clock preemption of C library operations.
A host-observed failure closes that session. Document snapshots additionally cap
construction at **65,536 nodes / 8 MiB**. Large inputs can therefore fail before
source execution. SV contexts cap chart entries and query counts at 65,536.

## Exact positions and identifiers

The [manual's introduction to rows, beats and snap](../../index.html#chart-rows)
explains the musical grid. One standard measure is four quarter-note beats.
If `divisions_per_standard_measure` is 960, one quarter-note beat spans 240 rows;
use the snapshot's actual resolution rather than hardcoding that example.
A measure's local rows start at 0 and end at `row_count - 1`. The next measure
starts at the preceding measure's `start_row + row_count`.
For a local position, `absolute_row = measure.start_row + position.row`.
Row distances are musical distances; timing events determine elapsed seconds.

In `wormedit`, IDs are decimal **strings**. General unsigned counters are Lua
integers up to `INT64_MAX`, otherwise decimal strings. Exact rationals are
`{num = numerator, den = denominator}`, with a positive denominator. Do not pass
IDs through `tonumber`: large IDs lose precision. Copy their strings unchanged.

Generator output accepts unsigned Lua integers or decimal strings for IDs and
row positions. Rows are local to a measure, start at **0**, and must lie on the
chart's exact grid. No automatic floating-point rounding is performed.
`divisions_per_standard_measure` and each measure's `row_count` describe that grid.
SV curve contexts instead use absolute chart rows and numeric timing values;
see **curve** before reusing generator snapshot code there.

## Execution, publication and reproducibility

Loading and saving a project never runs scripts. Preview builds a proposal;
Apply publishes accepted changes as one undoable transaction. Regenerate reuses
stable output keys and IDs, adds new keys, and removes omitted owned keys.
Manually changed or deleted generated content is protected by witness checks:
resolve the conflict rather than assuming regeneration will overwrite it.
Loose generators target their recipe's chart, including the pinned BMS outcome.
Difficulty declarations create separately owned charts.

Source text, API version, content hash, seed, parameters, and declared inputs are
stored with the recipe. Scripts use the frozen source, not arbitrary live files.
Generators replay and compare typed output. SV pure mode replays queries in
reverse order and repeats them; ordered mode replays the original schedule.
Nonfinite curve values, nondeterministic output, unsupported versions, invalid
hashes, cancellation, and resource exhaustion reject the operation with diagnostics.
Prepared curve samples are immutable; composition/postprocessing do not run Lua.

## Generating SV data

The generator host accepts `sv_points` and `sv_spans` together with notes/layers.
Use `sv_point {key=..., at=at_measure(native_measure_id,row), value=...}` for explicit
held baseline values and `sv_span {key=..., from=..., to=..., value_from=...,
value_to=..., step_rows=...}` for persistent built-in recipes. A project include can
return these same arrays. Inside a difficulty declaration, the helpers append directly;
`scroll` retains its existing meaning and scripts remain API1.

Spans multiply the existing authored baseline within a half-open range. Explicit
points set that baseline and hold beyond their position, so write a reset point when
needed. Values are dimensionless multipliers, positions are integer chart rows, and
`step_rows` is a sampling interval rather than seconds or beats. Exact `ratio` values
avoid floating approximation; finite Lua numbers use the chart API's bounded
rationalization and exact-value report. Duplicate point rows reject, including rows
already occupied by unrelated authored points. Regeneration replaces only owned,
unchanged keys. Overlapping spans compose through the ordinary SV owner.

The generated difficulty witness owns its recipe list only when it declares spans.
Earlier scripts that declare no spans keep independently authored recipes outside
ownership. After declaring spans, editing that list blocks regeneration until Detach.
The current native schema25 retains the optional witnesses introduced in schema23;
earlier schema23 files migrate on load. Saving/loading and Undo/Redo never evaluate
scripts. Prepared scoped BMS uses existing native event adapters and rejects any
unsupported or unprovable edit with diagnostics. There is no flat mutable BMS mirror.

Only constant, linear and exponential persistent descriptors are exposed initially.
For other generator formulas, emit explicit points; use the separate Lua curve host
for persistent Lua function recipes. No closures or live VM state are serialized.
SPEEDS spans are available with `output="speeds"`; explicit generator points currently
write the scroll/SV baseline. Sampling and export retain their existing grid,
point-cap and target-format limitations. The runnable tutorial scripts
`sv/generate.lua` and `sv/points.lua` demonstrate both choices.

## Building and maintaining this reference

`cmake --build build --target lua-api --parallel 2` generates this book with pinned
LDoc 1.5.0, Penlight 1.14.0, LuaFileSystem 1.8.0, and the same pinned Lua source as
the optional runtime. No system Lua or LuaRocks installation is required.
The native build-time runner is separate from the application's target toolchain.
`manual` depends on this target, and the `Manual` install component includes all
HTML, styles, examples, and dependency license notices under `manual/lua-api/`.
The ordinary manual opt-out (`WORMEDIT_INSTALL_MANUAL=OFF`) also disables this build.
For offline dependency checkouts use `WORMEDIT_LUA_SOURCE_DIR` and CMake's
`FETCHCONTENT_SOURCE_DIR_WORMEDIT_LDOC`, `..._WORMEDIT_PENLIGHT`, and
`..._WORMEDIT_LFS` overrides. Match the pinned revisions in
`cmake/dependencies/ldoc.cmake`. Rebuild after editing schemas or examples.
