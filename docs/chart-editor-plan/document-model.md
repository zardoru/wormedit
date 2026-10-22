# Plan domain: document model

[Back to the chart editor plan index](../chart-editor-plan.md).

## Proposed document model

Use one editor-owned `song_document_t`, independent of the loader's transient runtime structures. Importers and exporters adapt between file formats and this model.

```text
song_document_t
  shared song metadata, ordered search tags, and source paths
  format-scoped metadata groups with source/export provenance
  navigation bookmarks and labeled song sections
  audio assets
    source file
    decoded stream information
    waveform cache
  visual assets
    image/video/storyboard source and preview metadata
  resource catalog
    sample, BGM, and BGA roles
    BMS WAV/BMP definitions and osu! sample/event provenance
  MIDI assets
    source file and timing division
    parsed tracks, notes, controllers, and metadata
  generator recipes
    Lua source/reference, API version, parameters, seed, and input asset IDs
  input profiles
    physical lane-key bindings, stage focus rules, and user overrides
  ordered charts/difficulties (one or many)
    stable difficulty ID, name, mode, author, rating, and output groups
    format profile and source-format metadata
    original bytes and detected/selected text encoding where applicable
    optional lossless BMS control-flow tree and active branch-selection environment
    global divisions per standard measure
    signed beat-zero/audio offset with unit and source provenance
    lane-layout segments and source/export channel maps
    semantic note-layer palette
    ordered measures
      exact length and displayed time signature
      local notes and autoplay/BGM events
    chart-level timing, scroll, warp, kiai, bookmark, and layout tracks
    audio clips/slices
    optional storyboard documents and assets
    unsupported source data retained for round trip where practical
```

### Measure-based storage and positions

Use an ordered sequence of measures as the primary editable container. Each measure is a self-contained chunk, similar to a segment in a piece table: inserting, deleting, resizing, or redividing one measure should not require rewriting one large chart-wide event buffer. Measure-local lane objects use a local division index or rational offset. Chart-level tracks address those same exact positions without being owned by a measure. A derived measure index and prefix-duration table provide fast conversion to global beat and time coordinates.

Treat each measure's contiguous note/event lists as the primary spatial buckets for editing and preview compilation. Range selection, hit testing, validation, schedule construction, and rendering first narrow through the ordered measure-prefix index and then scan only intersecting buckets; they do not rebuild one monolithic all-chart note vector on every frame. Optional per-measure row/lane indexes are derived caches invalidated by the owning measure revision. Cross-measure holds keep their head ownership and exact tail reference while queries include them through a bounded derived span index.

Measure ownership is limited to measure-local chart objects: playable notes, holds, autoplay/BGM events, and other lane objects whose authoring row is naturally local to one measure. BPM changes, stops, delays, warps, SV/scroll/speed changes, kiai state, bookmarks/sections, and layout changes live in separate chart-level ordered tracks. Their positions still use exact `(measure ID, local row)` anchors, so moving or resizing measures updates one shared position index without nesting cross-measure timing semantics inside a measure declaration. Holds may reference an end position in a later measure.

Each chart has one editable global division count, expressed as the maximum divisions in a standard 4/4 measure. The default is **960**, or `192 * 5`; 192 remains a supported lower-resolution choice. A measure derives its local capacity from that global count and its exact length ratio. Store events sparsely by local division so a high global count does not allocate large dense arrays. The visible editing snap may be a coarser exact divisor of the global grid.

Changing the global division count when existing events or measure lengths cannot be represented exactly must present explicit choices: cancel, raise to the smallest compatible global count, or quantize with a preview of every moved event. It must never silently move events. Holds and transitions crossing a measure boundary retain stable endpoint positions across a global redivision.

Store measure length as an exact positive rational value in BMS `#xxx02` units relative to a standard 4/4 measure: `1` is four quarter-note beats, `0.75` is three, `1.25` is five, and so on. Decimal entry is a presentation and interchange form, not the internal numeric type. Store an optional musical meter separately as its unreduced, explicitly authored numerator and denominator. A length ratio of `3/4` may therefore pair with `meter = 3/4` or `meter = 6/8`; these are equal in duration but imply different beat grouping, and an explicit `6/8` must remain displayed as `6/8` rather than being reduced to `3/4`.

