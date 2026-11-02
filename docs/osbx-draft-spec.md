# OSBX Lua DSL draft specification

Status: design draft 0.2. OSBX 0.1 conventions were accepted by the owner on 2026-10-31, including the shear/matrix export amendment. Unimplemented syntax remains a design contract, not a claim of shipped support.
Implementation plan: [OSBX task cards](chart-editor-plan/tasks/osbx.md) (T-180–T-192); the settled conventions at the end record T-180 / OSBX-0.

OSBX is wormedit's extended osu!-style storyboard source format. It uses a Premake-style Lua DSL to declare or generate storyboards with stable IDs, keyframes, parent/child groups, ordered compound transformations, and 2D affine matrices. The same project DSL may declare chart notes, but storyboard modules and note/difficulty modules can remain separate files and export independently.

Lua evaluates into a typed intermediate document. Preview, visual editing, standard `.osb` export, and video rendering consume that document rather than interpreting source text independently.

## Implemented storyboard path (OSBX-1a–9, 2026-10-31)

On the post-1.0 `storyboard-osbx` branch, groups and image sprites now run through
Lua declarations → generator diff → one Apply/Undo transaction → native save/load
→ affine player preview → standard osu! export. Ordered translate, rotate, scale,
pivot, skew and six-coefficient matrix tracks, opacity/colour, supported easing,
node provenance and generator ownership persist. Native loading does not execute Lua.
The evaluator uses signed integer milliseconds and clockwise degrees in a Y-down canvas.
The preview preserves shear through full affine quads; existing OSB scenes retain their path.
OSBX image paths also preview without prior resource imports: the presenter owns
transient decode identities, skips persistent resource IDs, and uses the existing
bounded asynchronous image decoder/cache. It resolves safe project-relative files
through shared media inspection. Aliases share decoded sources. Bindings and
borrowed authored paths clear with document/index or project-directory changes;
completion generations also invalidate texture content. No transient resource or
asset is written to the document. Registered resources keep their existing
resolution and marked-missing policy. Missing/unsafe/undecodable fallback files
show diagnostics while usable images and lanes keep rendering; invalid graphs,
frame failures and sprite-cap errors remain structural preview failures.


osu! export classifies exact, baked and unsupported sprites and reports baked IDs
and normalized worst error. Bounded baking checks image-corner error
≤0.5 px and opacity/colour error ≤1/255 at every integer millisecond in the
bounded span. Image dimensions and the playback span come from export preflight.
Shear and explicit matrix operations **always refuse**, naming the operation ID;
no outputs are returned or published. No rasterization fallback is implemented.

The runnable [Sunshine example](lua-api/examples/storyboard/README.md) includes
`project.lua`, a small chart, rotating sun rays, an orbiting worm, fades, colour
and original PNG/SVG artwork. `banner-project.lua` adds a sheared banner as an
expected refusal case. `tests/lua/osbx_acceptance_tests.cpp` freezes the real
modules, generates/applies, saves/loads, compares frame matrices at three times,
exports/reparses `.osb`, compares sprite count and positions within 0.5 px, and
checks banner refusal and broken-module preservation.

Clips, OSBX animation/sample execution, loops/triggers, scripted links, video and
StepMania storyboard adapters remain follow-up work. Their syntax elsewhere in
this draft is a design contract, not part of this implemented subset.

## Implemented project entry point (T-186)

Project-level loading shares the host-controlled generator path with the implemented OSBX subset.
The initial adapter accepts `project.lua` through Compose's existing Lua note-generator
file picker. Loose note/layer output targets the selected chart. `worm_project "Name"` names the script group;
it does not rename or create a native project. `include "path.lua"` evaluates modules in
explicit order, relative to the including file, once per canonical file. A module may
return an existing API-v1 `{notes, layers}` proposal or ordinary helper values; proposals
are merged once into one preview/Apply transaction. Stable output keys must be unique
across all modules. T-187 also accepts measure-based difficulty declarations, described below.

`audio`, `image`, and `midi` accept `{id = "name", file = "relative/path"}`. Named assets
are available as `project.assets.name` with `kind`, project-relative `file`, `content_hash`
and optional decimal-string `asset_id` if already imported. Declarations hash files;
they do not import/decode them or grant filesystem access. Use the existing UI to import
assets and select generator MIDI inputs. Includes and assets must resolve inside the native
project directory, including through symlinks. Missing files, cycles and duplicate named
assets reject the batch.

An explicit live Run performs bounded graph discovery/replay, then evaluates/replays a
frozen bundle under the other half of the normal instruction budget. Discovery can execute
bounded Lua before changed-source confirmation, but cannot publish edits. A changed module
or declared asset changes the saved source hash and requires the existing confirmation;
confirmation evaluates the captured bundle rather than rereading files. Saved-source runs,
project opening/saving and exports never reread modules. Source text, per-file hashes and
asset metadata are carried in the existing recipe source/hash fields; no schema migration
or persisted VM is needed. Limits are 64 distinct kind/file entries, 256 normalized aliases,
64 KiB aggregate source, 64 KiB final bundle (including the runtime/escaping), and 64 MiB
aggregate asset bytes. Host IO checks cancellation between bounded reads.

## File roles

Settled file conventions:

- `project.lua`: optional native project entry point and ordered module includes;
- `*.osbx.lua`: storyboard-only modules, each file its own entry point;
- `*.chart.lua`: chart/difficulty and note modules, each file its own entry point;
- imported MIDI, image, and audio files: declared project assets.

A project can include any number of storyboard and chart modules:

```lua
worm_project "Example Song"

audio { id = "music", file = "song.ogg" }
midi  { id = "arrangement", file = "song.mid" }

include "storyboard/main.osbx.lua"
include "charts/easy.chart.lua"
include "charts/hard.chart.lua"
```

Includes resolve relative to the containing module. Their evaluation order is explicit and stable. Glob includes, if added, are sorted by normalized path before evaluation.

Standalone storyboard and chart modules can also be loaded/exported without a `project.lua` wrapper when all required assets and metadata are declared or supplied by the host.

## Language execution

The DSL runs in a versioned Lua environment. It exposes wormedit declarations, math/table/string primitives, deterministic random-number generation, and read-only structured asset APIs. Generator code does not need to build text lines.

The host records:

- DSL API version;
- Lua source files and content hashes;
- declared asset IDs and hashes;
- user parameters;
- deterministic seed;
- generated object provenance and last successful materialized output.

Opening a project uses its materialized chart/storyboard documents. Lua reruns only through an explicit preview/regenerate action. A failed generator leaves the prior materialized result intact.

## Multiple difficulties and independent output

One song may declare several named difficulties, including the several charts emitted into one SM/SSC file:

```lua
song "Example Song"

difficulty "Beginner"
  target { "sm", "ssc" }
  mode "dance-single"
  columns 4
  measures(generate_beginner())

difficulty "Challenge"
  target { "sm", "ssc", "osu-mania" }
  mode "dance-single"
  columns 4
  measures(generate_challenge(midi_asset "arrangement"))
```

Difficulty names and stable IDs are distinct. SM/SSC export may collect compatible difficulties into one song file; BMS, BMSON, and osu!mania adapters may emit one target chart per difficulty according to their format rules.

Storyboard modules do not need to contain note data. Chart modules do not need to contain storyboard data. A combined Lua generator may emit both in one atomic preview when coordinated generation is desired.

## Measure-based chart declarations

