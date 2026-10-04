# osu! cursor sample-frame conversion regression

User screenshot reports "Chart cursor time cannot be converted to audio frames"
at Hybris INSANE measure 58. Root authorized Sol coordination and Luna medium
implementation in isolated branch `coord/osutest-cursor`, base `454280b`.
All original charts/assets remain read-only. Root alone integrates main.

Initial audit: `sample_frame_at` reduces rate/denominator then multiplies the
remaining numerator by sample rate into uint64 before division. Fractional
cursor rows can produce large rational numerators/denominators while the true
sample quotient remains small. Rejecting an overflowing intermediate product
can therefore reject mathematically valid playback cursor times.

## Ownership before edits

Luna exclusively owns `src/core/timing.cpp` and
`tests/core/timing_tests.cpp` for exact sample conversion arithmetic and focused
rounding/overflow regressions. Public `include/wormedit/core/timing.h` may be
edited only if necessary. Coordinator owns this brief and external /tmp probes.
Supporting `src/sys/preview_cursor_frame.h` and existing
`tests/core/interaction_tests.cpp` are approved within the cursor outcome if
necessary; message the coordinator before adding edits there. Root does not
edit these source/test paths during this batch. No other source edits.

## Required outcome and checks

Convert every representable positive rational sample frame exactly despite
intermediate multiplication overflow. Preserve floor, ceil, nearest ties-to-even,
negative-time errors, invalid denominator/rate errors, and actual unrepresentable
quotient/rounding errors. Avoid float estimates, clamping, unchecked narrowing,
and nonportable arithmetic assumptions. Every new C++ function <=25 cognitive
complexity, checked using configured clang-tidy. Preserve existing limits.

Reproduce actual all-eight fractional cursor failures, especially measure 58;
verify all-eight cursor frames and actual mid-chart start/pause/resume using
real audio with SDL dummy. Retain schedule/binding checks. Build offline using
`/tmp/wormedit-batch-implementation/configure.sh`, cap builds at j4, use real
headless core/app paths and no Xvfb or approval bypass. Record tests actually run.

## Final reviewed implementation

Luna implemented only `src/core/timing.cpp` and
`tests/core/timing_tests.cpp`; coordinator reviewed the exact final diff and
released Luna's slot after its focused gates. No public API, cursor helper,
importer, chart geometry, or source timestamp changes were necessary.

`sample_frame_at` retains its gcd reduction and checked uint64 multiply fast
path. Only an overflowing intermediate product uses existing portable
`wide_math::multiply` / `divide`. A nonzero quotient high word remains a genuine
overflow error. The exact remainder is below the nonzero uint64 denominator;
it feeds the existing floor, ceil, and nearest ties-to-even logic unchanged.
Rounded carry beyond UINT64_MAX still errors. No floats, clamping, unchecked
narrowing, compiler-specific integer type, or wider accepted final frame range.

The actual INSANE measure 58 row 0+0.123457 cursor gives source seconds
1046902753305360727/14284000000000000: valid floor frame 3232176 and nearest/
ceil frame 3232177 at 44.1 kHz. Before this fix the intermediate multiplication
failed. The independent baseline queried 27,468 integer/fractional positions
across all eight charts; timing_at succeeded for every query, while 951 valid
fractional cursor positions failed sample conversion (90 each across seven
charts, 321 in BEGINNER). Integer and half-row positions already succeeded.

## Final verification evidence

Root integration `3cdc1a6`: Debug build and full 2,228-case main CTest suite pass.
Root's new linked cursor probe independently passes all 27,468 queries against
the integer oracle, with exactly 951 prior failures fixed and unchanged time
fractions. Root's previously failing large-component 90-second sample probe now
returns the correct 4,320,000 frame; all eight sample-frame unit cases pass.

The isolated core test target built at j4. Luna's focused sample-frame tests
passed 29 assertions across 8 cases. Coordinator ran all core and documentation
CTest cases: **1,382/1,382 passed** (1,381 core and 1 documentation). Configured
clang-tidy ran on timing.cpp and timing_tests.cpp: no production cognitive
complexity warning and all new tests <=25; existing Catch violations remain
unchanged. git diff --check passes. Boundary regressions cover the real cursor
fraction, a large coprime 90 second fraction, full UINT32_MAX sample rate, exact
UINT64_MAX frame, actual quotient overflow, and floor-valid UINT64_MAX+3/4
whose ceil/nearest conversions must fail. Existing ties-to-even cases preserve
both tie parities. Negative time, zero denominator, and zero rate retain their
specific errors in independent checks.

An independent Python integer oracle matches 9,021 exact sample conversions
across all three rounding modes and full-range randomized rational components.
The final all-eight cursor sweep matches the same integer oracle for all 27,468
queries; all 951 prior failures are corrected and all timing rationals are
identical to baseline. Negative cursor times retain the established frame-zero
handling in the unchanged helper.

The actual `chart_preview_t::play_pause` path compiled schedules/resolved real
Hybris.ogg bindings and started all eight charts at previously failing
fractional mid-chart cursors. Seven charts start at measure 58 row 0+0.123457,
frame 3232177; BEGINNER starts at measure 29 row 959+0.123457, frame 1875619.
Every chart remained active/playing, advanced 5120 frames beyond start with a
20 sample stable window, and passed pause/resume. Music and schedule endpoint
remain 7314712 frames. This probe links the newly built core library with prior
unchanged SDL/preview/audio object files read-only from the playback worktree,
as explicitly approved by root. No object/cache files were copied into Git or
this tree. Root owns the final main application integration/build; this batch
has no application-interface changes. The known X sandbox restriction still
prevents a visual gate.

All 14 original chart/asset SHA-256 hashes remain unchanged. Artifacts under
`/tmp/wormedit-batch-implementation/osutest-cursor/` include `before.json`,
`after.json`, `cursor-check.log`, `sample-check.log`, `midchart-runtime.log`,
`final-core-tests.log`, `core-build.log`, `sample-frame-tests.log`,
`timing-clang-tidy.log`, and `timing-tests-clang-tidy.log`, plus independent
probe/check sources. `core-tests.log` is Luna's explicitly interrupted early
run; `final-core-tests.log` records the completed coordinator gate.
