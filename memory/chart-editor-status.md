# Chart editor status

Last updated: 2026-09-13

## Current milestone

The planning review is complete enough to begin implementation. Stage 0 is establishing the build/test/dependency environment, fixture inventory, and loss-aware document foundation.

The Catch2 v3/CMake/CTest harness and pinned optional Lua 5.4.8 target are established. Twenty-eight text fixtures cover BMS/BME/BML/PMS, SM/SSC, osu!mania, and BMSON positive and negative semantics, including the BMSON specification's published `vox.wav` slicing sequence transcribed to JSON and all zero/null/omitted default-resolution spellings. Eight byte-authored BMS streams now cover UTF-8 with BOM, CP932, EUC-KR, CP949-only text, Windows-1252, ambiguous ASCII, malformed UTF-8, and a truncated CP932 lead byte; authoritative lowercase-hex recipes are checked against every fixture byte-for-byte. [`tests/fixtures/README.md`](../tests/fixtures/README.md) is the capability inventory, and [`docs/fixtures/libotoworm-capability-audit.md`](../docs/fixtures/libotoworm-capability-audit.md) records precisely what the current library bridge exposes and discards. Clipboard captures and real audio fixtures remain outstanding before Stage 0 reaches its exit condition.

The first replacement document slice is implemented in `include/wormedit/core/document.h` and `src/core/document.cpp`. It defines strong stable IDs, exact measure-local positions, ordered measure chunks, explicit notes/holds and BGM source lanes, typed timing/pause/warp/scroll/speed/kiai data, lane layouts and physical-key input profiles, semantic layers, resource/audio/visual/slice provenance, BMS control-tree storage, MIDI/generator/storyboard extension records, conversion requests/reports, and graph validation diagnostics. Exact measure-prefix indexing and atomic exact-only global redivision are implemented and tested; inexact positions reject without mutating the source chart.

Data-defined profiles now cover BMS 5K/10K free-zone and foot-pedal variants, BME 7K/14K with selectable inside/outside scratches and optional pedals, standard PMS 5K/9K, BME-style PMS 9K/18K, double-scratch/FPP, octave/foot-pedal, Project2DX, and BMSON-only generic 24K/48K piano banks. Each BMS-family lane keeps its exact visible/invisible/long-note/mine channel identities independently from stage, display order, role, and physical input binding. Confidence-based profile detection and lossless custom-map fallback fixtures remain to implement.

Stage-zero CMake now includes generic headless cross-toolchain presets for Linux targets and Windows/MinGW targets from Linux. Compiler paths, target triples, sysroots, resource compiler, and optional emulator/Wine paths are caller supplied; full application cross-presets wait for target-side SDL3 GPU/audio/package discovery.

The headless core also implements exact textual BMS measure-length parsing, bounded deterministic IEEE-754-to-rational conversion with explicit approximation/error reporting, exact row-capacity/meter validation, the default editor-to-BMSON grid mapping, and supported-snap filtering with a guaranteed one-row fallback. `quantize_chart_exact` provides standard-measure and authored-measure grids, deterministic ties, selection and hold policies, collision diagnostics, exact opt-in global-resolution promotion, cross-measure rehoming, movement previews, and atomic rejection for invalid holds/layout effects. CTest discovers 33 core/fixture/profile cases and two optional Lua runtime cases; all 35 pass on the macOS development host. The legacy desktop scaffold also configures and builds with this core linked. Semantic readers/writers do not exist yet.

The BMSON 1.0 normative rule for `info.resolution` is now explicit: zero, null, or omission selects the default 240 pulses per quarter note and retains source provenance. Wrong JSON types and invalid pulse values remain negative cases. This corrects the earlier reference-note wording that treated zero as invalid.

## Settled decisions