**Implemented T-187 subset:** difficulty declarations create owned native charts, matched
by `stable_id` during explicit regeneration. They can run directly as `.chart.lua` or
inside `project.lua`; unrelated charts remain unchanged. The packaged [chart API
schema](lua-api/chart.lua) is authoritative for currently accepted fields. Chart name,
grid, offset, measures/notes/BGM, layout/input, BPM, pauses, warps and scroll are witnessed
ownership. Manual changes there reject regeneration. Independent authored content remains;
removing its referenced measures rejects, as does changing resolution while authored
positions remain. Omitted charts with extra authored content cannot be deleted implicitly.
The existing Project output groups select export destinations; `song`/`target` declarations,
combined storyboard output and the typed samples shown in the draft examples below remain
future work. Existing numeric sound-resource IDs are accepted. Exact source spelling is
retained in the frozen source snapshot, not duplicated as mutable per-event metadata.


Chart note data is authored as an ordered series of `measure { ... }` descriptor tables. A measure table mixes named parameters with ordered lane-object descriptors returned by `note`, `hold`, and `bgm` constructors:

```lua
difficulty "Challenge"
  stable_id "challenge"
  resolution 960 -- divisions per standard 4/4 measure
  offset(measure_time "0.125") -- beat zero occurs 125 ms into the audio
  columns 4

  measure {
    id = "intro-1",
    length = measure_length "1.0",
    meter = meter(4, 4),

    note { row = 0,   lane = 1, sample = bms_wav "01" },
    note { row = 240, lane = 2, sample = audio_file "keysounds/snare.ogg" },
    hold { row = 480, lane = 3, end_at = at_measure("intro-2", 120) },
    bgm  { row = 720, sample = osu_sample { set = "normal", index = 2 } },
  }

  measure {
    id = "intro-2",
    length = measure_length "0.75",
    meter = meter(6, 8), -- retained as 6/8; it is not displayed as 3/4
  }

-- Global tracks are declared outside measure tables.
bpm    { at = at_measure("intro-1", 0), value = 150 }
stop   { at = at_measure("intro-2", 0), length = rows(60) }
warp   { from = at_measure("intro-2", 240), to = at_measure("intro-2", 360) }
scroll { at = at_measure("intro-2", 0), value = 0.75 }
```

`resolution` is one difficulty-wide count of divisions in a standard 4/4 measure; it defaults to 960. `length` uses BMS measure-length units: `1` is four quarter-note beats, `0.75` is three, and `1.25` is five. The preferred exact forms are `measure_length "0.75"` for familiar BMS decimal spelling and `measure_length "3/4"` or `ratio(3, 4)` for an explicit ratio. They produce the same reduced internal rational while retaining useful source spelling as provenance. For a measure with exact length `a/b`, its capacity is `resolution * a / b` rows and must be integral. Valid local rows are integers in `[0, capacity)`. The boundary `row = capacity` is written as row zero of the following measure instead of having two spellings for the same position. Measure declaration order defines chart order, while the optional unique `id` supplies a stable anchor for cross-measure holds and chart-level tracks.

`meter(n, d)` carries musical grouping and preserves the explicitly entered numerator and denominator without fraction reduction. It also derives a required length of `n/d` standard measures: `meter(6, 8)` derives `3/4` or BMS `0.75`, while the UI and project continue to show `6/8`. A table may omit `length` and let an explicit meter derive it, or provide both. If both are present they must be exactly equivalent. A slash-form value entered through a general UI field is disambiguated as **BMS ratio** or **musical meter** before commit; a DSL author chooses explicitly with `measure_length`/`ratio` versus `meter`.

A bare Lua numeric length such as `length = 0.75` is accepted for convenience, but Lua exposes it as binary floating point after parsing. The host converts its exact IEEE-754 value with an overflow-checked bounded continued-fraction/best-approximation algorithm. The algorithm has a fixed iteration cap, a configured maximum denominator, and explicit no-progress termination; it never waits for a floating remainder to become exactly zero. Preview shows the chosen fraction and error when the numeric input was not exactly representable. An exact string or `ratio` is required when that error exceeds the DSL profile's tolerance. Zero, negative, non-finite, overflowing, or over-limit values reject the full generator transaction.

The named part of a measure contains measure-local parameters such as stable ID, exact length, optional display meter, and later explicit per-measure lane presentation. It does not contain BPM, stop, delay, warp, SV/scroll/speed, kiai, bookmark, or other cross-measure timing declarations. Those are separate ordered chart-level tracks whose `at`, `from`, and `to` values use measure IDs/indices plus local rows. Inserting, deleting, or resizing a measure can therefore update the shared exact position index without rewriting Lua events into another ownership model.

The difficulty-level `offset` is a signed exact duration locating beat zero in the audio: positive means beat zero occurs after the audio begins. `measure_time "0.125"` parses decimal seconds exactly and retains the spelling. It is outside measure tables because it changes the relationship between the entire chart and audio. SM/SSC serialize it as native `OFFSET`; BMS serializes the `#OFFSET` extension; osu!mania/BMSON conversion must shift target-native timed data exactly or produce a blocking diagnostic.

Constructors return typed descriptors; they do not mutate the live document as Lua evaluates. `measure { ... }` validates the whole table, preserves same-row declaration order, and adds one proposed measure to the generator transaction. `measures(sequence)` accepts a deterministic array or iterator result of the same descriptor tables for procedural and MIDI-driven generation. A failure rejects the entire proposed regeneration and leaves the last materialized chart unchanged.

BMSON materialization flattens each `(measure, row)` through exact prefix lengths into an absolute fraction of a standard measure. If editor resolution is `D` and output BMSON resolution is `R` pulses per quarter note, absolute editor row `r` maps to `y = 4 * R * r / D`. The writer selects or promotes `R` until all notes, hold lengths, timing events, and emitted measure-boundary `lines` are integral. The common editor values map as `D = 960 -> R = 240` and `D = 192 -> R = 48`. It groups the flattened sound events by resolved source audio into BMSON `sound_channels`, maps BGM to `x = 0`, emits continuation/restart as `c`, and never serializes Lua measure tables as non-standard BMSON fields. Display meter remains native-project metadata because BMSON carries bar lines rather than time signatures.

## Audio sample references and writer allocation

Chart notes and BGM events accept a typed `sample_reference_t` directly. The initial alternatives are:

- `osu_sample_reference_t`: osu!-style normal/addition set, custom index, volume, and optional custom filename;
- `bms_wav_reference_t`: raw BMS WAV index plus an optional source chart/difficulty ID, defaulting to the current imported chart context;
- `audio_file_reference_t`: a declared asset ID or project-relative audio file reference that the host interns as an asset during evaluation.

Provisional constructors as ordered entries inside a `measure` table:

```lua
note { row = 240, lane = 1,
       sample = osu_sample { set = "normal", addition = "clap", index = 2 } }

note { row = 480, lane = 2,
       sample = bms_wav "0A" }

bgm  { row = 720,
       sample = audio_file "keysounds/riser.flac" }
```

The shorthand `bms_wav "0A"` addresses the current chart; a table form supplies another stable source chart ID. A direct file is resolved relative to its module/project and becomes a declared, hashed project asset before materialization. Persisted recipes do not rely on an arbitrary working directory or grant Lua unrestricted filesystem access.

The typed reference remains on the materialized note/BGM event. Export invokes one deterministic writer-allocation pass that produces a `resource_plan_t` before output bytes are written:

1. resolve each typed reference through its source profile and shared resource catalog;
2. retain an explicit BMS WAV slot or osu! sample descriptor when it is valid and unconflicted for the target;
3. reuse an existing target resource when content identity, clip/slice semantics, and target sample behavior are compatible;
4. allocate remaining BMS base-36 slots, BMSON sound channels, or osu! custom sample filenames in stable difficulty/event order;
5. report the final mapping, copied or referenced files, reuse decisions, and any loss or conflict for preview;
6. serialize only after the resource plan validates.

