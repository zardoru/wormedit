# Project guidelines

## Code style

- Follow [docs/chart-editor-plan/decisions/code-style.md](docs/chart-editor-plan/decisions/code-style.md): one owner per concept, construction and queries on the type (`T::from`), the naming table (`is_`/`has_`/`can_`, `build_<x>_view`, `make_`/`build_`/`create_`), the command transaction, shared result types, one table per fact, and dependencies over hand-rolled parsers. Use its review checklist before finishing a change.
- Read [docs/architecture.md](docs/architecture.md) first: the edit pipeline (direct commands and gesture dispatch into one transaction). A chart edit contains only its validation, data change and effect; lifecycle, stamp, transaction and result plumbing belong to the shared runners.
- Before writing a helper, search for an existing one by what it does, not just by name.

## C++ function complexity

- Keep each new C++ function at or below a cognitive complexity score of 25, as measured by clang-tidy's `readability-function-cognitive-complexity` check.
- Check affected files with `clang-tidy -p build <source-file>` (or point `-p` to the directory containing `compile_commands.json`).
- When a function exceeds the threshold, extract cohesive helper functions that clarify responsibilities. Do not add `NOLINT` solely to silence a complexity warning.
- Existing violations may be addressed incrementally; avoid increasing their complexity while working nearby.

## Build and verify

- Application build directory: `build` (contains `compile_commands.json`, `build/wormedit`, and the frontend tests). Headless-only: `cmake --preset headless`.
- **GATE:** `scripts/gate.sh` builds, runs every test, fails if any test binary cannot list its tests, checks whitespace, and prints one summary. Cards that say "Verify: GATE" mean this script.
- Job count: `build/` uses Make, which is serial unless told otherwise, so always pass a count. Default to `min(8, nproc / concurrent agents)`; use every core only when the owner says the run may use the whole computer. The gate does this (`--agents N`, `--jobs N`, `--whole-machine`; `--print-jobs` shows its choice).
- Build by hand: `cmake --build build --parallel <jobs>`. Run everything: `ctest --test-dir build -j <jobs> --output-on-failure`. Filter by prefix: `-R '^core\.'`, `'^frontend-state\.'`, `'^audio\.'`, `'^documentation\.'`. A ctest line "Error listing tests" means a test binary never ran; treat it as a failure.
- Visual check of the real window: `scripts/smoke-compose-virtual-x.sh /tmp/out.png compose`, then open the PNG.
- Keep automated visual checks inside isolated Xvfb; do not open test windows on the user's Niri desktop.
- Input-routing and UI workflow checks without a window: use the [frontend event harness](docs/frontend-event-harness.md) (`ctest --preset headless-events -L event-harness`, or `-L event-harness` in the app build), not Xvfb.
- Never report a test as passing without running it and reading its output.

## Delegating from Claude to Codex implementers

These command-line instructions apply only when Claude delegates to Codex.
Codex should use its native sub-agent tools when delegating.

- Launch with `codex exec --skip-git-repo-check -m <model> --config model_reasoning_effort="<effort>" --sandbox workspace-write -C <repo> -o <report.txt> "<prompt>" </dev/null >stdout.txt 2>stderr.txt`, in the background.
- `</dev/null` is required: `codex exec` reads stdin and hangs forever if it stays open. `-o` saves only the final report; nothing appears until it finishes.
- This Codex version rejects `--full-auto`. Do not use `--approve-for-me` or `--dangerously-bypass-*`; plain `workspace-write` is enough.
- The sandbox has no display or network, so `scripts/smoke-compose-virtual-x.sh` fails inside it. Run the visual check yourself afterwards.
- Keep prompts and reports outside the repo. Review the diff and rerun the tests yourself before reporting.

## Working from plan task cards

- Work is planned as cards in `docs/chart-editor-plan/tasks/`; `memory/chart-editor-status/resume.md` says which is next. Follow the rules in `docs/chart-editor-plan/tasks.md`: subagents edit only the card's owned files and escalate supporting-file extensions to their coordinator. Coordinators may approve and record those extensions within the card's intended outcome without user approval, coordinating overlapping ownership before edits. Do steps in order; stop on `STOP` or an unanswered open question, and respect explicit out-of-scope limits.
- The `documentation.chart_editor_state` test enforces card fields and links; run it after editing any plan or status file.