- Support authoring and semantic round trips for BMS-family charts, `.sm`, `.ssc`, osu!mania `.osu`, and BMSON 1.0.
- Keep Quaver rhythm-game chart support as a late-stage read/write adapter; it does not displace the initial format milestones.
- Store several ordered difficulties in one native project. Bundle compatible SM/SSC difficulties in one output and emit deterministic transactional per-difficulty output sets for BMS-family, BMSON, and osu!mania targets.
- Provide a dedicated Metadata tab with shared song fields, per-difficulty fields, inheritance/overrides, and visibly grouped target-format fields. Preserve BMS `#SUBARTIST`/`#STAGEFILE` and osu!mania difficulty name, OD, HP, key count, retained AR, spelling, and provenance.
- Count a format/profile as supported only when both its reader and writer pass semantic import-edit-export-reimport tests; import-only adapters remain experimental.
- Store editable chart content in ordered measure chunks rather than one chart-wide event buffer.
- Store notes, holds, and autoplay/BGM lane objects in measure chunks. Keep BPM, stops, delays, warps, SV/scroll/speed, kiai, bookmarks, and other cross-measure information in separate chart-level tracks addressed by exact measure-local positions.
- Use one editable global division count per chart, expressed as divisions per standard 4/4 measure. New charts default to 960 (`192 * 5`); 192 remains supported.
- BMSON stores pulses per quarter note rather than the editor's divisions per standard measure. Map 960 editor divisions naturally to BMSON resolution 240 and 192 to 48; emit exact measure prefixes as BMSON bar lines and promote or reject rather than truncate pulses.
- Store variable measure lengths exactly. Keep the exact length ratio separate from its optional time-signature interpretation and let users resolve equivalent-meter ambiguity.
- Express measure length in BMS units (`1` = four quarter-note beats, `0.75` = three). Parse persisted decimal strings to exact reduced rationals; convert bare floating values only through a checked algorithm with fixed iteration/denominator limits and explicit approximation diagnostics.
- Show the exact BMS length and musical meter together. A user-entered meter such as `6/8` derives BMS length `0.75` but retains and displays `6/8`; length and meter remain separate fields and must agree exactly when both are supplied.
- Support variable column counts through semantic lane-layout profiles.
- Offer StepMania-style directional arrow rendering for 4K, 5K, and 6K profiles as well as square/bar notes.
- Toggle the field between target-game skin mode and an exact rational note-snap color mode using the StepMania editor palette.
- Let double-style charts use a continuous or split-stage field. Provide BMS 10K/14K player-stage profiles and user-configurable osu!mania split boundaries without changing source column identity.
- Provide user-defined semantic note layers with names and colors, such as yellow for vocal-following notes. Support painting, filtering, locking, selection, undo, and native-project persistence without changing exported gameplay note types.
- Preserve repeated BMS `#xxx01` lines as distinct ordered BGM lanes, including structural `00` slots. BGM lanes share vertical time position with notes but scroll horizontally on their own.
- Provide dedicated editable BGM lanes for BMS autoplay events and BMSON non-playable sound events.
- Clicking a BGM object auditions its exact keysound or BMSON-derived slice/layers.
- Provide Samples, BGM, and BGA resource lists over one shared catalog. Support BMS `#WAV`/`#BMP` identities and osu! sample/background/video/storyboard metadata, including unused and missing definitions.
- Let authors audition audio, preview visual thumbnails, find uses, import resources, and explicitly create/assign/replace playable notes, autoplay events, and timed BGA events without duplicating the underlying asset.
- Stream finite, seekable MP3 and Ogg files without decoding the whole asset into memory.
- Use cross-platform audio-decoding libraries on Windows, macOS, and Linux rather than relying on an OS-only codec path.
- Provide a DDReam-inspired synchronization preview with draggable beat anchors, Ctrl-drag local beat fitting, and drag-to-fit from the prior BPM change.
- Provide a fixed iBMSC-style timing rail at the left with separate measure/meter, BPM, scroll/SV, speed, stop, delay, warp, kiai, anchor, and section lanes sharing the chart's vertical axis.
- Provide a modeless Timing Settings panel for metronome/count-in, lane and note/BGM/BGA visibility, and timing lock. Locks are enforced below the UI so pointer, keyboard, paste, quantization, and Lua commands cannot mutate timing while locked.
- Schedule a meter-aware metronome through the same exact timing/offset/discontinuity evaluator as preview, including retained groupings such as 6/8.
- Provide a chart-play preview synchronized with streamed music and the effective timing/scroll map. Full autoplay plays BMS/BMSON BGM events and every visible playable-note keysound, including simultaneous BMSON slices/layers and the selected BMS branch projection.
- Stop is a hard transport cancellation boundary: cancel the session, clear scheduled/decoded buffers, kill music, BGM, playable-note, and click-preview voices immediately, produce silence by the next audio mix callback, and prevent late jobs from reviving playback.
- Make timing fits live-previewable, cancellable, atomic, and undoable.
- Stops and delays are required distinct internal timing events even when an export target cannot represent them. Preserve source units/order and diagnose target loss explicitly.
- Store an exact signed beat-zero/audio offset where positive means beat zero occurs after audio start. Read/write native SM/SSC `OFFSET`; read and always write BMS `#OFFSET` when present while diagnosing player support; shift or reject explicitly for osu!mania/BMSON rather than dropping it.
- Treat both `.sm` and `.ssc` as authoring targets. Determine support through a tested feature matrix.
- BMSON 1.0 audio authoring supports its specified pulse/continuation slicing behavior only. It does not add fade, gain, pan, or arbitrary slice fields.
- Use the BMSON 1.0 specification as normative for distinct-pulse slices, continuation/restart, same-pulse conflict, playable-over-BGM, polyphony, layering, and event order. Use BmsONE only as interaction reference for snapped click-to-slice, Shift-click restart, waveform lanes, and audition.
- Provide an atomic multi-marker razor workflow over each timing-aligned sound-channel waveform, with snapped markers, continue/restart toggles, drag/delete, region audition, lane/BGM assignment, cancel, commit, and undo.
- Core model, timing, audio, and editor-controller behavior must be headless-testable to prevent regressions, with CI on all three desktop targets.
- Every parser/converter/interaction contract needs positive and negative regression cases; rejected input must not partially mutate documents or outputs.
- Use Catch2 v3 for C++ tests. Gate the test tree with CMake's `BUILD_TESTING`, link test executables to `Catch2::Catch2WithMain`, and register individual cases through `catch_discover_tests(... DISCOVERY_MODE PRE_TEST)` so CTest and CI can discover and label them.
- Support direct mouse selection and StepMania-style two-point Space range selection.
- Support snapped mouse placement for notes and holds, transactional paint drag, visible hover state for every editable chart object, and same-object double-click property editing such as BPM values. Route these through the same validated undo commands as keyboard input.
- Page by a configurable positive number of measures, default one, with Page Up/Down on Windows/Linux and Option+Up/Down on macOS. Controller-owned hold timing starts repeat after one second and accelerates after three seconds.
- Support coarse wheel and smooth two-axis trackpad viewport scrolling with fractional accumulation, natural direction, stable pane routing, independent BGM horizontal scroll, configurable sensitivity, cursor-anchored zoom, and interruptible momentum. Scrolling never emits chart edits.
- Middle-click latches visible two-axis autoscroll to the starting pane; this is especially for horizontally wide BMS BGM-lane banks. A second middle click, Escape, focus loss, modal opening, or normal editing click cancels it without emitting an edit.
- Support profile-driven lane-key entry. Default the left/only stage to number keys and the right double-play stage to the QWERTY row; use `Q A W S E D R F` and `H U J I K O L P` for BMS 14K.
- With a selection, a bare lane key moves all selected playable notes and keeps them selected. Shift + lane follows uBMSC: atomically move the first selected playable note in deterministic order, deselect/audition it, and retain the rest for successive assignment. Failed moves retain data and selection.
- Up/Down move the edit cursor by the active exact snap; Left/Right cycle a configurable StepMania-style snap list after filtering out fractions unsupported by the current resolution.
- Use a two-octave virtual-piano physical-key map for BMSON-only Keyboardmania-style 24K. For BMSON 48K, reuse it for the visibly focused 24-key stage, with remapping and later direct MIDI/multi-device bindings available. Emit BMSON `generic-24keys`/`generic-48keys` with lanes ordered left-to-right; do not map these modes to BMS/MGQ channels.
- Support synchronized side-by-side difficulty preview, marking different musical rows with a yellow background.
- Support a versioned native rich clipboard format plus BMSE and iBMSC clipboard compatibility with paste preview and loss/conflict diagnostics.
- Defer osu! storyboard authoring, OSBX/video export, and Lua generation until the core milestones are stable, while keeping model extension points and lossless source retention.
- Use a native, versioned project format that stores Lua generator recipes, declared asset inputs, parameters, deterministic seeds, hashes/provenance, and materialized editable notes.
- Import MIDI as a project asset and expose structured exact-tick MIDI data to Lua through a cross-platform host API. Loading an asset alone does not change chart timing, and regeneration remains explicit and previewable.
- Use a Premake-style, versioned OSBX Lua DSL. Storyboard and note modules can be separate; one project can emit several difficulties, including grouped SM/SSC output.
- Allow Lua generators to emit notes/layers, OSBX storyboard objects/keyframes, or both in one transaction.
- Use a keyframe-style OSB/OSBX editor and one deterministic scene evaluator for preview, OSB compatibility compilation, and offline video rendering.
- Support storyboard-to-video export through an encoder interface; evaluate FFmpeg libraries and a packaged/discovered FFmpeg process across Windows, macOS, and Linux.
- Treat `libotoworm` only as readable reference code and fixture evidence. Wormedit owns every production parser, writer, timing evaluator, and converter; do not add or extend it as the implementation path.
- Detect BMS encoding from bytes with user override for uncertain cases. Support at least UTF-8, CP932/Shift-JIS, EUC-KR/CP949, and Windows-1252; normalize internally to UTF-8 and retain original bytes/encoding for loss-aware saves. Evaluate iconv-compatible conversion.
- Preserve ordered osu! search tags, millisecond bookmarks, labeled native song sections, and kiai timing state. Editing kiai must retain other timing-point effect bits.
- Support editable custom BMS channel maps connecting semantic lanes to raw BMS channels and optional MIDI channel/note destinations.
- Ship data-defined command-memo BMS lane profiles: BMS 5K/10K, BME 7K/14K, standard and BME-style PMS 5K/9K/18K, plus named free-zone/foot-pedal, double-scratch, octave, and Project2DX variants. Historical MGQ remains raw/custom BMS data rather than a Keyboardmania profile. Keep semantic role, raw channel, display order, and input binding separate.
- Detect a BMS layout only with sufficient extension/header/channel evidence; ambiguous or unusual layouts remain lossless custom maps. Mirror semantic lanes across visible/invisible/LN/mine families only where the selected profile defines them.
- Store invisible notes, long notes, mines, and retained target-specific note kinds explicitly. BMS uses its parallel channel families, StepMania retains its native symbols, BMSON supports taps/long lengths, and unsupported exports require a diagnosed explicit transform.
- Preserve StepMania roll and tick-count provenance early, but defer full roll re-hit/life authoring and Pump-style ticked-hold timing/preview/export to the late compatibility milestone after the main formats are stable.
- Export Standard MIDI through those maps with explicit musical and audible-performance timing policies and no silent tick quantization.
- Preserve BMS `#RANDOM`/`#SETRANDOM`, IF/ELSEIF/ELSE, and SWITCH/CASE/DEF/SKIP structures as an editable ordered tree. The chart view renders root/common content plus the leaves activated by explicit manual or seeded branch choices.
- Branch edits have an explicit selected-leaf, ancestor/common, or replicated-siblings scope. Export can preserve the tree or deliberately flatten the active projection.
- Do not rely on current `libotoworm` evaluation for this contract: its loader discards inactive branches, and its fixed-roll, end-marker, mixed-nesting, caller-selection, and switch-fallthrough support is incomplete.
- Let versioned Lua chart modules generate SV, StepMania `SCROLLS`, and `SPEEDS` from deterministic functions over normalized `p in [0,1]`, with previewed fixed/adaptive sampling.
- Include linear, endpoint-safe exponential, constant, finite two-region bump, multiplication, and typed Dirac-impulse primitives. A bump takes user-specified `Len(A)` and derives `Len(B) = TotalSegment - Len(A)`; balancing may solve `X` or `Y` so the normalized weighted area is 1.
- Run normalization and bump balancing as ordered post-processes over the raw typed batch; they can preserve prior integrated scroll displacement or absolute distance for generated or selected existing segments.
- Use a layered, data-oriented C++20 style: plain aggregates and free-function pipelines by default, methods where behavior naturally belongs to one struct, and modern safety through RAII, `optional`, `variant`, spans/views, ranges, and typed results.
- Use `snake_case` for project code, `<type>_t` for project-defined types, and scoped `enum class` values. Readable Rust-like monadic result/optional pipelines and exhaustive variant handling are encouraged.
- Prefer the C++ standard library. Boost is permissible for focused portability/correctness gaps, but each component must be justified, narrowly linked, and hidden behind wormedit-owned APIs.
- Autosave every dirty native document revision at five-minute intervals to a validated atomic `.bak`; use a recovery-directory project-ID backup for untitled/import-only sessions. Detect backups and crash markers at startup/open, compare validated IDs/revisions, and never overwrite the primary automatically.
- If a crash marker exists and the primary is valid with no newer recovery snapshot, preserve that state to `.bak`. A top-level coherent exception may request an emergency validated snapshot; hard-fault handlers only write preallocated markers/diagnostics and recover from the last autosave/checksummed command journal.
- Keep UI views, application controllers, commands/policies, core data, format codecs, and platform backends separate and independently testable.
- Nuklear is the initial replaceable UI frontend. Controllers expose backend-neutral view models and typed actions; only `src/ui/nuklear` may own Nuklear types/composition. High-volume chart/waveform/BGM/storyboard surfaces use shared draw and hit-test data through SDL3 GPU. Plan for a single CMake-selected Nuklear, ImGui, or Clay frontend without linking UI libraries into headless targets.
- Store warps as exact musical intervals. StepMania 3.9 negative BPM and negative-stop warp encodings normalize into `warp_event_t` values on import with source provenance retained.
- Select native `WARPS`, legacy negative-BPM/negative-stop encoding, or rejection through an explicit target-implementation export policy, then semantically re-import to verify the result.
- Use one transactional conversion library for in-editor conversion and a deterministic rdconverter-style CLI, including difficulty/branch/profile/encoding/loss policies and machine-readable diagnostics.
- Use an osu!-style tabbed workspace for Setup, Metadata, Timing, Compose, Storyboard, Audio/Slices, and Compare/Validate. Tabs have separate controllers and focus contexts while sharing the song document, difficulty context, transport, and command history.
- Support exact row quantization to a user-requested grid, either standard-measure divisions or equal divisions of each authored measure. Coincident selected row objects move together to the nearest destination with a visible tie/collision/hold policy, preview, atomic undo, and no silent resolution change.
- Use the same `quantize_request_t` transform for editor and conversion/CLI quantization, including exact global-resolution promotion when explicitly accepted.

