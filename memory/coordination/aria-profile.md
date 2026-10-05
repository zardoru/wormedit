# T-383 scratchless 10-key profile detection

The isolated `fix/aria-profile` worktree starts from `main` at `ff9ae58`, which
contains requested base `e709ce3` and its intervening commits. The user chart
directory `/mnt/pukeipukei/G/bms/10K2S/KR10k/x-Aria/` is read-only input.

Only complete two-stage 10-key channel/header evidence can select the existing
`bms-10k-scratchless` descriptor automatically. `.BME` is an allowed extension
but contributes weak evidence by itself. Preserve explicit profile choices,
source channel inventory, and ambiguity for scratch/aux channels, sparse input,
player/header contradictions, PMS, and special/OCT/FP dialects. Root owns
integration/review.

Focused tests, documentation validation, and changed-file complexity checks are
complete. The compact x-Aria-shaped fixture selects the scratchless profile and
preserves the ten raw source channels; root owns original-chart runtime probes
and the integrated full suite.

The integrated fixture-validation test initially forced every unlisted `.bme`
fixture to the generic 7K profile. The approved narrow exception in
`tests/formats/fixture_validation_tests.cpp` lets only `x-aria-10k-hd.bme` use
automatic detection; the positive fixture-validation test passes afterward.

## Final root integration (2026-10-04)

Main includes the profile, bounded-clock, SpecialStyle 12K/16K and fixture-harness cuts. Application build and all 2,293 CTest cases pass (45.40 s). Root combined original-chart Fraction/sample oracle and SDL dummy start/advance/pause/resume/seek/EOF probes pass for all four x-Aria charts; original hashes are unchanged. No user application was restarted and no desktop window was opened. Earlier pending main/runtime gates above are now complete; real-window smoke remains blocked by the existing sandbox approval restriction.
