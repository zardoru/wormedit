# Plan decisions: open questions

[Back to technical decisions](../decisions.md).

1. What should note labels mean in practice: private author comments, visible note text, keysound names, gameplay annotations, or several of these?
   - **Provisional default (unconfirmed):** Treat labels as private author comments stored only in the native project; never emit them as visible note text.
   - **Answered (2026-09-30):** Private author comments stored only in the native project (T-116).
   - **Blocks:** any label export or gameplay-annotation work. Do not start it until the user answers.
2. Must labels travel with standalone chart files, or is native-project persistence sufficient for formats without a native representation?
   - **Provisional default (unconfirmed):** Native-project persistence only; omit labels from standalone files with a non-fatal loss diagnostic.
   - **Answered (2026-09-30):** Native project only; exporters omit labels with a non-fatal loss note (T-116).
   - **Blocks:** label export writers. Do not start it until the user answers.
3. Which BMS dialect/player is the primary export compatibility target (for example, conservative legacy BMS versus modern extensions used by beatoraja)?
   - **Provisional default (unconfirmed):** Conservative legacy BMS output; emit modern extensions only when the target profile names them.
   - **Answered (2026-09-30):** beatoraja first; LR2/legacy compatibility is a separate named profile (T-282).
   - **Blocks:** new BMS export profiles beyond the current ones. Do not start it until the user answers.
4. How much non-gameplay source data must round-trip initially: BGA/storyboard/video, preview metadata, custom headers/tags, and comments?
   - **Provisional default (unconfirmed):** Round-trip everything the importer already preserves losslessly; do not add new BGA/storyboard/preview writers.
   - **Answered (2026-09-30):** Format-dependent: robust round-trip for BMS and osu!mania (T-288, T-289); SM/SSC keeps what the importer already preserves, no new writers.
   - **Blocks:** new non-gameplay writers. Do not start it until the user answers.
5. When a BPM edit affects time-anchored osu!mania notes, which behavior should be the default: keep their audio times fixed or move them with the beat grid?
   - **Provisional default (unconfirmed):** Keep audio times fixed (move notes with the audio, not the grid); expose as a policy value, not hard-coded.
   - **Answered (2026-09-30):** Notes move with the beat grid by default; keeping audio time is an explicit per-edit option (T-280).
   - **Blocks:** osu!mania BPM-edit commands. Do not start it until the user answers.
6. May column count change only at measure boundaries, or must mid-measure layout changes be editable too?
   - **Provisional default (unconfirmed):** Column count may change only at measure boundaries.
   - **Answered (2026-09-30):** Column count changes only at measure boundaries for now; mid-measure layout changes are a late ambition (niche).
   - **Blocks:** mid-measure layout-change editing. Do not start it until the user answers.
7. When locally dragging an unlocked beat, should the default preserve both neighboring beat anchors by changing BPM on both sides, or preserve only the previous anchor and allow the following grid to move?
   - **Provisional default (unconfirmed):** Preserve only the previous anchor; the following grid may move.
   - **Answered (2026-09-30):** Preserve only the previous anchor; the following grid moves (T-283).
   - **Blocks:** local beat-drag solver UI. Do not start it until the user answers.
8. Which exact 5K and 6K StepMania-style layouts and arrow orientations should ship as built-in profiles?
   - **Provisional default (unconfirmed):** Ship no 5K/6K StepMania-style built-ins until the user names the layouts; keep custom maps.
   - **Answered (2026-09-30):** Ship Pump 5K (pump-single) and Solo 6K (dance-solo) (T-284).
   - **Blocks:** 5K/6K arrow profiles. Do not start it until the user answers.
9. Should the later storyboard editor initially target standalone shared `.osb` files, difficulty-specific `[Events]`, or both in its first release?
   - **Provisional default (unconfirmed):** Standalone shared `.osb` first.
   - **Blocks:** storyboard authoring UI (stage 7). Do not start it until the user answers.
