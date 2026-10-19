# BMS user walkthrough — 2026-10-18

## Scope

Revision: `09f4b2abb0bb3611959c9cfbcd0e356581d19f7c` (`drake/walkthrough`). Read `AGENTS.md`, the edit architecture and code-style checklist first. No product changes.

Walked `docs/manual/chapters/walkthrough-bms.adoc` steps 1–7, plus `sounds-and-layers.adoc` (arming, import review, BMS slots), `the-wormedit-way.adoc`, `placing-notes.adoc` (Draw, keyboard holds, Alt duration, BGM), `measures.adoc` (Ctrl+M), and the timing/getting-started references those steps need.

The windowless harness exercises actual frontend events/actions; production functions exercise mutation, preview/commit/Undo, native file save/reopen and export. The export case seeds one playable note after sound import and retains passing chart-publication checks; sound packaging failed a separate removed assertion (F3). It does **not** exercise SDL's real clipboard, native file dialogs, audible playback or rendering. The new test uses a two-entry layout catalog for the wizard, feeds reviewed imports through the real confirmation widget, and uses projected lane-key geometry. The Sounds row is supplied explicitly after import; sound-list projection is traced separately. Draw is supplied as application state and projected into the view; existing production routing tests cover Alt+2.

Important scope boundary: the from-scratch BME path is broken before slot-based paste/BGM. To inspect downstream behavior independently, all-four-format paste tests start with an **imported BME** containing matching WAV identities and a BGM column. This is a diagnostic control, not a workaround supplied by the manual.

Logs live in `.agent/`. The temporary **BMS walkthrough manual contract probes** ran 21 assertions: 15 passed, six failed as expected; the probes were then removed so only passing walkthrough cases remain. The failures are preserved in `.agent/bms-manual-contract-probes.log` and quoted in the findings. Final verification:

- `ctest --test-dir build -R '^documentation\.' -j2 --output-on-failure`: **5/5 passed**. The first attempt lacked the manual-reference executable; building `wormedit-manual-reference` resolved that prerequisite.
- `ctest --test-dir build -R '^frontend-state\.BMS walkthrough' -j2 --output-on-failure`: **5/5 passed**, covering the five reviewed new cases in `tests/ui/walkthrough_bms_event_harness_tests.cpp`; the paste case runs all four pinned formats.
- A bounded selection of existing production-path tests plus the new cases: **108/108 passed** (`.agent/bms-production-path-tests.log`). It includes clipboard rejection/priority, slot review/replacement, sound picker, tool routing, hold duration, BGM strokes/columns, measure toggle and output-group routes. These results establish the checked contracts, not the usefulness or completeness of the whole suite.
- `clang-tidy -p build tests/ui/walkthrough_bms_event_harness_tests.cpp` completed; only the existing included `src/sys/compose_speed_spans.h:51` complexity warning (29) remains. `scripts/check-complexity.py --build-dir build --jobs 1 tests/ui/walkthrough_bms_event_harness_tests.cpp`: **passed**, no new/increased violations. `git diff --check`: **passed**.

Builds used `cmake --build build -j4`, sequentially; tests used at most `-j2`. Retained tests are passing; no product fixes, generated-reference updates or baseline allowances were made.

## Steps

