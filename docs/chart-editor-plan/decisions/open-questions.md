# Plan decisions: open questions

[Back to technical decisions](../decisions.md).

1. What should note labels mean in practice: private author comments, visible note text, keysound names, gameplay annotations, or several of these?
2. Must labels travel with standalone chart files, or is native-project persistence sufficient for formats without a native representation?
3. Which BMS dialect/player is the primary export compatibility target (for example, conservative legacy BMS versus modern extensions used by beatoraja)?
4. How much non-gameplay source data must round-trip initially: BGA/storyboard/video, preview metadata, custom headers/tags, and comments?
5. When a BPM edit affects time-anchored osu!mania notes, which behavior should be the default: keep their audio times fixed or move them with the beat grid?
6. May column count change only at measure boundaries, or must mid-measure layout changes be editable too?
7. When locally dragging an unlocked beat, should the default preserve both neighboring beat anchors by changing BPM on both sides, or preserve only the previous anchor and allow the following grid to move?
8. Which exact 5K and 6K StepMania-style layouts and arrow orientations should ship as built-in profiles?
9. Should the later storyboard editor initially target standalone shared `.osb` files, difficulty-specific `[Events]`, or both in its first release?
10. Should notes eventually allow several semantic tags/layers at once, and if so, how should multiple assigned colors be drawn?
11. Which exact BMS double-play profiles should be named 10K and 14K in the UI, and should scratch columns sit outside or inside each player stage by default?
12. Should the native project ship first as a human-readable folder/manifest, a single-file archive, or both?
13. Which MIDI features beyond notes, tempo, meter, markers, and controller data must the first Lua API expose (for example pitch bend, aftertouch, lyrics, or SysEx)?
14. What is the complete note-snap color table for straight and tuplet divisions beyond the initial red/blue/yellow classes?
15. Which BMS encodings and aliases belong in the automatic detector's initial candidate set, and what confidence threshold should force manual confirmation?
16. Which initial video containers/codecs, resolutions, frame rates, alpha/background policies, and audio-mix options are required?
17. What should the converter executable be named, and should it provide an `rdconverter` compatibility alias or only a compatible workflow?
18. Which additional StepMania implementations merit named export profiles, and can pinned fixtures safely expand the current StepMania 5.1/Etterna 0.75.1 native-SSC subsets to any legacy negative-BPM or negative-stop encoding?
