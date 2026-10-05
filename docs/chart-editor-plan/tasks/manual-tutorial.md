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

## T-430 Set up the manual skeleton and its link check

- **Status:** blocked-on the docs milestone
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

- **Status:** blocked-on T-430
- **Outcome:** `scripts/manual-captures.sh` opens fixture projects in isolated Xvfb, drives each shot listed in `docs/manual/shots.txt` with xdotool steps, and writes `docs/manual/images/<shot>.png`. With `--check`, it compares fresh captures against the checked-in ones using `scripts/compare-ui-captures.py`, and lists the shots that drifted.
- **Read first:** `scripts/smoke-compose-virtual-x.sh`, `scripts/compare-ui-captures.py`, `docs/fixtures/`.
- **Files owned:** new `scripts/manual-captures.sh`, new `docs/manual/shots.txt`, new `docs/manual/images/`, this card.
- **Steps:**
  1. Build on the smoke script's STEPS hook. Each shot has an ID, a fixture, a window size, steps and an optional crop.
  2. Fix the window size, font and theme ("Wormedit" scheme) so captures are reproducible.
  3. Add the `--check` mode.
- **Invariants:** captures never open on the user's desktop. They run only inside isolated Xvfb.
- **Reject/failure behavior:** a step that fails names its shot and stops, without overwriting the existing images.
- **Verify:** `scripts/manual-captures.sh --check` on a clean tree reports no drift.
- **Out of scope:** video (T-435).

## T-432 Write the Getting started and Compose basics chapters

- **Status:** blocked-on T-431
- **Outcome:** the Getting started and Compose basics chapters let a new user create a project, import a chart, place, select and move notes, change snaps, and play back, using only the manual. Every screenshot comes from `shots.txt`.
- **Read first:** `docs/manual/README.md`, the task cards for the features described (Draw tool, quantize, docks), `docs/chart-editor-plan/editing/interactions.md`.
- **Files owned:** `docs/manual/getting-started.md`, `docs/manual/compose-basics.md`, `docs/manual/keyboard.md`, `docs/manual/shots.txt`, `docs/manual/images/`, this card.
- **Steps:**
  1. Check each documented gesture in the event harness or the real window before describing it. Record anything that differs on this card, rather than documenting the bug.
  2. Write task-oriented sections ("Place notes", "Move notes"), each with a screenshot and its shortcuts.
  3. Generate the keyboard reference from the command registry, not by hand. Add a small exporter if one is missing.
  4. Ask someone unfamiliar with wormedit to follow the chapter, and record what confused them.
- **Invariants:** shortcut names match the shortcut profile.
- **Reject/failure behavior:** not applicable.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; `scripts/manual-captures.sh --check`.
- **Out of scope:** the workflow chapters (T-433).

## T-433 Write the workflow chapters

- **Status:** blocked-on T-432
- **Outcome:** the remaining chapters are written: Keysounding BMS charts (Sounds dock, groups, armed sounds with lane keys, move-and-deselect, Move to BGM, select same sound), osu!mania and hitsounds, Timing, Exporting and converting, and Troubleshooting. Each one ends with a short "try it" exercise on a bundled fixture.
- **Read first:** `docs/manual/README.md`, [BMS editing cards](bms-editing.md), the export cards, and the timing cards.
- **Files owned:** `docs/manual/keysounding.md`, `docs/manual/osu-mania.md`, `docs/manual/timing.md`, `docs/manual/export.md`, `docs/manual/troubleshooting.md`, `docs/manual/shots.txt`, `docs/manual/images/`, this card.
- **Steps:**
  1. Check each workflow in the real window before writing it up.
  2. Add the shots and generate the captures.
  3. Build Troubleshooting from the actual diagnostic messages users see (the notification texts), each with a fix.
- **Invariants:** as in T-432.
- **Reject/failure behavior:** not applicable.
- **Verify:** `ctest --test-dir build -R '^documentation\.'`; `scripts/manual-captures.sh --check`.
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