10. Should notes eventually allow several semantic tags/layers at once, and if so, how should multiple assigned colors be drawn?
   - **Provisional default (unconfirmed):** One layer per note.
   - **Answered (2026-09-30):** One semantic layer per note.
   - **Blocks:** multi-layer assignment or blended color drawing. Do not start it until the user answers.
11. Which exact BMS double-play profiles should be named 10K and 14K in the UI, and should scratch columns sit outside or inside each player stage by default?
   - **Provisional default (unconfirmed):** Name them 10K and 14K; scratch columns outside each player stage.
   - **Answered (2026-09-30):** Beatmania-style DP5/DP7 with scratches outside, plus a separate scratchless osu!mania-style 10K (T-285).
   - **Blocks:** renaming or regrouping double-play profiles. Do not start it until the user answers.
12. Should the native project ship first as a human-readable folder/manifest, a single-file archive, or both?
   - **Provisional default (unconfirmed):** Single-file archive is not started; keep the current whole-document JSON and folder work.
   - **Answered (2026-09-30):** Single JSON project file with assets in the song folder beside it; no archive or manifest (T-123).
   - **Confirmed for 1.0 (owner, 2026-10-26):** folder form is the release format because charters work with the assets directly; an archive could only ever be an export bundle. A build refuses a project whose schema is newer than it supports.
   - **Blocks:** native-project packaging. Do not start it until the user answers.
13. Which MIDI features beyond notes, tempo, meter, markers, and controller data must the first Lua API expose (for example pitch bend, aftertouch, lyrics, or SysEx)?
   - **Provisional default (unconfirmed):** Notes, tempo, meter, markers, and controller data only.
   - **Blocks:** extra MIDI fields in the Lua API. Do not start it until the user answers.
14. What is the complete note-snap color table for straight and tuplet divisions beyond the initial red/blue/yellow classes?
   - **Decision (user, 2026-09-30):** 4th red, 8th blue, 12th purple, 16th yellow, 24th pink, 32nd orange; other divisions grey. Use one palette table so it can be changed consistently.
   - **Blocks:** none; T-204 may implement this confirmed palette.
   - **Answered (2026-09-30, extension):** StepMania/ITG palette adds 48th cyan and 64th green; everything else stays as above (T-286).
15. Which BMS encodings and aliases belong in the automatic detector's initial candidate set, and what confidence threshold should force manual confirmation?
   - **Provisional default (unconfirmed):** Candidates UTF-8, CP932/Shift-JIS, EUC-KR/CP949, Windows-1252; below the current detector confidence, require manual confirmation.
   - **Answered (2026-09-30):** BMS export prefers UTF-8 because beatoraja is the target (T-282); the LR2 profile writes CP932. Import detection keeps the current candidates.
   - **Blocks:** detector candidate-set changes. Do not start it until the user answers.
16. Which initial video containers/codecs, resolutions, frame rates, alpha/background policies, and audio-mix options are required?
   - **Provisional default (unconfirmed):** None chosen; do not start encoder work.
   - **Blocks:** video export. Do not start it until the user answers.
17. What should the converter executable be named, and should it provide an `rdconverter` compatibility alias or only a compatible workflow?
   - **Provisional default (unconfirmed):** Do not create the converter target yet.
   - **Answered (2026-09-30):** Separate executable `wormedit-convert` with its own JSON schema; no rdconverter alias (T-281).
   - **Blocks:** converter executable naming. Do not start it until the user answers.
18. Which additional StepMania implementations merit named export profiles, and can pinned fixtures safely expand the current StepMania 5.1/Etterna 0.75.1 native-SSC subsets to any legacy negative-BPM or negative-stop encoding?
   - **Provisional default (unconfirmed):** Keep the pinned StepMania 5.1/Etterna 0.75.1 native-SSC subsets only.
   - **Answered (2026-09-30):** Keep StepMania 5.1 and Etterna 0.75.1; a modern AMX profile is a much later addition. osu!mania: pin the latest lazer format (T-291).
   - **Blocks:** new StepMania export profiles. Do not start it until the user answers.
