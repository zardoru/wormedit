# Plan slice: metadata and interoperability workflows

[Back to the editing-plan index](../editing.md).

## Tags, bookmarks, and song sections

Store UTF-8 search tags as ordered metadata with normalized lookup plus retained source spelling/order for loss-aware export. The osu! adapter reads and writes the space-separated `[Metadata]` `Tags` field defined by the official [`.osu` file-format documentation](https://osu.ppy.sh/wiki/en/Client/File_formats/osu_%28file_format%29). Tags may be edited per difficulty or applied across selected difficulties in the project; semantic note layers and note labels remain separate concepts.

Provide a marker/section lane and a searchable section list. A marker may be an unlabeled point bookmark or a labeled point/range such as `Intro`, `Verse 1`, `Chorus`, `Bridge`, or `Outro`. Commands add a marker at the cursor, set a range from the current selection, jump previous/next, loop a section, rename/recolor, and select the section's chart events. Markers may be beat-anchored or audio-time-anchored, and timing edits show which will move.

Deferred user request (2026-10-19): create a section from the current selection
and allow that section to designate a portion of the song to **cut out on
export**. The native project retains the full source song and section. Display
cut-out spans only in **Timing focus mode**, with the same visual language as
warp dead-spans and a dedicated, distinct color. In ordinary **Compose mode**,
skip these intervals entirely: the waveform and displayed timeline close up
across each cut as though that portion never existed, and playback/navigation
skip from the preceding retained portion to the following one. Timing focus
exposes the original intervals so their boundaries remain editable; switching
modes preserves the source song and cut definitions. Export/audio splicing,
timeline rebasing, scope across charts/output groups, boundary handling and
Timing-focus audition policy need a later design pass; this is not immediate
implementation work. See [T-719](../tasks/markers.md#t-719-plan-selection-created-export-cut-sections).

osu! `[Editor]` bookmarks are integer millisecond points and carry no label or range. Import them as audio-time point markers. Export every compatible point marker, plus the start of a labeled range when requested; retain labels, colors, range ends, and beat anchors in the native project and diagnose that loss when producing standalone `.osu`. MIDI export may emit labeled markers as marker meta-events.

### BMS channel maps and MIDI destinations

Make a BMS channel map editable per lane-layout segment or reusable as a named profile. Each entry has a semantic lane ID, display order, stage group, role (`playable`, `scratch`, `invisible`, `mine`, `autoplay`, or retained/unknown), raw two-character BMS object channel, and optional MIDI port/channel/note/velocity defaults. Keep timing/resource channels reserved and validate any attempted collision with them. Preserve unknown source channels as raw retained lanes until the user maps or explicitly discards them.

Custom maps allow nonstandard BMS layouts to import without squeezing objects into built-in columns. Export validates duplicate raw channels, dialect-specific channel syntax, scratch/player-side expectations, long-note pairing, and conflicts with repeated BGM allocation. A map change proposes a lane migration and loss report; it never changes existing semantic lane IDs merely because a display or output code changed.

MIDI export uses the same semantic-lane destinations. Holds become note-on/note-off pairs; taps use a configurable short duration; keysound/layer labels and song sections may become text or marker meta-events. Provide two explicit timing policies: **musical** export preserves chart beats, BPM, and meter where Standard MIDI can express them, while **performance** export preserves audible note times against a chosen MIDI tempo/grid. Stops, delays, warps, scroll velocity, kiai, and format-specific keysound semantics require diagnostics because MIDI has no direct equivalents. Choose a PPQ that represents all exported chart positions exactly or reject/preview quantization rather than rounding silently.

### BMS branch tree and active projection

Preserve BMS random and conditional source as an ordered control-flow tree rather than evaluating and discarding it during import. Initial nodes cover `#RANDOM`/`#SETRANDOM`/`#ENDRANDOM`, `#IF`/`#ELSEIF`/`#ELSE`/`#ENDIF`, and `#SWITCH`/`#SETSWITCH`/`#CASE`/`#DEF`/`#SKIP`/`#ENDSW`. Retain original spelling, raw source spans, line endings, nesting, and branch-local commands. Common commands outside a branch remain root content.

The chart window includes a visible outline/tree component. Random and switch nodes show their range or fixed value; conditional and case nodes show labels, probability/effective reachability, contained object counts, measure range, and diagnostics. For every active random/switch node, the user chooses a value manually, steps through values, or asks for a deterministic seeded roll. Independent top-level blocks and active nested blocks produce a branch-selection environment containing several selected leaves.

The note, timing, BGM, waveform, metadata/resource preview, and playback views are a deterministic projection of root/common content plus the leaves activated by that environment. Notes from inactive leaves are absent by default, as requested; an optional non-editable ghost overlay may be added later. Changing a tree choice is view state and does not rewrite the chart. It clears or parks selections that are no longer visible so keyboard and batch commands cannot mutate hidden objects accidentally.

Direct branch-body and structural editing is desirable but is not on the critical path for correct BMS writing. When added, new events and metadata edits need an explicit insertion scope shown beside the tree: the current selected leaf, a chosen ancestor/common scope, or all sibling leaves through a deliberate replicated command. Moving an object between branches and adding/removing/reordering branches are atomic undoable operations. Before commit, validate branch-local `#WAV`/BPM/STOP/SCROLL definitions, lane/resource collisions, and timing effects against every affected projection.

Evaluation must be profile-driven and sequence-aware. `#RANDOM n` chooses an integer in `1..n`; `#SETRANDOM` fixes the value. `#IF` groups, `#ELSEIF`, and `#ELSE` select according to that surrounding value. `#SWITCH`/`#SETSWITCH` begins at the matching `#CASE` or `#DEF` and preserves target-profile fallthrough until `#SKIP` or `#ENDSW`. Nested random and switch blocks remain nested nodes. The editor should enumerate reachable outcomes for tests and warnings within a configured cap, while seeded sampling handles combinatorial trees interactively.

Export offers two distinct operations: preserve the authored control tree, or flatten the currently selected projection into a non-random BMS file. Full-tree preservation is the default and required writer path, independent of direct branch-editing support: emit every active and inactive branch with its corresponding `#RANDOM`/`#SETRANDOM`, `#IF`/`#ELSEIF`/`#ELSE`, `#SWITCH`/`#CASE`/`#DEF`/`#SKIP`, and closing statements in stable source order. A BMS writer must not serialize only the currently materialized branch or only the patches applied to it. An edit outside the tree must not replace the file with only the currently materialized projection. Root/common content remains editable. Provenance-backed active-branch metadata, resource paths, measure ratios, notes/holds, BGM/BGA slots, direct/extended BPM, and uniquely owned STOP values patch only their identified retained statements while preserving every branch. Cross-measure moves, occupied-slot collisions, timing declarations shared with sibling branches, and timing/event classes without import projections remain source-backed and reject ambiguous edits. Flattening is an explicit lossy compatibility transform and never replaces the branched source without a warning. Compatibility diagnostics are tied to the selected BMS target because command nesting, `#ELSE`, switch behavior, malformed recovery, and maximum depth vary among implementations; see the [control-flow research and `libotoworm` audit](../../references/bms-control-flow.md).

### Clipboard interoperability

Copy and paste must work within wormedit and across established BMS editors. Publish a versioned native wormedit payload containing exact relative measure/division positions, lane semantics, event types, holds, timing/BGM events, stable sound references, labels, and semantic layers. Also publish BMSE and iBMSC-compatible clipboard representations when the selected data can be represented by them. Use plain text as an inspectable fallback where the platform clipboard supports only text.

Paste detects the native, BMSE, and iBMSC representations and converts them into one preview transaction. Anchor the earliest copied musical position at the edit cursor while retaining relative spacing; offer lane mapping when source and destination profiles differ. Show conflicts, quantization caused by the destination global division count, missing `#WAV`/BPM/STOP definitions, unsupported event kinds, and metadata loss before commit. One paste is one undoable command.

Treat BMSE and iBMSC clipboard formats as external compatibility formats with fixture-backed parsers/writers, not as the internal clipboard model. Stage 0 should capture clipboard output from representative editor versions and compare it with their available documentation/source; see the [clipboard compatibility research notes](../../references/bms-editor-clipboard.md). Historical limitations such as partial multi-line BGM transfer must be diagnosed at the compatibility boundary rather than collapsing wormedit's ordered BGM lanes.

Cut deletes source events only after at least one requested clipboard representation is written successfully. Malformed or unsupported clipboard input must leave the document and existing selection unchanged.

## Side-by-side chart comparison

Allow two or more difficulties from the same song project to be previewed in parallel panes. The panes share transport, waveform time, vertical position, zoom, playback rate, and edit cursor; each keeps its own horizontal lane scroll where layouts differ. A baseline chart can be selected, or comparison can flag a row when any visible chart differs from another.

Default comparison aligns exact musical rows and compares a normalized row signature: semantic lane, note kind, hold start/end state, keysound/slice identity when requested, and selected metadata such as semantic layer. Every differing row receives a yellow background band across the compared chart panes. Matching rows retain the normal background. Options may exclude layers, labels, or sound assignments when the author wants to compare gameplay only.

If charts have different timing maps, offer explicit musical-position and audio-time alignment modes and show which is active. If lane profiles cannot be mapped semantically, mark the layout boundary and report the unmatched lanes rather than producing a misleading equality result. Comparison is read-only initially; later copy/paste can use the existing selection/command system to move a marked interval between difficulties.
