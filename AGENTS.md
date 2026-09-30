# Project guidelines

## C++ function complexity

- Keep each new C++ function at or below a cognitive complexity score of 25, as measured by clang-tidy's `readability-function-cognitive-complexity` check.
- Check affected files with `clang-tidy -p build <source-file>` (or point `-p` to the directory containing `compile_commands.json`).
- When a function exceeds the threshold, extract cohesive helper functions that clarify responsibilities. Do not add `NOLINT` solely to silence a complexity warning.
- Existing violations may be addressed incrementally; avoid increasing their complexity while working nearby.

## Build and verify

- Application build directory: `build` (contains `compile_commands.json`, `build/wormedit`, and the frontend tests). Headless-only: `cmake --preset headless`.
- Build: `cmake --build build`. Run everything: `ctest --test-dir build --output-on-failure`. Filter by prefix: `-R '^core\.'`, `'^frontend-state\.'`, `'^audio\.'`, `'^documentation\.'`.
- Visual check of the real window: `scripts/smoke-compose-virtual-x.sh /tmp/out.png compose`, then open the PNG.
- Never report a test as passing without running it and reading its output.

## Delegating to Codex implementers

- Launch with `codex exec --skip-git-repo-check -m <model> --config model_reasoning_effort="<effort>" --sandbox workspace-write -C <repo> -o <report.txt> "<prompt>" </dev/null >stdout.txt 2>stderr.txt`, in the background.
- `</dev/null` is required: `codex exec` reads stdin and hangs forever if it stays open. `-o` saves only the final report; nothing appears until it finishes.
- This Codex version rejects `--full-auto`. Do not use `--approve-for-me` or `--dangerously-bypass-*`; plain `workspace-write` is enough.
- The sandbox has no display or network, so `scripts/smoke-compose-virtual-x.sh` fails inside it. Run the visual check yourself afterwards.
- Keep prompts and reports outside the repo. Review the diff and rerun the tests yourself before reporting.

## Working from plan task cards

- Work is planned as cards in `docs/chart-editor-plan/tasks/`; `memory/chart-editor-status/resume.md` says which is next. Follow the rules in `docs/chart-editor-plan/tasks.md`: edit only the card's owned files, do steps in order, stop on `STOP` or an unanswered open question.
- The `documentation.chart_editor_state` test enforces card fields and links; run it after editing any plan or status file.