| Step | Manual says | Production route, effect and feedback | Result |
|---|---|---|---|
| New BME 7K | Ctrl+N → outside scratch → Create; one chart and BMS group; keys 1–8. | Wizard emits layout ID (`src/ui/nuklear/frontend.cpp:2695`, `:2800`); app invokes factory (`src/sys/application.cpp:3646`); catalog supplies profile/output (`src/core/lane_profiles.cpp:1241`). Factory creates correct lanes/group but marks chart `native_project` (`src/sys/application_defaults.cpp:86`). Success says “New … project created. Save it to keep your work.” (`src/sys/application.cpp:3657`). | **Works with surprise**; causes F1. Tested wizard/factory. |
| Title/artist | F2, fill details. | Song inputs stage text; **Apply** publishes (`src/ui/nuklear/metadata_song_section.cpp:192` → `src/sys/metadata_actions.cpp:59` → `src/core/metadata_editor.cpp:423` transaction); “Metadata updated.” (`src/sys/metadata_actions.cpp:42`). Simply typing and switching tabs does not apply. | **Works with surprise**: walkthrough omits Apply. Traced. |
| Genre | Fill genre in Metadata. | Common Song section has title/artist/tags; typed `GENRE` is in BMS fields (`src/ui/nuklear/metadata_format_fields.cpp:34`, `:394`). Entire BMS section is hidden unless source family is BMS (`:407`), including on the new BME. | **Broken** as the obvious typed-field path; F1. Ordered format-field editor is a separate, undocumented route. Traced. |
| Chart author/rating | Set in F1 Project. | Table columns are read-only (`src/ui/nuklear/setup_panel.cpp:358`); chart selection/batch editor emits metadata action → `apply_project_chart_command` (`src/sys/project_view.cpp:105`) → transaction. Invalid ratings reject (`src/sys/project_view.cpp:184`). | **Works with surprise**: need selection/editor, not typing in table. Traced. |
| Import sounds | Drop onto Sounds or +, review groups, Confirm import. | + opens picker (`src/ui/nuklear/compose_sounds_panel.cpp:544`); drops stage review (`src/sys/application.cpp:1415`); preparation → review → confirmation dispatch (`src/sys/application.cpp:1347`, `:1486`) → `apply_bulk_sound_import` (`src/sys/compose_sound_actions.cpp:835`). Core commits at `src/core/sound_import.cpp:436`. One Undo; “Added …” (`:845`). Stale/pending gestures retain review and explain why (`src/sys/application.cpp:1498`, `:1515`). | **Works** for ordinary import. Review/confirmation exercised. Native picker/drop delivery traced only. |
| Imported WAV numbers | First column gets 01, 02, … | Confirmed import allocates BMS identities only when source family is BMS (`src/sys/compose_sound_actions.cpp:820`); new BME gets no identities. Sound-list IDs capability is likewise source-gated (`src/sys/workspace_view.cpp:1051`). | **Broken**, F1. |
| Exact slots | BMS slots → select empty/filled slot → +/drop; batch review and occupied-slot confirmation. | Mode depends on IDs capability (`src/ui/nuklear/compose_sounds_panel.cpp:518`); exact-slot preparation explicitly rejects native chart (`src/sys/compose_sound_actions.cpp:889`). For imported BMS: choice/arming (`src/ui/nuklear/compose_sounds_panel.cpp:834`), review/range checks (`src/sys/compose_sound_actions.cpp:900`), transaction (`:379`). | **Broken** on new BME; works in imported-BMS controls. F1. |
| Tempo | Double-click initial/empty BPM; F3 Ctrl-click inserts current BPM. | Rail hit → stage BEGIN and inline edit (`src/ui/nuklear/compose_input_panel.cpp:664`); staged timing controller → `src/core/bpm_editor.cpp:215` validates and commits. Timing reference explicitly requires Enter/Confirm event. Locked/invalid values remain pending with feedback. | **Works** by trace and existing timing-geometry tests; inline focus/initial-marker visibility needs window check. |
| Draw and arm | Alt+2; double-click sound or Ctrl+K then Enter. | Registry → tool route; double-click emits Draw source (`src/ui/nuklear/compose_sounds_panel.cpp:1351`), picker emits same. Missing row cannot replace armed source. Draw label/“armed” row communicates choice (`src/ui/nuklear/compose_sounds_panel.cpp:1251`). | **Works**. Review → double-click → source dispatch exercised; picker/refusal covered by existing tests. |
| Snap/walk/tap/hold | Left/Right snap; Up/Down cursor; lane keys place/toggle; hold key while walking. | Arrow command registry (`src/core/command_registry.cpp:872`); cursor dispatch (`src/sys/compose_navigation_input.cpp:125`); lane press/release → shared hold gesture (`src/core/interaction.cpp:1845`, `:1857`) → `src/core/interaction_gesture_lifecycle.cpp:810` → transaction. Native new BME still accepts keysounded taps/holds. | **Works**. Sound, tap and held-key/up/release chain exercised. |
| Alt-drag duration | Select tool; lengthen/shorten tap/hold; whole selection atomic. | Alt pointer capture (`src/ui/nuklear/compose_object_input.cpp:1037`) → core duration/hold gesture (`src/core/interaction.cpp:1868`, `src/core/interaction_gesture_lifecycle.cpp:60`). Refused collision releases/cancels and reports diagnostics. | **Works** in existing production-path tests; requires Select as placing-notes says. |
| Listen | Space from cursor/stop. | Space registry (`src/core/command_registry.cpp:878`) → `play_stop_transport`, with “Chart preview is unavailable.” on unavailable service (`src/sys/application_command_dispatch.cpp:289`). | **Works** by route trace; audible timing needs window/audio check. |
| BGM columns/drawing | Set BGM columns, Draw armed sound. | New chart has no columns; control disabled for native chart (`src/sys/compose_view.cpp:2563`; frontend `src/ui/nuklear/compose_toolbar.cpp:339`). Core also refuses (`src/core/bgm_columns.cpp:71`). Imported BMS Draw stamps source lane (`src/ui/nuklear/compose_object_input.cpp:576`) and commits through paint. | **Broken** from scratch, F1; imported-BMS stroke controls covered separately. |
| Move to BGM | Select notes, press backquote. | Registered command is capability-gated (`src/sys/compose_note_to_bgm.h:11`); source must be BMS. Note menu disabled hint says “Requires selected playable notes on an editable BMS chart” (`src/ui/nuklear/compose_note_menu.cpp:190`). | **Broken** on new BME, F1; misleading hint because user chose BME. |
| External paste, four formats | Ctrl+V → following preview → Enter/click; Escape cancels; one Undo. | Command → `begin_compose_paste` (`src/sys/compose_view.cpp:311`) → grammar/resource/lane decode (`src/formats/bms_clipboard.cpp:723`) → proposal → `apply_compose_paste` (`src/sys/compose_view.cpp:348`) → transaction. Pointer commit is routed at `src/ui/nuklear/compose_input_panel.cpp:1231`. Start says “Pasting BMS editor data: click or Enter to commit; Escape to cancel.”; commit “Selection pasted.” | **Works** with matching identities/authored measures: all four mixed fixtures preview/commit/Undo exercised. **Broken** after ordinary new-BME import, F1. |
| Paste refusal | Missing sound says which; missing columns/invalid input change nothing. | Decode rejects before session publication; prefix at `src/sys/compose_view.cpp:332`. Missing sound diagnostic omits number (`src/formats/bms_clipboard.cpp:222`); missing BGM column at `:322`; decimal/header validation at `:723`. | **Works with surprise**, F4. |
| Own clipboard priority | Copy elsewhere makes external copy next. | Internal Copy records current system text (`src/sys/application_command_dispatch.cpp:414`). Paste compares bytes (`src/sys/compose_view.cpp:289`); clipboard-update event only refreshes availability (`src/sys/application_frame_services.cpp:100`, `src/sys/application.cpp:4451`). Identical external recopy does not override internal copy. | **Works with surprise**, F6. |
| Ctrl+M | Split/merge at cursor; first boundary protected; notes stay in time. | Registry (`src/core/command_registry.cpp:885`) → `apply_cursor_measure_split` (`src/sys/compose_measure_commands.cpp:90`) → `src/core/measure_editor.cpp:1186` → transaction. Feedback “Bar line added/removed at cursor.”; first boundary “The first measure has no bar line to remove.” Virtual cursor rejects: “Materialize the cursor measure before splitting it.” | **Works** for authored measures, **undocumented** virtual-space limitation. Existing production command tests exercised. |
| Save | Ctrl+S, choose .wormedit. | App Save rejects busy operations/bad extension, resolves destination/rebases paths, calls `create_native_project_file` (`src/sys/application.cpp:2387`). Native codec/publication retains chart data (`src/formats/native_project_storage.cpp:2118`). | **Works** by native file write/reopen test and Save-path trace; native chooser needs window check. |
| Output path | Project → Groups → Edit → path. | Actual heading is **Output groups** (`src/ui/nuklear/setup_panel.cpp:545`); Edit opens draft; only **Save group** emits update (`:499` → `src/sys/project_view.cpp:337`). Preview/export read committed group while draft remains open. | **Works with surprise**, F7; actions exercised. |
| Preview | Shows what export will write first. | Preview action → worker → `convert_document` returns only file/warning counts (`src/sys/project_view.cpp:278`). No artifact list, warning detail, destination collision or resource-copy check (F3). Explicit `.bme` path fails conversion (F2). | **Works with surprise**, F5. |
| Export… / direct Export | Publish without replacing existing files; notification report. | **Export…** opens group/policy dialog; checkbox is **Replace existing chart files**, final button **Export group** (`src/ui/nuklear/compose_resources.cpp:139`). Row **Export** directly uses overwrite=false (`src/ui/nuklear/setup_panel.cpp:530`). Saved-base/busy/gesture rejection (`src/sys/compose_resource_actions.cpp:279`); worker stages assets then atomically publishes (`src/sys/output_group_export.cpp:214`, `:168`). Completion reports publication/losses (`:108`). | **Broken** for the documented `.bme` filename, F2. Folder chart publication/collision refusal exercised separately; keysound packaging is also broken (F3); labels/order also mislead, F7. |

