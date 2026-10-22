# Options dialog resizing

[Back to task cards](../tasks.md).

## T-527 Migrate Options to the shared dialog and fill resized space

- **Status:** done (2026-10-11; verified; uncommitted follow-up)
- **Outcome:** Options uses the shared painted dialog, title and themed actions. Resizing expands both the category and settings panes; page content scrolls independently and actions stay at the bottom.
- **Read first:** `src/ui/nuklear/preferences/options_window.cpp`, `src/ui/nuklear/dialog.h`, `src/ui/nuklear/dialog.cpp`, `tests/ui/dialog_tests.cpp`, `tests/ui/nuklear_event_harness_tests.cpp`.
- **Files owned:** those source/test files, `docs/chart-editor-plan/tasks/dialogs.md` (document the settings-tool resize exception), this card; coordinator owns task index/checker and status. The shared dialog receives an opt-in resize mode; existing content-fit dialogs keep their behavior.
- **Steps:** 1. Audit Options and shared dialog sizing. 2. Add opt-in retained resizing with minimum bounds. 3. Migrate Options and compute body height from available content space, reserving the footer. 4. Verify resize/input regression, existing dialog behavior, native captures and complexity. 5. Reconcile status.
- **Invariants:** Preference draft, Apply, Revert, defaults and Close semantics stay intact. Settings controls retain typed actions. Existing confirmation dialogs keep content-fit sizing. Footer remains visible while settings scroll.
- **Reject/failure behavior:** Small windows clamp tool bounds; long settings pages scroll. Disabled Apply stays disabled. Close icon and Escape use the existing draft-discard action; Enter does not unexpectedly apply settings.
- **Verify:** `cmake --build build -j4`; production frontend event harness; focused Options/dialog checks; full CTest suite; affected-source clang-tidy; isolated native Xvfb at default and enlarged bounds; documentation validation; release rebuild.
- **Done so far:** Confirmed Options used raw Nuklear title chrome and fixed 290-pixel body. Implemented the shared dialog migration and dynamic settings/footer layout; added a production input resize regression.
- **Verification evidence:** Normal and release builds pass. Full CTest: 3,176/3,176 passed without retries. Production frontend event harness: 380 cases / 5,516 assertions. Focused Options/dialog checks: 30 cases / 247 assertions. Native Xvfb captures at 800×500 and 1040×700 show the shared painted chrome, expanding panes, newly visible Editor settings and bottom-anchored buttons with 16-pixel padding. Production clang-tidy has no source warnings; new event test has no complexity warning (existing unrelated test warnings remain). Evidence: `/tmp/options-dialog-{ready-build,ready-release,all-tests,events,focused-final,tidy-final,tests-tidy,native}.log`; `/tmp/options-dialog-{default,enlarged}.png`.
- **Remaining:** None.
- **Out of scope:** Preference meaning changes, redesigning settings pages, automatically committing this batch.
