# Plan domain: decisions and next work

[Back to the chart editor plan index](../chart-editor-plan.md).

## Early technical decisions

- Use Catch2 v3 for C++ tests and discover individual cases through CMake/CTest with `catch_discover_tests`, rather than registering each executable as one opaque test.
- Use a layered, data-oriented C++20 architecture: C-shaped aggregates and free-function transformations, with `optional`, `variant`, RAII, spans/views, ranges, and typed results providing modern safety.
- Rust-like explicit error/value flow and readable monadic pipelines are welcome; expected failures remain values rather than control flow hidden in exceptions.
- Prefer `std::` facilities. Boost is allowed for specific justified gaps, remains narrowly linked, and stays behind project-owned types and interfaces.
- Use `snake_case` for project code and `<type>_t` for project-defined types; prefer scoped `enum class` types. Methods on structs are welcome when they express behavior local to that data.
- Keep views, controllers, commands/policies, core data, format codecs, and platform backends as separate dependency layers. Avoid manager singletons and routine class hierarchies.
- Treat Nuklear as a replaceable initial frontend: controllers own semantic view state/actions, frontend adapters own widget-library types/composition, and performance-critical editor surfaces use shared renderer-neutral draw/hit-test data. Permit a future wxWidgets, ImGui, or Clay frontend through one CMake/frontend boundary; wxWidgets is not a completion criterion. Split the growing Nuklear adapter into frontend-private pane and shared-widget units after the Stage 3 surfaces stabilize, without moving controller state into those units.
- Keep plan and project state as indexed, single-owner slices. Cross-turn work loads the stable index, the short resume checklist, and only the relevant plan/status ledgers; split a growing slice by subsystem and extend the checked documentation inventory instead of duplicating its requirements or history.
- Express closed conversion strategies as policy values plus tables/free functions or variant visitors. Reserve virtual interfaces for open runtime backends such as decoders and encoders.
- Keep file adapters outside the UI layer.
- Use ordered, independently editable measures as the chart's primary event containers; do not use a monolithic chart-wide event buffer.
- Use measure-owned contiguous note/event lists as spatial buckets and measure-prefix/optional derived span indexes for range work. Design performance fixtures around at least 50,000 notes, including dense-measure worst cases, and keep routine viewport/seek work proportional to intersecting buckets plus output. Target discrete p95 interaction completion within 125 ms; continuous selection/drag/scroll previews have a 33.3 ms p95 ceiling, a 16.7 ms target, and an 8.3 ms aspirational tier on documented reference hardware.
- Keep the serialized document layout platform-neutral. For parallel parsing, validation, schedule compilation, and derived-index construction, partition measure buckets into deterministic work ranges and place mutable worker-local cursors, counters, scratch arenas, and result spans on platform-selected cache-line boundaries. Avoid shared hot counters and cross-worker writes; dense single measures may be subdivided into disjoint ranges, then merged by stable chart/source order. Cache-line size, page/alignment policy, and worker count are runtime/build-platform parameters with conservative fallbacks, never persisted project semantics.
- Build waveform peak/RMS pyramid leaves in parallel across independent channel/block ranges when the decoded frame count exceeds a calibrated threshold, then reduce higher levels in deterministic index order. Give each worker disjoint cache-line-aligned output spans and local accumulation state, reuse the bounded job system, honor cancellation/generation checks between chunks, and retain a serial path for short keysounds where dispatch overhead would exceed the work.
- Keep global division count, exact measure-length ratio, and displayed time signature as distinct properties.
- Keep playable notes, holds, and BGM lane objects inside ordered measure chunks. Keep BPM, stops, delays, warps, SV/scroll/speed, kiai, bookmarks, and other cross-measure information in separate chart-level tracks addressed by exact measure-local positions.
- Default new charts to 960 divisions per standard 4/4 measure (`192 * 5`).
- Store high-resolution measures sparsely and prohibit silent quantization when changing the global division count.
- Quantize row data through one exact-rational `quantize_request_t` transform shared by editor commands and conversion. Preserve coincident rows, preview all movement/collisions, and never silently alter global resolution.
- Map the editor's divisions per standard measure to BMSON pulses per quarter note explicitly: 960 maps naturally to 240 and 192 to 48. Emit measure boundaries as BMSON bar-line pulses and promote output resolution or reject instead of truncating.
- Treat BPM and scroll velocity as separate authored tracks even if a source format stores them in the same section.
- Make ordinary BPM editing decimal-first: parse decimal text exactly into rational core values, keep fraction/numerator-denominator access advanced, and display non-terminating exact values approximately without mutating them until the user commits an edit.
- Store one exact signed beat-zero/audio offset using the StepMania sign convention. Read/write native SM/SSC `OFFSET`, always write the requested BMS `#OFFSET` extension when present, and transform or reject explicitly for targets without an equivalent field.
- Keep song/music selection and the canonical offset in a cross-cutting Audio and synchronization model/UI group, never in a target format's generic metadata rows. Offer the same sample-frame-exact, ghosted waveform-drag offset command from Compose's toolbar mode and Metadata.
- Represent playable columns through layout segments rather than a fixed compile-time maximum.
- Keep lane identity separate from note rendering, with StepMania-style arrow profiles available for 4K, 5K, and 6K.
- Toggle the note field between target-game skin rendering and an exact rational snap-color view.
- Render exact measure, beat/group, and active-snap grid lines across every timing-aligned editor pane, reducing only visual subdivision density as zoom decreases and never changing the underlying snap grid or note positions.
- Support continuous and split-stage presentation for double-style charts, with built-in BMS 10K/14K grouping and configurable osu!mania partitions.
- Keep semantic note layers separate from gameplay note kinds and exported format data; persist their palette and assignments in the native project document. When a target format has no layer feature, flatten layer membership with a non-fatal loss diagnostic while preserving the underlying tap, hold, mine, or other supported gameplay kind.
- Keep bookmarks in the native project, but allow standalone export to formats without bookmark support by omitting them with a non-fatal loss diagnostic.
- Preserve repeated BMS `#xxx01` statements as ordered BGM lanes with structural `00` cells; keep their horizontal scroll independent from playable lanes and make their sounds directly previewable.
- Expose the complete virtual BMS `#000`–`#999` measure canvas immediately, with a dedicated measure-number column and lazy materialization of only authored data.
- Maintain one stable resource catalog with Samples, BGM, and BGA list views. Preserve unused BMS WAV/BMP and osu! sample/event resources, and keep placed note/BGM/BGA events separate from the assets they reference. Offer armable create-only auto-increment in the compact placement toolbar, advancing in stable eligible order only after successful note/BGM/BGA insertions.
- Treat stops and delays as required distinct timing events regardless of target support, with explicit loss diagnostics and well-defined discontinuity ordering.
- Store warps canonically as exact musical intervals. Normalize StepMania 3.9 negative BPM and negative-stop warps on import while retaining provenance, then choose native-warp or legacy encoding through a target-profile export strategy.
- Treat `libotoworm` only as readable reference code and fixture evidence. Wormedit owns and tests every production parser, writer, timing evaluator, and converter and removes the `otoworm::otoworm` link after replacement coverage lands.
- Preserve BMS random/IF/ELSE/switch programs as editable trees; render root content plus the currently selected active leaves and never collapse the source to one hidden random evaluation on import.
- Detect BMS text encoding before parsing, normalize to UTF-8 internally, retain byte/encoding provenance, and evaluate iconv-compatible cross-platform conversion.
- Treat finite, seekable MP3 and Ogg files of variable length as the required streamed-audio case.
- Use cross-platform decoding libraries for Windows, macOS, and Linux; keep waveform generation and playback independent of OS-only codec APIs.
- Selecting or relinking an audio stream immediately reuses or starts cancellable incremental waveform-pyramid generation; generation tickets prevent stale results from replacing the active stream.
- Treat waveform beat anchors as editable synchronization objects and derive the necessary BPM changes through a previewable timing solver.
- Preview chart motion and music through the same timing/scroll evaluator used by editing. Full autoplay includes BMS/BMSON BGM and playable-note keysounds.
- Keep a live DDReam/osu!mania/StepMania-inspired player preview available modelessly while editing, as a dock, detachable panel, or bounded picture-in-picture overlay. Treat neutral/mania/arrow skins as renderer profiles over the shared transport and immutable preview schedule; do not require gameplay rules or a workspace-mode change.
- Treat transport Stop as a hard cancellation boundary: flush every music/keysound/click-preview voice and queue immediately, silence the next mix buffer, and reject stale asynchronous completions by session ID.
- Support both `.sm` and `.ssc` as authoring targets and classify their capabilities feature by feature.
- Preserve source-format provenance until the user intentionally normalizes it.
- Use immutable/stable identifiers for editable entities.
- Make audio clips reference source frames and keep slicing non-destructive.
- Keep BMSON 1.0 authoring limited to its pulse/continuation-based slicing model; do not add fade, gain, or pan fields.
- Use the BMSON 1.0 distinct-pulse slicing and restart rules as normative. Use BmsONE's grid-click, Shift-restart, waveform-lane, and audition behavior as interaction reference, extended by an atomic multi-marker razor workflow.
- Do not use the current seconds-keyed `std::map` representation as the long-term editable document.
- Keep core editing and preview behavior headless-testable and run regression tests on all three target platforms.
- Support both mouse selection and both StepMania-style Space range gestures through testable controllers: hold Space while navigating and release to commit, or tap to arm a flashing anchor and tap again at the tail.
- Treat the mouse as a complete authoring input: snapped note placement, bidirectional Alt/Option press-drag hold entry, reference-relative duration deltas across selected notes, hover feedback for every editable object, paint drag, and same-object double-click property editing such as BPM values.
- Draw exact proposal-derived ghosts for every captured object move, hold/resize, range, recipe, timing, BGM, and BGA transform, distinguishing valid, destructive-normalizing, and invalid outcomes before commit.
- Normalize a hold dragged across its origin by swapping the moving head/fixed tail interpretation; collapsing both endpoints to one row converts it to a regular tap. Covered taps are removed and touching/overlapping ordinary holds merge through one previewed atomic command.
- Make Delete/Backspace remove the active-cursor note when nothing is selected or atomically delete the selected editable objects while preserving shared resources.
- Treat a held lane key plus cursor movement in either direction as StepMania-style hold entry: preview the moving head while retaining the opposite endpoint and commit on release through the same collision/union command as pointer input. Pressing a lane key on an occupied ordinary tap or hold removes the whole object.
- Page by a configurable positive number of measures (default one) with Page Up/Down on Windows/Linux and Option+Up/Down on macOS; begin held repeat at one second and accelerate it at three seconds through controller-owned timing.
- Support coarse mouse wheels and smooth two-axis trackpad scrolling with precise-delta accumulation, natural direction, gesture-stable pane routing, independent BGM horizontal motion, configurable sensitivity, cursor-anchored zoom, and interruptible momentum.
- Provide a visible middle-click autoscroll latch captured to the starting pane, including two-axis velocity and independent horizontal BGM-lane scrolling, with immediate deterministic cancellation.
- Store remappable physical lane-key bindings in lane profiles; use the number row for the left/only stage, QWERTY row for the right stage, the specified interleaved BMS 14K layout, and a banked virtual-piano layout for 24K/48K.
- Store invisible notes, long notes, mines, and retained format-specific note kinds explicitly; render and edit them without collapsing their source semantics.
- Reserve distinct StepMania roll and tick-count timing semantics in the model, with full roll/Pump-style ticked-hold authoring and preview deferred to the late compatibility milestone.
- Follow uBMSC's successive Shift + lane assignment semantics: move, deselect, and optionally audition the first selected playable note while retaining the rest of the selection.
- Use repeatable Up/Down keyboard and held on-screen controls for exact active-snap cursor movement, translating every aligned chart pane beneath one full-width edit head; use Left/Right to cycle only snap fractions supported by the current resolution.
- Treat osu! tags, bookmarks, and kiai timing state as first-class data; preserve unrelated timing-effect bits when kiai changes.
- Use editable semantic-to-BMS/MIDI channel maps for custom BMS layouts and Standard MIDI export.
- Include the command memo's common BMS/BME/PMS lane mappings as data-defined profiles while preserving exact raw channel identities and unusual custom layouts.
- Support synchronized side-by-side difficulty preview and mark differing musical rows with a yellow background.
- Use a compact tabbed song workspace with separate Setup, Metadata, Timing, Compose, Storyboard, Audio/Slices, and Compare/Validate controllers over shared document, difficulty, transport, and command history. Compose is the hybrid BMSE/iBMSC/StepMania surface: toggleable timing and measure rails on the left, bounded square-note gameplay columns, BGM/BGA tracks to the right, and collapsible sound controls; Timing reuses the same grid and field projection.
- Use one conversion library for in-editor and rdconverter-style command-line conversion, with deterministic profiles, typed loss reports, transactional output, and stable automation behavior.
- Defer `.osb`/OSBX authoring, video export, and Lua generation until the chart/timing milestones are stable, while preserving lossless storyboard data and clean extension points now.
- Make Lua generators first-class, versioned project records that can emit chart or storyboard data separately, or both atomically, with explicit regeneration, deterministic seeds, declared asset inputs, and materialized editable output.
- Let one project/DSL source emit several named difficulties, including grouped SM/SSC output.
- Expose imported MIDI to generators through a structured host API with exact tick data; do not require Lua scripts to parse MIDI bytes or read arbitrary paths.
- Treat Lua scroll curves as versioned recipes over `p in [0,1]`; preview their sampled events and use the same deterministic integration for normalization, display, regeneration, and tests.
- Use the versioned Premake-style OSBX Lua DSL for matrix/compound storyboard generation and one scene evaluator for keyframe editing, preview, OSB compilation, and video rendering.
- Use a bounded dynamically growing/recycled frame arena for OSB/OSBX preview and rendering, while retaining authored nodes and stable IDs outside it. Store compiled events in preallocated sorted arrays, binary-search on seek, and advance head handles during playback. Parse, evaluate, and emit OSB loop commands structurally instead of silently flattening them; direct loop/tree editing remains a later UI capability.
- Treat FFmpeg as the leading video encoding/muxing candidate behind an implementation-neutral cross-platform encoder interface.
- Treat area-selected player-preview video export as a very late consumer of the same scene/transport, reachable from the area-selection context menu and never as a prerequisite for live preview or core chart editing.
- Give native projects several ordered difficulties and reviewed output groups: bundle compatible SM/SSC charts and emit deterministic per-difficulty files for BMS-family, BMSON, and osu!mania targets.
- Put metadata in a dedicated tab with explicit shared, per-difficulty, and target-format scope; show profile-default fields as typed controls, include BMS `#SUBARTIST`/`#STAGEFILE` and osu!mania difficulty name, OD, HP, key count, and retained AR, preview key-count migration, and support transactional song-file copy/drop/relink into project or export directories.
- Keep Quaver rhythm-game support as a late-stage bidirectional adapter governed by the same capability and regression gates.
- Autosave dirty documents every five minutes to a validated atomic `.bak`, detect and compare it on reopen, preserve the primary and last good backup on every failure, and keep hard-crash handlers limited to safe marker/diagnostic work.