19. When bar lines are edited as objects in a measure lane, what happens to everything else, and what does "displace" mean for them?
   - **Asked (user, 2026-10-17):** a measure lane that treats measure lines as natural notes, "with full copy/paste displace etc. support" (T-654–T-657).
   - **Provisional default (unconfirmed):** Other objects keep their song time (rows), as the Alt-drag bar-line gesture does; a placed or pasted bar line splits the measure it lands in; a deleted one merges its two measures; a bar line moving onto another merges with it. "Displace" means bar lines move with the rest of a selection under the same move and collision policy as notes; nothing ripples later measures.
   - **Answered (2026-10-17):** The provisional default stands: bar-line edits are generally presentational, so other objects keep their song time. Time propagation (later content riding with a moved bar line) is an explicit toggle, off by default and used only when requested (T-654–T-657).
20. Does the C ABI between sys and the Nuklear frontend still earn its cost for Compose?
   - **Asked (Claude, 2026-10-17, from the [measure-lane retrospective](../../../memory/coordination/oct17-measure-lane-retrospective.md)):** every Compose view datum travels core type → sys projection (`src/sys/compose/views/compose_view.cpp`) → frame storage (`src/sys/project/workspace_view.h`) → C struct (`WormUiComposeView`, about 430 fields in one flat struct) → Nuklear. T-657 needed edits at each step. If the only consumer is the in-process Nuklear frontend, this boundary is the costliest layer in every UI feature; if it exists for WASM or other frontends, it should stay and only get cheaper.
   - **Provisional default (unconfirmed):** keep the C ABI. A cheaper, separate improvement: group `WormUiComposeView` into per-feature sub-structs so a feature's fields sit together. That changes ABI layout but no lifetimes (D6 restricts lifetimes, not grouping).
   - **Blocks:** any proposal to replace or bypass the C ABI, and the sub-struct grouping (an ABI layout change). T-653 proceeds either way.
   - **Owner input (2026-10-17):** "I would *like* to support a potential migration to a new front-end if it were to be necessary. What I'm less sure about is whether the C ABI needs to be there to achieve this." Frontend portability is a goal; the C shape of the boundary is not settled as the means. Still open: whether the boundary may become plain C++ types (spans, string views, enum classes) while keeping the same view-and-action seam and `cmake/check_ui_boundary.cmake`.
   - **Answered (2026-10-17):** Relax the requirement. Frontend portability stays a goal; the C ABI is no longer required for it. Boundary types may be plain C++, migrated slowly as surfaces are touched, with no wholesale conversion. Rule: [code-style.md, "Frontend boundary"](code-style.md). The provisional `WormUiComposeView` sub-struct grouping is superseded: a touched surface converts to C++ types instead.