## Findings (most severe first)

### F1 — Creating BME does not enable the BMS authoring workflow

**Severity: blocks the workflow.** New BME has a BMS output group and correct eight lanes, but `source_family=native_project`. After successful ordinary import, WAV identities are absent; BMS slots/typed BMS metadata are hidden, BGM column controls are disabled, Move to BGM is unavailable, and slot-based external paste refuses the sounds just imported. There is no explanation connecting these symptoms to project creation. Saving/reopening retains the family; an output group does not grant editing capabilities.

Evidence: `src/sys/application_defaults.cpp:86`; import `src/sys/compose_sound_actions.cpp:820`; slot gate `:894`; typed metadata `src/ui/nuklear/metadata_format_fields.cpp:407`; sound-ID projection `src/sys/workspace_view.cpp:1051`; BGM `src/sys/compose_view.cpp:2564`; Move to BGM `src/sys/compose_note_to_bgm.h:11`. The app calls this exact factory at `src/sys/application.cpp:3646`; existing legacy-BMS tests use a different overload (`tests/sys/application_defaults_tests.cpp:76`).

Removed probe failures: `CHECK(flow.history.document.charts.front().source_family == format_family_t::bms)` expanded `0 == 1`; `CHECK(flow.harness.view().compose.bgm_column_count_editable)` was false; `CHECK_FALSE(flow.history.document.resources.front().target_identities.empty())` expanded `!true`; `CHECK(result.applied)` after requesting one BGM column was false with `BGM column counts are available only for BMS and osu!mania charts`.

