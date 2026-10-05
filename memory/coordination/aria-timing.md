# T-382 dense BPM timing arithmetic

Worktree `wormedit-aria-timing`, branch `coord/aria-timing`, base `ff9ae58`.
Original mounted x-Aria BMS files are read-only. T-383 profile ownership and
T-384 osu layout ownership remain separate; root alone integrates main.

## Reproduction and approved policy

The actual 10K HD native import has 85 measures, resolution 3840 and 16 BPM
changes. The measure 58 ramp uses 66,71,76,81,86,91,96,101,106,111,116,121,126,130.
Measures 49–56 are half-length and measure 57 is 1.25-length. Strict timing
compilation reproduces `timing segment accumulation exceeds exact arithmetic bounds`.
At 110.157998425160642 s, the reduced accumulated fraction is
190455043183606137137/1728926141600129472: its numerator needs 68 bits.
The final exact time is 3039004713752153429851/19018187557601424192,
159.794654698180551 s; nearest ties-even 44100 Hz frame is 7046944.
Evidence: `/tmp/wormedit-aria-timing/fraction-proof.log`, `probe.log`, `import.log`.

Portable 128-bit intermediates now reduce exact add/sub sums before enforcing
signed 64-bit numerator and unsigned 64-bit denominator representation. They
preserve representable results but correctly reject the actual unrepresentable
ramp fraction. Root independently tested 2010 signed full-width add/sub cases
against Python Fraction: `/tmp/wormedit-aria-root/check-arithmetic.py`.

Explicit opt-in `allow_bounded_clock` defaults false. Only genuine accumulation
overflow selects a complete index rebuild at a 1e-12 s quantum; source BPMs,
meters and model data remain exact. The hard 300001-rounding cap and signed tick
range bound clock accumulation error to 150.0005 ns, plus <=0.5 ps for an interior
forward query. Inverse interpolation rounds to a 1e-9 row grid and quantizes
input time by <=0.5 ps; source-time error also includes the clock bound. Reject
positive intervals collapsing to zero and total rows outside signed nanorow range.

## Ownership and verification

The task card records exact coordinator/helper/test and executor/API/consumer
ownership before source edits. New private helper functions and tests use no
compiler-specific integer types. Initial 22 arithmetic/helper assertions passed;
new tests were split after actual clang-tidy exposed Catch macro complexity.
Configured clang-tidy uses the main recorded flags with owned source/include
paths replaced, dependencies read-only; new helper/test functions pass the <=25
gate, with unchanged historical timing test violations only.

`/tmp/wormedit-aria-timing/build-focused.py` copies the main core archive, then
recompiles every core object whose recorded Make dependency file references
changed timing.h or wide_math.h into own artifacts, plus the owned timing tests.
It never writes main objects/dependency build directories. Actual original four
chart forward/inverse/sample/schedule probes and final focused build pass as recorded below.
Root final application build/full suite/runtime integration remain pending.
Real-window verification remains blocked by the previously rejected Xvfb sandbox
approval; no retry or user desktop window is authorized for this task.

## Reviewed implementation checkpoint

Final source review preserves default strict exact compilation and source model
values. Exact wide intermediate sums retain representable results. Opt-in fallback
rebuilds the complete clock with fresh compilation cursors and diagnostics; invalid
interval conversions never select fallback. Capacity and tick/nanorow range limits
reject cleanly. Signed pre-chart preview extrapolation uses an initial-BPM portable
wide rounded factor ratio, including a 3000000-second positive offset and a
fractional 1/44100-second query, without requiring an unrepresentable rational
intermediate. Tests also reject an extrapolated result outside the signed row grid.

Derived opt-ins are chart preview, Player preview, chart length, bookmark chooser,
Compose marker/speed timing, playhead skips, loop selection timing and bookmark
seek. The seek-only bookmark helper extension was approved and recorded before
editing; beat bookmark creation and all mutation/fit/exchange callers remain exact.

Configured source tidy: `/tmp/aria-timing-executor/clang-tidy-final.log` exits 0
across nine changed source files with only four unchanged caller warnings. Valid
threshold-zero comparison logs are `timing-baseline-tidy.log` and
`timing-current-tidy.log` in the same directory. Existing scores do not increase:
integer timing_at 14→1 (exact body remains 14), seconds_inside_segment 7→1,
row_inside_segment 6→1, musical_position_at 20→13, apply_timing_pause 4→3.
Every new source/helper function is <=14. Owned helper/tests configured tidy:
`/tmp/wormedit-aria-timing/helper-configured-tidy.log`, unchanged historical
Catch macro warnings only. No NOLINT or compiler-specific integer type was added.

Full isolated timing tests pass: 61 cases, 50549 assertions. Final original four-chart
precision oracle covers 64 segments: maximum clock error 2.05110664111e-12 s,
maximum inverse error 1e-9 rows, every 44100 Hz sample frame matches exact Python
Fraction calculations, and preview schedules compile. Strict timing still rejects
all four originals. Final arithmetic oracle covers 2010 full-width signed add/sub
results. Logs: `/tmp/wormedit-aria-timing/all-timing-tests.log`, `precision.log`,
`precision-summary.log`, `arithmetic-summary.log`. The 50000-note schedule
safety test passes: baseline 0.226 s vs changed 0.227 s, both carrying the same
host-specific 125 ms advisory and within the enforced 500 ms ceiling.

Root final main application build, full CTest suite and actual SDLdummy playback
start/advance/pause/resume/seek/EOF probe remain pending integration. No worker
claim covers these pending gates. The four-chart standalone probe intentionally
omits the independent T-383 input-binding assertion because this worker's base
predates that profile patch; root's final combined probe retains that assertion.

## Final root integration (2026-10-04)

Main includes the profile, bounded-clock, SpecialStyle 12K/16K and fixture-harness cuts. Application build and all 2,293 CTest cases pass (45.40 s). Root combined original-chart Fraction/sample oracle and SDL dummy start/advance/pause/resume/seek/EOF probes pass for all four x-Aria charts; original hashes are unchanged. No user application was restarted and no desktop window was opened. Earlier pending main/runtime gates above are now complete; real-window smoke remains blocked by the existing sandbox approval restriction.
