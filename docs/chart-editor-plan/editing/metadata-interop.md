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
implementation work. See [T-729](../tasks/markers.md#t-729-plan-selection-created-export-cut-sections).

osu! `[Editor]` bookmarks are integer millisecond points and carry no label or range. Import them as audio-time point markers. Export every compatible point marker, plus the start of a labeled range when requested; retain labels, colors, range ends, and beat anchors in the native project and diagnose that loss when producing standalone `.osu`. MIDI export may emit labeled markers as marker meta-events.

### BMS channel maps and MIDI destinations

Make a BMS channel map editable per lane-layout segment or reusable as a named profile. Each entry has a semantic lane ID, display order, stage group, role (`playable`, `scratch`, `invisible`, `mine`, `autoplay`, or retained/unknown), raw two-character BMS object channel, and optional MIDI port/channel/note/velocity defaults. Keep timing/resource channels reserved and validate any attempted collision with them. Preserve unknown source channels as raw retained lanes until the user maps or explicitly discards them.

Custom maps allow nonstandard BMS layouts to import without squeezing objects into built-in columns. Export validates duplicate raw channels, dialect-specific channel syntax, scratch/player-side expectations, long-note pairing, and conflicts with repeated BGM allocation. A map change proposes a lane migration and loss report; it never changes existing semantic lane IDs merely because a display or output code changed.

MIDI export uses the same semantic-lane destinations. Holds become note-on/note-off pairs; taps use a configurable short duration; keysound/layer labels and song sections may become text or marker meta-events. Provide two explicit timing policies: **musical** export preserves chart beats, BPM, and meter where Standard MIDI can express them, while **performance** export preserves audible note times against a chosen MIDI tempo/grid. Stops, delays, warps, scroll velocity, kiai, and format-specific keysound semantics require diagnostics because MIDI has no direct equivalents. Choose a PPQ that represents all exported chart positions exactly or reject/preview quantization rather than rounding silently.

### BMS branch tree and active projection

Model BMS as an ordered scoped program, retaining all active and inactive content.
The owner-approved [scoped-program contract](bms-scoped-program.md) defines Control,
Chart and Text nodes: every editable scope, including Root and RANDOM, owns at least
one Chart child; selecting a control targets its first Chart, and an explicit Chart
selection targets that content node. These Chart nodes are not separate difficulties.
Commands with known meaning, such as WAV definitions, need typed scoped owners;
comments and commands without implemented meaning belong to ordered opaque Text.
Original source remains provenance or safe migration backing, not a second writable
semantic graph. The mapping of non-body control directives is an explicit open question.

The chart window's tree is an edit-scope selector and node-authoring surface.
Control nodes can be added, moved and removed, RANDOM's outcome count can be edited
directly, and several Chart children may coexist in a scope. A confirmed Delete
removes the complete subtree; separate Unwrap promotes ordered contents to the
parent only when the approved structural policy permits it. Text travels with its
containing block. RANDOM's range, IF arm count and preview outcome are distinct facts.

The note, timing, BGM, waveform, metadata/resource and playback views are derived
from the program and its evaluation environment. Inactive content remains owned by
the program even when absent from the current projection. Edit targeting and preview
choice must not be conflated; their interaction, including inactive-scope authoring
and selection restoration, is question 24 in the
[open decisions](../decisions/open-questions.md).

Data edits resolve a stable scope/Chart target through the shared transaction;
existing objects keep their semantic owner, and a target mismatch does not authorize
implicit relocation or mutation of another scope. Inherited-context/override behavior
is part of question 24. Validate scoped WAV/BPM/STOP definitions, lane/resource collisions and
timing effects against affected projections. T-731–T-744 provide the model,
migration, authoring and integration sequence. Moving/copying individual selected
notes between scopes remains T-93, distinct from moving whole program subtrees.

Evaluation must be profile-driven and sequence-aware. `#RANDOM n` chooses an integer in `1..n`; `#SETRANDOM` fixes the value. `#IF` groups, `#ELSEIF`, and `#ELSE` select according to that surrounding value. `#SWITCH`/`#SETSWITCH` begins at the matching `#CASE` or `#DEF` and preserves target-profile fallthrough until `#SKIP` or `#ENDSW`. Nested random and switch blocks remain nested nodes. The editor should enumerate reachable outcomes for tests and warnings within a configured cap, while seeded sampling handles combinatorial trees interactively.

Default export means **do not discard control flow**, not freeze ordinary timing.
Branch-free charts export edited BPM/STOP timing normally. Branched output must
emit the complete authored program with inactive content, scoped definitions and
Text, not merely the active projection. Explicit active-projection export is the
separate lossy operation that may discard control flow, with existing warnings and
selection requirements. Until the scoped model is activated, provenance-backed
writer patches remain compatibility backing with their existing attribution and
collision safeguards; they are not the new model's semantic authority. No edit may
silently replace the whole program with its current projection. Profile-dependent
nesting, switch behavior, malformed recovery and depth rules remain governed by the
[control-flow reference](../../references/bms-control-flow.md).

### Clipboard interoperability

Copy and paste must work within wormedit and across established BMS editors. Publish a versioned native wormedit payload containing exact relative measure/division positions, lane semantics, event types, holds, timing/BGM events, stable sound references, labels, and semantic layers. Also publish BMSE and iBMSC-compatible clipboard representations when the selected data can be represented by them. Use plain text as an inspectable fallback where the platform clipboard supports only text.

Paste detects the native, BMSE, and iBMSC representations and converts them into one preview transaction. Anchor the earliest copied musical position at the edit cursor while retaining relative spacing; offer lane mapping when source and destination profiles differ. Show conflicts, quantization caused by the destination global division count, missing `#WAV`/BPM/STOP definitions, unsupported event kinds, and metadata loss before commit. One paste is one undoable command.

Treat BMSE and iBMSC clipboard formats as external compatibility formats with fixture-backed parsers/writers, not as the internal clipboard model. Stage 0 should capture clipboard output from representative editor versions and compare it with their available documentation/source; see the [clipboard compatibility research notes](../../references/bms-editor-clipboard.md). Historical limitations such as partial multi-line BGM transfer must be diagnosed at the compatibility boundary rather than collapsing wormedit's ordered BGM lanes.

Cut deletes source events only after at least one requested clipboard representation is written successfully. Malformed or unsupported clipboard input must leave the document and existing selection unchanged.

## Authored multivoice sounds (deferred)

Owner request (2026-10-20): plan a shared authored sound model for several voices
on one logical note, including native projects, edited osu! custom sample sets,
and BMSON layered sounds. This is later work, not an instruction to replace the
current model while polishing the hitsound palette. [T-745–T-748](../tasks/compose-ux.md#t-745-specify-one-authored-multivoice-sound-model)
define the ordered design and implementation lane; [question 25](../decisions/open-questions.md)
blocks unresolved model and authoring choices.

Imported osu! set/index/volume references remain read-only and continue to resolve
faithfully, including timing-point inheritance. When an actual sound edit affects
such a note, materialize **only that note's affected authored sound configuration**
into ordinary Sounds-list resources and the approved multivoice bindings in one
undoable edit. Leave unedited notes, shared custom sets and timing declarations
unchanged. Retain distinct normal/whistle/finish/clap identities, resolved source
paths, effective gain and any source slice; apply inherited volume exactly once.
Selection, audition, playback and a no-op edit do not trigger conversion.

BMSON already supports retained sound-channel layering and fused runtime audition;
extend authoring without losing that behavior. The later model must preserve
coincident voices, channel/event attribution, exact slice spans and
continuation/restart semantics while giving editable content one authoritative
owner. Specify which retained constructs remain format backing and how their
authored bindings stay synchronized; do not disguise BMSON source objects as a
generic native sound model. Ordinary Sounds resources and decoded-source services
remain shared owners, not a second per-format audio library.

The design must cover stable identities, native save/reopen, Undo/Redo,
copy/paste, sound assignment/usage/deletion, audition, playback, preview.ogg and
target-format compatibility. Unsupported output requires an explicit loss report
or refusal, never silently dropping voices. Flattening to a generated composite
file is not an implementation shortcut. No whole-set migration or automatic
rewrite of untouched imported content is authorized.

## Side-by-side chart comparison

Allow two or more difficulties from the same song project to be previewed in parallel panes. The panes share transport, waveform time, vertical position, zoom, playback rate, and edit cursor; each keeps its own horizontal lane scroll where layouts differ. A baseline chart can be selected, or comparison can flag a row when any visible chart differs from another.

Default comparison aligns exact musical rows and compares a normalized row signature: semantic lane, note kind, hold start/end state, keysound/slice identity when requested, and selected metadata such as semantic layer. Every differing row receives a yellow background band across the compared chart panes. Matching rows retain the normal background. Options may exclude layers, labels, or sound assignments when the author wants to compare gameplay only.

If charts have different timing maps, offer explicit musical-position and audio-time alignment modes and show which is active. If lane profiles cannot be mapped semantically, mark the layout boundary and report the unmatched lanes rather than producing a misleading equality result. Comparison is read-only initially; later copy/paste can use the existing selection/command system to move a marked interval between difficulties.