The editor shows both values together: the BMS ratio/decimal and the musical meter/grouping. Entering a decimal or selecting a BMS-ratio input derives the exact length and offers equivalent common meters without choosing one silently. Entering a fraction in musical-meter mode derives the BMS ratio from `numerator / denominator` and retains that exact signature spelling. If a slash-form entry is ambiguous in a general field, ask whether it is a BMS ratio or a musical meter; either choice has the same duration but different persistence and beat-grouping meaning. When both an exact length and meter are supplied, require `length_ratio == meter_numerator / meter_denominator`; reject a mismatch rather than changing either value.

Parse textual decimal measure lengths exactly by splitting sign, integer, and fractional digits into an overflow-checked integer ratio and reducing by `gcd`; accept an explicit fraction form as well. Never pass persisted decimal text through binary floating point. APIs that receive only a Lua `number` or imported floating value use a deterministic bounded rationalization routine over the exact IEEE-754 value, with a configured maximum denominator, checked intermediate arithmetic, a fixed iteration cap, and termination on denominator limit or lack of progress. They return the candidate plus approximation error for preview, or reject it. No conversion loop depends on floating equality or compiler rounding eventually reaching zero. After conversion, promote the chart resolution or reject/preview quantization when `global_divisions * length` is not integral.

Store musical event positions exactly rather than as `double`. A position can be represented by `(measure ID, division index)` interpreted against the chart's global division count and the measure's length ratio, then reduced to a rational offset for comparison and conversion. Preserve source resolution/provenance separately when it affects stable export.

BMSON is the only initial interchange target with a global integer pulse resolution, but its unit is pulses per quarter note rather than divisions per standard measure. On BMSON import, apply the BMSON 1.0 default of 240 when `info.resolution` is zero, null, or omitted, while retaining the source spelling/provenance; then choose editor divisions `D = 4 * source_resolution` by default. The common `resolution = 240` therefore becomes the editor default `D = 960` and each source pulse initially maps to one editor row. On export, every absolute editor position `r / D` of a standard measure maps to BMSON pulse `y = 4 * R * r / D`, where `R` is the emitted BMSON resolution. Choose a positive integer `R` that makes every emitted note, hold length, BPM, stop duration, BGA event, and bar line integral; prefer `R = D / 4` when `D` is divisible by four and retain a larger imported resolution when requested for stable round trips. Otherwise promote `R` exactly or reject/preview quantization. Never truncate a pulse.

BMSON has no native measure or time-signature object. Export the prefix positions of the editor's ordered measures as BMSON `lines` bar-line pulses; exact measure-length ratios therefore survive as bar spacing, while the optional display meter remains native-project metadata. Import derives measure chunks from `lines`, using generated 4/4 bar lines only when `lines` is absent as specified. A present empty `lines` array is distinct and means no bar lines; it must not be replaced silently. BMS, SM/SSC, and osu!mania adapters use the same editor grid but serialize through their own native time representations.

Also support absolute-time anchors for naturally time-based data such as osu!mania hit objects. A timing service maps between musical position, chart time, and audio time while accounting for BPM changes, stops/delays, and warps. Derived seconds should be cached, not treated as the canonical value for every format.

Each event records its positioning domain. Editing commands decide whether an item remains fixed to the beat grid or fixed to audio time when timing is changed. The UI should expose this distinction where an operation could move notes relative to the music.

### Events

Initial core event types:

