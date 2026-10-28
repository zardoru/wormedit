# Scoped BMS edit performance

Prepared BMS owners reuse one immutable projection result, including diagnostics, by
exact evaluation choices and optional authoring Chart. Cache replacement is synchronized;
readers can retain results across replacement. A new semantic owner cannot inherit a stale
result. At most one result is retained by each owner, rather than one per alternative.
The existing bounded evaluation, collision, hold pairing, reference and document checks
still run. Detached `build_projection` results retain their value semantics.

## Reproduce

Build in Release with testing enabled and the application disabled, then build the manual
`wormedit_bms_edit_benchmark` target. Pass a disposable complete copy of an imported BMS
project, including its assets:

```sh
cmake -S . -B build/perf -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DWORMEDIT_BUILD_APP=OFF
cmake --build build/perf --target wormedit_bms_edit_benchmark --parallel 4
build/perf/tests/wormedit_bms_edit_benchmark /disposable-copy/chart.bms
```

The benchmark reads files but never saves. It times applying one real note drag and seven
empty layer insertions through public core actions. Outside the timed regions it verifies
the moved note, layer counts, one Undo step per edit, exact owner restoration by Undo/Redo,
and final document validation. Setup/import, gesture begin/preview, Undo/Redo and the final
validation are excluded from the action timings. No frontend publication observers are
registered, so these are synchronous core costs rather than input-to-present latency.

## Fungus Dance measurement

Fixture: Fungus Dance by paraneumann, `0_fgd_I.bms`, 67 measures. All 637 original files
were SHA-256 checked against the disposable copy before and after the experiment. The
fixture and assets are not part of the repository.

Host: Reshiram, AMD Ryzen 7 5700X, GCC 16.2.1, `-O3 -DNDEBUG`. Both binaries are freshly
built from the same base. Alternating baseline/optimized process order reduces shared-host
load bias. This is a local measurement, not a CI performance threshold.

Base: `db4528768c5cb8fc0ed6f60aa0b6c026ba5ae899`. Nine processes per binary, with
seven layer insertions in each process. Every process passed all correctness checks.

| Action | Samples per binary | Baseline median | Cached median | Reduction |
|---|---:|---:|---:|---:|
| Apply note drag | 9 | 117.088 ms | 60.998 ms | 47.9% |
| Insert empty layer | 63 | 51.193 ms | 20.983 ms | 59.0% |

One additional process per binary measured peak RSS: 236,704 KiB before and 267,604 KiB
after (about 30 MiB / 13% more). This includes the eight edited snapshots retained by the
benchmark's Undo history. The cache trades memory for repeated projection work; its one-entry
limit prevents retaining every bounded alternative within each owner. Actual memory use
also depends on history length and program size.

These timings do not establish frontend frame latency or a universal speedup for conditional
programs; different authoring/evaluation keys can evict one another. No validators or bounded
evaluation budgets were weakened.
