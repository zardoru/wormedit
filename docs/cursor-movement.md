# Compose cursor movement

The yellow edit cursor stays at its screen anchor. Chart position, snap and the
fractional row beneath that line are separate from its screen position. Pointer
placement changes notes without moving the cursor or camera. Keyboard navigation
and seeks change the chart position by moving the field beneath the line.

## Shared implementation

`include/wormedit/core/interaction_cursor_pose.h` owns cursor mutations:

- `place_cursor_at` and `place_cursor_on_picked_row` move the logical authored row
  and discard stale fractional offsets. Neither moves the screen anchor.
- `place_cursor_on_canvas_row` resolves authored or virtual rows and their offset
  together; virtual BMS rows do not materialize measures.
- `capture_cursor_pose` / `restore_cursor_pose` include position, virtual row,
  fractional offset, scroll, screen anchor and scroll direction.
- `set_viewport_scroll_pixels`, `set_cursor_scale`, `set_cursor_subrow`, `set_cursor_screen_anchor`
  and `configure_cursor_viewport` own geometry writes. Resize preserves the
  source-order anchor fraction and the chart position under it.
- `editor_cursor_screen_anchor` defines the default 20 percent margin from the
  chart-start edge. `editor_cursor_screen_y_pixels` is the rendering policy:
  the fixed screen anchor takes precedence over the logical edit row.

`src/core/interaction_cursor.cpp` implements
`synchronize_editor_cursor_to_viewport`: resolve the snap nearest the screen
line after wheel scrolling, pointer zoom or viewport geometry changes, retaining
the fractional offset and choosing a lane valid at that position.

## Traced movement paths

Alt+scroll moves to the next snap strictly above or below the fractional cursor
position. One wheel burst produces one step; 150 ms without wheel events, a zero
delta, or releasing Alt rearms it. Ctrl+scroll retains its zoom binding.

Ctrl+Space starts a latched range one snap wide. Its tail follows cursor
navigation and scrolling until Ctrl+Space commits it or Escape cancels it.
Releasing the keys leaves the range active. Space starts playback from the
fractional row beneath the cursor line.

| Trigger | Path and policy |
| --- | --- |
| Wheel / touchpad / toolbar scroll | Compose viewport routing dispatches `scroll_editor_view_action_t`; core scroll synchronizes the logical row beneath the anchor, including keyboard hold previews. |
| Arrow / page / measure / bookmark navigation | `interaction_cursor.cpp` resolves the target with the shared placement helpers; `keep_cursor_visible` scrolls the field to the stationary anchor. |
| Keyboard lane keys and holds | Core interaction actions use the same authored/virtual placement helpers; keyboard hold previews extend as navigation or scroll changes their logical row. |
| Pointer note / sound placement | `compose_input.cpp` captures and restores the complete cursor pose around placement. Gesture previews can use temporary logical positions without moving the yellow line. |
| Pointer selection, area ranges and paste | Core actions and Compose area helpers use logical placement or a temporary pose. They never clear or relocate the screen anchor. |
| Pointer / keyboard zoom | Core zoom changes scale and scroll atomically, then synchronizes the edit row beneath the fixed line. |
| Seek / audio follow | `follow_editor_playhead` updates logical row, scroll and fractional offset through the shared helpers. Inline playback captures the fixed source-order anchor and restores the complete pose on Escape. |
| Resize / scroll-direction preference | Compose projection calls `configure_cursor_viewport`, then synchronizes idle state against the new geometry. |
| Undo / document replacement | `recover_editor_interaction` recovers a valid authored position and retains viewport and screen anchor through the same setters. |
| Retained scene / fresh scene | `editor_scene.cpp` selects the visible lane beneath the screen anchor and draws with `editor_cursor_screen_y_pixels`; off-screen logical edit positions cannot hide or relocate the line. |

The source guard in `tests/core/cursor_write_guard_tests.cpp` scans all production
source, including core. Raw writes to cursor position, virtual row, offset,
viewport scroll/height/scale, anchor or direction fail the test. Cursor state helpers
are the only permitted write boundary; view projections remain plain data.

## Verification

The application build and all 1,810 CTest tests passed. Cursor regressions cover
placement, scrolling, zoom, paused seeks, resize, both scroll directions and
temporary pose restoration. The source guard also checks individual row and
measure writes. Clang-tidy checks found no new functions exceeding the complexity
limit; existing unrelated warnings remain.

An isolated Xvfb check of the real window measured the yellow line at pixel row
725 before placement, after placement, after scrolling and after pointer zoom.