Two explicit BMS WAV references using the same index for different audio are an error until the author remaps one. Identical audio does not automatically collapse two retained source identities when doing so would break a promised textual or source round trip; newly allocated and cross-format references reuse compatible resources by default. Filename collisions, exhausted slots, missing source charts, indices, or files, unsafe paths, unsupported codecs, and target-incompatible slices reject the proposed output without changing the materialized document or existing files.

## Storyboard declaration example

```lua
storyboard "Main"
  api_version "0.1"
  canvas { width = 640, height = 480 }

image { id = "logo", file = "sb/logo.png" }

group "title_group"
  layer "foreground"
  transform.translate { 320, 240 }
  transform.rotate {
    key { 0, value = 0, easing = "linear" },
    key { 2000, value = 360, easing = "in_out_sine" },
  }

sprite "title"
  parent "title_group"
  image "logo"
  origin "centre"
  transform.matrix {
    key { 0, value = { 1, 0, 0, 1, 0, 0 } },
    key { 2000, value = { 1, 0.2, 0, 1, 0, 0 } },
  }
  opacity {
    key { 0, value = 0 },
    key { 250, value = 1 },
  }
```

Premake-style context calls such as `storyboard`, `difficulty`, `group`, and `sprite` select the current object. Indented layout is conventional, not syntactic. Block/table arguments are used where ordering or nested values matter.

The exact callable names are provisional. Their emitted typed schema and evaluation rules are normative once versioned.

## IDs and provenance

Every emitted song, difficulty, storyboard, node, transform operation, reusable clip, keyframe, and asset has a stable string ID. The DSL may derive an ID from an explicit declaration name when unique. Generated loops should provide stable keys so rerunning a generator can diff objects rather than delete and recreate them.

OSBX node IDs remain strings. Exported OSB sprites receive a `uint64` `stable_id` computed as FNV-1a-64 of the node ID path. A collision rejects the entire export with the responsible paths; export does not silently renumber objects.

Every emitted object records its module/source location and optional generator key. Diagnostics and regeneration diffs point back to this provenance.

## Coordinate and time systems

The default storyboard canvas uses osu!'s 640 by 480 logical coordinates. Positive X points right and positive Y points down. Output rendering scales this canvas according to an explicit fit policy.

Portable storyboard keyframe time is signed integer milliseconds for OSB compatibility. The DSL may place keys through helpers such as `at_beat`, `at_sample`, and MIDI tick conversion; the emitted keyframe retains the resolved millisecond value plus project-only authoring provenance where available.

Video frames use rational presentation timestamps. Evaluation derives each timestamp from frame index and frame-rate numerator/denominator rather than accumulating floating-point frame deltas.

## Scene graph and layers

Visual nodes are `group`, `sprite`, or `animation`. Timed storyboard audio uses `sample` nodes. Parent references form an acyclic graph. Complete `animation` and `sample` nodes carry optional canonical metadata (native schema 39). Metadata-absent legacy typed placeholders remain representable but incomplete and refuse execution/export with a node-ID diagnostic. Animation uses the shared exact frame query and preview catalog; Sample exposes cues through the existing schedule, without audio playback. Unsupported media/temporal compositions refuse explicitly.

A node may additionally own an explicit inclusive lifetime `{begin_ms, end_ms}` (native schema 30); absence is unbounded. The shared evaluator combines this range with ordinary visibility and inherited parent visibility. An exact end instant is visible, but any positive fractional remainder at `end_ms` is outside the range. Reversed bounds reject; negative, extreme signed millisecond bounds and single-instant ranges are valid. Key editing never silently changes this range. The typed set/clear lifetime edit is undoable; SB-5 exposes it in the inspector. Older schema-29 nodes migrate to an absent, unbounded lifetime.

Standard visual layers are `background`, `fail`, `pass`, and `foreground`. Within a layer, draw order uses explicit order followed by stable declaration order. Cross-layer parenting is invalid in OSBX 0.1 because inherited transforms and standard OSB conditional ordering would be ambiguous.

Groups do not imply an offscreen compositing surface. They apply inherited transforms, opacity, and color to descendants.

## Matrices and compound transforms

OSBX uses column vectors and serializes a 2D affine matrix as:

```text
[ a, b, c, d, tx, ty ]

[ a c tx ] [ x ]
[ b d ty ] [ y ]
[ 0 0  1 ] [ 1 ]
```

Thus `x' = a*x + c*y + tx` and `y' = b*x + d*y + ty`.

A node owns an ordered transform stack containing:

- translation;
- clockwise rotation in degrees in the Y-down canvas;
- non-uniform scale;
- X/Y skew;
- an explicit affine matrix;
- a pivot wrapper around nested operations.

Operations apply to a point in declaration order. With column vectors, the local matrix is `M_n * ... * M_2 * M_1`. World transform is `parent_world * local`.

Stack order is visible and reorderable in the editor because transform operations do not generally commute.

## Keyframes

Each transform operation and property track has a default value plus ordered keyframes. A key contains stable ID, time, value, and easing.

Initial easing forms are `step`, `linear`, standard OSB easing values, and cubic Bézier control points. Numeric/vector values interpolate component-wise after easing. Rotation values are continuous authored degrees; the evaluator does not silently choose a shorter wraparound. Raw matrix keyframes interpolate their six coefficients component-wise only; OSBX 0.1 has no decomposed interpolation mode. Authors should use component transforms for rigid motion.

Spatial Bézier authoring is a declaration/editor operation, distinct from timing easing.
`keys.bezier { from, to, points, tolerance }` accepts four scalar or vector2 control
points and returns ordinary linear keys. The inspector exposes the same baker for
translation, continuous-degree rotation and scale; its inclusive range replacement is
one transaction, retains outside keys, and refuses Lua-linked or temporal-program tracks.
Uniform time parameterization and adaptive De Casteljau subdivision use a control-hull
bound on Euclidean error relative to the linear time chord, with a conservative binary64
rounding allowance. Requests below that supported numeric precision refuse explicitly. Tolerance is positive and
finite in authored value units (Lua default 0.5); it is not a world-space export metric.
Integer-time resolution, 4096 output keys and a maximum duration of 2147483647 ms are
hard bounds: an unrepresentable request refuses instead of silently reducing accuracy.
The existing runtime, native schema 39 and export pipeline consume the baked keys; no
curve handle metadata, new persisted interpolation kind or runtime path cache is added.

Initial property tracks include opacity, linear RGBA color multiplication, visibility and blend mode. Blend modes are `normal` and `additive`; additive requires renderer support in OSBX-3. If the renderer cannot support it, only the owner may narrow 0.1 to `normal`. Animation frame identity is a numeric value on the evaluated draw, safe to retain across frame-arena resets; its selected filename is derived only in the current adapter arena.

Reusable clips use `clip { id, ops, tracks, parameters, uses }`, grouping ordered transform operations and property tracks. `use_clip { id, clip, start, time_scale, parameters }` appends an instance to the current node; nested `uses` are ordered literal descriptors with the same fields. A parameter has a unique lexical name and a finite vector default; instance bindings supply literal vectors of the same arity. A numeric template track may select a parameter for its default only. Authored keys/easing retain their ordinary meaning; there are no parameter expressions or implicit outer capture. Raw Lua links inside templates reject, while ordinary linked nodes and prepared children retain their existing ownership.

Node operations run first, followed by ordered clip operations and nested uses. Local time is `(parent_time - start) / time_scale`: before activation the contribution is identity, and afterwards keyed tracks hold normally. Opacity and RGBA multiply, visibility is ANDed, and the last active explicit blend override wins. Absent clip blend leaves the current blend unchanged. Parent/node lifetimes and conditional choice still gate the result. Recursive or missing references, colliding identities, invalid bindings and aggregate expansion/storage/work exhaustion reject before materialization. One immutable compiled owner retains bound tracks and clock maps across evaluator-index copies; frame evaluation creates no second cache, clock or Lua execution.

