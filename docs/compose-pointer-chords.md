# Compose pointer chords

Canvas modifiers come from one SDL snapshot per frame. A left press captures
its chord until release, so changing modifiers cannot hand a release to another
tool. Beat fitting intentionally reads the live snapshot to switch snapping
during a drag. Wheel events keep their own event-time modifier snapshot.

Primary means Command on macOS and Ctrl elsewhere. Routing follows this order:
an existing capture, paste/popup ownership, timing handles, explicit resource or
razor tools, Draw, then canvas selection. A timing handle consumes its press;
Shift-stretching a Speed marker does not also add-select it.

| Chord | Target | Behavior |
| --- | --- | --- |
| Primary+Shift | Sound-bearing note, BGM marker or stem cut | Toggle matching sounds in the visible field |
| Primary+Alt/Option | Sound-bearing note, BGM marker or stem cut | Toggle matching sounds in the whole chart |
| Shift | Speed marker | Stretch the transition, leaving the head in place |
| Shift | BMSON BGM slice | Toggle restart |
| Alt/Option | Editable playable field | Resize Tap/Hold, or draw a hold on empty lane |
| Ctrl | Empty BPM lane, timing focus | Insert matching BPM |
| Alt/Option | Empty Anchor lane, timing focus | Insert anchor |
| Primary | Other selection targets | Toggle selection |
| Shift | Other selection targets | Add selection |
| None | Note or empty field | Move candidate or marquee/click selection; Draw uses its armed tool |

Target tests use the original press position. Moving a playable-field press
over the Anchor lane cannot turn it into an anchor insertion. Timing focus
disables playable-note edits. Mine and BGM objects cannot start a note-duration
gesture. A pointer hold becomes active only when application projection
acknowledges its begin; rejection consumes the remaining physical press.

Primary+Shift+Alt does not invoke same-sound selection. It retains the Alt-hold
fallback on the playable field and ordinary selection where supported elsewhere.

For the canvas wheel, Ctrl or Command zoom takes priority over Alt navigation.
Alt alone advances snapped rows. During beat fitting, Ctrl+wheel changes the
fit snap; Ctrl+Alt+wheel remains consumed by onset fitting. The BGM overview
keeps its own wheel zoom and Shift-wheel pan controls.
