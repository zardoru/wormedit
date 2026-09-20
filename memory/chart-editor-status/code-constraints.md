# Status: current code constraints

[Back to the chart editor status index](../chart-editor-status.md).

## Current code constraints

The Compose pane now draws bounded scalar projections of the renderer-neutral field scene in Nuklear: layout bands, lanes, exact grid lines, notes/holds, comparison differences, selection styling, cursor geometry, and live note-paint/rectangle/Space-range previews. Its retained frontend state wires core pointer/keyboard note editing, bounded atomic note-paint drags, rectangle selection, tap-anchor/tap-tail and press-hold/navigate/release Space selection, including blinking exact anchor and tail rows, selection-aware hold resizing, deletion, lane-key holds, vertical wheel scrolling, pointer-anchored wheel zoom, toolbar zoom, page scrolling, and reset actions. Revision-bound valid hold and duration-resize proposals render as noninteractive amber ghosts with subdued committed origins; generic move and invalid/destructive proposal geometry, select-and-drag move/property wiring, horizontal BGM routing, and several richer interaction modes remain unwired. Bookmark/section widgets and their revision-local draft state are the first Compose controls extracted into a frontend-private unit. Audio-time navigation crosses that unit only as typed previous/next actions and scalar availability; exact marker lookup and preview-clock seeking stay in the application layer. This boundary remains reusable by a future frontend adapter.

The earlier Compose constraint about horizontal BGM routing is resolved: BGM wheel/autoscroll routing, stable-ID tile selection, revision-bound click-audition intent, startup decoder binding, exact clip preparation, bounded generation-aware decode lookahead, headless session/timeline ownership, and SDL device/callback/stream-flush output are wired. The enabled `transport.stop` command now hard-stops an active click preview and clears queued device samples. Recovery/session replacement reconciles the project-scoped audio-asset inventory, hard-stops playback before removing stale decoders, assigns fresh immutable binding identities, and reports assets that cannot be reopened. Property editing, pane-specific drag geometry, and smooth trackpad gesture lifecycle remain unwired.

- `src/common/chart.cpp` imports through `libotoworm` and converts most event positions to seconds.
- `src/common/Chart.h` uses chart-wide time-keyed maps and a fixed `MAX_CHANNELS` constant.
- BMSON slice data, source sound channels, BGM events, measure lengths, and much source metadata are currently discarded by wormedit's conversion.
- `src/editor/field.cpp` has no implementation yet.
- `src/sys/audio.cpp` still only owns SDL audio subsystem lifetime; private one-shot and chart-preview adapters own their playback streams. The first Audio/Slices waveform overview is wired through the task scheduler and Nuklear draw boundary, while timing-aligned waveform navigation, slice markers, and SDL GPU rendering remain separate.
