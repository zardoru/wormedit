--- Read-only document snapshot supplied as the global `wormedit` to generators.
-- Exact rational and ID conventions are explained in the scripting overview.
-- Numeric enum fields retain the core vocabulary values; they are not bit masks.
-- @module snapshot

--- Root snapshot (global `wormedit`).
-- @field api_version Integer 1.
-- @field project_id Project ID string.
-- @field chart_id Target chart ID string.
-- @field revision Unsigned document revision.
-- @field divisions_per_standard_measure Unsigned grid resolution for one standard measure.
-- @field selection Array of @{Selection}.
-- @field measures Array of @{Measure} in chart order.
-- @field timing @{Timing}.
-- @field lane_profile Array of @{Layout} segments.
-- @field notes Array of @{Note} in measure order.
-- @field layers Array of @{Layer}.
-- @field midi_assets Array of @{MidiAsset}; only the generator's declared inputs.
-- @table wormedit

--- Exact rational number.
-- @field num Signed or unsigned numerator; large unsigned values may be strings.
-- @field den Positive denominator (unsigned integer or decimal string).
-- @table Rational

--- Local chart position.
-- @field measure Measure ID string, not its ordinal index.
-- @field row Zero-based exact local row.
-- @table Position

--- Measure snapshot.
-- @field id Measure ID string.
-- @field length @{Rational}, measured in standard measures.
-- @field start_row Absolute starting row.
-- @field row_count Local row capacity.
-- @field meter Optional table `{num, den}` with meter numerator and denominator.
-- @table Measure

--- Selection member.
-- @field id Object ID string.
-- @field kind Numeric selection kind: 0 note, 1 bgm_event, 2 bpm_event, 3 stop_event,
-- 4 delay_event, 5 warp_event, 6 scroll_event, 7 speed_event, 8 kiai_event,
-- 9 timing_anchor, 10 bga_event, 11 bookmark, 12 section, 13 bar_line.
-- @table Selection

--- Lane layout segment.
-- @field id Segment ID string.
-- @field position @{Position} where this layout begins.
-- @field profile_id Profile identifier string.
-- @field display_name Layout name.
-- @field split_stage Boolean.
-- @field lanes Array of @{Lane}.
-- @table Layout

--- Lane definition.
-- @field id Lane ID string.
-- @field name Display name.
-- @field role 0 playable, 1 scratch, 2 autoplay, 3 invisible, 4 mine, 5 free_zone, 6 foot_pedal.
-- @field stage 0 none, 1 left, 2 right.
-- @field display_order Unsigned display ordering value.
-- @field glyph 0 bar, 1 square, 2 arrow_left, 3 arrow_down, 4 arrow_up,
-- 5 arrow_right, 6 arrow_up_left, 7 arrow_up_right, 8 arrow_down_left, 9 arrow_down_right, 10 arrow_center.
-- @field key_color 0 generic, 1 white, 2 black, 3 scratch, 4 foot_pedal, 5 free_zone,
-- 6 pms_white, 7 pms_yellow, 8 pms_green, 9 pms_blue, 10 pms_red.
-- @field midi_destination Optional `{channel, note}` integers, channel 0–15 and note 0–127.
-- @table Lane

--- Existing note (more kinds are readable than generators can emit).
-- @field id Note ID string.
-- @field position Start @{Position}.
-- @field lane Lane ID string.
-- @field kind Human-readable core note-kind label; generators emit only `tap` or `hold`.
-- @field layers Array of semantic-layer ID strings.
-- @field end Optional end @{Position}; access as `note["end"]` because `end` is a Lua keyword.
-- @field sound Optional resource ID string.
-- @field label Optional text label.
-- @table Note

--- Semantic layer.
-- @field id Layer ID string.
-- @field name Display name.
-- @field visible Boolean.
-- @field locked Boolean.
-- @table Layer

--- Timing snapshot. Event arrays preserve their document order.
-- Each event except warps has `id` (string) and `position` (@{Position}).
-- @field bpms Events with `value` (@{Rational}, beats per minute).
-- @field scrolls Events with `value` (@{Rational}, multiplier).
-- @field speeds Events with `value`, `transition` (@{Rational}), and `transition_is_seconds` (boolean).
-- @field pauses Events with `duration` (@{Rational}), `kind` (`stop` or `delay`) and numeric `unit` (0 seconds, 1 milliseconds, 2 bms_stop_ticks, 3 beats).
-- @field warps Events with `id` and `begin` / `end` positions; use bracket access for `end`.
-- @field kiai Events with `enabled` (boolean) and unsigned `effect_bits`.
-- @field anchors Events with unsigned `source_sample_frame` and boolean `locked`.
-- @field beat_zero_audio_offset_seconds @{Rational} audio offset.
-- @table Timing

--- Declared MIDI input. No file access is required to inspect these owned values.
-- @field id Asset ID string.
-- @field ticks_per_quarter Unsigned MIDI tick resolution.
-- @field events Array of @{MidiEvent}.
-- @field discarded_event_count Number of unmodeled or malformed events omitted from the array.
-- @table MidiAsset

--- Modeled MIDI event.
-- @field tick Unsigned absolute MIDI tick.
-- @field channel MIDI channel (0–15).
-- @field kind `note_on`, `note_off`, `controller`, `track_name`, `marker`, `tempo`, or `time_signature`.
-- @field data Dense byte array (0–255), including status/meta bytes, retained from the event.
-- @field pitch Present for note_on/note_off (0–127).
-- @field velocity Present for note_on/note_off (0–127); velocity-zero note_on is retained as such.
-- @field controller Present for controller events (0–127).
-- @field value Present for controller events (0–127).
-- @table MidiEvent