Smallest proposed fix, if the manual describes the intended behavior: initialize BMS-family presets with `format_family_t::bms` in the new-project factory (using the catalog’s format fact), so choosing BME enables the BMS features; retain a from-new-project integration check rather than only imported-BMS fixtures. Owner must decide the intended format/capability representation; no product or manual behavior was changed here.

### F2 — The documented .bme output filename cannot export

**Severity: blocks the workflow.** Save the new BME project, set the BMS group's path to `song/normal.bme`, and Export: publication fails with `requested BMS lane profile is unknown`. Preview uses the same request and fails conversion too. This is independent of F1: a correct authored lane map already exists, but the output plan turns the extension into `target_profile="bme"`; the writer expects a catalog lane-profile ID such as `bme-7k-outside-scratch`.

Evidence: `src/sys/output_group_export.h:34`–`:38` → `src/formats/conversion.cpp:827`–`:831` → `src/formats/bms_writer.cpp:1008`–`:1014`. In the initial new walkthrough run, **BMS walkthrough output group exports a new project sound and refuses a second publication** failed `REQUIRE(exported.published)` with expansion `false` and that exact diagnostic; log `.agent/bms-walkthrough-initial-ctest.log` preserves it. The final passing export case uses a folder destination instead; it does not retain a failing test. The same extension-to-profile branch also covers `.bml`/`.pms` (traced, not separately exercised).

Folder destinations avoid the invalid profile token, but the conversion filename chooses an empty profile for a non-BMSON chart (`src/formats/conversion.cpp:835`–`:840`), which defaults to `.bms` (`include/wormedit/core/catalogs.h:143`), even for this BME layout. Thus the folder route does not produce the promised `.bme` filename either.

Smallest proposed fix: distinguish output file extension from lane-profile identity in the export plan; derive the suggested extension from the selected chart's BMS layout when no explicit profile is requested. Add a production new-BME → output-plan → publication check for both filename and directory destinations.

