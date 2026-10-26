# User manual and tutorial video

[Back to the task card index](../tasks.md).

User request from 2026-10-05: start a user manual and a tutorial video, placed
between the end of the current ready backlog and the middle of the deferred work
(storyboard, OSBX/Lua, Quaver, rolls and ticked holds).

**Scheduling:** these cards are `blocked-on` a milestone, not a card. The **docs
milestone** is reached when no card from the 2026-10-05 feedback batches
(T-385–T-429) is `ready` or in progress. Then:

1. Write the manual (T-430–T-433).
2. Do about half of the deferred work, as the user names it.
3. Make the tutorial video (T-434, T-435), so it shows a stable UI.

The manual documents what exists. Screenshots and video shots are produced by
scripts so they can be regenerated after UI changes, never by hand.

**Owner follow-up (2026-10-25):** fill the remaining manual gaps. T-432/T-433 also own
`docs/manual/chapters/bms-branches.adoc`, `settings.adoc`, `troubleshooting.adoc`, the
book index/maintaining appendix, `shots/lib/compose.sh`, all existing shot-script/capture refreshes,
`shots/bms-branches.sh` and its image for the new branch-authoring guide,
and `scripts/manual-captures.sh` for current wizard/dock coordinates, isolated fixture copies and
an isolated Zenity picker. T-705's supporting scope includes `cmake/wormedit_manual.cmake`
(package exercise fixtures), `src/ui/nuklear/app/manual_location.h`, `frontend.cpp`, `menu_bar.cpp`,
`src/ui/workspace_menu.h` and their existing backend/menu tests (open the installed book from
Help/About, escape local URL paths, and report an omitted manual). These extensions implement
the requested documentation and discoverability outcome; they do not change chart editing.

**Owner Lua-reference follow-up (2026-10-25):** T-433/T-705 also own `docs/lua-api/`,
`docs/manual/chapters/navigating.adoc` (introduce rows, resolution, beats and snap),
`docs/building.md`, `cmake/dependencies/{lua,lua_source,ldoc}.cmake`,
`cmake/ldoc-host/`, `cmake/wormedit_manual.cmake`, `scripts/check-lua-api-docs.py`,
`tests/support/lua_documentation.h`, `tests/lua/{lua_curve_tests,lua_generator_tests}.cpp`
and `tests/CMakeLists.txt`. This supporting extension supplies pinned LDoc generation,
a host-native runner for cross-builds, packaged offline API docs and executable examples;
it does not extend the runtime API.

## T-430 Set up the manual skeleton and its link check

- **Status:** done (2026-10-18; owner started the manual ahead of the docs milestone)
- **Decision (owner, 2026-10-18):** AsciiDoc, in `docs/manual/` (`index.adoc` plus `chapters/`), built with `asciidoctor docs/manual/index.adoc`. Chapters follow the owner's emphasis instead of the outline below: the hybrid "Wormedit way" first, then getting started, navigation, placing notes, sounds and layers, two walkthroughs (BMS from a new project with BMSE/iBMSC paste; osu!mania and StepMania from a new project), and generated keyboard and layout references. The check is `documentation.manual_links` (`cmake/check_manual_docs.cmake`): includes, images, `<<id>>` anchors and screenshot revision attributes.
- **Outcome:** `docs/manual/` holds an outline and empty chapter files, and a `documentation.manual` CTest checks that every link and image in the manual resolves.
- **Read first:** `cmake/check_chart_editor_docs.cmake` (the link-check pattern), `docs/building.md`, `docs/frontend-event-harness.md`.
- **Files owned:** new `docs/manual/README.md`, new `docs/manual/*.md` chapter stubs, new `cmake/check_manual_docs.cmake`, `CMakeLists.txt` (test registration), this card.
- **Steps:**
  1. **STOP:** ask the user where the manual should be published. The options are plain Markdown on the repository, a static site (for example mdBook), or in-app help. Record the decision on this card.
  2. Write the outline:
     - Getting started (installing, opening and importing, the Project tab);
     - Compose basics (tools, snaps, playback, docks);
     - Keysounding BMS charts;
     - osu!mania and hitsounds;
     - Timing (BPM, tap tempo, beat fit, measures);
     - Exporting and converting;
     - Keyboard reference;
     - Troubleshooting.
  3. Add the link and image check as a CTest.
