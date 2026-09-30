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

## Rules for whoever executes a card

1. Read this file, the card, and only the files the card lists under **Read first**. Do not explore the whole plan.
2. Edit only **Files owned**. If the card cannot be finished without editing another file, stop and report; do not widen scope.
3. Do everything under **Steps** in order. Do not reorder, merge, or skip steps.
4. Run every command under **Verify** and paste the real output tail. A card is not done until each one passes. Never claim a test passed without running it.
5. Do not touch anything under **Out of scope**, even if it looks broken.
6. If a step says **STOP**, or an **Open question** is linked and unanswered, stop and ask the user. Do not pick a default unless the card names one.
7. Follow [`AGENTS.md`](../../AGENTS.md): no new function above cognitive complexity 25, no `NOLINT` to silence it.
8. When finished, edit `resume.md` only to remove or advance the finished card's line, and report what you changed.

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