21. When lowering `#RANDOM n`, what happens to existing IF values or a selected preview value above the new range?
   - **Agreed (2026-10-20):** the range is independent of arm count; editing it does not implicitly delete or renumber children.
   - **Answered (2026-10-23):** retain now-unreachable IF/ELSEIF arms with a warning; clear an out-of-range explicit preview choice without selecting a replacement. Preserve all child IDs, values and ordering. The owner accepted these proposed rules after clarifying the target-node vocabulary.
   - **No provisional choice:** do not silently clamp, delete or renumber anything.
   - **Unblocks:** [T-739](../tasks/bms-scoped-authoring.md#t-739-edit-random-outcome-ranges-without-implicit-arm-edits). See the [approved scoped-program contract](../editing/bms-scoped-program.md).
22. Which non-body BMS directives appear as editable scope nodes rather than parameters or structural markers?
   - **Answered (2026-10-23):** `SETRANDOM` and `SETSWITCH` are separate selectable typed nodes with editable settings, retaining their authored program order and existing selector semantics. They do not introduce bodies or receive Chart children.
   - **Accepted:** `SKIP` is typed stop-fallthrough behavior on its case; preserve its execution position and existing profile semantics. Parser-only grouping and closing markers (`ENDIF`, `ENDRANDOM`, `ENDSW`, etc.) are structural syntax handled automatically, without separate selectable nodes.
   - **Scope rule clarified:** actual body scopes (Root, RANDOM, IF/ELSEIF/ELSE, SWITCH and CASE/DEF) each own at least one Chart child. Settings-only fixed-selector nodes are distinct from scopes; they must not create extra executable bodies or hide meaningful controls as Text. Scope-target versus preview interaction remains question 24.
   - **Owner input:** “setrandom/switch should be a separate node w/settings, everything else lgtm”.
   - **Unblocks:** complete source-node mapping in [T-733](../tasks/bms-scoped-model.md#t-733-decode-modeled-content-in-every-bms-scope) and the question-22 gate in [T-738](../tasks/bms-scoped-authoring.md#t-738-add-control-chart-and-text-nodes-in-selected-scopes). T-738 still requires T-737. See the [ownership map](../audits/bms-scoped-ownership.md).
23. What is the supported unwrap operation for a selector whose children depend on that selector?
   - **Agreed (2026-10-20):** Delete removes a confirmed subtree; Unwrap separately promotes ordered contents to the parent and does not silently discard content.
   - **Answered (2026-10-23):** if removing a selector would leave dependent branches without that selector or bind them to a different selector, ask whether to make those affected branches bare: explicit selectorless nodes whose bodies always apply in their containing scope. Confirm performs that conversion with ordered child promotion; Cancel does nothing. Retain all branch bodies, identities and order rather than selecting one outcome. Nested controls unrelated to the removed selector retain their conditions.
   - **Owner input:** “ask for confirmation on whether the user wants to just move the block to be ‘bare’ (i.e. the containing branch just becomes a new selectorless node so it always applies) or do nothing”.
   - **Safety contract:** never silently convert or rebind selectors, discard content or drop collisions. Preview the resulting reachability and definition effects; invalid combined content still rejects atomically after confirmation.
   - **Unblocks:** selector-unwrapping behavior in [T-742](../tasks/bms-scoped-authoring.md#t-742-unwrap-controls-as-an-explicit-structural-operation). Confirmation widgets remain T-743.
24. How does selecting an inactive edit scope interact with the evaluated field and playback preview?
   - **Agreed (2026-10-20):** selecting a control targets its first Chart child; another Chart can be selected explicitly; edits outside a leaf target the containing scope. RANDOM's range and its chosen preview outcome are different settings.
   - **Answered (2026-10-23):** selecting a node opens a separate scope-authoring view without changing playback preview choices. Ancestor/common content is read-only context; editing it requires deliberately selecting its owning node. Switching content targets clears object selection. Node/scope selection stays outside document Undo, retaining an existing valid target through Undo/Redo. Playback retains normal SWITCH fallthrough. Selecting a body scope resolves its first direct Chart for content edits; selecting a Chart explicitly chooses that Chart. Structural commands act on the selected node itself.
   - **No provisional choice:** never implicitly mutate another scope/Chart owner, edit hidden unrelated content, silently change authored control parameters, or redirect a missing target to Root.
   - **Unblocks:** final selection/preview integration in [T-743](../tasks/bms-scoped-authoring.md#t-743-present-the-branch-panel-as-a-scope-authoring-tree).

25. What is the shared authored multivoice sound model for native notes, edited osu! custom sets and BMSON layered sounds?
   - **Agreed (2026-10-20):** plan this extension separately, not as immediate implementation. Imported osu! custom sample-set references remain read-only for faithful playback; editing materializes only the affected notes into the ordinary Sounds model, never every note sharing that set. Include BMSON layered sound authoring in the later design.
   - **Existing behavior to preserve:** retained BMSON sound-channel layers already fuse for audition/playback; this is not a claim that BMSON layering is unsupported. Existing runtime preview voice bindings are derived audio data, not a persistent authoring model.
   - **Still open:** whether the canonical binding belongs to a note or reusable sound definition; stable voice identities/order, gain and slice ownership; how layers are added, selected, removed and assigned from Sounds; the loss policy for targets that cannot encode those bindings. Resolve how inherited osu! set/index/volume is frozen on affected notes without baking gain twice, and how BMSON continuation/restart and channel attribution retain one semantic owner.
   - **No provisional choice:** do not flatten voices into generated composite audio, use hidden retained BMSON objects as the generic model, mutate shared source sets, or silently omit voices. No migration starts merely because a note is selected or played.
   - **Blocks:** implementation in [T-746–T-748](../tasks/compose-ux.md#t-746-integrate-approved-authored-voices-with-sounds-and-audio); [T-745](../tasks/compose-ux.md#t-745-specify-one-authored-multivoice-sound-model) is the code-free ownership/design audit that prepares this decision.

26. Who owns the authored song preview, and how does it relate to formats and preview audio export?
   - **Answered (2026-10-24):** one song-wide preview owner. osu! writes the same `PreviewTime` into every difficulty; an import whose difficulties disagree keeps a per-chart override and reports a diagnostic. The point and optional duration are anchored to audio time (milliseconds in the source song), not to musical position, so BPM and offset edits leave them on the same audio. The span exists mainly for wormedit's own preview-audio export, which produces BMS previews (owner, 2026-10-24); it is the default range for Export preview audio (BMS `#PREVIEW`, BMSON `preview_music`); a selection still overrides it for one export.
   - **No provisional choice:** never fabricate an authored duration for point-only targets, and never write an audition default back into authored metadata.
   - **Unblocks:** [T-749](../tasks/resources-audio.md#t-749-author-preview-points-and-spans-with-format-aware-duration).

27. What are the scope and boundary rules of export-cut sections?
   - **Answered (2026-10-24):** cuts are song-wide: the audio is cut once and every chart is rebased identically. A cut is an audio-time interval (source-song milliseconds), created from a selection through the area menu next to Create section. Creating and resizing a cut respects the Compose snap toggle: with snap on, edges land on snapped positions before conversion to audio time, so authors get precise control (owner, 2026-10-24). On export, objects fully inside a cut are dropped with a diagnostic. Holds, keysounds/BGM and BGA that cross a cut boundary reject the export with targeted diagnostics until fixed. Timing state in effect at the cut end carries over. Overlapping and adjacent cuts stay separate for editing, and export removes their union. Ordinary Compose skips cuts in waveform, timeline, playback and navigation. Timing focus shows them and plays the original audio through them.
   - **No provisional choice:** never modify the source audio or the authored project when exporting, and never silently shorten holds or truncate sounds at a cut.
   - **Unblocks:** [T-756–T-759](../tasks/markers.md#t-756-persist-song-wide-export-cuts-and-create-them-from-a-selection), planned by [T-729](../tasks/markers.md#t-729-plan-selection-created-export-cut-sections).

## How to use this list

An executing model never resolves an open question by itself. If a task depends on one, implement only behavior that holds for the provisional default, keep it behind a policy value, and say in the report which question it relied on. The defaults are unconfirmed suggestions written on 2026-09-29; the user may overturn any of them.


## Other decisions recorded 2026-09-30

- Removing a nonempty measure deletes its contents and shifts later content (T-71).
- Tap timing Apply only sets the initial BPM segment when later BPM changes exist (T-239).
- Trackpad momentum: no synthetic momentum (T-48).
- BMS branches: per-chart sources; malformed control flow preserved, with opt-out repair proposals (T-94, T-287).
- preview.ogg uses pinned libogg/libvorbis through FetchContent (T-107).
- osu! hitsounds: bundled CC0 defaults overridable by a user skin folder (T-217).
- SV impulses compile to a 1 ms finite box by default (T-162).
- Standard MIDI export is deferred; it is a quality-of-life tool for advanced uses (T-115).
- The command journal replays edits (T-121).
- Unsaved changes prompt Save / Discard / Cancel (T-53).
- Lua SV recipes (T-156, T-161, T-162, T-189–T-191) are near-term, not deferred.
- T-245: no real-display measurement; render acceptance uses T-246 counters.