- **Invariants:** the manual describes only shipped behavior. Planned features are not mentioned.
- **Reject/failure behavior:** a broken link or a missing image fails the test.
- **Verify:** `ctest --test-dir build --output-on-failure -R '^documentation\.'`.
- **Out of scope:** writing chapter content (T-432, T-433).

## T-431 Generate manual screenshots from scripted Xvfb runs

- **Status:** done (2026-10-18)
- **Refresh (2026-10-25):** all 18 shots regenerate successfully at `01ad358`, with no stale revisions. The current family/layout wizard and right dock strip replace obsolete coordinates. New Select/Layer, Layers, sound-import review, Project exports and BMS branch captures are included. Each shot edits a temporary fixture copy; an isolated Zenity picker cannot use the desktop portal. Fixture sources remain unchanged and journal-free. `bash -n`, the full capture run, visual inspection and `--check-stale` pass.
- **Done:** `scripts/manual-captures.sh` runs each `docs/manual/shots/<id>.sh` (a STEPS file with `# mode:` and `# project:` headers) through the smoke script in a 1600×900 window, writes `docs/manual/images/<id>.png`, and stamps the commit (with `+dirty` for uncommitted UI changes) into the PNG comment and `docs/manual/images/revisions.adoc`; captions print it (owner request: stamp shots with a revision). `--check-stale` lists shots whose UI sources changed after their stamp, replacing the pixel comparison planned below. Fixture projects live in `docs/manual/fixtures/`.
- **Outcome:** `scripts/manual-captures.sh` opens fixture projects in isolated Xvfb, drives each shot listed in `docs/manual/shots.txt` with xdotool steps, and writes `docs/manual/images/<shot>.png`. With `--check`, it compares fresh captures against the checked-in ones using `scripts/compare-ui-captures.py`, and lists the shots that drifted.
- **Read first:** `scripts/smoke-compose-virtual-x.sh`, `scripts/compare-ui-captures.py`, `docs/fixtures/`.
- **Files owned:** new `scripts/manual-captures.sh`, new `docs/manual/shots.txt`, new `docs/manual/images/`, this card.
- **Steps:**
  1. Build on the smoke script's STEPS hook. Each shot has an ID, a fixture, a window size, steps and an optional crop.
  2. Fix the window size, font and theme ("Wormedit" scheme) so captures are reproducible.
  3. Add the `--check` mode.
- **Invariants:** captures never open on the user's desktop. They run only inside isolated Xvfb.
- **Reject/failure behavior:** a step that fails names its shot and stops, without overwriting the existing images.
- **Verify:** `scripts/manual-captures.sh --check-stale` on a clean UI tree reports no stale revisions; inspect regenerated PNGs for the intended workflow.
- **Out of scope:** video (T-435).

## T-432 Write the Getting started and Compose basics chapters

- **Status:** partial (2026-10-25; content and captures complete; independent reader trial remains)
- **Done so far:** chapters the-wormedit-way, getting-started, navigating, placing-notes (with Alt-drag lengthening), sounds-and-layers, measures (bar lines as objects in the measure lane; owner emphasis 2026-10-18 on lengthening, page keys with Tab acceleration and bar-line editing); keyboard reference generated from the command registry by `wormedit-manual-reference` (`documentation.manual_reference` keeps it current), with a hand-written table of non-command keys. Gestures were checked against the code, and the keyboard loop, keyboard hold and paste in isolated Xvfb.
- **Done so far (2026-10-25):** Select/Layer tools, the Layers dock and sound-import review now have linked, scripted captures. Tool-location prose matches the current toolbar. The maintaining appendix supplies a reader-trial checklist; all 18 captures are refreshed at `01ad358`.
- **Remaining:** step 4 only: a reader unfamiliar with Wormedit follows the chapters and their confusion is recorded. Maintainer verification and automated tests are not recorded as this independent trial.
- **Outcome:** the Getting started and Compose basics chapters let a new user create a project,
  import a chart, place, select and move notes, change snaps, and play back, using only the manual.
  Every screenshot has a matching reproducible script in `docs/manual/shots/`.
