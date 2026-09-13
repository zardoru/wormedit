# OSBX Lua DSL draft specification

Status: design draft 0.1. Syntax and names may change before implementation.

OSBX is wormedit's extended osu!-style storyboard source format. It uses a Premake-style Lua DSL to declare or generate storyboards with stable IDs, keyframes, parent/child groups, ordered compound transformations, and 2D affine matrices. The same project DSL may declare chart notes, but storyboard modules and note/difficulty modules can remain separate files and export independently.

Lua evaluates into a typed intermediate document. Preview, visual editing, standard `.osb` export, and video rendering consume that document rather than interpreting source text independently.

## File roles

Provisional conventions:

- `project.lua`: optional native project entry point and ordered module includes;
- `*.osbx.lua`: storyboard-only modules;
- `*.chart.lua`: chart/difficulty and note modules;
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

Every emitted object records its module/source location and optional generator key. Diagnostics and regeneration diffs point back to this provenance.

## Coordinate and time systems

The default storyboard canvas uses osu!'s 640 by 480 logical coordinates. Positive X points right and positive Y points down. Output rendering scales this canvas according to an explicit fit policy.

Portable storyboard keyframe time is signed integer milliseconds for OSB compatibility. The DSL may place keys through helpers such as `at_beat`, `at_sample`, and MIDI tick conversion; the emitted keyframe retains the resolved millisecond value plus project-only authoring provenance where available.

Video frames use rational presentation timestamps. Evaluation derives each timestamp from frame index and frame-rate numerator/denominator rather than accumulating floating-point frame deltas.

## Scene graph and layers

Visual nodes are `group`, `sprite`, or `animation`. Timed storyboard audio uses `sample` nodes. Parent references form an acyclic graph.

Standard visual layers are `background`, `fail`, `pass`, and `foreground`. Within a layer, draw order uses explicit order followed by stable declaration order. Cross-layer parenting is invalid in draft 0.1 because inherited transforms and standard OSB conditional ordering would be ambiguous.

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

Initial easing forms are `step`, `linear`, standard OSB easing values, and cubic Bézier control points. Numeric/vector values interpolate component-wise after easing. Rotation values are continuous authored degrees; the evaluator does not silently choose a shorter wraparound. Raw matrix keyframes interpolate their six coefficients component-wise. Authors should use component transforms for rigid motion.

Initial property tracks include opacity, linear RGBA color multiplication, visibility, blend mode, and animation frame/loop state. Reusable clips group transform operations and property tracks and may be instanced with start time, time scale, and parameters. Recursive clip references are invalid.

Standard OSB loops and triggers receive normalized typed forms. Trigger evaluation takes an explicit preview game state. Offline video export requires a deterministic pass/fail and trigger scenario.

## MIDI and generator API

`midi_asset(id)` returns immutable structured MIDI data parsed by the host: timing division, tracks, channels, note intervals, pitch, velocity, controllers, tempo events, time signatures, markers, and track names where available. Exact MIDI ticks remain integers.

The API supplies explicit conversion helpers among MIDI ticks, metric time, chart measure/division positions, and storyboard milliseconds. Reading MIDI does not automatically change chart timing.

A generator transaction may emit:

- notes/holds and semantic layer assignments for one or several difficulties;
- storyboard nodes, transform stacks, clips, and keyframes;
- or both sets together.

The editor validates and previews the typed diff. Commit applies it as one undoable command; cancellation changes neither materialized notes nor storyboard data.

## Parametric scroll and speed generation

Chart modules may generate generic SV events, StepMania `SCROLLS`, or StepMania `SPEEDS` from a function of normalized parameter `p` over a selected chart interval. `p = 0` is the interval start and `p = 1` is its end. The interval declares whether interpolation follows musical position, chart time, or audio time; musical position is the default for scroll authoring.

Provisional Premake-style syntax:

```lua
scroll_curve "chorus-rise"
  range { from = at_measure(32), to = at_measure(40), domain = "beat" }
  output "scrolls" -- "sv", "scrolls", or "speeds"
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

`values` accepts a built-in curve descriptor or a pure Lua function `function(p, context) -> finite_number`. The context is immutable and contains the interval endpoints, baseline effective scroll at `p`, active measure/timing data, declared parameters, and deterministic seed. The function cannot mutate the document or emit events during sampling.

Initial built-ins are:

- `curve.linear { from, to }`, equal to `from + (to - from) * p`;
- `curve.exponential { from, to, shape }`, using the endpoint-safe normalized form `from + (to - from) * (exp(shape*p)-1)/(exp(shape)-1)`, with `shape = 0` defined as linear;
- `curve.constant { value }`;
- `curve.bump { start, length_a, x, y }`, a finite two-region plateau described below;
- `curve.dirac { at, area }`, an impulse primitive described below;
- `curve.multiply { value, curve }`, which scales another curve and is also the operation used by normalization.

Fixed sampling declares an exact point count or exact musical step. Adaptive sampling recursively subdivides until the piecewise interpolation stays within the declared error tolerance or reaches `max_points`. It always includes both endpoints, uses a specified deterministic traversal/order, and evaluates the same points on every platform. Generated positions must be exactly representable at the chart resolution; the preview offers resolution promotion or explicit quantization when they are not.

The output family controls interpretation:

- `sv` emits generic internal instantaneous multiplier points;
- `scrolls` emits StepMania `SCROLLS`-style multiplier points;
- `speeds` emits StepMania `SPEEDS` values plus an explicit transition duration and beat/time transition mode. A curve cannot target `speeds` until those transition fields are supplied or inherited from a declared policy.

### Post-process pipeline and length-preserving normalization

Sampling produces an immutable raw curve batch before normalization or target-specific compilation. An ordered `postprocess` list transforms a copy of that batch. Initial stages are `multiply { value }`, `normalize { mode, value }`, and `balance_bump { solve, target_average }`; later stages can be versioned without changing the curve-function contract. Stage order is significant and is shown in the preview. Both the raw batch and final processed batch remain inspectable in generator provenance.

The same post-process interface may operate on a generated batch or a selected existing SV/`SCROLLS`/`SPEEDS` segment after it has been converted to the typed piecewise representation. This makes normalization a reusable explicit command instead of behavior embedded in each curve function.

Normalization is optional. `preserve_displacement` multiplies every processed value and impulse area by one scalar so the signed integral of effective scroll over the interval equals its value before applying the post-process chain. With baseline `b(p)` and the incoming multiplier `f(p)`, the scale is:

```text
k = integral(b(p), p=0..1) / integral(b(p) * f(p), p=0..1)
output(p) = k * f(p)
```

An optional `value` multiplies the preserved target integral, so `value = 1` retains the original displacement and `value = 2` requests twice that displacement. `preserve_distance` uses integrals of absolute effective scroll when negative scroll makes signed displacement cancel. `preserve_average` is an alias of displacement preservation over normalized `p`; it exists as UI wording, not a different calculation.

Built-in curves may retain an analytic-integral hint, but post-processing computes against the actual typed batch that will be compiled, including structural impulse areas and any prior post-process stages. Arbitrary Lua curves use the same deterministic piecewise representation selected for emitted output, so preview, normalization, and regeneration cannot disagree. A zero or non-finite denominator, overflow, or an output forbidden by the target profile rejects the proposed batch.

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

If an export target has no exact impulse, the user may choose an explicit finite box or triangular pulse with a representable width. Its height is derived so its integrated area remains `a`; the generated entry/exit points and approximation error appear in preview. Otherwise export rejects the impulse. Coincident impulses combine only through an explicit policy, and impulses at interval boundaries have documented left/right ownership.

### Preview and regeneration

The curve editor shows the source function, sampled control points, raw and post-processed values, each post-process stage, cumulative scroll displacement/distance, point count, resolution changes, and the exact typed events to add, replace, or remove. Applying one curve or one post-process operation is one undoable command. The stored recipe records API version, range anchors, domain, output family, built-in/function source identity, parameters, sampling settings, ordered post-process stages, target profile, and hashes of timing inputs.

Regeneration is explicit. A timing or resolution change marks dependent recipes stale and offers a new diff; it does not silently rewrite materialized SV/speed/scroll events.

## Visual editing of Lua output

The keyframe editor operates on the materialized typed document. Canonically generated DSL modules may be rewritten by the editor. For arbitrary procedural Lua, direct visual edits are stored as project overrides or require an explicit "detach/bake to editable DSL" operation; wormedit must not pretend it can safely rewrite general Lua source.

Regeneration shows how procedural output and visual overrides interact. Conflicting overrides require an explicit keep-generated, keep-override, or manual resolution.

## Standard OSB compatibility export

Standard `.osb` export flattens inherited transforms into descendants and classifies each object range:

- `exact`: maps to standard OSB commands/easing;
- `baked`: samples compound transforms into supported OSB commands within a chosen error tolerance;
- `rasterized`: renders an unsupported subtree to an image sequence referenced by OSB;
- `unsupported`: blocks export and identifies the responsible IDs.

Affine shear and some combinations of non-uniform scale and inherited rotation cannot be represented by standard OSB sprite commands. The user chooses approximation, rasterization, or cancellation; export never silently removes the effect.

Imported OSB source lines and ordering remain available as provenance for loss-aware saves.

## Preview and keyframe editor

The editor provides a canvas, hierarchy/layer view, asset browser, and expandable keyframe timeline. It supports transform gizmos and pivots, curve/easing inspection, multi-selection, reusable clips, loops/triggers, and snapping to milliseconds, video frames, chart beats, timing anchors, and MIDI events.

Preview supports scrub, loop, playback rate, audio, pass/fail state, named trigger scenarios, chart overlays, and Lua generated-result overlays. One deterministic scene evaluator supplies both interactive preview and offline video frames.

## Video export

OSBX renders directly to video without first reducing the scene to standard OSB. Export parameters include resolution, rational frame rate, time range, fit/background/alpha policy, deterministic trigger state, and audio mix.

The encoder boundary accepts timestamped frames and audio. The first implementation spike should compare:

- linked FFmpeg libraries (`libavcodec`, `libavformat`, `libavutil`, and scaling/resampling helpers);
- a packaged or discovered `ffmpeg` executable receiving frames/audio through owned pipes.

The choice depends on Windows/macOS/Linux packaging, licensing, encoder availability, cancellation, diagnostics, and build complexity. Process invocation uses an argument vector rather than constructing a shell command from asset paths.

Preview and export share one time origin for chart audio offset, storyboard samples, frame timestamps, and export range. Cancellation must not leave a partial file presented as a successful render.

## Validation and tests

Validation rejects duplicate/missing IDs, missing assets/parents, cycles, cross-layer parenting, non-finite values, malformed matrices, wrong keyframe value shapes, invalid ranges, and invalid generated references.

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
- deterministic parametric SV/`SCROLLS`/`SPEEDS` generation, ordered post-processing, normalization of generated and selected existing segments, impulses and finite-pulse compilation, stale-recipe detection, and preview/apply/cancel;
- standard OSB exact/baked/rasterized/rejected export paths;
- identical scene-evaluation snapshots for preview and offline rendering timestamps;
- short video renders probed for frame count, duration, and audio synchronization on Windows, macOS, and Linux.

## Open specification questions

- Final extensions and entry-point naming for project, storyboard, and chart modules.
- Canonical generated-Lua formatting and the boundary between editable declarations and procedural code.
- Exact typed forms for reusable clips, loops, and triggers.
- Whether matrix tracks should add an explicit decomposed interpolation mode.
- Standard OSB bake error metric, sample limit, and rasterization policy.
- Initial blend modes and video export profiles.
- How project overrides should be serialized and merged after regeneration.
- Default parametric sampling tolerances/point caps and which targets, if any, should receive a standard finite-pulse approximation for impulses.