### F3 — Published chart does not include its imported keysound

**Severity: blocks the workflow.** Import an external keysound, place a note, save the project and export BMS to a fresh folder. Chart publication succeeds and writes `#WAV01 keysound-c4.ogg`, but that referenced sound is absent beside the exported chart. The resulting output cannot play its keysound. The only completion warning in the probe concerns measure beat grouping, not the missing audio. This also occurs after using the folder route to get around F2.

Evidence: export discovers only copies already at the output destination (`src/formats/conversion_publication.cpp:216`); an absent destination is silently skipped (`:218`, `src/core/resource_file_copy.cpp:456`). New pending copies are planned only for osu!mania/StepMania music (`src/sys/output_group_export.cpp:14`), not BMS keysounds. Without a staged mapping, BMS emits the resource name as its WAV path (`src/formats/bms_writer.cpp:2784`) while the actual imported asset remains external.

Removed failing assertion: `check_step(std::filesystem::is_regular_file(output.parent_path() / parsed.value.song->audio_assets.front().source_path))`; Catch reported `CHECK(condition)` → `false`, with `walkthrough assertion at 298` and `Output group published with 1 conversion loss: BMS measure ratios preserve duration but not authored beat grouping`. `.agent/bms-missing-export-sound-probe.log` preserves this run (107/108 selected tests passed). The retained **BMS walkthrough saves then publishes a folder chart and refuses replacement** tests chart publication, declarations and collision refusal; it does not assert complete sound packaging.

Smallest proposed fix: enqueue referenced BMS source assets through the existing pending-assets publication path and rewrite WAV references to their staged paths. At minimum, refuse publication with the missing source/destination named rather than reporting successful publication of an unusable keysounded chart. The manual currently has no required resource-copy step.

### F4 — Missing-WAV paste error cannot identify the slot to repair

**Severity: misleads.** Manual promises “the message says which,” but a missing 01 yields `BMS editor clipboard was not pasted: BMS clipboard references a WAV identity absent from the target song.` No slot, record or line is included. Atomic refusal is correct; resolving a large keysound set becomes guesswork.

Evidence: `src/formats/bms_clipboard.cpp:211`, `:222`, `:754` → prefix at `src/sys/compose_view.cpp:332`; existing “Paste reports BMS editor text it cannot decode without pasting” checks only the prefix (`tests/sys/compose_notification_tests.cpp:786`).

Removed probe: missing-01 paste was refused without a session; `CHECK(status.find("01") != std::string::npos)` failed with the exact generic message above.

Smallest proposed fix: include the already-computed WAV token in missing/ambiguous resource diagnostics; optionally include the record index. Keep atomic refusal.

### F5 — Preview is conversion counts, not a publication preview

**Severity: misleads.** A successful Preview says `Preview: 1 file(s), 0 conversion warning(s).` It does not show filenames/assets or explain losses; it can remain successful when Export subsequently refuses an existing destination. The walkthrough's “shows what the export will write” overstates it. Successful publication also reports only “Output group published from the selected revision.” without destination/file names (`src/sys/output_group_export.cpp:109`, delivered at `src/sys/resource_job_state.cpp:254`).

Evidence: `src/sys/project_view.cpp:278`–`:289` only converts/counts; export separately discovers assets (`src/sys/output_group_export.cpp:28`) and publishes (`:168`). New export walkthrough converts successfully after publishing, then verifies repeat publication is refused and the old file remains unchanged.

Smallest proposed fix: describe Preview as a conversion summary now; if intended as a readiness preview, reuse the export plan to list destinations/assets and preflight publication errors.

### F6 — Recopying identical external data does not regain clipboard priority

**Severity: misleads.** Copy pattern A in iBMSC; copy selection B in Wormedit; copy the same A again in iBMSC; Paste still chooses B. There is no signal that external ownership was ignored.

Evidence: byte equality at `src/sys/compose_view.cpp:296`; snapshot at `src/sys/application_command_dispatch.cpp:416`; SDL update only sets/refreshes availability (`src/sys/application_frame_services.cpp:100`, `src/sys/application.cpp:4451`). Existing “Paste keeps wormedit's own copy until the system clipboard changes” (`tests/sys/compose_notification_tests.cpp:762`) verifies content changes, not ownership changes.

