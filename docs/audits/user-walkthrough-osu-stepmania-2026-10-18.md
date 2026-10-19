# osu!mania / StepMania author walkthrough — 2026-10-18

## Scope

Revision: `09f4b2abb0bb3611959c9cfbcd0e356581d19f7c` (hydra/walkthrough).
Walked `docs/manual/chapters/walkthrough-osu-stepmania.adoc`, sections 1–5, then
`timing.adoc`, `command-palette.adoc`, and the relevant parts of `getting-started.adoc`,
`placing-notes.adoc`, `the-wormedit-way.adoc`, and `layout-reference.adoc`.
No product changes; no window, sound output, OS file picker or external game was exercised.

New file: `tests/ui/walkthrough_osu-stepmania_event_harness_tests.cpp`.
It uses real frontend events for the audio wizard, Metadata confirmation, palette search,
and lane-key taps/holds. Lane-key actions go through `dispatch_compose_input_action` into
real document transactions. It also calls production project construction, chart duplication /
creation, membership commands, native save, conversion preview, and atomic publication for all
three groups, including song-audio copies and multiple-music refusal. The independent cases
are checkpoints, not a simulation of the application loop: tab/mode projection and supplied
stream geometry are fixture setup; shortcut dispatch, timing tools and save dialogs are traced
or covered by existing tests. CMake adds the production defaults and Project-view sources to
the harness target so the tests reuse those implementations.

Verification: new walkthrough **5/5 passed**; relevant frontend regression selection **24/24
passed** (includes those five); documentation **5/5 passed** after building
`wormedit-manual-reference` (initially absent). `clang-tidy -p build` reports only the existing
`compose_speed_spans.h:51` baseline warning; changed-file complexity passes. Whitespace passes.
Logs and exported artifacts stay under `.agent/`:

- `osu-stepmania-final-tests.log`: final combined run, **29/29 passed** (24 frontend + 5 documentation).
- `osu-stepmania-frontend-tests.log`: CTest, `-j2`, new flow + command menu, wizard, Project
  groups/export chooser, Draw refusal, timing marker geometry and Tap-dialog ownership.
- `osu-stepmania-documentation.log`: `ctest --test-dir build -j2 -R '^documentation\.'`.
- `osu-stepmania-complexity.log`, `osu-stepmania-clang-tidy.log`: new C++ file.
- Builds: `cmake --build build -j4 --target wormedit_frontend_event_harness_tests
  wormedit-manual-reference`; no overlapping builds. No other full test suite was run.

## Steps

File references below are relative to the repository root. **Test** means the new walkthrough
case exercises that boundary; **trace** means source reading, not execution.

