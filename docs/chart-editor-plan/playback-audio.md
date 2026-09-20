# Plan domain: playback and audio

[Back to the chart editor plan index](../chart-editor-plan.md).

## Playback and timing preview

The modeless chart-play surface, its shared deterministic schedule, and the very-late selected-area video exporter have their own [player-preview slice](playback/player-preview.md). Load that slice only when working on presentation preview or offline video.

The DDReam-style waveform is the primary timing-authoring and synchronization surface, not a decorative overview. It overlays draggable beat markers, measure/active-snap grid lines, timing anchors, current/proposed BPM spans, stops, delays, warps, SV/scroll state, note rows, BGM/BGA events, and a sample-frame playhead over the waveform. Authors can zoom from whole-song peaks to sample-level alignment, scrub directly, select/loop a waveform interval, and keep the detailed waveform locked to the chart field while editing.

Playback streams from the original audio, supports seek and looped regions, and offers beat-assist, note-assist, and a configurable metronome. The metronome schedules from exact musical positions through the same timing/offset/discontinuity evaluator as chart preview; it accents the retained musical meter and grouping when present, otherwise uses the selected fallback grouping. Optional subdivisions must be representable on the current grid. Notes inside warps produce no elapsed-time click, and a click on a stop/delay boundary follows the format-profile event-order rule. Count-in may begin before beat zero when the audio/offset range allows it. The waveform stays visually fixed while a timing preview moves the proposed beat grid over it, making before/after alignment immediately visible. Playback-rate changes are preview controls and must not rewrite chart timing or audio.

Preview audio modes include music-only and full chart autoplay. Full autoplay schedules both autoplay/BGM events and playable-note keysounds, so a BMS preview plays `#xxx01` sounds plus the `#WAV` assigned to every visible playable object as if an autoplay player hit it. BMSON fused/layered sounds and specification-derived slices play together; BMS branch-dependent resources and notes come only from the active branch projection. A later interactive playtest may trigger playable-note sounds from live input, but deterministic full autoplay is the initial required verification mode.

The first headless implementation of this contract is documented in
[`timing-preview-scheduling.md`](../timing-preview-scheduling.md). It fixes the shared
delay/event/stop boundary vocabulary, exact sample rounding, half-open canonical
warp behavior, stable family ordering, bounded schedule construction, and
binary-search/head-handle queue API before a frontend or SDL callback consumes it.

Stopping transport is a hard audio stop. It synchronously marks the current `transport_session_id_t` cancelled, prevents queued/decode-completion work from submitting more voices, clears all scheduled events and decoder/ring buffers, and stops every active music, autoplay, playable-note, and click-preview voice. The mixer must output silence for that session by the next audio callback/buffer; no keysound tail, release envelope, or stale asynchronous job may continue after Stop. Starting again creates a new session ID so an old callback cannot resurrect playback. Closing a document or audio device uses the same stop path.

An area or Space selection in a chart can also define the source interval for **Generate `preview.ogg`**. The command shows the exact selected musical and resolved audio-time bounds, accepts an explicit output duration, and previews any trim, loop, fade-out, or duration-fit policy before rendering. It mixes the same BGM/keysound event schedule and timing map used by live preview, writes through a bounded cancellable background render to a temporary output, and publishes `preview.ogg` only after encode and validation succeed. Selection, duration, and render options are request state rather than chart mutations; cancellation, stale document generations, missing assets, an empty interval, or an unsupported encoder leave both the project and any existing preview file unchanged.

Timing edits use a live preview transaction. While a beat is dragged, keep the waveform fixed and redraw the proposed beat grid, BPM values, notes, and affected event times continuously. Show the target audio time/sample frame, old and proposed BPM, affected beat/measure range, and whether the operation will insert, replace, or remove BPM events. Mouse release commits the whole fit as one undoable command; Escape restores the original timing.

Support two beat-drag operations:

1. **Ctrl-drag an individual beat (local beat snap).** Pin the dragged beat to the selected audio frame and adjust the adjacent timing interval or intervals so the beat reaches that point. Neighboring locked anchors remain fixed. This may create or update BPM changes around the dragged beat, which allows live or drifting performances to be synchronized beat by beat without moving unrelated anchors.
2. **Drag-to-fit from the last BPM change (interval fit).** Keep the previous BPM-change anchor fixed, drag the current beat to an audio frame, and solve one BPM for the entire interval. All intermediate beat markers expand or contract uniformly. For an interval without pauses or warps, `new BPM = 60 * beat distance / audio-time distance`; the timing service must account for stops, delays, and warps before applying the equivalent calculation.

The first mode is local and may yield adjacent BPM segments. The second rewrites the constant-BPM span from the last change point through the dragged beat. Changes after the fitted endpoint follow the document's anchor policy: locked anchors remain fixed and require compensating timing, while unlocked downstream events are recomputed from the new timing map. The preview must make that consequence visible before commit.

Beat snapping should always allow direct alignment by eye with the waveform. Optional magnetic snapping may target a nearby waveform onset/peak, with an adjustable threshold and a temporary bypass modifier. Store the chosen target as an audio sample frame so repeated edits do not accumulate millisecond rounding errors.

## Waveform and streaming audio

Here, streamed audio means finite, seekable MP3 and Ogg files of variable length. It does not currently mean a live input or network stream. MP3 is a required first-class music format for SM/SSC and osu!mania projects: import, playback, accurate seeking, waveform generation/cache, timing preview, looping, and chart-play preview must all work from MP3 sources. Waveform generation and playback must not require retaining a fully decoded song in memory. Add a decoder interface that yields interleaved PCM chunks and reports sample rate, channels, duration, and seek capability.

Immediately when an audio stream becomes the selected song/music asset or waveform-inspection target, a background waveform job should reuse a matching completed/in-progress content cache or:

1. decode sequential chunks;
2. calculate min/max peaks and optionally RMS values in fixed source-frame windows;
3. build coarser peak levels for zoomed-out views;
4. publish completed ranges incrementally so the waveform appears while scanning continues;
5. cache the result using source identity plus decoder/cache version.

Rendering selects the cache level that is closest to one peak bucket per screen pixel. The same cache supports a full-song stream, a keysound, a BGM click-preview, and a zoomed slice editor. Playback uses a bounded decode/ring buffer and seeks independently from waveform analysis. File length must affect processing time and cache size, not decoded-memory residency.

Audio decoding must use cross-platform libraries from the shared codebase on Windows, macOS, and Linux. Do not make a platform's system codec framework the sole implementation for a supported format. The decoder spike should choose the library/backend, verify seeking and sample-frame consistency on all three targets, and establish the exact initial format matrix. MP3 and Ogg Vorbis are required for streamed music; WAV, Ogg Vorbis, and FLAC are required for BMS keysounds and fallback resolution. Full-song FLAC and Opus may be exposed when the chosen backend supports their seek behavior consistently.