- **Read first:** `docs/manual/index.adoc`, the task cards for the features described (Draw tool, quantize, docks), `docs/chart-editor-plan/editing/interactions.md`.
- **Files owned:** `docs/manual/chapters/the-wormedit-way.adoc`,
  `docs/manual/chapters/getting-started.adoc`, `docs/manual/chapters/navigating.adoc`,
  `docs/manual/chapters/placing-notes.adoc`, `docs/manual/chapters/sounds-and-layers.adoc`,
  `docs/manual/chapters/measures.adoc`, `docs/manual/chapters/keyboard-reference.adoc`, new
  `docs/manual/shots/select-layer-tools.sh`, new `docs/manual/shots/layers-dock.sh`, new
  `docs/manual/shots/sound-import-review.sh`, new `docs/manual/images/select-layer-tools.png`, new
  `docs/manual/images/layers-dock.png`, new `docs/manual/images/sound-import-review.png`,
  `docs/manual/images/revisions.adoc`, this card.
- **Coordination:** while T-716 is active, T-716 exclusively owns its listed corrections to the
  overlapping chapters. T-432 retains only its reader trial and three missing captures.
- **Steps:**
  1. Check each documented gesture in the event harness or the real window before describing it. Record anything that differs on this card, rather than documenting the bug.
  2. Write task-oriented sections ("Place notes", "Move notes"), each with a screenshot and its shortcuts.
  3. Generate the keyboard reference from the command registry, not by hand. Add a small exporter if one is missing.
  4. Ask someone unfamiliar with wormedit to follow the chapter, and record what confused them.
- **Invariants:** shortcut names match the shortcut profile.
- **Reject/failure behavior:** not applicable.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; `scripts/manual-captures.sh --check-stale`.
- **Out of scope:** the workflow chapters (T-433).

## T-433 Write the workflow chapters

- **Status:** done (2026-10-25)
- **Done so far:** the BMS walkthrough (with BMSE/iBMSC paste, wired in T-700) and the osu!mania/StepMania walkthrough.
- **Done so far (2026-10-18, owner's second pass):** timing chapter (lanes, inline editing, Timing focus, tap tempo, beat fitting, offset, lock, the area menu's stop/delay conversion and SV recipes); command palette chapter; importing into specific BMS slots; Ctrl+M toggling (T-701); wide-layout lane keys (T-702) in the generated layout table. Twelve shots captured at `f1dad8d`.
- **Completion (2026-10-25):** Troubleshooting maps actual diagnostics to recovery actions, including BMS read-only/stale drafts, clipboard refusals, missing files, external playback, export, recovery and Linux graphics startup. Both walkthroughs end with exercises using the packaged examples. New BMS branch and settings/external-player chapters explain content ownership versus playback choice, RANDOM alternatives, node edits/confirmations, per-page Apply and immediate shortcut saves. Project output groups/Export and the branch panel have scripted captures; all 18 shots are current. GATE for the associated Help/package implementation passes 5,280/5,280 and four changed-TU complexity checks; the manual builds without Asciidoctor warnings. Documentation checks pass 5/5, manual component and complete DESTDIR-staged installs include all nine fixture files, and rendered chapter checks show no missing images or desktop overflow. An independent reader trial remains exclusively on T-432.
- **Lua-reference follow-up (2026-10-25):** pinned LDoc 1.5.0, Penlight 1.14.0 and LuaFileSystem 1.8.0 generate an offline API reference from `docs/lua-api/`, using the existing pinned Lua source through a separate native build-time runner. Generator snapshots/output, SV contexts/factories, exact positions, MIDI, limits and reproducibility are documented and linked from the manual. Three source examples execute through the real Lua backends (18 assertions / two cases). The manual target and Manual install component include the reference, local styles, example pages and license notices. Native and Windows cross-builds (application Lua disabled in the latter) generate and stage the book; all eight HTML pages pass local-link/anchor/asset checks. A headless rendered module page was inspected. GATE passes 5,283/5,283, whitespace and two changed-TU complexity checks without increased allowances.
- **Outcome:** both walkthroughs end with a short "try it" exercise on a bundled fixture;
  Troubleshooting maps the actual notification text to a recovery step; and the Project output-group
  capture shows Preview and Export where the walkthrough tells the reader to use them.