| Step | Manual promise | UI → system → core / feedback | Result |
|---|---|---|---|
| Drop MP3/Ogg with no project | Ask “Import audio as a new project?”, choose 7K | Drop accepted/staged in `src/sys/application_frame_services.cpp:25`; wizard emits layout/path at `src/ui/nuklear/frontend.cpp:2800`; inspection/construction/asset selection and success message at `src/sys/application.cpp:2481`; music resource and chart extent in `src/sys/application_defaults.cpp:117`. Wizard + construction **test**; OS drop/decoder wiring **trace**. | works |
| Ctrl+N alternative | Create layout, then drop audio on Metadata | Registry `src/core/command_registry.cpp:780`; wizard request `src/sys/application.cpp:3717`; construction `src/sys/application.cpp:3629`. Drop stages a path in `src/sys/metadata_drop_state.cpp:56`, then requires **Use as song audio**, `src/ui/nuklear/metadata_audio_section.cpp:525`. Action commits via `src/sys/compose_resource_actions.cpp:62` → `src/core/resource_assignment.cpp:1888`, reports “Song audio set to …”. Confirmation action **test**; drop/dispatch **trace**. | works with surprise: expand Audio and export, then confirm |
| Initial 7K outputs | osu!mania, SM, SSC include initial chart | Catalog `src/core/lane_profiles.cpp:1141`, 7K descriptor `:1221`, groups `:1307`; document factories `src/sys/application_defaults.cpp:91`. Default paths are directories `osu`, `sm`, `ssc`; StepMania step type is `kb7`, `src/core/lane_profiles.cpp:1120`. **Test**. | works |
| Starting BPM | Double-click origin BPM, type exact decimal, confirm | Rail double-click emits BEGIN / opens inline editor at `src/ui/nuklear/compose_input_panel.cpp:665`; decimal CONFIRM at `src/ui/nuklear/compose_state.cpp:573`; proposal revise/apply and “Timing event updated.” at `src/sys/application.cpp:3766`; positive rational validation `src/core/timing_lane_controller.cpp:266`. **Trace**. | works |
| Tap Timing | Palette, start playback, four taps, round, Apply BPM | Registry `src/core/command_registry.cpp:920`, palette dispatch `src/sys/application.cpp:3671`, runtime toggle/tap `:3734`; T/Space capture `src/ui/nuklear/frontend.cpp:487`; estimate/Apply reasons in `src/ui/nuklear/tap_timing_panel.cpp:84`; proposal applies BPM **and offset**, `src/core/tap_timing_apply.cpp:119`; success `src/sys/application.cpp:1825`. Palette action **test**; tempo/playback/apply **trace**. | works with surprise: Apply also moves audio offset |
| Beat fitting in F3 | Ctrl-drag a beat line onto waveform; Ctrl wheel snaps; Escape cancels | Start is restricted to x=22…42 of the chart viewport, `src/ui/nuklear/compose_viewport_input.cpp:240`; beat picking `:265`, Ctrl/onset flags `:313`; system proposal/anchor/frame bounds `src/sys/compose_waveform_fit_input.cpp:227`; commit `:351` reports “Beat interval fitted to the selected audio frame.” **Trace**. | works with surprise: only the narrow rail starts a fit |
| Lock timing | Ctrl+Shift+T locks timing regardless of editing tool | Registry availability requires Timing focus, `src/sys/application.cpp:892`; handler repeats the restriction and reports “Timing edits locked.”, `src/sys/application_command_dispatch.cpp:853`. Note inputs obey mode at `src/sys/compose_input.cpp:1774`; timing edits receive lock in their proposal paths. **Trace**. | works with surprise: lock shortcut unavailable in F4 |
| Begin charting after fitting | Alt+2 selects Draw | F3 leaves `compose_timing_focus` set; Draw disabled at `src/sys/application.cpp:695`; handler rejects at `src/sys/application_command_dispatch.cpp:971`. No F4 step appears in the walkthrough. **Trace**. | broken as written after the F3 branch |
| Plain notes, lane keys 1–7 | Draw works with nothing armed | Native 7K bindings factory `src/core/lane_profiles.cpp:1180`; frontend lane routing `src/ui/nuklear/compose_keyboard_input.cpp:270`; Draw-only admission `src/sys/compose_input.cpp:1104`; keyboard hold begin/release transaction `src/core/interaction_gesture_lifecycle.cpp:615`. **Test**: actual tap and hold, no sound resource, while timing locked. | works after returning to F4 |
| Snap, cursor, playback/rate | Left/Right snap, Up/Down walk, Space playback, Ctrl+F11/F12 rate | Registry navigation/rate commands `src/core/command_registry.cpp:872`, `:913`; navigation input `src/sys/compose_navigation_input.cpp:1`; transport callbacks `src/sys/application.cpp:4391`. Up and held-lane release **test**; other shortcuts/transport **trace**. | works by trace; audible check needed |
| Mouse corrections | Drag moves, right-drag Draw erases, Alt-drag holds | Ordered pointer capture `src/ui/nuklear/compose_input_panel.cpp:1231`; Draw/hold lifecycle `src/core/interaction_gesture_lifecycle.cpp:398`, paint application `:585`; shared transaction publishes result. Move requires Select per `placing-notes.adoc`; walkthrough does not say to leave Draw. **Trace**. | works with surprise: tool prerequisite omitted |
| Special note kinds | Alt+4…8 when formats support them | Registry derives keys from note-kind catalog `src/core/command_registry.cpp:962`; native project accepts every kind `src/core/interaction.cpp:1672`; unavailable kinds reject with diagnostic `:1692`, rather than “fall back to a tap” as `placing-notes.adoc` says. Native group conversions can warn about losses. **Trace**. | works with surprise; documentation mismatch on rejection |
| Duplicate / New chart | Another difficulty, then add to groups | ⋯ → Duplicate `src/ui/nuklear/setup_panel.cpp:142`; + (“New chart” tooltip) `:400`; UI handler `src/sys/project_view.cpp:114` → `src/core/chart_editor.cpp:203`, `:218`; duplicate preserves contents, New resets tempo/extent. Neither copies membership. **Production-command test**. | works with surprise: New loses fitted tempo |
| Add difficulties to groups | Include chart in each desired output | Row Edit → Included charts → Save group, `src/ui/nuklear/setup_panel.cpp:453`, `:478`; stamps and apply-set-group `src/sys/project_view.cpp:320` → `src/core/chart_editor.cpp:305`, feedback “Output groups updated.” **Command test**; widget path **trace**. | works |
| Save | Ctrl+S saves project; save needed before export | Registry → picker request `src/sys/application.cpp:3725`; filename checks, rebase, native write, mark saved and “Project saved to …” `:2387`; atomic native storage `src/formats/native_project_storage.cpp:2118`. **Native-write test**; picker/session path **trace**. Save does not copy audio; unsaved audio imports already have a nonempty export base. | works with surprise |
| Output paths | Project-relative chart file or folder for multiple osu charts | Group editor `src/ui/nuklear/setup_panel.cpp:492`; core path/membership checks `src/core/chart_editor.cpp:273`; publication path planning `src/formats/conversion_publication.cpp:78`; generated names `src/formats/conversion.cpp:139`. **Test** uses default directories and three difficulties. | works |
| StepMania policy | Choose engine target and timing syntax for SM/SSC | Form emits policy `src/ui/nuklear/setup_panel.cpp:610`; “policy updated” stored in separate view state `src/sys/transport_player_actions.cpp:120`; preview/export request builder ignores it, `src/sys/output_group_export.h:20`. **Trace**. | broken: policy is disconnected |
| Preview | Shows what will be written first | Row `src/ui/nuklear/setup_panel.cpp:528` → async conversion `src/sys/project_view.cpp:276`; only count + warning count displayed, no filenames or warning detail, no publication preflight. **Test** verifies current summary. | broken for absolute linked audio; successful preview shows only counts |
| Export all three | Select group and Export… | Row Export emits group index directly `src/ui/nuklear/setup_panel.cpp:512`; shared Export… opens chooser `:541`. Validation requires the **active chart** in that group, `src/sys/compose_resource_actions.cpp:302`; conversion/publication `src/sys/output_group_export.cpp:127`; success/error posted by `src/sys/resource_job_state.cpp:255`. **Worker test** publishes all three; UI/action gate **trace**. | works with surprise: active-chart restriction |
| Song-audio copy | Audio next to chart | Music planned at `src/sys/output_group_export.cpp:10`, mapped to **assets/** at `:44`, atomically published `:169`; chart references rewritten by conversion. **Test** verifies SM/SSC references and the actual copied audio file for all three. | works with surprise: subfolder, not sibling file |
| Multiple music refusal | More than one music file refused | Counts music **resources**, even two pointing at one file, `src/sys/output_group_export.cpp:17`; error “The output has more than one music resource.”; worker completion posts error notification. **Test**, all three groups, no destination created. | works; wording imprecise |
| Palette in this flow | Search substring, Enter invokes active tab/chart, disabled matches dimmed | UI matches case-insensitively, up to 20, `src/ui/nuklear/command_menu.cpp:34`; captured context `:69`, navigation skips disabled `:51`; app rechecks stamp/tab `src/sys/application.cpp:3671`. New **test** selects production Tap Timing label with “TAP”; other routing **trace**. | works |

## Findings (severity first)

### 1. The walkthrough cannot proceed from F3 timing to Draw — blocks the workflow

Follow section 2's F3 branch, lock timing, then section 3's Alt+2: Draw stays disabled.
`src/sys/application.cpp:695` disables it in Timing focus;
`src/sys/application_command_dispatch.cpp:975` rejects it explicitly. A real shortcut reports
“Command is currently unavailable: edit.tool_draw” (`src/sys/application.cpp:2777`), not the
useful “press F4” remedy. The timing chapter mentions F4, but the walkthrough never uses it.
Conversely, skipping F3 and setting BPM/Tap Timing in ordinary Compose makes Ctrl+Shift+T
unavailable (`src/sys/application.cpp:892`). Smallest fix: add “F3, lock, then F4” to the
walkthrough and state the lock shortcut's focus prerequisite; show that remedy in refusal feedback.

### 2. Exporting a valid group depends on an unrelated active difficulty — blocks the workflow

Create another difficulty, leave it out of an existing group, keep it active, then export that
group. The row button is enabled by nonempty group + selected chart, without membership checking
(`src/ui/nuklear/setup_panel.cpp:534`). The dispatcher refuses “The selected output group has
no matching charts.” (`src/sys/compose_resource_actions.cpp:302`), even though it has charts.
Smallest fix: validate the group's own chart IDs rather than require the active chart in it;
at minimum name the actual restriction and let the user switch to an included chart.

### 3. StepMania export policy is a disconnected form — misleads

Changing Implementation target or Timing syntax says “StepMania export policy updated.”, but
neither Preview nor Export receives that value. `src/sys/transport_player_actions.cpp:126`
assigns the view-owned policy; `src/sys/output_group_export.h:28` creates a fresh policy using
only the group's output extension (`.sm` / `sm` versus everything else). Preview calls this same
builder (`src/sys/project_view.cpp:281`). The “compatible” form status therefore does not
establish compatibility of the exported request; File family can also disagree with the file.
Smallest fix: pass the selected typed policy into both planners, with a clear per-group rule
for file family. Do not keep the manual's promise without wiring it.

### 4. Preview refuses the linked audio that Export successfully publishes — misleads

A newly audio-started project has an absolute song path. Preview calls `convert_document`
directly without staging asset paths (`src/sys/project_view.cpp:282`). The osu writer rejects
it (`src/formats/osu_mania.cpp:2889`); SM/SSC also reject absolute paths
(`src/formats/stepmania.cpp:2307`). Export first maps/copies that same asset to `assets/`
(`src/sys/output_group_export.cpp:29`) and succeeds. Saving retains absolute paths, so it
cannot resolve this refusal. The exploratory walkthrough assertion failed:

```text
CHECK(std::string_view{view.output_groups[0].preview_status} ==
      "Preview: 1 file(s), 0 conversion warning(s).")
actual: "Cannot convert: music resource refers to an unsafe or missing audio asset"
```

The retained passing palette/preview case asserts this observed refusal, then relinks to a safe
relative path and verifies successful preview. The publication case verifies that the absolute
source exports successfully to all three formats. Smallest fix: have preview use the same
nonpublishing asset-reference planning as export; show planned copy paths without writing files.

### 5. Saving does not bring linked song audio into project assets — misleads

`getting-started.adoc` promises “Once the project is saved, Wormedit copies the file into the
project's assets.” Both audio-wizard construction (`src/sys/application_defaults.cpp:118`)
and **Use as song audio** (`src/sys/compose_resource_actions.cpp:67`) link the source path.
Save rebases paths and writes native bytes (`src/sys/application.cpp:2423`); absolute paths are
left untouched (`src/sys/project_save_paths.cpp:22`). There is no automatic copy here. A
portability notification does warn that moving the project may break links
(`src/sys/application.cpp:2064`), but it contradicts the manual's assurance.
Smallest fix: correct the text and describe the explicit project-copy control; alternatively
implement the promised copy as separate future work.

### 6. New difficulty silently starts at 120 BPM and one measure — misleads

After timing the song, **New chart** carries only layout, resolution and audio offset from the
active chart (`src/core/chart_editor.cpp:203`). It creates 120 BPM / one standard measure
(`:168`), discarding fitted BPM changes and anchors. Duplicate preserves them (`:224`). The
success message is merely “New chart created.” (`src/sys/application.cpp:2004`).
Smallest fix: explain this choice in the walkthrough and recommend Duplicate then clearing notes
when timing should be shared; decide separately whether New should template song timing.

### 7. Metadata audio drop needs a second action — misleads

Dropping the song only stages a path (`src/sys/metadata_drop_state.cpp:98`). The visible
**Use as song audio** button actually emits import (`src/ui/nuklear/metadata_audio_section.cpp:531`).
The audio field is inside the initially collapsed **Audio and export** section
(`src/ui/nuklear/metadata_audio_section.cpp:778`). The new wizard/Metadata test expands it
and covers the required confirmation. Without it there is no music to
play or tap. Smallest fix: add that exact button click to both manual locations describing song drops.

### 8. Beat fitting starts only on a narrow rail — misleads

The manual says to drag “a beat line of the grid”; the frontend only accepts a press in chart
x=22…42 (`src/ui/nuklear/compose_viewport_input.cpp:245`), within six screen pixels of a valid
beat (`:267`). Grabbing the same line elsewhere on the waveform does not start a fit and gives
no beat-fit refusal. Smallest fix: identify the rail and exact grab location in the instructions
and its hover text, or broaden the hit target to match the described workflow.

### 9. Preview omits the information needed to review export — polish

The complete success view is “Preview: N file(s), M conversion warning(s).”
(`src/sys/project_view.cpp:288`; new palette/preview case asserts it). No paths, filenames,
warning descriptions, audio copies, existing-file conflicts or active-chart eligibility appear.
Thus “Preview shows what will be written first” overstates it. Smallest fix: show the planned
output names and conversion diagnostics, and label publication checks separately.

### 10. “Save required” is not enforced for an audio-started project — misleads

The audio wizard installs the song directory as `asset_base` before any native project exists
(`src/sys/application.cpp:2498`). Export only checks whether that base is empty
(`src/sys/compose_resource_actions.cpp:283`), so it can publish under the song folder while the
manual says a saved `.wormedit` is required. The nonempty-base path is also the resources input
used by dispatch. Smallest fix: gate on native-project save identity, or document this actual
base-directory behavior and show the resolved destination before publishing.

### 11. Smaller feedback/documentation discrepancies — polish

- Tap **Apply BPM** also changes audio offset (`src/core/tap_timing_apply.cpp:125`), with success
  “Applied initial BPM and audio offset.” (`src/sys/application.cpp:1828`). The tooltip says first
  tap sets the downbeat, but `timing.adoc` never explains this side effect.
- `placing-notes.adoc` says unsupported note kinds fall back to tap. Explicit arming instead
  rejects “Note kind is unavailable for this chart profile.” (`src/core/interaction.cpp:1692`);
  fallback happens during rebind/action preparation (`:552`, `:2184`), a different operation.
- The walkthrough says “drag to move” while still in Draw. The referenced tool chapter correctly
  assigns moving to Select. Add **Alt+1** before moving and **Alt+2** before Draw erasing.
- Audio goes into a chart-relative `assets/` subdirectory, not alongside the chart as a sibling.
  The multiple-music limitation counts resources rather than distinct files; the refusal test
  demonstrates that two music resources referring to the same asset are refused.

Smallest fixes for finding 11: document the offset side effect, distinguish rejection from
rebind fallback, name the tool transitions, and describe the assets subfolder/resource limit.

These findings are source traces unless a test is identified. The failed preview assertion above was run and removed; retained tests verify observed behavior.
Wizard probes also required scrolling the layout list and expanding the collapsed audio section;
those were test-navigation corrections, not evidence of a broken control.

## Manual corrections

Proposed wording describes current behavior; it does not adjudicate intended product changes.

- `walkthrough-osu-stepmania.adoc`, §1: replace “drop the song onto the audio field … afterwards”
  with “drop the song onto the audio field … afterwards, expand **Audio and export**, then click **Use as song audio**.”
- §2, after tempo choices: “Press **F3** if necessary, then **Ctrl+Shift+T** to lock timing.
  Press **F4** to return to Compose before charting.”
- §2 / `timing.adoc`, Beat fitting: replace the generic grab instruction with “In Timing focus,
  grab the beat line in the narrow rail immediately left of the timing lanes and drag vertically
  until it aligns with the waveform hit.” Confirm the visual rail label in the real window.
- §3 mouse fix: “Press **Alt+1** to drag notes to move them. Press **Alt+2** for Draw right-drag
  erasing. **Alt**-drag on an empty lane draws a hold.”
- §4: “Open the chart's **⋯** menu for **Duplicate**; **+** (**New chart**) creates a chart with
  120 BPM and one measure, carrying over its layout and audio offset. Duplicate keeps timing.”
- §5: replace “Select a group and click Export…” with “Use a row's **Export** button, or
  **Export…** to choose a group and overwrite policy. Currently an included chart must be active.”
- §5 policy statement: currently false; do not promise target/syntax control until it is wired.
  Current behavior: “SM/SSC file family is inferred from the group's output path. The policy
  form's target and syntax choices currently do not reach Preview or Export.”
- §5 Preview: “Preview reports the converted file count and conversion-warning count; it does
  not list files or perform publication checks.”
- §5 final paragraph: “Exports copy the song audio into **assets/** under the chart directory
  and reference that copy. More than one music resource is refused, even if resources share a file.”
- `getting-started.adoc`, Song audio: replace the automatic-save-copy assurance with “Expand **Audio and export**, then click
  **Use as song audio** after dropping. This links the original file. Saving the project keeps
  that link; use the explicit project-copy control to keep a copy in project assets.”
- `timing.adoc`, Tap tempo: add “Apply changes both the initial BPM and the song's audio offset,
  using the tapped downbeat. Later BPM changes keep their musical positions.”
- `timing.adoc`, Locking: add “The lock shortcut is available in Timing focus (**F3**).
  The resulting lock remains effective after returning to Compose (**F4**).”
- `placing-notes.adoc`, Note kinds: replace “choosing one the format cannot store falls back to
  a tap” with “unavailable kinds are disabled; an explicit unsupported request is refused.”
- “exporting needs a saved project” in §5 / `getting-started.adoc` conflicts with the audio-start
  path. Resolve finding 10 before asserting that invariant.

## Needs a real-window check

1. Launch with no project. Drop `tests/fixtures/audio/tagged-project.ogg`; open the layout list,
   choose **7K**, create. Repeat with Ctrl+N → 7K → F2 → **Audio and export** → song drop →
   **Use as song audio**.
   Check field hit targets, confirmation visibility, title/artist prefilling and audible playback.
2. Double-click the origin BPM at the bottom of its lane; enter `174.375`, Enter, reopen and
   Escape. Try `0` and malformed text; verify the exact value survives and errors stay readable.
3. Ctrl+Shift+P → `tap` → Enter; start playback with the transport button; tap T at least four
   times; try rounding, Apply and locked Apply. Check focus remains usable, tooltip/refusal text
   is visible, and the offset change matches the displayed downbeat.
4. F3; show waveform; Ctrl-drag a beat at waveform center (should not acquire), then repeat in
   the x=22…42 rail. Try Ctrl-wheel, Ctrl+Alt onset snapping and Escape. Check rail affordance,
   audio alignment, preview labels and lock behavior.
5. In F3 lock timing; try Alt+2 before F4, then F4 → Alt+2 → 1…7 with Up/Down holds.
   Check unavailable-command feedback, snap status, soundless notes, right-drag erasing,
   Alt-drag holds, and Select dragging. Space and Ctrl+F11/F12 should audibly affect playback.
6. On F1 open **⋯ → Duplicate**, then **+**; inspect the selected chart, default BPM/length and
   group membership. Fit a non-120 BPM before creating New to expose the reset.
7. Save outside the song's directory; inspect disk for retained external audio links. Edit each
   group's output path and membership. With an excluded difficulty active, Preview then Export
   an otherwise valid group; verify the exact refusal and recovery by activating an included chart.
8. Choose SM/Etterna/legacy timing in **StepMania export policy**, Preview/export SM and SSC,
   compare output policy/format and visible status. Repeat at 1280×720 to check policy/group
   editor scrolling, long paths and clipped labels.
9. Export all three groups, inspect generated files and **assets/** references; repeat to trigger
   overwrite refusal, then use Export… with overwrite enabled. Test a genuinely multi-music
   imported project and missing audio; verify errors and no partial chart/audio publication.
10. Before saving an audio-started project, export a default group and inspect the song folder
    for output. Confirm the chosen location and save prerequisite are communicated accurately.

## Real-window checks (Claude, 2026-10-18)

Run in isolated Xvfb at 1600×900 on the application built from `f570e3b` (equal to `09f4b2a` apart
from the README), with the manual's tutorial project. These confirm one finding above and add two
that the source trace and harness did not catch, both left over from retiring the Timing tab.

- **Check 5, finding 1: confirmed.** Entering Timing focus with the toolbar's timer button and
  pressing Alt+2 shows "Command is currently unavailable: edit.tool_draw" in the status bar. The
  message names the command ID and says nothing about F4.
- **New, blocks the workflow: F3 does nothing.** Pressing F3 in Compose leaves Timing focus off
  (toolbar timer not highlighted, hint bar unchanged, no status message). The handler would set it
  (`src/sys/application_command_dispatch.cpp:814`), and the registry enables the command
  (`src/sys/application.cpp:880`), but `workspace.open_timing` is registered with destination tab
  `workspace_tab_t::timing` (`src/core/command_registry.cpp`, the F1–F7 loop over
  `workspace_tabs`), and `is_eligible` runs a workspace-scoped tab command only when its
  destination tab is visible (`src/core/command_registry.cpp:204`). The Timing tab is hidden, so
  the shortcut is silently ineligible. `scripts/smoke-compose-virtual-x.sh`'s `timing` mode
  presses F3 and therefore captures ordinary Compose: the smoke check passes without reaching
  Timing focus. Every manual instruction that says "F3" fails as written.
- **New, misleads: Timing Settings cannot be opened from Compose.** The command palette lists
  *Timing Settings* as enabled; Enter closes the palette and nothing appears. The command toggles
  `timing_settings_visible` (`src/sys/application_command_dispatch.cpp:805`) and is registered with
  tab `workspace_tab_t::timing`; no Compose surface was seen to show the panel. The manual sends users
  there for timing lane visibility and the metronome.

Smallest fixes: register `workspace.open_timing` against Compose (or exempt the alias from the
destination-visibility rule) and make the smoke `timing` mode assert Timing focus; give Timing
Settings a Compose surface or retarget the command; word refusals in Timing focus as "Press F4 to
return to Compose" instead of the command ID.
