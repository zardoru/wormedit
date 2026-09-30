# Plan decisions: open questions

[Back to technical decisions](../decisions.md).

1. What should note labels mean in practice: private author comments, visible note text, keysound names, gameplay annotations, or several of these?
   - **Provisional default (unconfirmed):** Treat labels as private author comments stored only in the native project; never emit them as visible note text.
   - **Blocks:** any label export or gameplay-annotation work. Do not start it until the user answers.
2. Must labels travel with standalone chart files, or is native-project persistence sufficient for formats without a native representation?
   - **Provisional default (unconfirmed):** Native-project persistence only; omit labels from standalone files with a non-fatal loss diagnostic.
   - **Blocks:** label export writers. Do not start it until the user answers.
3. Which BMS dialect/player is the primary export compatibility target (for example, conservative legacy BMS versus modern extensions used by beatoraja)?
   - **Provisional default (unconfirmed):** Conservative legacy BMS output; emit modern extensions only when the target profile names them.
   - **Blocks:** new BMS export profiles beyond the current ones. Do not start it until the user answers.
4. How much non-gameplay source data must round-trip initially: BGA/storyboard/video, preview metadata, custom headers/tags, and comments?
   - **Provisional default (unconfirmed):** Round-trip everything the importer already preserves losslessly; do not add new BGA/storyboard/preview writers.
   - **Blocks:** new non-gameplay writers. Do not start it until the user answers.
5. When a BPM edit affects time-anchored osu!mania notes, which behavior should be the default: keep their audio times fixed or move them with the beat grid?
   - **Provisional default (unconfirmed):** Keep audio times fixed (move notes with the audio, not the grid); expose as a policy value, not hard-coded.
   - **Blocks:** osu!mania BPM-edit commands. Do not start it until the user answers.
6. May column count change only at measure boundaries, or must mid-measure layout changes be editable too?
   - **Provisional default (unconfirmed):** Column count may change only at measure boundaries.
   - **Blocks:** mid-measure layout-change editing. Do not start it until the user answers.
7. When locally dragging an unlocked beat, should the default preserve both neighboring beat anchors by changing BPM on both sides, or preserve only the previous anchor and allow the following grid to move?
   - **Provisional default (unconfirmed):** Preserve only the previous anchor; the following grid may move.
   - **Blocks:** local beat-drag solver UI. Do not start it until the user answers.
8. Which exact 5K and 6K StepMania-style layouts and arrow orientations should ship as built-in profiles?
   - **Provisional default (unconfirmed):** Ship no 5K/6K StepMania-style built-ins until the user names the layouts; keep custom maps.
   - **Blocks:** 5K/6K arrow profiles. Do not start it until the user answers.
9. Should the later storyboard editor initially target standalone shared `.osb` files, difficulty-specific `[Events]`, or both in its first release?
   - **Provisional default (unconfirmed):** Standalone shared `.osb` first.
   - **Blocks:** storyboard authoring UI (stage 7). Do not start it until the user answers.
10. Should notes eventually allow several semantic tags/layers at once, and if so, how should multiple assigned colors be drawn?
   - **Provisional default (unconfirmed):** One layer per note.
   - **Blocks:** multi-layer assignment or blended color drawing. Do not start it until the user answers.
11. Which exact BMS double-play profiles should be named 10K and 14K in the UI, and should scratch columns sit outside or inside each player stage by default?
   - **Provisional default (unconfirmed):** Name them 10K and 14K; scratch columns outside each player stage.
   - **Blocks:** renaming or regrouping double-play profiles. Do not start it until the user answers.
12. Should the native project ship first as a human-readable folder/manifest, a single-file archive, or both?
   - **Provisional default (unconfirmed):** Single-file archive is not started; keep the current whole-document JSON and folder work.
   - **Blocks:** native-project packaging. Do not start it until the user answers.
13. Which MIDI features beyond notes, tempo, meter, markers, and controller data must the first Lua API expose (for example pitch bend, aftertouch, lyrics, or SysEx)?
   - **Provisional default (unconfirmed):** Notes, tempo, meter, markers, and controller data only.
   - **Blocks:** extra MIDI fields in the Lua API. Do not start it until the user answers.
14. What is the complete note-snap color table for straight and tuplet divisions beyond the initial red/blue/yellow classes?
   - **Decision (user, 2026-09-30):** 4th red, 8th blue, 12th purple, 16th yellow, 24th pink, 32nd orange; other divisions grey. Use one palette table so it can be changed consistently.
   - **Blocks:** none; T-204 may implement this confirmed palette.
15. Which BMS encodings and aliases belong in the automatic detector's initial candidate set, and what confidence threshold should force manual confirmation?
   - **Provisional default (unconfirmed):** Candidates UTF-8, CP932/Shift-JIS, EUC-KR/CP949, Windows-1252; below the current detector confidence, require manual confirmation.
   - **Blocks:** detector candidate-set changes. Do not start it until the user answers.
16. Which initial video containers/codecs, resolutions, frame rates, alpha/background policies, and audio-mix options are required?
   - **Provisional default (unconfirmed):** None chosen; do not start encoder work.
   - **Blocks:** video export. Do not start it until the user answers.
17. What should the converter executable be named, and should it provide an `rdconverter` compatibility alias or only a compatible workflow?
   - **Provisional default (unconfirmed):** Do not create the converter target yet.
   - **Blocks:** converter executable naming. Do not start it until the user answers.
18. Which additional StepMania implementations merit named export profiles, and can pinned fixtures safely expand the current StepMania 5.1/Etterna 0.75.1 native-SSC subsets to any legacy negative-BPM or negative-stop encoding?
   - **Provisional default (unconfirmed):** Keep the pinned StepMania 5.1/Etterna 0.75.1 native-SSC subsets only.
   - **Blocks:** new StepMania export profiles. Do not start it until the user answers.

## How to use this list

An executing model never resolves an open question by itself. If a task depends on one, implement only behavior that holds for the provisional default, keep it behind a policy value, and say in the report which question it relied on. The defaults are unconfirmed suggestions written on 2026-09-29; the user may overturn any of them.