## Open decisions

- Define the meaning and persistence expectations of note labels.
- Choose the primary BMS dialect/player compatibility profiles.
- Decide which malformed BMS control-flow constructs each target profile repairs, preserves as raw text, or rejects, plus safe nesting/outcome-enumeration caps.
- Define how much BGA, storyboard, preview metadata, custom tags, and comments must round-trip initially.
- Choose default behavior for beat-anchored versus time-anchored notes after timing edits.
- Decide whether lane-layout changes may occur mid-measure.
- Decide whether local beat dragging preserves both adjacent anchors or lets the following grid move by default.
- Specify the built-in 5K and 6K directional-arrow layouts.
- Decide whether a note may eventually have multiple semantic layers/tags and how to render competing colors.
- Specify UI naming and default scratch placement for BMS 10K/14K split-stage profiles.
- Scope the first storyboard milestone between standalone `.osb` and difficulty-specific `.osu` events.
- Select cross-platform audio decoder and Lua runtime libraries through focused implementation spikes.
- Choose folder/manifest, single-file archive, or dual packaging for the native project format.
- Finalize the first structured MIDI/Lua API surface beyond core notes, timing, tracks, channels, velocities, controllers, and markers.
- Complete the finer straight/tuplet note-snap color table beyond the initial red, blue, and yellow classes.
- Choose BMS detector candidates/aliases, confidence threshold, and whether to package GNU libiconv on every platform or normalize system implementations behind one wrapper.
- Select BMSE/iBMSC versions for clipboard compatibility fixtures and settle behavior for data their clipboard formats cannot carry.
- Finalize the ordered common snap palette after the required `1/4`, `1/8`, and `1/6` entries.
- Decide the default tap duration, PPQ cap, track grouping, and loss policy for Standard MIDI export.
- Choose default curve sampling tolerance/point caps and per-target finite-pulse policy for Dirac impulses.
- Finalize OSBX clip/loop/trigger syntax, visual-override storage, OSB bake/rasterization policy, and initial video profiles.
- Choose the converter executable name/compatibility alias and initial stable CLI/JSON schema.
- Define the exact supported StepMania implementation profiles and the negative-BPM/negative-stop encoding rules each accepts.