`time_scale` is the exact stored positive finite binary64 ratio. Clock composition uses checked bounded factors/products and the shared signed64 whole-ms plus reduced uint32 fraction domain; unrepresentable maps reject explicitly. In particular, a decimal scale such as `0.1` can refuse when a mapped endpoint's exact reduced fraction exceeds that capacity. Authored integer keys are never retimed by rounding. Native schema 35→36 adds definitions, uses and selectors, not compiled expansions; earlier records migrate empty defaults and future clip fields under older schemas refuse.

OSB export consumes the same compiled expansion and conservatively bakes used clips under existing error, density and work checks, retaining both conditional branches. Fractional mapped activation/key times and unsupported transform/property combinations refuse instead of disappearing. Selected used-clip StepMania actors return an explicit unsupported storyboard advisory before sidecars; unused definitions and ordinary actor output remain unchanged. Chart export keeps its existing loss-handling route. Clip editing UI remains separately scoped below.

Typed OSBX tracks accept flat `loop { start, count, keys }` and `trigger { name, from, to, keys }` programs (native schema 38), using the existing key/easing owners. A sealed immutable direct-firing scenario is copied, validated and sorted once; binary queries reuse it through track/index/frame/op-space/clip evaluation. It is not inferred from gameplay or the independent fixed Pass/Fail/Neither layer selector. Exact relative clocks, finite capacities and authored tie rules apply. There is no recursive temporal-key grammar or custom scenario UI.

Imported standard OSB `T` blocks are structural containers, separate from that planned OSBX execution model. Supported names are `Passing`, `Failing` and documented `HitSound` combinations of up to two sample-set tokens, an addition and a nonnegative custom sample number in signed32 range. Accepted spelling is retained; unsupported names stay raw with a diagnostic. Optional signed integer start/end and authored signed32 group retain omission and explicit zero/negative values. Header numbers and indentation normalize. Native schema 36→37 adds trigger metadata without renumbering older node kinds or changing ordinary node fields; older records refuse future trigger kind/metadata.

Leading spaces, tabs and underscores are recognized only as indentation. A malformed or unknown trigger header, or any malformed descendant, retains the whole bounded original raw subtree; child commands never become unconditional. Validation counts ignored descendants. Preview, properties and scheduling ignore trigger branches, and loops containing only triggers have no drawable sprite lifetime; genuinely empty/comment-only loops keep their existing envelope. Sample cues, Animation frames and the fractional clock retain their existing owners. A Pass/Fail/Neither layer choice does not fire gameplay triggers. Cut rebasing and conversion to OSBX refuse trigger-bearing content until a supported mapping exists; ordinary OSB/osu! emission preserves its structural blocks and both conditional layers.

## MIDI and generator API

`midi_asset(id)` returns immutable structured MIDI data parsed by the host: timing division, tracks, channels, note intervals, pitch, velocity, controllers, tempo events, time signatures, markers, and track names where available. Exact MIDI ticks remain integers.

The API supplies explicit conversion helpers among MIDI ticks, metric time, chart measure/division positions, and storyboard milliseconds. Reading MIDI does not automatically change chart timing.

A generator transaction may emit:

- notes/holds and semantic layer assignments for one or several difficulties;
- storyboard nodes, transform stacks, clips, and keyframes;
- or both sets together.

The editor validates and previews the typed diff. Commit applies it as one undoable command; cancellation changes neither materialized notes nor storyboard data.

## Parametric scroll and speed generation

Chart modules may generate SV events (osu! SV, StepMania `SCROLLS`) or StepMania `SPEEDS` from a function of normalized parameter `p` over a selected chart interval. `p = 0` is the interval start and `p = 1` is its end. The interval declares whether interpolation follows musical position, chart time, or audio time; musical position is the default for scroll authoring.

Provisional Premake-style syntax:

```lua
scroll_curve "chorus-rise"
  range { from = at_measure(32), to = at_measure(40), domain = "beat" }
  output "sv" -- "sv" or "speeds"; "scrolls" is another name for "sv"
  sampling { mode = "adaptive", tolerance = 0.002, max_points = 256 }
  values(curve.exponential {
    from = 0.75,
    to = 2.0,
    shape = 3.0,
  })
  postprocess {
    normalize { mode = "preserve_displacement", value = 1.0 },
  }
```

`values` accepts a built-in curve descriptor, a Lua function `function(p, context) -> finite_number`, or a versioned generator entry point returning a function/closure. Recipes distinguish pure random-access evaluation from ordered stateful emission. Owner direction (2026-10-18): a stateful closure may keep counters, accumulators and seeded RNG state across cycles within one recipe evaluation. Mutable state belongs to that session, never to the editor frame, another recipe or a previous preview/export build.

The durable recipe stores its execution mode and declared emission/cycle plan alongside module source or a project-relative source reference, scripting API version, entry-point name, named parameters, declared input asset identities/content hashes and seed. It never serializes opaque Lua VM bytecode, registry references, stack state, userdata, ambient globals, wall-clock values or process state. Preview and export each create a fresh sandbox/session, instantiate the generator once and apply the same instruction/memory/cancellation bounds over the complete build. Rebuilding identical inputs and the same ordered emission plan must reproduce the same values and diagnostics.

Pure evaluators remain order-independent and support the existing adaptive sampling contract. Stateful evaluators advance in a specified cycle/emission order, once per declared emission; repeated-value suppression and composition do not invoke them again. Their prepared output is consumed by post-processing, composition, inspection and target compilation. Initially they require an explicit ordered sampling plan: arbitrary adaptive probing is incompatible and must reject clearly until a compatible strategy is specified. Behavior when composition needs positions absent from an emitted batch must be defined and tested in T-681 rather than silently re-running the closure. The [extension reference review](chart-editor-plan/tasks/extension-friction.md) records capabilities from sv-tools and blackhole-studio; their additional placement/output features are reference requirements for later scoped cards, not an implicit parity promise.

The context exposes immutable interval endpoints, baseline effective scroll at `p`, measure/timing data, declared parameters, seed and cycle/emission indices. Functions cannot mutate the document, directly publish events, perform I/O or retain host pointers. Pure-mode functions cannot change captured state and must be referentially transparent when samplers revisit points. Ordered stateful functions may update bounded sandbox-local closure and seeded RNG state only within the session described above; inspection, post-processing and rendering never advance that state.

Initial built-ins are:

**Implemented recipe storage (native schema 17).** `lua_expression` retains frozen source in
`expression_source` (1 to 16384 bytes, no NUL), plus an optional API-v1 `lua` descriptor.
Legacy descriptors default to pure mode with seed zero. Source references are project-relative
reload origins; evaluation consumes the embedded snapshot and performs no file I/O. The
inspector reloads and hashes source explicitly. Named module entries are factories receiving
`ctx` and returning the curve closure; inline sources may be a function expression, `values`
expression or a chunk returning a function. Native projects store the descriptor and source,
never sampled points or VM state. Lua-enabled application workers and the headless converter
supply the optional evaluator; builds without Lua retain storage and report evaluation unavailable.

API-v1 ordered cycles partition the fixed ascending musical-row schedule, including its end
inspection query. `p` spans the entire interval and does not reset per cycle. `cycle_index`,
`emission_index` and `cycle_emission_index` are 1-based in ordered mode; pure queries use zero
indices. The range remains half-open: the end query inspects the curve, while composition restores
the authored baseline at the range end. Prepared materialized output holds the preceding emission
between sampled rows. Pure adaptive probes use deterministic midpoint traversal; no live closure
survives preparation. `ctx.seed` is the full unsigned seed as a decimal string, with
`ctx.seed_integer` also present when representable as a signed Lua integer.

