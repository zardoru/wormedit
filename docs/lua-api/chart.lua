--- Measure-based generated difficulties.
-- Available in the generator host, including modules included by project.lua.
-- Run previews difficulty summaries; Apply creates/replaces owned difficulties in one
-- Undo step. The selected chart supplies context and remains unchanged.
-- @module chart

--- Begin a difficulty; subsequent settings, measures and timing belong to it.
-- Defaults: 960 rows per standard measure, four columns, zero offset, 120 BPM.
-- @string name Display name; also the default stable key.
-- @function difficulty

--- Set a unique stable difficulty key (1–128 bytes).
-- Keep this unchanged when renaming a difficulty to retain its native chart ID.
-- @string key Stable key.
-- @function stable_id

--- Set exact rows per standard four-quarter-note measure.
-- @int value Positive resolution; all lengths and event positions must fit exactly.
-- @function resolution

--- Set beat-zero audio offset in seconds.
-- @param value Signed number, ratio(n,d), or measure_time "-0.125".
-- @function offset

--- Set the playable column count, from 1 to 18. Default 4.
-- @int value Column count; note lanes are one-based column numbers.
-- @function columns

--- Use a supported StepMania step type, such as dance-single or pump-single.
-- Its lane count must agree with columns. Omit for an osu!mania-style key layout.
-- @string value Step type.
-- @function mode

--- Exact rational value; denominator must be positive.
-- @int num Numerator (signed for offset, scroll and time values).
-- @int den Denominator.
-- @function ratio

--- Parse an exact positive measure length, relative to a standard measure.
-- @string value Decimal or fraction, e.g. "0.75" or "3/4".
-- @function measure_length

--- Parse an exact signed time in seconds.
-- @string value Decimal or fraction.
-- @function measure_time

--- Unreduced meter signature; implies measure length num/den if length is omitted.
-- An explicit length must agree. The default measure is length 1, meter 4/4.
-- An explicit length without meter leaves the meter unspecified.
-- @int num Positive numerator.
-- @int den Positive denominator.
-- @function meter

--- Express a stop/delay length as musical rows, converted exactly to beats.
-- Other stop/delay lengths are seconds.
-- @int value Nonnegative row count.
-- @function rows

--- Refer to a local position. Forward references are allowed.
-- Row must be smaller than the measure capacity; an endpoint at capacity must
-- instead use row 0 of the next measure. IDs refer to this difficulty only.
-- @param id Measure string ID, or one-based measure ordinal.
-- @int row Zero-based integer row.
-- @function at_measure

--- Append a measure with optional id, length and meter, and a dense list of events.
-- String IDs must be unique. Omitted IDs use ordinal strings, which shift on insertion;
-- explicit IDs are recommended. Chart timing declarations cannot appear inside measures.
-- @param descriptor Table containing note/hold/bgm descriptors.
-- @function measure

--- Append a dense array of measure tables or consume an iterator until it returns nil.
-- Previously appended measure descriptors are not appended twice.
-- @param sequence Array or zero-argument iterator.
-- @function measures

--- A playable tap inside a measure.
-- @param descriptor `{key="hit", row=0, lane=1, sound=123}`; key and sound optional.
-- Sound is an existing resource ID, not an asset ID. Typed sample references are future work.
-- Keys are stable within a measure. Omitted event keys use their ordinal.
-- @function note

--- A hold inside a measure; end must be strictly after its head.
-- @param descriptor Like note, with required end_at=at_measure("next",0).
-- @function hold

--- A BGM event inside a measure, on the difficulty's generated BGM lane.
-- @param descriptor `{key="bg", row=0, sound=123}`; no playable lane or hold end.
-- @function bgm

--- Chart-level BPM declaration. An origin event of 120 is supplied when absent.
-- @param descriptor `{key="tempo", at=at_measure("intro",0), value=150}`; key optional.
-- @function bpm

--- Chart-level stop, measured in seconds unless length is rows(n).
-- @param descriptor `{at=at_measure("intro",120), length=measure_time "0.25"}`; key optional.
-- @function stop

--- Chart-level delay, with the same fields/units as stop.
-- @param descriptor Position and length.
-- @function delay

--- Chart-level warp with increasing endpoints inside the grid.
-- @param descriptor `{from=at_measure("intro",0), to=at_measure("intro",120)}`; key optional.
-- @function warp

--- Chart-level signed scroll multiplier.
-- @param descriptor `{at=at_measure("intro",0), value=ratio(-1,2)}`; key optional.
-- @function scroll
