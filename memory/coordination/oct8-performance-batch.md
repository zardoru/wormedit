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