- `curve.linear { from, to }`, equal to `from + (to - from) * p`;
- `curve.exponential { from, to, shape }`, using the endpoint-safe normalized form `from + (to - from) * (exp(shape*p)-1)/(exp(shape)-1)`, with `shape = 0` defined as linear;
- `curve.constant { value }`;
- `curve.bump { start, length_a, x, y }`, a finite two-region plateau described below;
- `curve.dirac { at, area }`, an impulse primitive described below;
- `curve.multiply { value, curve }`, which scales another curve and is also the operation used by normalization.

The accepted OSBX parametric defaults are `adaptive`, tolerance `0.002`, and `max_points = 256` for pure evaluators. Ordered stateful evaluation still requires its explicit sampling plan. T-189/T-190 provide the sampling machinery, but the current native `sv_recipe_sampling_t` defaults remain fixed two-point sampling with tolerance `0.001`; the accepted DSL defaults do not change existing native recipes.

Fixed sampling declares an exact point count or exact musical step. For pure evaluators, adaptive sampling recursively subdivides until the piecewise interpolation stays within the declared error tolerance or reaches `max_points`. It always includes both endpoints, uses a specified deterministic traversal/order, and evaluates the same points on every platform. Sampling is transient preview/export state, not stored chart data. Recipe range anchors must be exactly representable at the chart resolution; generated output positions must be exactly representable by the selected target, and export preview offers a target-resolution promotion or explicit quantization policy when they are not.

The output family controls interpretation:

- `sv` evaluates as a generic instantaneous multiplier function that an exporter can sample into target points;
- `scrolls` evaluates with StepMania `SCROLLS`-style multiplier semantics;
- `speeds` evaluates with StepMania `SPEEDS` semantics plus an explicit transition duration and beat/time transition mode. A curve cannot target `speeds` until those transition fields are supplied or inherited from a declared policy.

### Post-process pipeline and length-preserving normalization

Preview or export sampling produces an immutable transient raw curve batch before normalization or target-specific compilation. An ordered `postprocess` list transforms a copy of that batch. Initial stages are `multiply { value }`, `normalize { mode, value }`, and `balance_bump { solve, target_average }`; later stages can be versioned without changing the curve-function contract. Stage order is significant and is shown in the preview. The recipe records the pipeline, while the transient raw and final sampled batches remain inspectable only in preview/export reports and derived caches.

The same post-process interface may use a selected existing SV/`SCROLLS`/`SPEEDS` segment as the baseline piecewise function. Applying it creates an editable recipe over that exact selected range instead of replacing the source segment with sampled points. This makes normalization a reusable explicit command instead of behavior embedded in each curve function.

Normalization is optional. `preserve_displacement` multiplies every processed value and impulse area by one scalar so the signed integral of effective scroll over the interval equals its value before applying the post-process chain. With baseline `b(p)` and the incoming multiplier `f(p)`, the scale is:

```text
k = integral(b(p), p=0..1) / integral(b(p) * f(p), p=0..1)
output(p) = k * f(p)
```

An optional `value` multiplies the preserved target integral, so `value = 1` retains the original displacement and `value = 2` requests twice that displacement. `preserve_distance` uses integrals of absolute effective scroll when negative scroll makes signed displacement cancel. `preserve_average` is an alias of displacement preservation over normalized `p`; it exists as UI wording, not a different calculation.

Built-in curves may retain an analytic-integral hint, but post-processing computes against the same transient typed batch that export will compile, including structural impulse areas and any prior post-process stages. Arbitrary Lua curves use the same deterministic sampling rules selected for emitted output, so preview, normalization, and export cannot disagree. A zero or non-finite denominator, overflow, or an output forbidden by the target profile rejects the preview/export batch without changing the recipe.

### Finite bumps

`curve.bump` divides the selected interval of total domain length `L` into two regions. The user supplies the exact length `L_A` of bump region A and its placement; region B is the remainder, so `L_B = L - L_A`. The curve has constant speed `X` on A and constant compensating speed `Y` on B. B may appear on both sides of an interior A, but its combined length is still `L_B`.

For a baseline multiplier of 1, preserving the segment's original scroll displacement requires:

```text
X * L_A + Y * L_B = L
```

Equivalently, with normalized lengths `a = L_A/L` and `b = L_B/L`, this is the requested invariant:

```text
X * a + Y * b = 1
```

The `balance_bump` post-process can keep the user-specified `X` fixed and solve the remainder as `Y = (L - X*L_A) / L_B`, or keep `Y` fixed and solve `X`. Alternatively, ordinary `normalize` keeps the ratio between supplied `X` and `Y` and multiplies both by `k = L / (X*L_A + Y*L_B)`.

For a varying baseline `b`, use baseline-weighted region integrals `B_A = integral_A(b)` and `B_B = integral_B(b)`. The preserved displacement is `D = B_A + B_B`, so balancing solves `X*B_A + Y*B_B = D`. The simple length formula is the unit-baseline case.

Require `0 < L_A < L` when solving the other region. A zero-length remainder, non-finite solution, or negative/zero speed forbidden by the selected target rejects the batch. The bump emits exact step boundaries; target compilation must preserve their same-position ordering.

### Dirac impulses

A Dirac delta is not sampled as an ordinary numeric function: a point sample would have either no integrated effect or an unbounded value. `curve.dirac { at = p0, area = a }` emits a typed scroll impulse with signed area `a`, creating a discontinuity in integrated visual position while consuming no parametric length.

Under the accepted OSBX 0.1 policy, every target rejects impulses unless the author explicitly chooses a finite box with a representable width. Its height is derived so its integrated area remains `a`; the generated entry/exit points and approximation error appear in preview. There is no implicit pulse approximation or triangular-pulse option in this policy. Existing native SV recipes retain T-162's 1 ms finite-box default; adopting the OSBX opt-in policy must not silently reinterpret those saved recipes. Coincident impulses combine only through an explicit policy, and impulses at interval boundaries have documented left/right ownership.

### Composition, preview, and recipe editing

Every enabled curve recipe evaluates to a multiplier. Where recipes overlap, multiply their outputs pointwise with the base authored scroll value in stable recipe order. Reordering affects deterministic evaluation/diagnostics but not the mathematical result while all finite multipliers commute; preserving an explicit order leaves room for versioned non-commutative post-process stages. A recipe never replaces or absorbs another recipe's source. The curve editor can enable/disable, reorder, duplicate, and edit recipes independently and shows each input curve plus the composed effective result.

The curve editor shows the source function, transient sampled control points, raw and post-processed values, each post-process stage, cumulative scroll displacement/distance, point count, target-resolution changes, and the exact typed events export would emit. Applying one curve or one post-process operation stores only the recipe as one undoable command. The stored recipe records API version, exact range anchors, domain, output family, built-in/function source identity, parameters, sampling settings, ordered post-process stages, stable composition order, enabled state, target profile, and hashes of timing inputs.

Range anchors are visual handles. Dragging either endpoint or the whole interval uses the active exact chart grid by default, previews without mutation, and commits one recipe edit; Escape restores the prior anchors. Timing or resolution changes re-evaluate recipes and may mark their preview/export report stale, but never rewrite a materialized SV/speed/scroll event set because none is stored. Only export materializes the composed function, and that compilation is read-only with respect to the project document.

## Visual editing of Lua output

The keyframe and generated-note editors operate on the materialized typed document. Canonically generated DSL modules may be rewritten by the editor, using one call per line and two-space indentation. For arbitrary procedural Lua, direct visual edits are stored as project overrides or require an explicit "detach/bake to editable DSL" operation; wormedit must not pretend it can safely rewrite general Lua source. Parametric SV/scroll/speed recipes are the exception described above: their functions and ranges remain directly editable and are never baked merely to display or save them.