## Resume here

1. Create short and long MP3/Ogg fixtures plus BMS/BMSON keysound sets, and define bounded-memory decode/seek/waveform, full-autoplay scheduling, polyphony, and hard-stop assertions.
2. Capture BMSE/iBMSC clipboard samples and add positive/negative clipboard fixtures with tool/version provenance.
3. Implement confidence-based BMS lane-profile detection and lossless custom fallback fixtures.
4. Implement the first wormedit-owned byte parser into the replacement document, starting with exact/provenance-preserving BMS command ingestion and control-tree construction or BMSON 1.0 JSON ingestion. Do not extend the `libotoworm` bridge.
5. Add broader document-graph validation for branch trees and target resource identities, then expose quantization through the future command/undo layer.
6. Keep [`docs/chart-editor-plan.md`](../docs/chart-editor-plan.md) and this status file current when decisions change.

## Current code constraints

- `src/common/chart.cpp` imports through `libotoworm` and converts most event positions to seconds.
- `src/common/Chart.h` uses chart-wide time-keyed maps and a fixed `MAX_CHANNELS` constant.
- BMSON slice data, source sound channels, BGM events, measure lengths, and much source metadata are currently discarded by wormedit's conversion.
- `src/editor/field.cpp` has no implementation yet.
- `src/sys/audio.cpp` initializes SDL audio but has no decoder, playback stream, waveform analysis, or preview engine.