- **Read first:** `docs/manual/index.adoc`, [BMS editing cards](bms-editing.md), the export cards, and the timing cards.
- **Files owned:** `docs/manual/chapters/walkthrough-bms.adoc`,
  `docs/manual/chapters/walkthrough-osu-stepmania.adoc`, `docs/manual/chapters/timing.adoc`,
  new `docs/manual/chapters/troubleshooting.adoc`, new
  `docs/manual/shots/project-output-groups.sh`, new
  `docs/manual/images/project-output-groups.png`, `docs/manual/images/revisions.adoc`, this card.
- **Coordination:** while T-716 is active, T-716 exclusively owns its listed corrections to the
  three overlapping chapters. T-433 retains only Troubleshooting, the exercises and the missing
  Project output-group capture. T-432, T-433 and T-716 run their captures serially because each
  regenerates `docs/manual/images/revisions.adoc`.
- **Steps:**
  1. Check each workflow in the real window before writing it up.
  2. Add the shots and generate the captures.
  3. Build Troubleshooting from the actual diagnostic messages users see (the notification texts), each with a fix.
- **Invariants:** as in T-432.
- **Reject/failure behavior:** not applicable.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; `scripts/manual-captures.sh --check-stale`.
- **Out of scope:** deferred features (storyboard, OSBX, Quaver) until they ship.

## T-434 Write the tutorial video script and shot list

- **Status:** blocked-on T-433 and the user naming the midpoint of the deferred work
- **Outcome:** `docs/manual/video/script.md` holds an 8–12 minute script: a narration line per shot, on-screen action, duration, and a chapter marker. It covers making a short BMS chart from audio stems through to export.
- **Read first:** the finished manual chapters.
- **Files owned:** new `docs/manual/video/script.md`, new `docs/manual/video/shots.txt`, this card.
- **Steps:**
  1. Propose a chapter outline, and **STOP** for the user's approval.
  2. Write each shot as a reproducible sequence of steps with a fixture, using the format of `docs/manual/shots.txt`.
- **Invariants:** every action on screen is a real, shipped behavior.
- **Reject/failure behavior:** not applicable.
- **Verify:** the user reviews the script.
- **Out of scope:** recording (T-435), and narration and editing, which the user does.

## T-435 Record tutorial shots with a scripted Xvfb pipeline

- **Status:** blocked-on T-434
- **Outcome:** `scripts/record-tutorial-shots.sh` plays each shot from `docs/manual/video/shots.txt` in isolated Xvfb at 1920×1080. It records with `ffmpeg -f x11grab` at 60 fps, with app audio captured from the SDL output, a pointer highlight and an on-screen key display. It writes one lossless clip per shot, ready for editing.
- **Read first:** `scripts/manual-captures.sh`, `scripts/smoke-compose-virtual-x.sh`.
- **Files owned:** new `scripts/record-tutorial-shots.sh`, this card. Recorded clips stay outside the repository.
- **Steps:**
  1. Make xdotool pointer moves eased and human-paced, and add a configurable pause after each action.
  2. Capture audio through a null PulseAudio or PipeWire sink inside the isolated session, and check that audio and video stay in sync on a metronome shot.
  3. Overlay the keys pressed, from the steps themselves rather than a keylogger.
- **Invariants:** nothing is recorded from the user's desktop session.
- **Reject/failure behavior:** a failed step aborts that shot and keeps any earlier clips.
- **Verify:** record two shots and review them with the user.
- **Out of scope:** narration, editing and publishing.