The recorded override format is a per-object JSON patch keyed by stable ID. Regeneration shows how procedural output and visual overrides interact; conflicts require manual resolution. This storage convention is recorded for later implementation and does not add override editing to the Storyboard screen v1 scope below, which uses explicit detach.

## Standard OSB compatibility export

Standard `.osb` export flattens inherited transforms into descendants and classifies each object range:

- `exact`: maps to standard OSB commands/easing;
- `baked`: samples eligible shear-free compound motion into supported OSB commands within the limits below;
- `unsupported`: blocks export and identifies the responsible IDs.

Shear and matrix ops block `.osb` export; use video or StepMania export to retain them. A skew op or raw matrix op anywhere on a sprite or in its ancestry makes that object unsupported, even if its current matrix looks representable. Implicit shear from the hierarchy, including combinations of non-uniform scale and inherited rotation, also makes the object unsupported. These cases cannot be approximated or baked for `.osb`.

For eligible shear-free motion only, baking must keep maximum screen-space position error at the sprite corners within 0.5 px on the 640×480 canvas and opacity error within 1/255, using at most 4 keys per 100 ms per property. Exceeding either tolerance or the key budget makes the object unsupported. OSBX 0.1 has no rasterization fallback. Any unsupported object blocks the entire export with responsible IDs and no partial output writes.

Imported OSB source lines and ordering remain available as provenance for loss-aware saves.

## Native SSC actor export (SMX-2/SMX-3, post-1.0 branch)

Native project schema **31** adds `stepmania_export_policy_t.storyboard`
(`actors`, `video`, `none`) and the existing shared `storyboard_conditional_layer_t`
(`pass`, `fail`, `neither`). New policies default to actors/pass. The mandatory
30→31 migration sets existing policies to none/pass and materializes absent/null
StepMania policies with their previous effective file/timing/profile choices.
Strict schema-31 codecs require the new fields and reject unknown fields/enums.
Node lifetimes retain schema-30 semantics. This remains post-1.0 work.

The actor route targets the [pinned SM5.1/ITGmania and Etterna 0.75.1 trees](references/stepmania-storyboard-capabilities.md).
Etterna SSC Actors mode reuses the same native plan/runtime and folder hooks;
SMX-3's SOURCE48 service GATE passed all 6,332 tests and 215 changed-C++
complexity comparisons, including prepared conditional and atomic publication
regressions. Actual engine theme/background/seek/rate behavior remains unexecuted. Plain SM reports unsupported
native output except for the separate conservative NotITG SM/XML profile. Video mode
currently reports unavailable; it writes no pretend video. None intentionally
omits OSBX. Pass/Fail/Neither selects one shared conditional outcome, without
gameplay trigger inference; ordinary `.osb` include-all behavior is unchanged.

Etterna Actors mode reports one advisory when authored or prepared scenes contain
Pass/Fail drawables, naming the chosen fixed state and absence of gameplay-trigger
interactivity. The warning remains advisory under reject policy and preserves
existing object classifications and clipping/retained-hook/budget reasons. Ordinary
background/foreground-only scenes, other profiles, None and Video do not receive it.

Each drawable receives a bounded duplicated ancestor path in canonical global
draw order. Ordered transforms become wrappers; rotation uses authored degrees.
Simultaneous two-axis shear and raw affine matrices use two rotations and signed
diagonal scale, including reflections and rank-zero/rank-one matrices, rather
than sequential X/Y shear setters. Prepared immutable Lua results supply tracks
and subtrees. Engine frames run neither the generator nor an OSB parser.

Generated Lua 5.1 samples absolute `GetMusicSeconds()*1000`, with linear/step
tracks; callback deltas and queued property tweens do not drive scene state.
Nonlinear baking calls the canonical fractional evaluator/sampler and shared
`simplify_to_metric`, comparing ordered identities, images, blend, visibility,
full affine image corners, final clamped opacity and RGB in the existing normalized
640×480 space (0.5 px and 1/255). Integer-ms source/emission keys are rounded once
to nine decimals, at most four keys per half-open 100 ms per track; final full-scene
checks cover the reported inclusive quarter-ms grid across playback and authored
track tails. This is a sampled-domain result, **not a continuous-time certificate**;
missing geometry, invalid reconstruction or exhausted work refuses the object.

Direct native actors have no certified canvas clipping or AFT compositing. An
outward-rounded affine interval containment proof must establish every image
corner stays inside the logical canvas; an uncertain or clipped object is
unsupported. Default emitter bounds are 8 MiB Lua, 64 MiB estimated detached scene
payload, 100,000 actor frames (including reserved layer roots), ancestry depth 128
and 8,000,000 shared work units. Baking also uses the shared 65,536-input-key bound.
Accepted activation, playback, key and explicit lifetime times lie within one day
of zero; actor roots retain a finite keep-alive through that clock domain, while
drawable visibility follows the requested playback range. Seeking before hook
activation or beyond the accepted domain is not certified. Engine-rate/seek/restart
runtime acceptance remains separate from mocked-actor mathematical fixtures.

SSC hooks start in beats, before the playback span, using a song-level timing map
while retaining chart timing. Multiple scenes share one generated script per BG/FG
hook. Existing source hooks retain ownership: a conflict produces per-object
unsupported reports without rejecting an otherwise valid chart, even under reject
loss policy. Lua artifacts stay outside chart reimport and use the existing atomic
publication/ownership/source-receipt path with referenced images. Unrelated Lua or
image destinations are never overwritten merely because overwrite was requested;
publication safety errors still refuse the package. No actual pinned-engine
execution certificate is implied by source or golden tests.

## Preview and keyframe editor

The owner approved the Storyboard screen's v1 scope on 2026-10-31 (SB-0; [question 9](chart-editor-plan/decisions/open-questions.md)). Delivery is post-1.0, alongside the OSBX work, not an extension of the 1.0 freeze. The [screen cards](chart-editor-plan/tasks/storyboard-screen.md) and [mockups](chart-editor-plan/mockups/README.md) define implementation slices and structure.

V1 edits one song-level OSBX storyboard at a time. It introduces no difficulty ownership. The osu! writer retains its per-difficulty `[Events]` behavior and refuses storyboard export for multi-chart songs; standalone shared `.osb` writing is not part of this screen.

Imported OSB storyboards display read-only. **Convert to OSBX** (SB-10) explicitly creates a new OSBX storyboard in one Undo step, keeping the original until the user deletes it. Conversion preserves canonical OSB easing, independent X/Y command precedence, held gaps, draw order, image/origin/layer and the original sprite lifetime from the shared OSB preview schedule. Rotation changes from OSB radians to OSBX degrees and RGB bytes become RGBA multipliers. Numeric tracks anchor defaults at the real sprite begin; no artificial `INT64_MIN` keys or extra visible millisecond are introduced. Loops, retained semantic directives (including P blend/flip parameters and triggers), animations, interrupted varying curves and discontinuous shared endpoints refuse explicitly with source-line/sprite diagnostics until a lossless mapping exists. No partial converted scene is published. Finite flat lifetimes survive native save/load and constrain OSB export without extending to the requested playback range; ancestor lifetime or animated-visibility clipping may refuse explicitly. The OSB exporter retains its existing bounded integer-ms baking/error contract; inclusive lifetime edges also match fractional preview. Generator-owned OSBX storyboards also display read-only, naming the owner and offering **Detach from generator** (SB-11). Detach clears the ownership reference and recipe binding in one Undo step. V1 has no override patches over generator-owned output.

