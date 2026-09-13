# Exact timing and preview scheduling

`build_timing_index` is the headless timing boundary shared by waveform alignment,
preview compilation, and later format conversion. Musical positions remain exact
measure-local rows. A standard measure contains four quarter-note beats, so a BPM
segment of `r` rows has the exact duration
`r * 240 * bpm_denominator / (chart_resolution * bpm_numerator)` seconds. The
chart's signed beat-zero audio offset is the exact time at row zero.

Every timing boundary exposes three values. `before_delays` is arrival at the row,
`event` is after delays and is where notes, BGM, BGA, and metronome pulses occur,
and `after_stops` is departure after stops. Same-row BPM entries are applied in
stable source order before converting beat- or BMS-tick-based pauses; the last BPM
at that row controls the following segment. One BMS STOP tick is treated as 1/192
of a whole note, or 1/48 quarter beat. Seconds, milliseconds, beats, and BMS ticks
remain distinguishable inputs.

Canonical warp spans are half-open `[begin, end)`. They consume no elapsed time,
events inside them are absent from preview queues, and BPM changes within them still
determine the effective BPM after the warp. Stops and delays inside a warp are
suppressed with a diagnostic. This is the initial canonical rule; format adapters
must still apply and test any profile-specific boundary ordering before claiming a
semantic round trip.

`musical_position_at` performs the inverse lookup against the same exact index and
returns an exact absolute row, including fractional rows between timing boundaries.
Audio times within a delay or stop resolve to that pause's row. Since a half-open
warp span collapses to one audio time, inverse lookup makes this ambiguity explicit:
`timing_inverse_options_t::warp_side` selects the warp entry or exit, with entry as
the default. This endpoint choice is the seam where a format adapter can apply a
profile's inverse convention.

Sample conversion is exact until the caller-selected final rounding step (`floor`,
`ceil`, or nearest with ties to even). Negative audio times are valid timing values,
but cannot enter a transport queue starting at frame zero and are diagnosed when a
schedule is compiled. Inverse lookup accepts negative times when the chart's signed
beat-zero offset places them inside the chart's timing range; queries beyond either
end return a typed `position_out_of_range` diagnostic.

`build_preview_schedule` creates separate contiguous arrays for playable sounds,
BGM, BGA, and meter-aware metronome pulses. Simultaneous playable events retain
source order, BGM retains source-lane then event order, and BGA retains draw-kind
then source order. Each family has an independent event cap. Queue cursors use a
binary search on seek and only advance a small head index during ordinary playback;
they never erase or allocate on the realtime path. A timing-source fingerprint
rejects accidental reuse after a timing or measure edit, while application-level
generation guards remain responsible for document/session publication.

`preview_transport_bridge_t` consumes the playable, BGM, and metronome heads from a
control thread. It merges them by frame, resolves bounded resource/accent bindings,
and fills pre-reserved transport voice slots only through the configured capacity.
When capacity is full, the first unscheduled event remains at its queue head for a
later refill. Seek and restart binary-search fresh heads under a new session
generation; Stop invalidates that generation and clears every active slot. Expired
slots are only marked inactive in the mix callback and are reused by control-thread
scheduling without callback-side vector growth or erasure. PCM blocks are mixed
through a synchronous non-copying cache view, so lookup does not allocate or retain
an eviction-unsafe pointer; cache access is still mutex-serialized. BGA remains a
renderer queue rather than an audio voice.

`bga_preview_queue_t` is the renderer-neutral consumer for that visual queue. It
validates one immutable schedule at session start, builds bounded per-kind history
indexes for background, poor, and layer state, and restores the effective state by
binary search on seek. Forward playback returns the stable source/draw-ordered
event span while changing only its queue head and three current-layer values; it
does not allocate or erase. Seek, restart, and Stop rotate generation tokens, so a
late renderer update cannot move a newer session. Video and storyboard entries
remain ordered events for their later specialized renderers rather than persistent
BMS layer state.

Current bounds are deliberate: the index requires a positive BPM at row zero.
Count-in before frame zero, scroll/speed evaluation, richer profile-specific
boundary rules, concrete image/video/storyboard rendering, and SDL device
integration remain subsequent timing-engine slices. The timing source fingerprint
continues to reject stale preview schedules after timing or measure edits; callers
still use document generation guards when publishing asynchronous work.
