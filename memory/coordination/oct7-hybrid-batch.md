# October 7 hybrid BMSON and slice-span batch

User approved the proposed next implementation batch. Base `c04413d`.
Root owns main and final review; only two fresh workers are registered.

- `wormedit-hybrid-bmson-oct7`, branch `batch/hybrid-bmson-oct7`:
  T-457 explicit stem/packed model, T-458 import classification/packing,
  T-460 packed-channel exclusion. Sol 6.1 medium `/root/budget_recovery_61`.
- `wormedit-slice-spans-oct7`, branch `batch/slice-spans-oct7`:
  T-465 deduplicated slice-span conversion foundation. Sol 6.1 medium
  `/root/bga_preview_next_61`.

Reuse Luna medium `/root/budget_recovery_61/budget_luna`; the four-agent
limit permits one executor at a time. Slice-span coordinator assigns the first
narrow implementation/test slice, then releases it to the hybrid coordinator.
Sol coordinators implement complementary exclusive paths and review Luna's work.
Root independently reviews source, runs the normal application build/full suite,
then retires both clean worker trees and generated builds, retaining branches/logs.

Hybrid alone edits `document.h` and `sound_slice.cpp`; spans reads those paths.
Root owns both build manifests and status/index documents. Coordinators record
exact supporting path extensions on their own cards before edits, coordinate
any overlaps through root, and send root their manifest requirements. T-458
explicitly permits a pure packing allocator while T-423 is not landed; the
broader hold-conversion command/UI remains out of scope. T-466/T-467 writers,
T-459/T-461–T-464 UI and performance cards are outside this batch.

Main had user planning edits in task index, stems-performance and resume at
start. At the user’s explicit request these were committed separately as
`279c1d2` (profiling findings and performance follow-up cards).
Use scratch `/home/zardoru/devel/wormedit-scratch-builds/hybrid-oct7`, never
rely on `/tmp`. Normal dependency-aware builds only, immutable main dependency
source overrides, no CTest during relinks. New C++ functions/tests <=25 configured
clang-tidy complexity. No source chart writes, desktop test windows, pushes or
user app restarts. Existing unavailable-Xvfb restriction remains; this batch's
specified verification uses core/formats/runtime automated tests.


## Completed integration and evidence

Completed on main 2026-10-07 after root source review:

| Task | Main commit | Result |
| --- | --- | --- |
| T-457 | `4fbb58a` | Explicit optional stem/channel links, schema 5 persistence and schema 4 migration; strict linked-lane validation and empty stem handling. |
| T-458 | `d1c0ef8` | Continuation channels remain stems; restart-only channels use deterministic packed BGM lanes, preserving retained channels/events. |
| T-465 | `a95413a` | Detached deduplicated 44.1 kHz slice plans and exclusive atomic WAV publication; overlapping ranges, silent EOF continuations and soundless gameplay are supported. Target writers remain separate T-466/T-467 work. |
| T-460 | `648db28` | Only explicitly linked stems enter indexes, waveform projection or analysis jobs, including selected-marker and compatibility fallback paths. |
| Fixture follow-up | `4a6b4d1` | Two old command fixtures now clear BMSON-only links when changing chart family to BMS; channel/event data and original assertions remain. |

Root normal application builds passed, including the final fixture rebuild.
The first full run passed 2,525/2,527; its two failures exposed the invalid BMS
fixtures above. After correcting those fixtures, the final full suite passed
**2,527/2,527** in 49.26 seconds, including all 196 event-harness cases.
Configured complexity checks introduce no new or increased violations.
T-465 focused audio-on gates passed 18/18 and decoder-OFF pure/docs gates 11/11.
T-460 focused tests passed 10 window, 20 projection and 14 runtime cases,
including actual three-stem/1,000-packed-channel requests.

Root production checks used original charts read-only:

- Fresh Kick imports as nine linked stems plus three packed lanes, retaining
  all 12 channels. Its BMSON export is byte-identical to the prior 45,956-byte
  baseline. The production project-open path resolves its real audio sources;
  its detached plan has 514 distinct spans and 1,030 mappings, repeats exactly,
  and leaves native document bytes unchanged.
- The dragback BMSON baseline imports all 626 channels into six packed lanes
  and zero stems. The original BMS chart is unchanged; its CP932 fixture copy
  was normalized only in scratch for baseline conversion.
- Old schema 4 native Kick migrates with all 12 legacy links and otherwise
  unchanged content; old BMS dragback migrates with zero stem links and otherwise
  unchanged content. Migration preserves saved intent rather than reclassifying.
- SHA256 checks confirm both original chart files remain unchanged.

Evidence is retained outside worker builds under
`/home/zardoru/devel/wormedit-scratch-builds/hybrid-oct7/{root,hybrid,spans}`.
Root logs include `final-main-suite-passed.log`, `fixture-final-main-build.log`,
`production-final-checks.log`, `migration-checks.log` and `kick-spans-final.log`.
Both clean worker trees and their generated build directories were removed,
reclaiming about 4 GB. Only main remains registered; both worker branches and
logs are retained. No desktop test windows, source chart writes or pushes.

Remaining hybrid work: T-459 packed long-waveform display, T-462 per-section
Add, T-464 lane width/labels; T-461/T-463 retain their prerequisite gates.
Conversion writers T-466/T-467 and independent T-468 remain pending. The user's
profiling priorities T-444/T-440/T-470/T-469/T-471 are preserved in resume.
