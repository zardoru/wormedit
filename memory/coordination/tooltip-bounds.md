# T-377 shared tooltip viewport bounds

## Reproduced cause and change

The Sounds clear-selection button called Nuklear's native `nk_tooltip`, which
places a tooltip beside the pointer without checking the logical app viewport.
A true Nuklear draw-command baseline reproduces right-edge overflow with the
same short “Clear the selection” text. The existing custom timing helper only
clamped horizontally to its parent dock. Its first attempted migration also
clipped a long timing label paired with shorter instructions; a real command
regression caught that and now passes.

The shared tooltip helper measures natural text width, wraps at spaces or UTF-8
codepoint boundaries, preserves explicit newlines, computes row spacing and
popup/menu/text padding and borders, and chooses its full placement before any
commands are emitted. Short hints remain compact. It uses logical viewport
dimensions set before frontend rendering, including error/landing routes; the
direct notification-toast render entry also sets its known viewport.

All native tooltip callers and custom chrome timing/notification detail paths
use the shared helper. Hover activation and input routing remain at their
original producers. Existing GPU Compose note/timing/BGM tooltip drawing remains
unchanged: those rectangles already clamp to the Compose viewport. Their
existing fixed-height long-label clipping is outside this edge-overflow repair.

## Reviewed verification

- `/tmp/tooltip-bounds-review/build-geometry.py --baseline` builds the unchanged
  native tooltip call with production Nuklear configuration. Its corner case
  fails the actual drawn right-bound check, as expected.
- Final geometry and actual Sounds clear-selection hover producer pass:
  7 cases / 240 assertions. Full text glyph widths are checked against emitted
  text rectangles, and actual popup border commands are checked against the
  viewport. Tests cover four corners, resize, long unbroken words, newlines,
  UTF-8, nonzero viewport origins, unusual padding, compact short hints, and
  long timing with shorter instructions.
  `/tmp/tooltip-bounds-review/actual-hover-tests.log`.
- Actual production frontend, isolated processes as normal CTest discovery:
  140/140 windowless event cases pass.
  `/tmp/tooltip-bounds-review/isolated-events.log`.
- Compile/link verification recompiles all 55 affected/new objects selected by
  changed sources and recorded dependencies on changed chrome/harness headers.
  Unchanged playback-worktree objects, Catch/SDL libraries, and pinned dependency
  sources are reused read-only. All new outputs live under
  `/tmp/tooltip-bounds-review/frontend`; no external dependency binary directory
  is built or modified. Reproducer: `build-frontend.py` in that same review dir.
- `clang-tidy -p build` across all 14 changed C++ sources/tests exits 0. No new
  cognitive-complexity violations; only unchanged Catch macro cases at original
  harness lines 1294 (32) and 1606 (44) remain. Source migrations replace calls
  without adding control flow; timing/notification helpers simplify control flow.
  `/tmp/tooltip-bounds-review/tidy-final.log`.

An aggregate single-process harness invocation exposed seven existing menu-state
interactions; the accepted gate is the normal isolated-case CTest model used by
the repository, where all 140 pass. Do not claim that aggregate invocation passed.

Root alone integrates the change and verifies the full app build and full suite.
Real-window visual verification remains pending because the prior Xvfb request
was automatically rejected by sandbox approval; no retry or desktop window was
attempted.

## Final main verification

Root integrated `34fdf42`, preserving both the playback-tail and tooltip tests.
The main application builds, and the independent 32-case focused run passes,
including actual Sounds hover and all six geometry cases. With T-378 integrated
as `f17d4f4`, the full suite passes 2,255/2,255, including 144 isolated
event-harness cases. Logs: `/tmp/tooltip-bounds-review/main-build.log`,
`main-focused.log`, and `/tmp/hidden-history-validation/main-suite.log`.
The real-window gate remains pending under the established approval block.
