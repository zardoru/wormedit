# uBMSC/pBMSC input-handling notes

Source inspected for planning on 2026-09-12:

- [pBMSC repository and keyboard-shortcut documentation](https://github.com/Shimi9999/pBMSC)
- [`MainWindow.vb` keybinding definitions](https://github.com/Shimi9999/pBMSC/blob/master/iBMSC/MainWindow.vb)
- [`PanelEvents.vb` input dispatch and editing commands](https://github.com/Shimi9999/pBMSC/blob/master/iBMSC/PanelEvents.vb)
- [`PanelKeyStates.vb` modifier helpers](https://github.com/Shimi9999/pBMSC/blob/master/iBMSC/PanelKeyStates.vb)

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