Removed byte-equality probe: `CHECK(status.starts_with("Pasting BMS editor data"))` failed; actual status was `Paste preview: click or Enter to commit; Escape to cancel.` This tests the priority branch, not OS clipboard ownership delivery.

Smallest proposed fix: either track clipboard update/ownership generations or explicitly document that external text must differ. Real clipboard same-content update delivery needs the check below.

### F7 — Save/export instructions omit commits and name a tooltip as an option

**Severity: misleads.** Song text requires **Apply**; group path requires **Save group**. Following the abbreviated path edit then Preview/Export uses the old path. “Publish this group without replacing existing chart files” is the row Export button's tooltip, not a dialog option. Export…'s actual policy is **Replace existing chart files** (unchecked means refuse existing destinations); publishing is confirmed with **Export group**. The section is **Output groups**, not Groups.

Evidence: `src/ui/nuklear/metadata_song_section.cpp:203`; `src/ui/nuklear/setup_panel.cpp:499`, `:530`, `:545`; `src/ui/nuklear/compose_resources.cpp:139`. The new Project action walkthrough confirms Save group, Preview, row Export and Export… are distinct routes.

Smallest proposed fix: replace the instructions with the concrete button sequence below. Keep the code/manual disagreement visible until the owner resolves F1 and F2.

### F8 — Ctrl+M does not materialize virtual measures

**Severity: misleads.** The measure chapter describes splitting at the cursor without limiting it to existing measures. Walk into empty virtual space and Ctrl+M refuses with `Materialize the cursor measure before splitting it.` Other note-entry steps can materialize that space, so this needs a stated prerequisite.

Evidence: first guard in `src/sys/compose_measure_commands.cpp:93`; authored-measure split/merge and first-boundary refusal passed **Measure line command toggles the bar line at the cursor** (`tests/sys/application_commands_tests.cpp:2504`). Virtual rejection was traced, not newly exercised.

Smallest proposed fix: add the authored-measure prerequisite and note-placement workaround below, or intentionally extend the command through the shared materialization path.

## Manual corrections

- `walkthrough-bms.adoc:18`: “fill in the title, artist and genre” omits applying song text and the location of genre. Proposed wording for the intended BMS UI: “In Metadata (F2), enter Title and Artist and click Apply in Song. In Format fields → BMS fields, enter GENRE and click Add (or Apply for an existing value).” The typed BMS path is currently hidden for new BME (F1); do not silently present this as working today.
- `walkthrough-bms.adoc:24`–`:29`, `:46`–`:47`: promises of automatic BMS IDs, BMS slots and BGM editing disagree with the new-BME path (F1). Resolve the product behavior or explicitly disclose that these steps presently require an imported BMS chart; changing the chapter alone would abandon its from-scratch objective.
- `walkthrough-bms.adoc:86`: replace “the message says which” with “Paste reports a missing WAV identity” until the error includes the number (F4).
- `walkthrough-bms.adoc:90`–`:92`: current behavior is “Wormedit's copy takes priority while the system clipboard text is unchanged. Copying identical text again in another editor does not switch priority.”
- `walkthrough-bms.adoc:5`, `:105`: the promised `.bme` output conflicts with the explicit-filename failure and folder `.bms` naming (F2). Resolve that before presenting `song/normal.bme` as a working example.
- `walkthrough-bms.adoc:102`–`:109` omits asset packaging (F3). Until corrected in the product, add: “Ordinary keysound import and Save do not package sounds beside the exported chart. Ensure every exported #WAV reference resolves to a sound file relative to that chart before distributing it.”
- `walkthrough-bms.adoc:104`–`:109` (button-sequence correction, conditional on resolving F2 and F3): “Open Project (F1). Under Output groups, click Edit on BMS, enter a project-relative Output path such as song/normal.bme, then click Save group. Click Preview for a conversion file/warning count; this does not check publication collisions. Click Export…; choose BMS, leave Replace existing chart files unchecked to refuse overwriting, then click Export group. The row's Export button publishes directly without overwriting. Read completion or refusal in notifications (Ctrl+Shift+M).”
- `measures.adoc`, after Ctrl+M description: add “The cursor must be in an authored measure. In empty virtual space, place a note to materialize the measure before toggling a bar line.” Evidence: `src/sys/compose_measure_commands.cpp:93`; this affects BMS virtual canvas independently of F1.

