# Build target reuse

## T-779 Compile common frontend implementations once

- **Status:** done (2026-10-29)
- **Completion:** 139 fewer compiler actions; zero identical extra production compilations; five narrow build closures unchanged; 227 matching preprocessor comparisons across 88 shared sources. Native GATE 5585/5585, headless 3572/3572, event-only 4407/4407, and isolated Compose smoke passed. Evidence and caveats are in the audit.
- **Outcome:** the native compile database contains no identical extra production compilations, and shared frontend implementation edits compile once per configuration.
- **Read first:** `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/building.md`, `docs/chart-editor-plan/decisions/code-style.md`, `docs/audits/build-target-reuse-2026-10-29.md`.
- **Files owned:** `CMakeLists.txt`, `tests/CMakeLists.txt`, `cmake/measure_compile_reuse.py`, `docs/building.md`, `docs/chart-editor-plan/tasks/build-target-reuse.md`, `docs/audits/build-target-reuse-2026-10-29.md`.
- **Steps:**
  1. Capture the d154284 native compile database; group ordered arguments after removing only `-o` and its output.
  2. Replace renderer test implementation copies with compiled target reuse.
  3. Give configuration-independent frontend code one static target; retain executable-specific units and allocation instrumentation.
  4. Measure compile entries and incremental fanout; inspect platform compatibility evidence.
  5. Run native, headless and event verification; review results before drafting a PR.
- **Invariants:** preserve frontend seam, shared transactions, runtime behavior, schema and formats; preserve macOS menu, test icon recording, Debug allocation-hook and decoder variants.
- **Reject/failure behavior:** configuration or missing dependency failures remain explicit; no skipped tests, relaxed complexity allowances or dependency removal.
- **Verify:** native `scripts/gate.sh --jobs 2`; headless `cmake -S . -B build -DWORMEDIT_BUILD_APP=OFF -DWORMEDIT_BUILD_EVENT_HARNESS=OFF`, then `cmake --build build --parallel 2` and `ctest --test-dir build -j 2 --output-on-failure`; event-only `cmake -S . -B build -DWORMEDIT_BUILD_EVENT_HARNESS=ON`, then the same build/CTest commands; restore `cmake -S . -B build -DWORMEDIT_BUILD_APP=ON -DWORMEDIT_BUILD_EVENT_HARNESS=OFF`; `ctest --test-dir build -R '^documentation\.' --output-on-failure`; `python3 cmake/measure_compile_reuse.py build/compile_commands.json` (zero identical extras); `git diff --check` (clean).
- **Target shape:** `wormedit_ui_frontend_common` and lower-level support libraries own shared compiled sources; executables link it and retain `WORMEDIT_UI_FRONTEND_VARIANT_SOURCES`. `rg WORMEDIT_UI_FRONTEND_SOURCES CMakeLists.txt tests/CMakeLists.txt` finds no old source-list reuse.
- **Out of scope:** core contracts/storage (Lane B), workflow (T-780), collection traversal (Lane D), dependency removal (PR64), browser implementation (PR65), codec additions (PR63), PCH/unity builds and central architecture/status/index edits (coordinator-owned).

Scope coordination: C's `src/sys/compose/compose_clipboard_workflow.cpp` registration in the app, frame-timing tests and event harness is delegated to C by coordinator approval; those three disjoint hunks are excluded from this branch. No other supporting-file extension is needed. The benchmark script is a build-related CMake supporting file within Lane A ownership.
