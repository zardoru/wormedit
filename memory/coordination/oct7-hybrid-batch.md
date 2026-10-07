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

Main has unrelated user planning edits in task index, stems-performance and
resume at start; preserve them and do not stage them in implementation commits.
Use scratch `/home/zardoru/devel/wormedit-scratch-builds/hybrid-oct7`, never
rely on `/tmp`. Normal dependency-aware builds only, immutable main dependency
source overrides, no CTest during relinks. New C++ functions/tests <=25 configured
clang-tidy complexity. No source chart writes, desktop test windows, pushes or
user app restarts. Existing unavailable-Xvfb restriction remains; this batch's
specified verification uses core/formats/runtime automated tests.
