# Task card: scratchless 10-key profile detection

[Back to the task-card index](../tasks.md).

## T-383 Recognize complete scratchless 10-key charts

- **Status:** ready (2026-10-04; implementation and focused validation complete in isolated `fix/aria-profile` worktree)
- **Outcome:** complete, scratchless two-stage 10-key channel evidence with a compatible header selects the existing scratchless 10-key profile, while ambiguous or contradictory charts remain unresolved.
- **Read first:** `AGENTS.md`, `docs/chart-editor-plan/tasks.md`, `include/wormedit/core/lane_profiles.h`, `tests/core/bms_profile_detection_tests.cpp`, `tests/formats/bms_tests.cpp`.
- **Files owned:** `include/wormedit/core/lane_profiles.h`, `src/core/bms_profile_detection.cpp`, `tests/core/bms_profile_detection_tests.cpp`, `tests/formats/bms_tests.cpp`, `tests/formats/fixture_validation_tests.cpp`, new `tests/fixtures/bms/profiles/x-aria-10k-hd.bme`, this card, `docs/chart-editor-plan/tasks.md`, new `memory/coordination/aria-profile.md`.
- **Steps:**
  1. Add core evidence regressions for complete 10-key channels plus compatible two-stage header and retain the explicit scratchless selection behavior.
  2. Add a real x-Aria HD `.bme` parse regression for `#PLAYER 3`, channels 11–15 and 21–25, no scratch/aux channels; assert profile, layout, ten input bindings and raw source inventory.
  3. Preserve negative coverage for scratch/aux channels, sparse channel evidence, incompatible headers, PMS/special dialects, and explicit profile choices.
- **Invariants:** do not alter parsed source inventory or override explicit selection; `.BMS`, `.BME`, `.BML` suffixes may contribute evidence, but never alone establish the profile.
- **Reject/failure behavior:** any scratch/auxiliary channel or incomplete/contradictory evidence must not silently select the scratchless profile.
- **Verify:** focused core profile tests, format BMS profile tests, `documentation.chart_editor_state`, and clang-tidy on changed C++ files; profile and binding assertions pass for the actual chart.
- **Out of scope:** parser semantics, format-core changes, automatic choices for sparse or special dialects.
- **Scope extension (2026-10-04, coordinator approved):** add a compact x-Aria-derived `.bme` fixture containing only `#PLAYER 3` and the complete visible note-family channels 11–15 and 21–25. The external source directory remains read-only, and the fixture carries no media files.
- **Scope extension (2026-10-04, root approved):** update the existing scratchless-profile catalog comment in `include/wormedit/core/lane_profiles.h` so its description matches the evidence-based automatic-selection exception.
- **Scope extension (2026-10-04, root approved):** keep only the new `x-aria-10k-hd.bme` positive fixture on automatic profile selection in `tests/formats/fixture_validation_tests.cpp`; its generic `.bme` explicit fallback contradicts this fixture's purpose. All other positive fixture profile choices remain unchanged.
- **Verification evidence:** focused profile core cases passed 243 assertions/27 cases; x-Aria fixture parse passed 3 assertions; BMS profile format filter passed 130 assertions/3 cases. After root's integrated build exposed the generic explicit `.bme` fallback in fixture validation, the approved exception keeps only this fixture on auto-detection; the positive fixture-validation test then passed 653 assertions. `check_chart_editor_docs.cmake` and `git diff --check` passed. Clang-tidy found no new cognitive-complexity findings in changed code; it reported the pre-existing test-case findings in these translation units. Root owns probes against the original charts and integrated full suite.