- `note_t`: stable ID, position, lane ID, optional end position, head/tail sound reference, scoped `note_kind_t` (tap, hold, invisible, mine, and retained target-specific kinds), optional label, and optional semantic layer ID.
- `bpm_event_t`: position and positive BPM value.
- `scroll_event_t`: position, multiplier, and optional interpolation/transition data. The model must distinguish an instantaneous scroll multiplier from a StepMania `SPEEDS` transition.
- `scroll_impulse_event_t`: position and signed integrated scroll displacement for parametric impulse generation; target adapters must represent, approximate with an explicit finite pulse, or reject it.
- `stop_event_t`, `delay_event_t`, and `warp_event_t`: separate types even when a target format collapses or lacks them.
- `kiai_state_event_t`: position, enabled state, source timing-point identity/order, and retained osu! timing-effect bits.
- `autoplay_sound_event_t`: position and clip reference for BMS/BMSON BGM sounds.
- `bgm_lane_t`: stable display/source-lane identity, source ordering, horizontal presentation state, and sparse autoplay slots.
- `bga_event_t`: position, visual resource ID, target role/layer, and source channel/event provenance.
- `resource_entry_t`: stable project resource ID, audio/visual asset reference, display name, usage roles, target definitions/identifiers, and retained source ordering.
- `measure_t`: stable ID, exact length ratio, optional time-signature interpretation, and sparse local event collections.
- `lane_layout_segment_t`: starting measure/position plus ordered playable column definitions, semantic stage groups, and optional gutters between stages.
- `channel_map_t`: stable mapping among semantic lane IDs, raw BMS object channels, editor input bindings, and optional MIDI channel/note destinations.
- `timing_anchor_t`: musical position, pinned audio sample frame, lock state, and an optional link to the BPM event(s) produced by the timing solver.
- `audio_offset_t`: a chart/difficulty-scoped signed exact duration locating musical beat zero in the audio timeline, with source field, spelling, unit, and sign-convention provenance.
- `navigation_marker_t`: stable ID, point or range, label such as `Verse` or `Chorus`, color/category, and an explicit musical-position or audio-time anchor.
- `bms_program_node_t`: ordered root/common, random, conditional, switch, case/default, branch-content, and retained-raw nodes with stable source identity.

Events need stable IDs so selection, undo/redo, labels, slice references, and validation results do not depend on mutable timestamps or container order.

### Shared timing categories

