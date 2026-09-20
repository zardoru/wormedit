# Settled decisions: editor, audio, and preview

[Back to the settled-decision index](../settled-decisions.md).

- Support variable column counts through semantic lane-layout profiles.
- Offer StepMania-style directional arrow rendering for 4K, 5K, and 6K profiles as well as square/bar notes.
- Toggle the field between target-game skin mode and an exact rational note-snap color mode using the StepMania editor palette.
- Draw synchronized measure, authored beat/group, and active-snap grid lines across timing, note, BGM/BGA, and waveform panes; zoom controls visual spacing and density only, never chart rows or snapping.
- Keep ArrowVortex as the visual/interaction reference for a dense DDReam-like waveform behind the chart field: high-contrast lane-readable peaks, chart-wide grid/playhead overlays, a narrow song overview, and adjacent synchronization controls. Waveform filters/colors/luminance are non-destructive view settings and may not mutate audio, timing, or canonical peak data.
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
- Keep iBMSC as the named Compose information-hierarchy reference: compact controls, dominant canvas, persistent left rails, compact playable columns beside BGM/BGA tracks, and bounded right-side resources/inspectors. Do not couple the controller or document model to iBMSC's platform UI.
- Use a mobile edit head: exact Up/Down movement traverses a central comfort band, then scrolls timing, waveform, notes, BGM/BGA, and measure rails as one projection at either band edge. Keep the edit-head line chart-wide and visually distinct from the transport playhead.
- Prefer iBMSC/ArrowVortex-style docked, collapsible side panels over vertically stacked Compose controls. Keep the chart canvas dominant; bookmarks, layers, branches, properties, waveform/sync tools, and resources belong in bounded side-panel groups backed by renderer-neutral view data and typed actions.
- Keep the compact Compose command strip sticky and hide recovery chrome when there is no actionable recovery state. Divide the canvas into fixed measure/toggleable timing rails, profile-sized playable lanes, and an independently horizontally scrollable BGM/BMSON slice/reset pane before the collapsible resource drawer.
- Render notes according to the active lane presentation: compact square indexed tiles for neutral BMS/iBMSC layouts and directional heads/hold artwork for ArrowVortex-like dance layouts, without changing lane identity or chart data.
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
