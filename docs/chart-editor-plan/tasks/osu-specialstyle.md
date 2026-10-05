# osu!mania SpecialStyle layouts

## T-384 Respect SpecialStyle for 12K and 16K imports

- **Status:** done (2026-10-04; main build, configured clang-tidy and all 2,264 CTest cases pass)
- **Outcome:** SpecialStyle 12K imports use a DP5-like two-stage field and SpecialStyle 16K imports use a DP7-like field, with outside scratch colors and every osu column still playable; ordinary 12K/16K alternate white and black.
- **Read first:** `AGENTS.md`, `src/core/lane_profiles.cpp`, `src/formats/osu_mania.cpp`, `tests/formats/osu_mania_tests.cpp`.
- **Files owned:** root in `wormedit-osu-specialstyle`: `include/wormedit/core/lane_profiles.h`, `src/core/lane_profiles.cpp`, `src/formats/osu_mania.cpp`, `tests/core/lane_profile_tests.cpp`, `tests/formats/osu_mania_tests.cpp`, this card, `docs/chart-editor-plan/tasks.md`.
- **Steps:**
  1. Add the named split profiles and deterministic outside scratch colors while preserving playable roles and column IDs.
  2. Select them when importing General.SpecialStyle:1 for 12K or 16K.
  3. Verify regular coloring, stage input, native reopen and osu export/reimport preserve columns and SpecialStyle.
- **Invariants:** all original hit-object columns remain playable; existing 10K coloring and unsupported SpecialStyle counts remain unchanged; source metadata remains retained.
- **Reject/failure behavior:** invalid key counts still reject, missing or non-1 SpecialStyle uses the ordinary layout.
- **Verify:** `cmake --build build`; `ctest --test-dir build --output-on-failure`; `clang-tidy -p build src/core/lane_profiles.cpp src/formats/osu_mania.cpp`; `ctest --test-dir build --output-on-failure -R documentation.chart_editor_state`.
- **Out of scope:** BMS detection and bounded timing compilation (T-382/T-383); converting osu columns into BMS scratch objects.

- **Implementation evidence:** isolated relink against read-only main dependencies passes 58 lane-profile/osu cases (7,094 assertions), including 1,755 assertions in the two new SpecialStyle cases. Configured clang-tidy passes both changed production sources with no emitted diagnostics. Main application build and all 2,264 CTest cases pass (38.86 s); configured clang-tidy on main passes both changed production sources. Real-window visual validation remains unavailable in this sandbox; no user-desktop window was opened.