The existing generic dock (`src/ui/nuklear/dock.cpp`) supplies the layout: storyboard picker and hierarchy/layers plus an Assets tab on the left, canvas in the centre, inspector on the right, and expandable keyframe timeline at the bottom. The canvas uses osu!'s 640×480 logical space, fit/zoom/pan, selection outlines and transform gizmos. The inspector exposes node properties, the visibly ordered transform stack and property tracks. The timeline provides key selection, add/move/delete and easing inspection/editing.

Scrub, loop, playback rate and audio use the shared transport; canvas evaluation follows its time. Starting an editing gesture pauses playback. A completed gesture is one Undo step; Escape cancels it exactly. Key times are signed integer milliseconds, with millisecond snapping by default and optional beat/chart-grid snapping through the existing snap owner (`snap.h`); there is no independent storyboard beat-rounding implementation. Video-frame snapping waits for video export. Timing-anchor and MIDI-event snapping remain later ambitions.

The **SB-13** preview control provides one shared **Pass**, **Fail** or **Neither** choice, default Pass; Neither hides both conditional layers. Canvas and player preview share this project view state. Export dialogs retain their own explicit saved selection through the same core value; changing preview state does not overwrite export settings. A Custom switch/trigger timeline remains optional later work. Explicit OSBX direct-firing scenarios do not infer gameplay or fire from this fixed layer selector.

Reusable clip editing UI and simultaneous editing of several storyboards remain outside v1; the underlying model supports bounded clips and flat typed temporal programs. Video uses its separate dialog and capability boundary. **SB-14**, backed by OSBX-12, provides hybrid Lua links in the inspector/timeline and scripted-subtree badges: linked ranges are read-only until baked/unlinked, and the last good result survives errors. This does not authorize override patches on generator-owned storyboards.

One deterministic scene evaluator supplies interactive preview and eventual offline video frames. The profiles below answer open question 16 / T-155 (VID-0); VID-1–VID-3 implement them separately from screen v1.

## Video export

OSBX renders directly to video without first reducing the scene to standard OSB, retaining shear and matrices. The owner approved these profiles on **2026-10-31** (VID-0 / T-155, [answered question 16](chart-editor-plan/decisions/open-questions.md)). These are implementation requirements, not a claim that video export or the encoders are already available.

The encoder boundary accepts timestamped frames and audio through **linked FFmpeg libraries** (`libavcodec`, `libavformat`, `libavutil`, `libswscale`, `libswresample`), behind a new `WORMEDIT_ENABLE_VIDEO_ENCODER` build option. Existing optional FFmpeg decoding does not imply encoding support; VID-2 adds the option and encoder dependencies. Linking reuses the existing dependency path and gives the export worker direct control of timestamps, errors and cancellation without managing frame/audio pipes. A packaged `ffmpeg` process is a fallback **only if Windows/macOS library packaging fails**, not a dependency discovered on the user's PATH. That fallback adds executable distribution, pipe ownership and process cancellation work; it must retain the same profiles and diagnostics and use an argument vector rather than a shell command built from asset paths. VID-2 records packaging evidence on Windows, macOS and Linux before selecting any fallback.

| Parameter | Approved v1 behavior |
|---|---|
| First profile | MP4, H.264 video + AAC audio when audio is selected. Prefer BSD OpenH264 when available; libx264 is permitted only in builds whose licence allows GPL. |
| Second profile | WebM, VP9 video + Opus audio when audio is selected. |
| Resolution, in both profiles | **1920×1080 default**; 3840×2160 (4K UHD), 1280×720 and 640×480 (osu! storyboard native) also offered, subject to verified encoder support below. |
| Canvas fit | Preserve the 4:3 canvas; pillarbox into 16:9 without stretching or cropping. |
| Frame rate | Exact rational 30/1 or 60/1 fps. Frame `n` samples scene time `range_start + n × denominator / numerator`; derive timestamps from the index, never repeated rounded increments. |
| Background/alpha | Opaque black; no alpha output in v1. |
| Audio mix | None or song only. OSBX-10 exposes Sample cues; mixing them into video remains unsupported. |
| Conditional layers | Shared Pass, Fail or Neither choice, default Pass. Never include both conditional layers; Neither omits both. StepMania exports use the same choice owned by SB-13. |
| Range | Whole song or the selected time range from the timeline. |

Preview and export share one time origin for song audio offset, storyboard samples, frame timestamps and export range. Cancellation must not leave a partial file presented as a successful render.

### Encoder availability and 4K acceptance (VID-2)