## Needs a real-window check

1. **New project and feedback:** Ctrl+N → BME 7K (outside scratch) → Create project → F4. Verify scratch/1–8 headers, readable success notice and canvas/docks at 1280×720 and 960×720. F2 → expand Format fields; check missing typed BMS fields. Import keysound-c4.ogg through Sounds + → Confirm import; check missing IDs/BMS slots, disabled BGM columns, and disabled Move to BGM after selecting a note. Capture visible explanations.
2. **Import/drop and exact slots:** open an imported BME with WAV01; Sounds → BMS slots → empty 09 → + → choose two sounds → inspect ordering/slots → Assign files. Then select occupied 01 → choose one replacement → Cancel, repeat → confirm; drop files onto the list too. Check native dialog ownership, replacement wording, readable batch errors and one-step Undo. Repeat a batch at ZZ to see overflow refusal.
3. **Tempo and ordinary charting:** F4 → double-click starting BPM → type 150 → Enter; double-click empty BPM row → 180 → Enter. F3 → Ctrl-click empty BPM row → inspect current BPM; lock timing and retry. F4 → Alt+2 → double-click sound → type keys 2,3,4 with Up between; hold 5 while pressing Up three times → release. Ctrl+K → type next sound → Enter → place note. Alt+1 → Alt-drag tap to a hold and back; attempt a collision. Inspect previews, errors and Undo.
4. **Playback:** with the keysounded chart above, Space from a mid-chart cursor → listen to taps/hold attack/BGM alignment → Space to stop → Ctrl+Shift+Space from start. Verify the status/history remains readable and no unexpected seek occurs while Sounds dock or Go to sound owns keyboard input.
5. **External clipboard:** in each BMSE 3.0, legacy BMSE, iBMSC normal and xNT environment, copy a mixed phrase (scratch, keys, long note, BGM, BPM/STOP). Import matching WAV IDs/add columns first. Ctrl+V in Wormedit → move pointer → Enter/click → Undo; repeat → Escape. Repeat with missing WAV01/insufficient BGM columns and beyond the authored last measure. Verify no partial paste and readable refusal.
6. **Same-text clipboard priority:** copy A externally → copy different B internally → recopy exactly A externally → Ctrl+V; distinguish A/B visually. Also copy different external text C and retry. Record whether the OS sends a same-content clipboard update and whether A is still ignored (F6).
7. **Measures and export:** on an authored measure, Up to an interior row → Ctrl+M twice; inspect boundaries and unchanged note/audio timing. Repeat at first bar and in virtual space. Ctrl+S → choose a real .wormedit location → F1 → BMS Edit → path song/normal.bme → Save group → Preview → Export… → BMS → unchecked Replace existing chart files → Export group. Record the unknown-profile refusal for the .bme path (F2). Change Output path to the folder song → Save group → Preview/Export and inspect the resulting .bms file, every #WAV reference and completion history. Verify the referenced external keysound is absent from the fresh output (F3), then manually copy it beside the chart and confirm playback resolves it. Repeat the folder export, then explicitly check overwrite; verify refusal vs replacement and Preview's missing collision warning.

## Real-window checks (Claude, 2026-10-18)

Run in isolated Xvfb at 1600×900 on the application built from `f570e3b` (equal to `09f4b2a` apart
from the README).

- **Check 1, F1: confirmed.** Ctrl+N's wizard → *BME 7K (outside scratch)* → *Create project* → Compose
  shows the correct eight lanes (scratch, 1–7) and starting BPM, but *BGM columns* is greyed out at 0,
  the Sounds dock has no *Automatic IDs* / *BMS slots* switch, and the measure rail reads `M1` (native
  numbering) instead of `#000`. Nothing on screen says why the BMS tools are missing.
- The manual's own screenshots of slots, BGM and iBMSC paste were taken on an *imported* BME fixture
  (`docs/manual/fixtures/tutorial/`), which is why the manual work did not hit F1. The BMS walkthrough
  chapter needs F1 resolved (or a stated workaround) before it is accurate from a new project.
- Checks 2–7 were not run in this pass; F2 and F3 rest on the production-path failures quoted above.
