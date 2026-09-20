# Status: open decisions

[Back to the chart editor status index](../chart-editor-status.md).

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
- Validate the selected miniaudio decoder and Lua runtime integrations on Windows and Linux, then decide the later WAV/FLAC decoder path.
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
- Decide which additional StepMania implementations merit named profiles and whether pinned fixtures justify expanding the current StepMania 5.1/Etterna 0.75.1 native-SSC subsets to legacy negative-BPM or negative-stop encodings.