Profile approval is distinct from encoder availability. [FFmpeg n7.1.1's encoder documentation](https://github.com/FFmpeg/FFmpeg/blob/n7.1.1/doc/encoders.texi) requires explicit `--enable-libopenh264` configuration; finding FFmpeg libraries for decoding is insufficient. VID-2 must enumerate the actual linked encoders and muxers, open each selected configuration, and refuse unavailable codec/container/resolution/frame-rate/audio combinations before rendering or publishing output. Record the FFmpeg/OpenH264 versions, enabled encoders and platform packaging results; do not silently substitute a codec, lower resolution or lower frame rate.

For 3840×2160 H.264, 2160p30 needs at least **level 5.1** and 2160p60 needs **level 5.2**. [OpenH264 v2.6.0's README](https://github.com/cisco/openh264/blob/v2.6.0/README.md#encoder-features) advertises encoder support through level 5.2. Its [level table](https://github.com/cisco/openh264/blob/v2.6.0/codec/common/src/common_tables.cpp#L331-L354) allows 36,864 macroblocks/frame at both levels, with 983,040 macroblocks/second at 5.1 and 2,073,600 at 5.2. UHD requires 32,400 macroblocks/frame: 972,000/second at 30 fps and 1,944,000/second at 60 fps. These limits support the level requirements; they do not prove that a particular packaged build can encode either configuration.

VID-2 must render and probe both UHD frame rates with the packaged OpenH264 wrapper, checking dimensions, signalled H.264 level, frame count, duration and audio synchronization. Keep frame skipping disabled so the encoder cannot meet its bitrate by dropping requested frames ([FFmpeg n7.1.1's wrapper](https://github.com/FFmpeg/FFmpeg/blob/n7.1.1/libavcodec/libopenh264enc.c#L58-L67) exposes `allow_skip_frames`, default false). If a configuration fails, offer that 4K configuration through WebM VP9 only, or also through a verified libx264 encoder only in a GPL-compatible build; explicitly record which combinations each package supports. If no permitted encoder supports the selection, refuse it with an actionable diagnostic. Keep 4K in both approved profiles as the product target; a package limitation must be visible rather than presented as full profile support. VP9 at 4K is expected to encode slowly; the export UI should anticipate long renders and retain progress and cancellation.

## Validation and tests

Validation rejects duplicate/missing IDs, missing assets/parents, cycles, cross-layer parenting, non-finite values, malformed matrices, wrong keyframe value shapes, invalid ranges, and invalid generated references.

OSBX-1a implements structural validation of the typed core scene graph without Lua,
filesystem access, evaluation or mutation. A malformed matrix has an arity other
than six or a non-finite coefficient and rejects. Finite singular matrices are
structurally valid in defaults and keys, without a warning: zero scale and
component-wise interpolation may produce them. Consumers must handle
noninvertibility safely rather than assuming every accepted matrix is invertible.

Default configurable structural limits (`osbx_validation_options_t`) are 250,000
nodes, 250,000 images, 250,000 transform operations and 500,000 keys in the entire
document, 128 transform nesting levels (root operations have depth 1), 1,024 bytes
per ID/reference/generator key, and 4,096 bytes per image/provenance path. IDs are
nonempty UTF-8 without NUL; optional provenance text may be empty. Image paths
must be project-relative without parent traversal, drive prefixes or backslashes;
validation checks their spelling, not file existence. Keys within each transform
or property track must have strictly increasing signed integer millisecond times.
Exceeding a structural count or nesting budget stops traversal with a typed error.


It also rejects duplicate measure IDs, invalid BMS measure-length decimal/fraction strings, invalid meter numerators or denominators, an explicit meter that disagrees with the supplied length, zero/negative/non-finite or overflowing numeric lengths, a rationalization that exceeds fixed iteration/denominator/error limits, non-integral measure capacities, rows outside `[0, capacity)`, `row = capacity` boundary aliases, chart-level timing descriptors nested inside a measure, missing cross-measure anchors, a requested BMSON resolution that would create fractional pulses, malformed osu! sample descriptors, invalid BMS base-36 WAV indices, missing source-chart contexts, unsafe direct paths outside declared project assets, conflicting explicit slots, exhausted target resource spaces, and incompatible attempts to reuse resources with different slice or target semantics.

Required regression coverage:

- DSL parsing/evaluation and stable IDs/provenance;
- separate and combined storyboard/note modules;
- several SM/SSC difficulties emitted from one project;
- ordered measure tables with named parameters and ordered note/BGM descriptors, chart-level timing outside measures, and cross-measure holds;
- exact BMS measure-length decimal/fraction parsing, retained unreduced meters such as 6/8, length/meter equivalence checks and disambiguation, plus bounded, deterministic, terminating conversion of bare Lua numbers;
- exact 960-to-240 and 192-to-48 BMSON mapping, promoted output resolution, fractional-measure bar lines, and rejection rather than pulse truncation;
- direct osu-style, BMS-WAV-index, and audio-file sample references with deterministic target resource reuse and allocation;
- exact matrix layout, stack order, pivots, hierarchy, and interpolation;
- keyframe editing, preview/apply/cancel, visual overrides, regeneration conflicts, undo/redo;
- fixed-seed Lua generation from structured MIDI;
- deterministic parametric SV/`SCROLLS`/`SPEEDS` recipes, snapped endpoint/whole-range dragging, stable multiplicative overlap composition, enable/disable/reorder, ordered post-processing, normalization against selected existing segments, impulses and finite-pulse export compilation, save/reload without sampled point clouds, stale preview detection, and preview/apply/cancel;
- standard OSB exact/baked/rejected export paths, bake error/key limits, shear/skew/matrix refusal including ancestry and implicit hierarchy shear, stable-ID collision rejection, and no partial writes;
- identical scene-evaluation snapshots for preview and offline rendering timestamps;
- short video renders probed for frame count, duration, and audio synchronization on Windows, macOS, and Linux.

## Settled OSBX 0.1 conventions

The owner accepted all ten defaults on 2026-10-31 (OSBX-0 / T-180), then amended the OSB export rule to refuse shear and matrices. These decisions settle the former specification questions; they do not imply that the dependent features are implemented.

1. **Modules:** `*.osbx.lua` for storyboards and `*.chart.lua` for charts; each file is its own entry point, optionally included from the already adopted `project.lua` (OSBX-5/6).
2. **Clips:** `clip { id, ops, tracks }`, instanced with `start`, `time_scale`, `params` (OSBX-1b).
3. **Loops/triggers:** `loop { start, count, keys }`, `trigger { name, from, to, keys }` (OSBX-1c only, off the critical path).
4. **Matrices:** component-wise key interpolation only, with no decomposed mode (OSBX-2).
5. **Blend modes:** `normal`, `additive`; renderer support is required, and any reduction to normal-only needs an owner decision (OSBX-1a/3).
6. **OSB baking:** the [compatibility export](#standard-osb-compatibility-export) limits apply only to shear-free motion. Skew or raw matrix ops on the sprite or in its ancestry, and implicit hierarchy shear always block `.osb`; video and StepMania retain them. No rasterization in 0.1 (OSBX-7).
7. **Generated source/overrides:** one call per line, two-space indent; per-object JSON patches keyed by stable ID, with manual conflict resolution. Recorded for later work, outside screen v1 overrides.
8. **Parametric sampling:** adaptive, tolerance 0.002, max_points 256; impulses require an explicit finite box. Existing native sampling defaults and impulse behavior differ as documented above; no code changes are made by this decision.
9. **Export identity:** string OSBX node IDs map to OSB `uint64 stable_id = FNV-1a-64(node ID path)`; collisions reject (OSBX-7).
10. **Media node boundaries:** complete Animation metadata and Sample cues use their shared canonical query owners. Incomplete placeholders and unsupported phase/effect/geometry compositions refuse. OSBX Sample playback and video/StepMania media-node support remain outside OSBX-10.

Video profiles belong to the separate VID-0 decision and question 16, not these OSBX conventions. SB-13 provides the fixed preview layer selector; custom simulation remains deferred.


## Current temporal and media execution boundaries

Native migrations remain separate: 37→38 adds typed temporal programs; 38→39 adds optional Animation/Sample payloads. Older schemas refuse future fields even when null/empty; current metadata is strict. Scenarios, compiled indexes, clocks and arenas are never persisted.

Loop keys are nonnegative, strictly ordered relative milliseconds. Count is 1..UINT32_MAX and the period is the last key time; a sole key at0 is constant from start, empty blocks refuse. Before the first key, fallback applies. Later-iteration gaps hold the previous final key, and the final iteration stays held. Root and program segments compete by latest effective start; programs win root ties, later authored programs win program ties. Trigger windows include their endpoints; the latest eligible direct firing whose first key has started restarts that block. Supplied source-song firing times map through the existing exact clip clock. No broad HitSound-family or gameplay inference is performed.

Exact symbolic OSB L/T output is conservative: a flat canonical Sprite with one numeric program/property, static remaining properties, supported native easing, no parent/clip/lifetime or competing root keys, and bounded canonical transform stacks. T additionally needs a finite activation window and first key at0. Actual emitted text is reparsed and active relative reconstruction checked; T153's ignored/inactive trigger sample is never proof. Other loops use bounded finite-span baking; trigger-dependent baking requires an explicitly sealed scenario (including intentional empty) and finite span, otherwise it refuses. This is a bounded reconstruction contract, not an engine certification. Selected temporal StepMania actors refuse before sidecars through the existing nonblocking chart advisory route.

An Animation's epoch is its own explicit lifetime beginning, otherwise zero. Final-extension frame paths share one canonical helper and bounded exact binary64/rational selection; Once saturates at the final frame. Lua's virtual-file form freezes every actual numbered image eagerly through the existing asset owner, and a complete template cannot later be changed by the generic image setter. Ordinary static image declarations retain their strict immediate read. No live metadata handle is returned.

The file-free media plan owns concrete frame paths, raw Sample sources and integer indices, precharging aggregate file/storage/work bounds before enumeration. Pure conversion and filesystem publication consume this same plan. Every frame gets a real source receipt/probe and must have provably compatible dimensions. Virtual template geometry is never a physical source receipt. Samples stage through the existing atomic publication/ownership/progress/rollback transaction, separately from authored-event deduplication.

OSB Animation proof independently reparses frame metadata, exact delay and the emitted earliest command epoch; max_digits10 alone is not phase proof. Missing assets, incompatible dimensions, unsupported phase/clip/program/lifetime/shear semantics refuse atomically. Sample output independently verifies time/layer/path/volume and preserves authored duplicates. Non-inert Sample/ancestor effects refuse, rather than silently bypassing clocks or scenarios. Video refuses both new media kinds before load/encode, StepMania keeps its explicit media-kind advisory refusal, OSBX cuts/reverse conversion stay guarded, and no new audio playback or mixer is introduced.
