# Packed waveforms and section-specific sound import

Authorized 2026-10-08 after the latency batch. Dispatch base b768337.
Root integrates main serially, owns manifests/status and final checks.

- T-459 Sol6.1 medium coordinator `/root/bga_preview_next_61`: tree
  `wormedit-packed-waveforms-oct8`, branch `batch/packed-waveforms-oct8`.
  Own its card paths, with projection tests concretely
  `tests/sys/stem_window_tests.cpp` and `tests/sys/frame_timing_tests.cpp`.
  Its frontend.h ownership is only waveform-piece identities/fields.
- T-462 Sol6.1 medium coordinator `/root/budget_recovery_61`: tree
  `wormedit-sound-sections-oct8`, branch `batch/sound-sections-oct8`.
  Own its card paths, with UI tests concretely
  `tests/ui/sound_import_state_tests.cpp` and
  `tests/ui/nuklear_event_harness_tests.cpp`. Its frontend.h ownership is
  only sound-import requests/actions/items/section fields.
- One existing Luna medium executor is loaned sequentially with exact exclusive
  file ownership and release; initially the sound-import coordinator may use
  `/root/budget_recovery_61/budget_luna`. No overlapping header sections, canvas
  writes or test files between worktrees. Exact extensions recorded before edits.

Stale-audit first; preserve hybrid classification, BMS slots, existing stem
rendering and undo/export data. Packed pieces use stable typed event identity
without aliasing stem cache keys; unfinished analysis retains pending bands.
Section imports fix their kind and BMSON stem adds create linked empty channel
and lane with zero events/markers. Keysound adds create resource only.

Scratch `wormedit-scratch-builds/oct8-bmson-docks/{waveforms,imports,root}`.
Normal dependency-aware builds use immutable existing dependencies with explicit
WORMEDIT_OTOWORM_SOURCE_DIR; no temporary submodule symlink. Keep all GUI checks
off the user's desktop. Existing isolated-Xvfb restriction remains; use actual
headless SDL/software rendering and frontend event harness where available,
recording omitted native-window captures. Original charts/audio stay read-only.

Each coordinator verifies focused tests, configured complexity<=25, docs and
commit/release. Root reviews, cherry-picks serially, builds/tests all main,
checks real-chart projection/import/rendering evidence, commits status and
removes clean worker trees/builds; retain branches, logs and artifacts.

Supporting ownership approved before implementation: T-459 may extend
`src/sys/compose_view.h` for retained packed entries, `src/ui/field/field_renderer.h`
and `.cpp` for typed cache identities, and `tests/ui/field_renderer_tests.cpp`
for key isolation. T-462 may extend `sound_import_state.h`,
`sound_import_runtime.h` and `.cpp`, `src/sys/compose_sound_actions.h`,
`tests/sys/application_commands_tests.cpp`, and the single frontend picker
forwarding call in `src/ui/nuklear/frontend.cpp`. Workers record these in their
own card sections. Root exclusively owns `src/sys/main.cpp` for the three narrow
staging changes: accept/forward fixed kind, retain it in reviewed items, and
pass the section kind from the import request. Apply after contract integration.

Root adds the existing `src/common/static_interval_index.cpp` to the core-test
target in `tests/CMakeLists.txt` so pure packed-index tests link the reused
interval-query implementation. No shared interval implementation edits.
Packed sample tails may extend beyond authored rows using their own display
clock; stem timing and authored chart data remain unchanged.

Completed integration 2026-10-08: T-462 worker 93e4cad -> main d4b810c,
root staging d64590a; T-459 worker 43f5d00 -> main 40c9b58. Disjoint card/header
hunks merged cleanly. Root normal application build and all 2,617 CTests pass.
Main staging tidy preserves the three existing function/score warnings exactly;
worker configured baseline comparisons report no new or increased violations.
T-462 passes 199 event-harness cases plus core/state/actual-dispatch checks;
T-459 passes 133 expanded waveform/BMSON/docs cases. Peer review found and fixed
same-row stacks: every simultaneous start shares the next strictly later cutoff.

Root independently compiled/linked the retained real-chart probe against normal
main objects and ran SDL dummy/software capture. Kick measure 30 has registration
failures 0, three ready packed pieces (`fx`, `hat`, `pc`), pending 0, twelve total
views, index builds 1/query scans 3. The unobscured capture is retained in
`wormedit-scratch-builds/oct8-bmson-docks/root/kick-measure 30.png`. Comparison
records 1,681 cursor/status pixel differences between Debug main and optimized
worker captures; all other pixels match. Full-frame pixel identity is not an
acceptance claim. Three original chart hashes and thirteen Kick chart/audio
hashes match after the repeat. Native picker/window/GPU cadence is unverified.

The interval-query bound excludes registry binding copies/equality, visible
piece lookups and viewport band calculations; whole-frame cost remains a
separate measurement. No new FPS/deadline or asset-independent frame claim.
T-462 adds section controls without implementing T-461's conversion verbs or
T-463's point placement. Next independent hybrid card is T-464.

Both clean released worker trees and 6.51 GB generated builds removed; only main
remains registered. Branches, raw logs, source/link scripts, configurations,
captures and standalone probes retained. Old worker probe ELFs can use
`LD_LIBRARY_PATH=.../oct8-bmson-docks/root/probe-libs`; repeat against current
main using the copied root linker/source and README.