## Open questions

1. What should note labels mean in practice: private author comments, visible note text, keysound names, gameplay annotations, or several of these?
2. Must labels travel with standalone chart files, or is native-project persistence sufficient for formats without a native representation?
3. Which BMS dialect/player is the primary export compatibility target (for example, conservative legacy BMS versus modern extensions used by beatoraja)?
4. How much non-gameplay source data must round-trip initially: BGA/storyboard/video, preview metadata, custom headers/tags, and comments?
5. When a BPM edit affects time-anchored osu!mania notes, which behavior should be the default: keep their audio times fixed or move them with the beat grid?
6. May column count change only at measure boundaries, or must mid-measure layout changes be editable too?
7. When locally dragging an unlocked beat, should the default preserve both neighboring beat anchors by changing BPM on both sides, or preserve only the previous anchor and allow the following grid to move?
8. Which exact 5K and 6K StepMania-style layouts and arrow orientations should ship as built-in profiles?
9. Should the later storyboard editor initially target standalone shared `.osb` files, difficulty-specific `[Events]`, or both in its first release?
10. Should notes eventually allow several semantic tags/layers at once, and if so, how should multiple assigned colors be drawn?
11. Which exact BMS double-play profiles should be named 10K and 14K in the UI, and should scratch columns sit outside or inside each player stage by default?
12. Should the native project ship first as a human-readable folder/manifest, a single-file archive, or both?
13. Which MIDI features beyond notes, tempo, meter, markers, and controller data must the first Lua API expose (for example pitch bend, aftertouch, lyrics, or SysEx)?
14. What is the complete note-snap color table for straight and tuplet divisions beyond the initial red/blue/yellow classes?
15. Which BMS encodings and aliases belong in the automatic detector's initial candidate set, and what confidence threshold should force manual confirmation?
16. Which initial video containers/codecs, resolutions, frame rates, alpha/background policies, and audio-mix options are required?
17. What should the converter executable be named, and should it provide an `rdconverter` compatibility alias or only a compatible workflow?
18. Which StepMania implementations define the initial export profiles, and exactly which native-warp, negative-BPM, and negative-stop encodings does each accept?

## Immediate next work

Finish Stage 0's runtime BMSE/iBMSC clipboard captures and broaden the StepMania source/target syntax-profile fixtures, including repeated negative-BPM regions; current profile names do not yet establish player/version compatibility. Continue BMS work with broader dialect/profile fixtures and structural/scoped branch editing beyond inactive raw-statement replacement/insertion/deletion while ordinary normalized saves retain the complete control program. The osu!mania reader and writer already cover every integer 1K–18K key count through dynamic layouts, with all lanes covered by semantic round-trip tests; expand version-specific metadata and implementation profiles. The next frontend slice refactors Compose/Timing around the shared hybrid field, virtual BMS canvas, compact placement toolbar with auto-increment and waveform-offset mode, decimal-first BPM controls, typed Metadata/audio controls, both Space-selection gestures, and proposal-derived interaction ghosts. Audio-stream selection must immediately start/reuse cancellable incremental waveform generation. The next timing/audio slice adds richer profile-specific discontinuity ordering, concrete image/video/storyboard rendering, and the SDL frontend connection for the bounded headless timing/scroll, count-in, preview transport, and BGA consumers. The periodic autosave loop now runs with bounded background publication; continue the journal/crash-marker work and remaining pane editors.
