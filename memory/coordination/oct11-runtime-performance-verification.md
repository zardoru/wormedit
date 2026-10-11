# T-558: Release playback verification, 2026-10-11

The workspace binding cache landed in `bedaff6`. The current application,
`785d7f6`, stays below the original 5 ms average frame CPU budget in the
HeadBONKache capture and four real BMSON charts supplied by the user. The BMSON
and Nuklear CPU regressions are not reproduced in these captures. A recorded
projection wall-time spike is mostly time during which the main thread was
not executing. No additional runtime change is justified by this evidence.

## Workloads and method

All measurements use `build/release/wormedit` (`Release`, `-O3 -DNDEBUG`),
1600×900, default skin and 192 px per standard measure. Each native window
runs in authenticated loopback TCP Xvfb with a private Xauthority file and
SDL dummy audio. No agent build or clang-tidy job runs during measurement;
other user workloads are left running. Original charts/audio are read only.

The headless converter imports each BMSON to a temporary native project with
`target: native_project`. Temporary audio paths resolve to the source chart's
directory; Kick Your Ass's `.wav` references resolve to the available `.ogg`
files. Source audio and musical content are not rewritten. These fixtures live
under `/tmp/bmson-documents-perf/`. All four conversions report success,
reimport validation and no import warnings or conversion diagnostics.

| Chart under `~/Documents/charts` | Sound channels | Retained notes | Continued notes |
| --- | ---: | ---: | ---: |
| `overaliasing/overaliasing_h.bmson` | 21 | 951 | 887 |
| `Forgetitalia/Forgetitalia.bmson` | 9 | 924 | 915 |
| `disambiguation/disambiguation.bmson` | 31 | 435 | 333 |
| `Kick_Your_Ass/kick_your_ass2.bmson` | 12 | 515 | 503 |

Overaliasing and Forgetitalia play for 18 seconds with the normal docks open.
Disambiguation is captured both with docks and with an expanded field. The
expanded Disambiguation and Kick Your Ass runs hide Sounds/Inspector, warm up
for five seconds, then play for 45 seconds. Their inspected screenshots show
actual waveform fills across several lanes at about 0:44, rather than relying
on a placeholder-only or unanalyzed projection workload.

HeadBONKache uses the existing `/tmp/headbonk-spa.wormedit` import of
`/mnt/pukeipukei/G/bms/bms-b/BOFXVII/[saaa]HeadBONKache/headBONK_SPA.bms`.
Its inspected screenshot shows playing transport at 0:17 and 1,182 loaded
keysounds.

## Final complete windows

Each entry is the last complete, non-overlapping 240-frame report emitted
before capture/Stop. Values are milliseconds of **main-thread CPU time**;
they are not audio-worker CPU, GPU time or elapsed time. These are individual
windows, not whole-song aggregates. Startup and cold import are not covered.

| Workload | Frame avg / p99 | BMSON projection avg / p99 | Nuklear avg / p99 |
| --- | ---: | ---: | ---: |
| HeadBONKache | 3.024 / 4.224 | n/a (BMS) | 0.457 / 0.795 |
| Overaliasing, docks | 2.205 / 2.857 | 0.194 / 0.343 | 0.453 / 0.704 |
| Forgetitalia, docks | 2.278 / 2.929 | 0.182 / 0.287 | 0.530 / 0.837 |
| Disambiguation, final expanded capture after docks | 1.811 / 2.862 | 0.199 / 0.391 | 0.452 / 0.657 |
| Disambiguation, expanded throughout playback | 1.776 / 2.109 | 0.239 / 0.349 | 0.472 / 0.673 |
| Kick Your Ass, expanded throughout playback | 1.714 / 2.086 | 0.222 / 0.342 | 0.496 / 0.655 |

The preceding HeadBONKache window has 2.968 ms average / 5.195 ms p99 CPU.
One earlier Forgetitalia window has 2.195 ms average / 5.772 ms p99 CPU.
These are within the **average** budget but demonstrate that this is not a
guarantee that every frame or p99 stays below 5 ms.

## Why elapsed p99 can look expensive

The final Overaliasing projection reports 0.230 ms wall average / 1.476 ms
wall p99, versus 0.194 / 0.343 ms CPU. Forgetitalia reports 0.217 / 1.481 ms
wall, versus 0.182 / 0.287 ms CPU. Comparing wall time alone overstates
application work under scheduling pressure.

Kick Your Ass's spike log records this frame at 37.633 seconds:

```text
frame 47.191 ms wall, 1.896 ms cpu, switches voluntary 4 involuntary 3
  Workspace projection: 4.933 ms wall, 0.632 ms cpu
    BMSON projection: 4.587 ms wall, 0.287 ms cpu
  Render/present: 38.994 ms wall, 0.550 ms cpu
    Present (VSync wait): 36.628 ms wall, 0.102 ms cpu
```

The CPU difference and context switches support scheduling delay in this
specific projection spike. They do not identify the competing process.
Presentation waits dominate this frame's elapsed time. Xvfb presentation
does not establish the behavior of the user's real compositor or GPU display.

## Reproduction and evidence

Temporary conversion fixtures and automation are under
`/tmp/bmson-documents-perf/`. `/tmp/scratchpad-perf-native.sh` captures the
normal dock layout; `/tmp/bmson-documents-perf/expanded.sh` hides the two docks
and plays for 45 seconds. Both launch only their own authenticated Xvfb and
clean up their own app/server. Set `SKIN_PROJECT` to the temporary project,
`PERF_PREFIX` to an unused `/tmp` artifact prefix, and
`WORMEDIT_FRAME_TIMING_REPORT` to that prefix plus `-report.log`. For individual
spikes, set `WORMEDIT_FRAME_SPIKE_LOG` and `WORMEDIT_FRAME_SPIKE_MS=30`.

Reports, app logs and inspected screenshots use these `/tmp` prefixes:

- `headbonk-perf-current`
- `overaliasing-perf-baseline`
- `forgetitalia-perf-baseline`
- `disambiguation-perf-baseline`
- `disambiguation-perf-expanded`
- `kickyourass-perf-expanded` (also `-spikes.log`)

The original before/after validation-cache capture remains at
`/tmp/headbonk-perf-{baseline,after}-report.log`: preparation CPU average
0.601→0.331 ms. Current preparation is 0.317 ms average in the final window.

Normal/Release implementation verification at `785d7f6` already passes both
3,386-test suites and affected default clang-tidy. This follow-up changes
documentation only; documentation state checks run again after the update.
The result closes the scoped current-corpus verification, with the limits
above retained. It does not claim universal performance or reproduce every
setting from the original report.