The owner picked up [T-730](tasks/editing-depth.md#t-730-plan-granular-timing-links-with-copy-on-write-overrides)
on 2026-10-22. **Designated charts are sources**, not named timing sets. Every link
is inside one project and names a source chart and one category. New difficulties
inherit the links selected in their creation workflow. The selection is explicit;
creating an unrelated new project has no source chart to inherit.

The independently linkable categories are **BPM**, **SV/scroll**, **SPEEDS**,
**Kiai**, **stops**, **delays**, **warps**, **timing anchors**, **beat-zero audio
offset**, and **measure layout**. SV recipes follow their output category: scroll
recipes belong to SV/scroll, speed recipes to SPEEDS. Stop and delay records retain
their separate boundary semantics even though both live in `pauses`. Measure layout
means the ordered musical lengths and bar boundaries, while each chart retains its
own row resolution and measure IDs. Lane profiles, playable content, BGA, bookmarks,
sections, labels and semantic layers remain local; these are not timing categories.

A linked category follows its source chart's current effective category. Editing the
source is ordinary chart editing. The first actual local write detaches only the
changed category, beginning with its current effective data. A no-op, failed edit,
preview or cancelled gesture does not detach anything. Relinking replaces only that
category with the source's current data; local data loss requires an explicit,
stamped confirmation. Linking, detaching, source propagation and relinking publish
through the shared transaction and Undo restores the whole consistent document.
Chains are allowed only when acyclic per category. A source cannot be deleted while
any surviving chart or selected creation default refers to it; detachment must occur
first. Duplicating a chart retains its source links, never creates a self-link, and
assigns fresh local identities using the existing duplication owner.

Map positions by exact cumulative musical beats, using `measure_index_t` and checked
rational arithmetic. Source and target measure IDs are unrelated. Preserve the target
resolution, reject unrepresentable rows and spans, and never round implicitly.
Seconds remain seconds; row-relative durations convert exactly between resolutions.
Measure-layout changes re-map every local object and both endpoints of holds/spans,
then reuse `rehome_measure_objects` for note/BGM storage. Source changes that cannot
preserve a dependent chart reject atomically, including objects outside the new
layout, collisions or unsupported retained conditional timing. Audio-time navigation
markers keep their audio positions. Sample-frame anchors require compatible known
audio-clock geometry; absent or incompatible geometry is a diagnosed rejection.

Use one category catalog and one link owner. Existing timing consumers currently read
chart vectors directly; converting each consumer into its own resolver would duplicate
policy. The implementation therefore materializes effective linked data into the
ordinary chart snapshot at the shared publication boundary. Linked vectors are derived
from their designated source, never a second authoring authority. Compare category
content before publication to detect local writes, detach those categories, then
resolve source dependencies in order and validate the resulting document once. Link
and relink operations explicitly declare intent so replacement is not mistaken for a
local edit. Preview uses the same detached transaction path. Undo/Redo restore complete
resolved snapshots without running edits again.

Native persistence stores the link identities and resolved snapshots and validates
their consistency on load. Older schemas migrate to unlinked categories. Journals and
recovery use that same native codec. Playback, recipes, preview and export consume the
same resolved snapshot. Interchange formats emit effective timing only; they never
serialize project links or silently flatten unsupported target semantics. Retained
BMS conditionals/projections and osu! absolute-time measure partitions need explicit
compatibility checks rather than generic vector copying.

The bounded implementation sequence is [T-751–T-755](tasks/timing-links.md): model,
exact mapping and persistence; shared transaction propagation and local detachment;
measure/clock compatibility and consumer verification; category controls and creation
links; then end-to-end recovery/export proof. This design does not claim runtime links
are already implemented.

### Beat-zero and audio offsets

Use one canonical sign convention in the core: `beat_zero_audio_time` is the signed audio time in seconds at which chart beat zero occurs. A positive `2.34` means beat zero occurs 2.34 seconds after the audio begins, matching the StepMania 3.9 description of `#OFFSET`. Store the value as an exact rational duration with retained source decimal spelling; the timing service applies it before BPM, stops, and warps and exposes conversion to sample frames with an explicit rounding policy.

The active song/music asset and canonical offset are cross-cutting normalized document properties, not rows in a BMS/StepMania/osu!/BMSON-specific metadata table. Metadata exposes them in a common **Audio and synchronization** group above target-specific fields. Compose exposes an **Offset/Waveform drag** toolbar mode, and Metadata embeds the same bounded waveform-drag control next to the numeric offset. Dragging the waveform along its time axis keeps the beat grid/edit head fixed and previews the corresponding signed sample-frame delta to `beat_zero_audio_time`; the ghosted waveform, exact seconds/sample readout, and affected timing range update continuously. Mouse release commits one revision-checked undoable offset command, while Escape, capture loss, timing lock, stale revision, or an invalid/missing stream restores the prior alignment. Both surfaces use the same controller and sign convention. Default dragging is sample-frame exact, with optional active-grid/onset snapping exposed as an explicit modifier rather than hidden rounding.

SM and SSC readers and writers preserve their native `#OFFSET:<seconds>;` field at the correct song/chart scope for the selected profile. BMS-family readers accept wormedit's `#OFFSET <seconds>` extension and writers always emit it when an offset is present, as requested, even for target profiles whose players are known to ignore it. Compatibility validation labels that player behavior without deleting the command. Preserve duplicate/conflicting source directives as provenance and require an explicit winner before normalized export.

osu!mania and BMSON have no equivalent global beat-zero field with the same semantics. Their adapters still read and write the formats fully: they retain their native absolute timing origins, and conversion of a nonzero canonical offset requires an explicit exact shift of every affected timed object when legal. If negative target time/pulse constraints, bookmarks/events, or source-round-trip promises prevent that shift, the writer rejects or offers an explicit rendered/leading-time transformation; it never silently zeros the offset or emits an invented standard field. Native projects always retain the canonical value.

### Stops, delays, and timing discontinuities

Stops and delays are required internal event types even when an export target cannot represent them. Preserve their distinct semantics, source units, source ordering, and same-position ordering relative to BPM changes, warps, scroll events, notes, and BGM sounds. Beat-to-time and time-to-beat conversion must define both sides of every discontinuity so seeking, hit times, waveform alignment, comparison, and preview remain deterministic.

BMS STOP values need their original definition/index and 1/192-whole-note unit representation as well as a derived duration. That unit is independent of `#xxx02` measure length. SM/SSC stops and delays remain separate because their event ordering and note timing behavior differ. Unsupported target exports produce a targeted error or an explicit user-approved transformation; they never discard a pause silently.

Stage 0 must use `libotoworm` behavior as one reference for stop/delay fixtures while specifying wormedit's independent result. Wormedit's parser and timing layer must preserve event type, source unit, ordering, and discontinuity boundaries directly; it does not rely on the library's transient representation.

### Warps and legacy negative BPM

`warp_event_t` is a native timing event with an exact musical start and exact skipped beat length/end. Timing evaluation maps both edges of the discontinuity and defines the visibility, seeking, and judgement treatment of notes and other events within the skipped interval. Keep a derived audio-time representation in the timing index where useful, but do not make seconds the editable source of truth.

StepMania 3.9-style negative BPM is an interchange encoding, not a negative value retained in the ordinary BPM lane. Import consecutive legacy negative-BPM regions as canonical `warp_event_t` values and convert their remaining positive timing sections to normal `bpm_event_t` values. Retain source entries, ordering, dialect/profile, and a provenance link on the generated warp so an unchanged chart can be exported stably or diagnosed precisely. Apply the same canonicalization, with distinct provenance, to negative stops used as legacy warps.

Warp import and export are selected by an explicit target-profile policy:

- a native-warp strategy reads or writes `WARPS` where the selected SM/SSC implementation supports it;
- a legacy strategy reads or synthesizes the negative-BPM/negative-stop representation expected by a selected 3.9-era profile;
- an unsupported strategy rejects the conversion with the affected interval and available alternatives.

The current paired codec names its bounded syntax presets `native_warps`, `legacy_negative_bpm`, and `legacy_negative_stop`. An explicitly selected source preset rejects other warp syntax, including native `WARPS` under a legacy preset. These names identify syntax rules only; the exact StepMania releases/players that define future target profiles remain an open compatibility decision.

The export strategy must consider timing changes, stops/delays inside a warp, same-position ordering, overlapping/adjacent warps, and notes inside the skipped range. After encoding, re-import through the selected profile and compare the semantic timing map. If the chosen representation cannot reproduce the warp exactly, conversion fails or presents a named approximation under the request's loss policy; it never silently drops or shortens the warp. See the [warp and converter source audit](../references/stepmania-warps-and-conversion.md).

### Audio assets and slices

Separate source audio from playable clips:

```text
audio_asset_t = path + stream metadata + content identity
audio_clip_t  = asset ID + start frame + optional end frame
note/BGM      = clip ID (or a layered list of clip IDs)
```

Use source sample frames as slice boundaries. Show seconds and musical positions in the UI, but avoid storing floating-point seconds as the only boundary representation. An open-ended clip represents "to the end of the source." Slicing is non-destructive: moving a boundary changes the clip reference and never rewrites the source file.

A single note may need multiple simultaneous clips because BMSON can fuse entries from multiple sound channels at the same lane and pulse. Import must preserve BMSON's continuation flag semantics and the relationship between a sound channel and the slices derived from it. Export should reconstruct sound channels without requiring a destructive render.

For BMSON 1.0, slices are derived from the ordered note pulses and each note's continuation flag; the format does not store free-form slice handles, fades, gain, or pan. Model BMSON source channels explicitly as an audio asset plus ordered sound events `(position, lane, length, continue)`, and derive preview clips by running the specification's slicing algorithm. The editor may display the resulting sample-frame boundaries, but BMSON export must be driven only by representable sound-channel data. A later explicit "optimize sound channels" operation may rewrite channel/event structure while preserving its audible result.

Add a razor-style authoring mode to the sound-channel waveform. The waveform is laid out against the chart timing map, and each click proposes a sound event at the exact musical row under the pointer, snapped to the active divisor by default. Authors can place several markers, drag or delete them, switch each marker between **continue/slice** and **restart**, audition the region beginning at any marker, and assign the resulting event to a playable lane or BGM. The proposed marker set remains non-destructive and commits as one undoable command. Optional sample-frame readouts are derived from the timing/slicing evaluator; arbitrary source-frame cuts that cannot be represented by BMSON `y` plus `c` stay project-only or require an explicit rendered-asset conversion.

This adopts the interaction documented and implemented by BmsONE: ordinary clicks create slicing/continuation notes, Shift-click creates a restart-from-beginning note, clicks are lowered to its active grid when snapping is enabled, and the BGM lane visualizes the played waveform. Wormedit extends that interaction with an explicit multi-marker transaction and region preview similar to a razor-edit workflow. BMSON compilation groups events by resolved source sound channel, sorts them by absolute pulse with stable same-pulse order, and emits only `x`, `y`, `l`, and `c`. Slice points are the distinct pulses across that whole channel. At one pulse, any restart (`c = false`) wins; if one derived slice is assigned to both BGM and playable notes at that pulse, discard the BGM assignment; multiple references to that same slice have polyphony one. Distinct slices, including slices from separate sound channels that reference the same file, may overlap. These are BMSON 1.0 rules rather than editor policy. See the [BmsONE slicing audit and BMSON mapping](../references/bmsone-slicing.md).

For BMS keysounds, preserve the authored `#WAVxx` filename token separately from the resolved audio path. Resolution first tries the exact referenced path. If it is absent, search the same directory and stem using the deterministic fallback order `.wav`, `.ogg`, `.flac`, skipping the already tried extension. Extension matching is ASCII case-insensitive, while the chosen on-disk spelling is retained. A target profile may override the order, but every import records the candidates and selected result.

Fallback applies to a missing exact file. A present but corrupt or undecodable exact file is diagnosed rather than silently replaced. If case-folded names or several fallback candidates make resolution ambiguous, show all candidates and require an explicit choice; do not depend on host filesystem ordering. The resource entry retains both authored and resolved identities so preview can play the fallback without rewriting the BMS token, and an explicit normalize/package operation can update output references later. See the [BMS audio fallback audit](../references/bms-audio-fallback.md).

### Sample, BGM, and BGA resource lists

Provide searchable **Samples**, **BGM**, and **BGA** lists as filtered views over one project resource catalog. A resource may appear in more than one audio view: for example, one BMS `#WAVxx` definition can be assigned to a playable note and also placed on a `#xxx01` BGM lane. The lists do not duplicate the underlying asset or invent separate target identifiers.

The Samples list supports two target-oriented presentations:

- a BMS sample-list view that shows retained `#WAVxx` identifiers, filename, decoded duration, usage count, missing/invalid state, and optional user label;
- an osu!-style sample view that shows normal/addition sample-set metadata, custom hit-sample filename, volume/index provenance where applicable, decoded duration, usage count, and missing/invalid state.

The BGM list shows audio resources available for autoplay placement, including unused definitions and BMSON source channels/clips. The BGA list shows images, animations, and videos available for timed visual events, including retained BMS `#BMPxx` definitions and osu! background/video/storyboard event assets. BMS BGA channel role/layer and osu! event kind remain properties of the placed `bga_event_t`, rather than being guessed from the file extension.

Authors can import one or several files into a list, retain unreferenced source definitions on chart import, rename editor labels without renaming files, filter by used/unused/missing/type, and reveal every event that references a resource. Clicking audio auditions the exact clip; hovering/selecting visual resources shows a thumbnail or short preview. Decode and thumbnail work remains derived cache data and never changes source resources.

Placement is explicit and undoable. With a sample armed, clicking a playable cell or using a lane-entry command creates a note with that sound; assigning to an existing selection changes its sound reference. Placing a BGM resource creates an `autoplay_sound_event_t` in the chosen ordered BGM lane. Placing a BGA resource creates a `bga_event_t` at the cursor with an explicitly selected role/layer. The UI distinguishes create, assign, and replace actions so choosing a resource cannot silently overwrite notes or events.

Compose exposes a compact placement toolbar for the active edit tool/note kind, armed Samples/BGM/BGA family and resource index, create/assign/replace mode, auto-increment, Offset/Waveform-drag mode, snap, scroll direction, timing-lane visibility, and sound-sidebar visibility. When auto-increment is armed, each successfully committed create operation advances the selected resource to the next visible eligible entry in stable catalog/target-index order within the same family, making numbered sound or image sequences quick to place. Assign, replace, delete, rejected/cancelled/no-op edits, preview-only gestures, undo, and redo do not advance it. A multi-place transaction advances once per successfully inserted event in deterministic event order. The default end-of-list policy stops at the final entry rather than wrapping; wrap may be an explicit toolbar preference. Imported numeric IDs may contain gaps, so advancement follows the selected profile's defined eligible ordering and reports exhaustion instead of relying on resource-vector offsets. The armed family/index and auto-increment mode are ephemeral editor state, not document history, while the created events remain one normal undoable command.

Deleting a catalog entry that is still referenced is rejected until the author replaces references, removes the events, or deliberately keeps a missing-resource placeholder. Import validates path safety and supported media without dropping unknown definitions. Exporters preserve existing valid BMS IDs and source order where possible, allocate new target IDs deterministically, and diagnose identifier exhaustion, duplicate/conflicting definitions, unsupported visual/audio types, and osu!/BMS metadata that cannot cross formats.

Lua-authored note/BGM data may carry one of three direct typed sample references: an osu!-style sample descriptor, a BMS WAV index in a named or current source chart, or a declared/project-relative audio file. The materialized event retains that `sample_reference_t` alternative rather than forcing the script to pre-allocate a generic resource ID. Before serialization, a pure writer-allocation pass resolves these references into the shared resource catalog, reuses compatible existing resources, honors unconflicted explicit source slots, and assigns remaining target slots or names deterministically. The proposed allocation and every conflict appear in conversion preview; details and provisional Lua syntax live in the [OSBX DSL draft](../osbx-draft-spec.md#audio-sample-references-and-writer-allocation).

### Note labels

Treat a note label as an optional UTF-8 editor annotation attached to the note's stable ID. Labels should be searchable and optionally drawn over or beside notes without affecting hit behavior.

No target format should silently receive invented syntax for labels. Labels can live in the native format only where a documented representation exists; otherwise store them in the native project document and warn when exporting a standalone chart. The exact label use case and desired portability remain open questions.

### Semantic note layers

Provide a user-editable layer palette for categorizing notes by musical source or charting intent. Each layer has a stable ID, name, color, optional description, visibility state, and lock state. For example, an author can define a yellow `Vocals` layer and paint vocal-following notes yellow, or use separate layers for strings, percussion, melody, and effects.

At minimum, each note has one optional primary semantic layer whose color controls its editor tint. Preserve note-kind, snap/quantization, hold, selection, and warning cues through shape, outline, or overlays so applying a layer color does not hide gameplay meaning. Provide paint, selection-assign, erase-layer, show/hide, solo, lock, rename, recolor, and select-by-layer operations. A paint drag should be one undoable command.

Semantic layers are editor/project metadata. Persist the palette and note assignments in the native project document and do not reinterpret them as native note types or add undocumented fields to exported charts. Lua pattern generators may inspect and assign layer IDs through the later versioned API.

Use a sound-first icon vocabulary for hitsound/keysound layering so it cannot be mistaken for semantic note layers. The shared UI icon set is Lucide, with `audio-lines` and `layers` available for sound layers. At 16 px, prefer one recognizable waveform glyph; reserve a composite badge for 20–24 px when it remains legible. Semantic layers keep generic stacked cards/color swatches, while hitsound layering, keysound painting, and audition states retain a waveform or speaker as the dominant silhouette. Test normal, armed, disabled, light/dark, and high-DPI rendering. Keep the upstream revision, glyph names, and ISC/Feather MIT notices beside the selected SVG subset.

The icon catalog now vendors a Lucide SVG subset at pinned commit `66d8f9fc394b8530377e5f6112f0b8908ba01280`, alongside its complete license and provenance README. The toolbar rasterizes this subset into embedded alpha masks and uploads it to the GPU at startup. Placement of the `audio-lines` and `layers` icons in the hitsound layering UI remains implementation work; the catalog adds no runtime package dependency.
