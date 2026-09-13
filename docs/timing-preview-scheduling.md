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

Sample conversion is exact until the caller-selected final rounding step (`floor`,
`ceil`, or nearest with ties to even). Negative audio times are valid timing values,
but cannot enter a transport queue starting at frame zero and are diagnosed when a
schedule is compiled.

`build_preview_schedule` creates separate contiguous arrays for playable sounds,
BGM, BGA, and meter-aware metronome pulses. Simultaneous playable events retain
source order, BGM retains source-lane then event order, and BGA retains draw-kind
then source order. Each family has an independent event cap. Queue cursors use a
binary search on seek and only advance a small head index during ordinary playback;
they never erase or allocate on the realtime path. A timing-source fingerprint
rejects accidental reuse after a timing or measure edit, while application-level
generation guards remain responsible for document/session publication.

Current bounds are deliberate: the index requires a positive BPM at row zero;
inverse audio-time-to-musical-position mapping, profile-selectable discontinuity
ordering, count-in before frame zero, scroll/speed evaluation, and transport voice
materialization remain subsequent timing-engine slices.
