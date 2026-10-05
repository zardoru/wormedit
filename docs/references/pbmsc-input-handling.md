# uBMSC/pBMSC input-handling notes

Pinned sources. Cite these exact revisions; re-pin deliberately when reading a newer upstream:

- **pBMSC** (uBMSC fork with quality-of-life shortcuts): [Shimi9999/pBMSC @ `06aaf74`](https://github.com/Shimi9999/pBMSC/tree/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28), the repository HEAD on 2026-10-05.
  - [`MainWindow.vb`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/MainWindow.vb): keybinding definitions.
  - [`PanelEvents.vb`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/PanelEvents.vb): input dispatch and editing commands.
  - [`PanelKeyStates.vb`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/PanelKeyStates.vb): modifier helpers.
  - [`PanelDraw.vb`](https://github.com/Shimi9999/pBMSC/blob/06aaf7438dbca3c8a0611bd13d134f6d1ce04a28/iBMSC/PanelDraw.vb): `IsNoteVisible`.
- **uBMSC / iBMSC** (the project owner's fork): [zardoru/iBMSC](https://github.com/zardoru/iBMSC), latest tag [`uBMSC-3.4.0.6` @ `971b0b6`](https://github.com/zardoru/iBMSC/tree/971b0b62a119d290855e2e3a695cd0323f4e47be) and HEAD `f5b21d1` on 2026-10-05. Its [release notes](https://github.com/zardoru/iBMSC/releases) describe the Multiselect function.

First inspected for planning on 2026-09-12, then re-pinned on 2026-10-05.

These files are implementation references, not code to copy. Wormedit needs cross-platform SDL input, exact rational positions, atomic commands, and deterministic tests.

## Observed behavior worth retaining

- Key combinations are built from Ctrl, Shift, Alt, and the pressed key, then resolved by editing category.
- For playable/BGM lane-assignment categories, Shift is removed before binding lookup. It changes how the selected-note command behaves rather than requiring a second binding for every lane.
- A normal lane assignment moves all selected sound notes to the target column.
- Shift + lane assignment moves the first selected sound note, deselects and previews it, then stops. The rest of the selection remains available for successive assignment.
- The fork defines number-row bindings for 1P lanes, QWERTY and numpad alternatives for 2P lanes, and user-rebindable note-lane commands.
- Its Up/Down commands move selected notes by the current grid interval (`192 / gDivide`), with Ctrl selecting a one-unit movement. Its Left/Right commands move selected notes across enabled columns.
- Modifier helpers distinguish combinations such as Shift without Ctrl, Ctrl without Shift, Ctrl+Shift, and Ctrl+Alt.

## Wormedit adaptation

Wormedit keeps the lane-assignment sequencing behavior requested by the project, including first-note Shift assignment and deselection. Arrow keys follow the separately requested StepMania-style cursor/snap controls: Up/Down navigate the edit cursor by the current exact snap and Left/Right cycle supported snap modes. If direct selected-note nudging is later added, it receives separate remappable commands so the two behaviors are not contextually surprising.

The dispatcher should resolve commands from an immutable key event containing physical key, modifier mask, repeat flag, focus context, active tool, current selection summary, and lane profile. It should return a command or navigation result that can be tested without a window.

## Select all with the same label (multiselect)

Observed at pBMSC `06aaf74` (`PanelEvents.vb` `SelectAllWithHoveredNoteLabel`, `IsLabelMatch`, the note mouse-down branch; `PanelKeyStates.vb`):

- **Label match:** with Show File Name on, two notes match when their WAV indices resolve to the same file name. Otherwise, they match when they have the same WAV index (`Value`).
- **Ctrl+Shift+Click on a note** (`ModifierMultiselectVisibleActive`): *toggles* the selection of every note that matches the clicked note and is visible (`IsNoteVisible`). A long note counts as visible if it intersects the viewport. Every column counts, BGM included.
- **Ctrl+Alt+Click on a note** (`ModifierMultiselectNoteActive`): *toggles* every matching note in the whole chart, limited to sound columns (`IsColumnSound`).
- **Ctrl+Shift+A** (keybinding 118, "Select All with Hovered Note Label"): *adds* every matching note in the whole chart, using the note under the mouse. It never deselects, and does nothing when no note is hovered.
- The uBMSC release notes describe the same function: "Ctrl+Shift+Click when hovering over a note will select all notes with the displayed label that are on screen, where Ctrl+Shift+A selects all notes with the label no matter if they are on screen."

Wormedit adaptation ([T-426](../chart-editor-plan/tasks/bms-editing.md#t-426-select-all-with-the-same-sound-in-the-visible-area-or-the-whole-chart)): keep the three gestures and their toggle-versus-add semantics. A label is a sound: the same resource, or the same audio asset when note labels show file names. Notes and BGM events are both included.
