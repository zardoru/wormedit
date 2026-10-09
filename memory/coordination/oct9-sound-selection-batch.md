# Select by sound and BMSON stem selection — 2026-10-09

Cards: [T-426](../../docs/chart-editor-plan/tasks/bms-editing.md#t-426-select-all-with-the-same-sound-as-in-ubmsc-and-pbmsc),
[T-449](../../docs/chart-editor-plan/tasks/bmson-bga-editing.md#t-449-select-same-sound-on-bmson-selects-the-whole-stem).

## Behavior

The user approved assigning distinct meanings to the combined modifiers after the
mandatory conflict audit. Primary+Shift+Click toggles matching visible objects;
Primary+Alt+Click toggles matching objects throughout the chart. Primary is Ctrl
on other platforms and Cmd on macOS. Plain Shift/Alt gestures, empty-space
operations and an active beat-fit gesture retain their existing routes.

The remappable `selection.select_same_sound_chart` command defaults to
Primary+Shift+A and adds matches. Its source is the current hovered object,
then the selection's sounds, then selected Sounds dock rows. Hover and dock
context are stamped with chart and revision. A hovered silent object has no
sound source. Missing sources disable the Edit menu command and show
“Hover a note or select a sound first”.

The pure selector returns typed note/BGM IDs. Visible scope tests the entire
hold span against the viewport and respects lane roles, semantic visibility,
required/excluded layer filters and solo layers. Whole-chart scope includes
matching offscreen/hidden objects. Existing BMS identity labels select by
resource; the existing file-stem label presentation selects by audio asset.
No new label preference was introduced.

BMSON keys name retained sound channels, resolved through each semantic object's
`sound_channel_event`, so different slices remain one stem and channels sharing
an asset remain distinct. Stem cut lines resolve to their semantic source.
Sounds rows reuse `bmson_channel_resource_bindings`; ambiguous resource reuse
selects nothing rather than guessing a channel. Channel/resource indexes and
one-pass selected-object resolution avoid repeated
full-chart scans for each candidate or selected object.

Note menus offer visible and whole-chart selection; the Sounds row menu offers
whole-chart selection. Menus add matches. All paths dispatch selection actions;
none changes the document or adds an Undo entry.

## Verification

Final normal application build passes. Full regression suite: **2,949/2,949**, 56.08 seconds.
Focused selector/registry tests: **4 cases / 30 assertions**.
Dedicated event label: **270/270** pass, 7.28 seconds.
Final selection-filter suite: **221/221** pass, 4.84 seconds.
Focused new event tests: **5 cases / 80 assertions**, including two click scopes,
toggle twice, note menus, BGM markers, stem cuts and keyboard source payloads.
Production application command dispatch: **1 case / 10 assertions** verifies
hover/selection/dock priority, disabled missing source and no Undo.

The first full run passed 2,948 of 2,949 cases; the new registry test omitted the
visible Compose tab from its synthetic focus. Its fixture is corrected to match
the real workspace focus, then rebuilt and rerun below. No product change was
needed for that test correction.

Final `documentation.chart_editor_state` and `git diff --check` pass.
Configured clang-tidy checks cover affected source and test units. New helpers
stay within 25; existing source findings are compared with HEAD, including
`refresh_application_command_states` (27), `main` (27),
`compose_surface_height` (26), `update_bmson_bgm_move` (29), and the shared
`build_speed_graph` header (29). No suppressions were added.

An ordinary application-object probe renders the real frontend into an SDL
software surface. Its reviewed menu capture shows the two selection entries and
matching holds/BGM selected together; it checks three returned objects and an
empty Undo history. No ordinary application object is replaced. This is software
rendering evidence; native display/cadence remains unmeasured because isolated
Xvfb listening sockets were unavailable in this session.

Evidence logs: `/tmp/wormedit-sound-build-verify.log`,
`/tmp/wormedit-sound-select-final.log`, `/tmp/wormedit-sound-event-label.log`,
`/tmp/wormedit-sound-events-final.log`, `/tmp/wormedit-sound-core-final.log`,
`/tmp/wormedit-sound-ctest-pass.log`, `/tmp/wormedit-sound-tidy-final.log`,
`/tmp/wormedit-sound-tidy-regression.log`,
`/tmp/wormedit-sound-tidy-core-final.log`,
`/tmp/wormedit-sound-tidy-final-selector.log`,
`/tmp/wormedit-sound-tidy-baseline.log`,
`/tmp/wormedit-sound-capture-menu.log`, `/tmp/wormedit-sound-docs-final.log`.
Capture and reproducible probe:
`/home/zardoru/devel/wormedit-scratch-builds/oct9-sound/captures/menu.png`,
`bgm-probe.cpp`, `link-probe.py` in the same directory.

Existing other worktrees are preserved.

Actual verification output tails:

```text
$ CCACHE_DIR=/tmp/wormedit-batch-ccache cmake --build build -j4
[ 88%] Built target wormedit
[100%] Built target wormedit_core_tests

$ ctest --test-dir build --output-on-failure -R select
100% tests passed out of 221
Total Test time (real) =   4.84 sec

$ ctest --test-dir build -L event-harness
100% tests passed out of 270
Total Test time (real) =   7.28 sec

$ ctest --test-dir build --output-on-failure -j4
100% tests passed out of 2949
Total Test time (real) =  56.08 sec
```
