# BmsONE slicing audit and BMSON mapping

Reviewed on 2026-09-13 against:

- [BmsONE repository](https://github.com/excln/BmsONE) revision `bb3406eeb8f45d79350241f09bb897dd229ce3af`, especially `docs/readme_en.txt`, `src/sequence_view/SequenceViewChannelInternal.cpp`, `src/sequence_view/SequenceViewWriteMode.cpp`, and `src/document/SoundChannelPreview.cpp`;
- [BMSON 1.0 specification](https://github.com/bemusic/bmson-spec/blob/master/doc/index.rst), especially Beat Resolution, Time Signatures, Sound Channels, and Slicing and Restarting.

This is behavioral reference material. Wormedit does not link BmsONE and does not copy its Qt architecture.

## Observed BmsONE interaction

BmsONE selects a source sound channel and edits its events directly in the playing or BGM lanes. In Write Mode:

- an ordinary left click creates a slicing note with the continuation flag enabled;
- Shift + left click creates a note that restarts the source from its beginning;
- when grid snapping is enabled, the click position is lowered to the active fine grid;
- selecting a note previews its derived slice;
- the sound-channel BGM lane draws the waveform that is active at each chart position;
- Alt + right click or middle click previews the current source channel from an arbitrary chart position.

The implementation stores one sound note per source channel and location. Its preview cache maps chart ticks through BPM changes to positions in the source stream, resetting source position at restart events and continuing it at slicing events. The next event bounds an individual note preview.

## Wormedit razor workflow

Wormedit uses the same representable BMSON primitive but exposes a multi-marker transaction:

1. Select or create a sound channel backed by one long audio asset.
2. Enter razor mode in the waveform-aligned sound-channel lane.
3. Click several chart positions. Each pointer position is converted to an exact musical row and snaps to the active divisor by default.
4. Each marker is explicitly `continue` or `restart`; the default is `continue`, while Shift-click creates `restart` for BmsONE familiarity.
5. Drag, delete, reorder through position changes, change lane/BGM assignment, and audition the region beginning at any marker while the transaction remains a preview.
6. Commit all valid changes as one command, or cancel without changing the document.

The authoritative authored values are sound-channel events, not arbitrary clip handles. Sample-frame boundaries shown in the waveform are derived by the timing and BMSON slicing evaluator. A free sample-frame cut is project-only until it is either fitted to a representable chart event or rendered into a new audio asset through an explicit conversion.

## Resolution and measure mapping

BMSON `info.resolution` is pulses per quarter note. Wormedit resolution `D` is divisions per standard 4/4 measure. An absolute wormedit position `r / D` of a standard measure maps to:

```text
y = 4 * R * r / D
```

where `R` is the emitted positive BMSON resolution. Import normally chooses `D = 4R`, so source pulse `y` maps to editor row `r = y`. For authored projects, the writer chooses the preferred or smallest practical `R` that makes every emitted position and duration integral. Thus `D = 960` naturally emits `R = 240`, and `D = 192` naturally emits `R = 48`.

BMSON has no measures or time signatures. Wormedit emits the exact prefix positions of its ordered measures as `lines[].y`. A missing `lines` field means generated 4/4 lines under the BMSON specification; a present empty array means no bar lines and must remain distinct.

## Sound-channel compilation

After sample-reference allocation, the writer groups events by BMSON sound channel/audio source, converts exact positions to pulse `y`, maps playable semantic lanes to BMSON `x`, maps autoplay/BGM to `x = 0`, converts hold duration to `l`, and emits the restart state as `c`.

BMSON derives slices from all distinct pulses in a sound channel. A note with `c = false` restarts the source at that pulse; `c = true` continues from the preceding source position. If events at the same pulse disagree, restart wins. Each derived slice has polyphony one, while different slices and different sound channels can play simultaneously. Fused notes from several sound channels remain separate channel events that the editor presents as one layered playable object.

If the same derived slice is assigned to both a BGM note and one or more playable notes at one pulse, the BMSON specification requires discarding the BGM note. Fused notes from different sound channels at the same lane and pulse must have equal long-note lengths; unequal lengths are a format error/warning that wormedit diagnoses before export. At a shared pulse, Note/BGA activation precedes BPM processing, then stop processing; preview and slice-time conversion use that defined discontinuity order.

No fade, gain, pan, free-form clip boundary, or measure/meter field is emitted for BMSON 1.0.

## Required regression cases

Positive cases cover the specification's published `vox.wav` example and exact expected source ranges, 960-to-240 and 192-to-48 resolution mapping, fractional measure bar lines, several snapped razor markers in one transaction, mixed restart/continue events, BGM `x = 0`, playable lanes, holds, duplicate pulses, restart-wins behavior, playable-over-BGM behavior, fused sound channels, polyphony, timing-event order, audition, cancel, commit, and undo/redo.

Negative cases cover zero resolution, non-integral measure capacity, a requested BMSON resolution that cannot represent all positions, out-of-range or boundary-spelled local rows, duplicate event conflicts within one source channel, zero/reversed holds, unsafe or missing audio, decode failure, ambiguous resource allocation, invalid marker drag, and a free sample-frame cut submitted directly to BMSON. Rejection leaves the materialized chart and output file unchanged.
