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
