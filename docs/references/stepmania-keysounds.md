# StepMania keysound model (SM / SSC)

Pinned source: [stepmania/stepmania](https://github.com/stepmania/stepmania) at commit
`825467bcd81c812b33ad684dc04dd151b2d5dec3` (branch `5_1-new`, committed 2026-08-22).
All citations are `src/<file>:<line>` at that commit, read from the raw files.
Plan card: [T-468](../chart-editor-plan/tasks/bmson-conversion.md#t-468-pin-stepmanias-keysound-model-in-a-reference).
Shape follows [quaver-qua-format.md](quaver-qua-format.md).

Entries marked **unconfirmed** were not verified in fetched source.

## `#KEYSOUNDS` tag

- Registered for `.sm` as `SMSetKeysounds` (`NotesLoaderSM.cpp:234`) and for `.ssc` as
  `SetKeysounds` (`NotesLoaderSSC.cpp:574`). Both are song-level tags, not per-steps tags.
- `.sm` handler: `split((*info.params)[1], ",", info.song->m_vsKeysoundFile);`
  (`NotesLoaderSM.cpp:183-185`). The value is split on commas. No trimming or
  file-existence check appears in this handler.
- `.ssc` handler (`NotesLoaderSSC.cpp:227-232`): if the value starts with `\#`, the
  backslash is stripped, then the value is split on `,` into `m_vsKeysoundFile`. The
  `\#` escape exists because a first sound name may begin with `#`.
- Writers emit `#KEYSOUNDS:` followed by the comma-joined list
  (`NotesWriterSM.cpp:184-188`; `NotesWriterSSC.cpp:326-336`, which applies the `\#`
  escape to a first entry starting with `#`, comment at `:329-332`).
- Storage: `Song::m_vsKeysoundFile` is `std::vector<RString>` (`Song.h:463`). Its
  doc comment says the index into this vector is the value stored in `TapNote`, and
  that any index outside the vector means "no keysound" (`Song.h:458-462`).
- A song with keysounds counts as having music: `Song::HasMusic` returns true when
  `m_vsKeysoundFile` is non-empty (`Song.cpp:1559-1560`).

## Per-note `[n]` index

- A note character may be followed by `[n]` (`NoteDataUtil.cpp:206-207`, comment
  `// look for optional keysound index (e.g. "[123]")`). This check runs for every
  note character, including `K`.
- Parsing: `sscanf(p, "%d]", &iKeysoundIndex)`; on success
  `tn.iKeysoundIndex = iKeysoundIndex` (`NoteDataUtil.cpp:210-212`). The comment on
  that line says failure is "not fatal ... due to malformed data".
- Writing: `[%d]` is appended only when `tn.iKeysoundIndex >= 0`
  (`NoteDataUtil.cpp:429-430`).
- Default: `iKeysoundIndex(-1)` in the constructor and `Init()`
  (`NoteTypes.h:178`, `:186`). A comment reads `// Index into Song's vector of
  keysound files if nonnegative` (`NoteTypes.h:162-163`).

## Limits

- **Count:** none in the SM or SSC parser. `SMSetKeysounds` and `SetKeysounds` cap
  nothing (`NotesLoaderSM.cpp:185`, `NotesLoaderSSC.cpp:232`).
- **Index:** no range check on read. `%d` accepts any `int`, including negatives
  (`NoteDataUtil.cpp:211`). The writer only emits indices `>= 0` (`:429`).
- **Playback guards:** the effective limit is at use sites. `Player::PlayKeysound`
  requires `0 <= idx < m_vKeysounds.size()` (`Player.cpp:2051`); the same bound check
  appears at `Player.cpp:1574` and `Player.cpp:2783`. Out-of-range indices are
  silently ignored there.
- **Autoplay path:** `AutoKeysounds::LoadAutoplaySoundsInto` checks only
  `tn.iKeysoundIndex >= 0` before indexing `m_vsKeysoundFile[...]`
  (`AutoKeysounds.cpp:89-91`). An index `>= size` is therefore not guarded there.
  Behavior on that path is **unconfirmed** (not run).
- Missing keysound files: `Player.cpp:780-783` calls `sound.Load(path, ...)` with no
  visible error handling in this block. Behavior for a missing file is **unconfirmed**.

## Notes without keysounds

- A tap with no `[n]` keeps `iKeysoundIndex == -1` (`NoteTypes.h:178`) and is written
  without `[n]` (`NoteDataUtil.cpp:429`).
- On play, `PlayKeysound` does nothing for a negative index (`Player.cpp:2051`).

## Autokeysound (`K`) and BGM behavior

- `K` parses to `TAP_ORIGINAL_AUTO_KEYSOUND` (`NoteDataUtil.cpp:166`); its writer char
  is `K` for `TapNoteType_AutoKeysound` (`NoteDataUtil.cpp:414`). The type is declared
  at `NoteTypes.h:110` with the comment "A special sound is played when this note
  crosses the target area."
- **Track-count change:** `LoadOverlapped` (`NoteDataUtil.cpp:610`, called at `:513`)
  reflows notes onto fewer tracks. When a plain note with a keysound cannot fit any
  track, it is moved onto an autokeysound track via `PlaceAutoKeysound`
  (`NoteDataUtil.cpp:660-664`). Autokeysound taps are skipped as sources (`:636`).
- `PlaceAutoKeysound` searches the row and the rows before and after it for an empty
  track and writes the tap there (`NoteDataUtil.cpp:576-607`). Autokeysounds from the
  source are placed the same way (`:680-687`).
- **Gameplay:** `ScreenGameplay` removes autokeysound taps from the player's
  `NoteData` (`ScreenGameplay.cpp:1096`) and loads them into `m_AutoKeysounds`
  (`:1100-1104`). Playback is driven from `AutoKeysounds::Update`, which plays each
  crossed row (`AutoKeysounds.cpp:314-343`).
- `Player::Update` also plays `K` taps at crossed rows when not in the step editor
  (`Player.cpp:2956-2964`). Given the removal at `ScreenGameplay.cpp:1096`, this path
  is inferred to matter only where `K` taps remain in the player's data, such as the
  editor. This inference is not separately confirmed.
- Scoring ignores autokeysounds: `NoteDataWithScoring.cpp:36`, `:73`, `:139`.
- Display: autokeysound notes are not drawn outside the step editor
  (`NoteDisplay.cpp:1271`).
- **Unconfirmed:** `AutoKeysounds.cpp:342` reads `tn.bKeysound` before playing
  `m_vKeysounds[tn.iKeysoundIndex]`. No `bKeysound` field was found in `NoteTypes.h`
  or elsewhere in the fetched tree. Treat the exact condition as unconfirmed.

## Recommendation

**Yes, a follow-up card is worth it, scoped to import only.** Evidence:

- The grammar is small: one comma-separated `#KEYSOUNDS` list plus an optional `[n]`
  on each note (`NotesLoaderSM.cpp:185`, `NoteDataUtil.cpp:206-212`).
- `src/formats/stepmania.cpp` has no keysound handling (a grep for `keysound` finds no
  matches). Charts with `#KEYSOUNDS` lose their sample data today.
- The open design point is mapping `K` autokeysounds, which play without being judged
  (`NoteDataWithScoring.cpp:36`), onto wormedit's semantic layers. That decision should
  go in the card, not in this reference.

Out of scope for T-468: any implementation. The follow-up plan is the [StepMania keysounds card group](../chart-editor-plan/tasks/stepmania-keysounds.md), T-478 to T-481.
