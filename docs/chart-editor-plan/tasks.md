# Plan domain: executable task cards

[Back to the chart editor plan index](../chart-editor-plan.md).

Task cards turn the stage roadmap into units a weaker or lower-effort model can
finish without design judgement. The [stages](stages.md) say *what* the product
needs, and the [resume checklist](../../memory/chart-editor-status/resume.md)
says *which card is next*. A card says exactly *how to do one slice and prove it*.

## Card groups

- [Compose panel split: behavior-neutral extraction of `compose_panel.cpp`](tasks/compose-split.md)
- [Compose features: inspector, BGM pane, virtual canvas, sync controls](tasks/compose-features.md)
- [Interop evidence: clipboard capture](tasks/interop-evidence.md)
- [Frontend shell: input plumbing, panel splits, transport, Compare/Validate](tasks/frontend-shell.md)
- [Editing commands: disabled registry commands, movement, quantize, clipboard, shortcuts](tasks/editing-commands.md)
- [Measures and timing: measure/layout editing, timing navigation, beat fitting](tasks/measures-timing.md)
- [BMS branches, channel maps, and BGM columns](tasks/bms-branches.md)
- [Resources and audio: catalog safety, usage, slices, preview audio export](tasks/resources-audio.md)
- [Export and conversion: preview/publish UI, loss gates, blocked profiles](tasks/export.md)
- [Persistence and metadata: recovery journal, migrations, metadata forms](tasks/persistence-metadata.md)
- [Editing depth and performance (stage 6)](tasks/editing-depth.md)
- [Storyboard, Lua, MIDI, SV recipes, video (stage 7)](tasks/scripting-storyboard.md)
- [Late compatibility: Quaver, rolls, ticked holds (stage 8)](tasks/late-compat.md)
- [OSBX Lua DSL and Lua SV curves](tasks/osbx.md)
- [Compose UX, rendering, and performance (2026-09-30 review)](tasks/compose-ux.md)
- [Global notifications: warnings and errors](tasks/notifications.md)
- [Design system, left menu bar, and dockable list panels](tasks/panels-menus.md)
- [GPU workload: Compose field renderer outside Nuklear](tasks/rendering.md)
- [Options window and project chart management](tasks/options-charts.md)
- [Frame time during live transport](tasks/live-transport.md)

Card IDs are unique across all files. Free ranges: T-17–T-29, T-32–T-39, T-176–T-179, T-193–T-199, and
every number above T-278.

## Rules for whoever executes a card

1. Read this file, the card, and only the files the card lists under **Read first**. Do not explore the whole plan.
2. Edit only **Files owned**. If the card cannot be finished without editing another file, stop and report; do not widen scope.
3. Do everything under **Steps** in order. Do not reorder, merge, or skip steps.
4. Run every command under **Verify** and paste the real output tail. A card is not done until each one passes. Never claim a test passed without running it.
5. Do not touch anything under **Out of scope**, even if it looks broken.
6. If a step says **STOP**, or an **Open question** is linked and unanswered, stop and ask the user. Do not pick a default unless the card names one.
7. Follow [`AGENTS.md`](../../AGENTS.md): no new function above cognitive complexity 25, no `NOLINT` to silence it.
8. When finished, edit `resume.md` only to remove or advance the finished card's line, and report what you changed.
9. **Stale-plan check first.** The stage roadmap lags the code. Before step 1, search for the behavior the **Outcome** describes (the card names a symbol or string to grep when it can). If it already works, set **Status** to `done (already implemented: <file:line>)`, change nothing else, and stop.
10. A card whose status is `blocked-on-user` starts with a **STOP** step naming the [open question](decisions/open-questions.md) or missing input. Do not implement the provisional default unless the user says so.

## Standard wiring pattern for a new UI action

Most frontend cards follow the same four-hop path; cards say "wire it through
the standard pattern" instead of repeating it.

1. **Core:** a revision-checked command in `include/wormedit/core/<area>.h` / `src/core/<area>.cpp` that takes `document_history_t&` and an expected revision, and rejects atomically. Test it in `tests/core/<area>_tests.cpp`.
2. **Contract:** a new enumerator in `src/ui/frontend.h` (for Compose, next to `WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL`) plus any scalar view fields. Only plain C types cross this boundary.
3. **Application:** a `case` in the matching dispatcher (`src/sys/compose_actions.cpp`, `src/sys/metadata_actions.cpp`, or `src/sys/timing_edit_runtime.cpp`) that calls the core command, and view projection in `src/sys/compose_view.cpp`, `src/sys/metadata_view_projection.cpp`, or `src/sys/timing_view.cpp`.
4. **Frontend:** a widget in the owning Nuklear unit (`src/ui/nuklear/compose_inspector.cpp`, `compose_toolbar.cpp`, `metadata_panel.cpp`, `timing_panel.cpp`, …) that sets the action. Frontend-local state goes in the matching `*_state.cpp` and is tested in `tests/ui/`.

New `.cpp` files go in the source list in `CMakeLists.txt`; new test files go in `tests/CMakeLists.txt` beside a sibling in the same directory.

## Card template

Copy this shape for every new card. Every heading is mandatory. The
`documentation.chart_editor_state` test rejects a card that lacks one.

```text
## T-NN Title (imperative, one behavior)

- **Status:** ready | blocked-on-user | blocked-on <T-id> | done
- **Outcome:** one sentence a reviewer can check by running something.
- **Read first:** exact paths (max 5).
- **Files owned:** exact paths this card may create or edit.
- **Steps:** numbered, each one a single edit or command.
- **Invariants:** data that must stay lossless, deterministic, or cross-platform.
- **Reject/failure behavior:** what must be unchanged after a bad input or failed step.
- **Verify:** exact commands with the expected result.
- **Out of scope:** adjacent work that belongs to another card.
```

## Standard verification commands

Application build (has the frontend tests, `build/wormedit`, and `compile_commands.json`):

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R '^frontend-state\.'   # Nuklear state tests
ctest --test-dir build --output-on-failure -R '^core\.'             # headless core
ctest --test-dir build --output-on-failure -R '^documentation\.'    # plan/status docs
clang-tidy -p build <changed .cpp>                                   # complexity gate
```

Whole suite: `ctest --test-dir build --output-on-failure` (about 765 tests).
Headless-only configuration: `cmake --preset headless && cmake --build --preset headless && ctest --preset headless`.
Visual smoke (needs `Xvfb xauth mcookie xdpyinfo xdotool import`):
`scripts/smoke-compose-virtual-x.sh /tmp/out.png compose`, then open the PNG and confirm the Compose pane renders.

## Authoring new cards

Each card owns one behavior and one verification. If a step needs the word
"and" between two independent behaviors, split the card. Keep a card near 30
lines, and a card file near 200 lines. Put requirements in the plan domains, not
here, and link to them from **Read first**.
