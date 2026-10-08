# Selection and audition latency batch

Authorized 2026-10-08. Root integrates main serially. Preserve external macOS
menus/mania precision commit f8a07b4 and plan audits a3dcf1f/136b8ca. Root
normal Linux build/full-suite verifies these together with the batch.

- T-477 coordinator `/root/bga_preview_next_61`: worker
  `wormedit-quantize-oct8`, branch `batch/quantize-oct8`; owns its card paths.
- T-473 coordinator `/root/budget_recovery_61`: worker
  `wormedit-audition-oct8`, branch `batch/audition-oct8`; owns its card paths.
- Existing Luna medium executor is loaned sequentially by explicit ownership
  handoff; initially the audition coordinator may use `/root/budget_recovery_61/budget_luna`.
- Root owns manifests, status, measurements/integration and cleanups.

Scratch artifacts in `wormedit-scratch-builds/oct8-performance/{quantize,audition,root}`.
Use normal dependency-aware optimized builds for measurements, immutable
existing dependency sources without network. Request exclusive timing windows;
no build/test/probe overlap during captures. No user desktop/app interaction,
original-chart writes or monitor settings. T-474 remains separate with its STOP
measurement gate; no transport mailbox changes in this batch.

Coordinators stale-audit, measure where specified, delegate narrow Luna briefs,
verify exact semantics/thread identity and configured complexity, then commit
and explicitly release clean workers. Root reviews and integrates serially,
runs normal full build/CTest and merged acceptance, commits planning/status,
removes clean workers/generated builds and retains branches/logs/probes.

Root Linux baseline build passes, but full suite reports 2583/2584 with the
incoming mania half-microsecond rounding case failing at ±1.2345 ms on x86
extended precision. Root owns the necessary portability follow-up in
`src/formats/osu_mania.cpp` and `tests/formats/osu_mania_tests.cpp`; these paths
do not overlap either worker. Ordinary timestamp scaling explicitly uses
binary64 rounding consistently with the incoming Apple Silicon/MSVC path;
checked integer scaling for large timestamps remains. Test exact ties and
nearby values on both sides, retaining the bounds/overflow tests.

Root portability follow-up: normal core target build and 42/42 mania CTests
pass, including the exact half ties, neighboring fractional values and large
timestamp bounds. Configured source clang-tidy baseline/current both report
18 filtered compiler warnings and zero enabled-check violations. Full merged
Linux suite remains a final batch gate.
