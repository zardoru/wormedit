# October 7 rendering implementation batch

User requested the next delegated batch after `fdb35f6`. Root owns main and final
review. Two fresh worktrees only; retire each after integration and clean gates.

- `wormedit-bga-preview-oct7`, branch `batch/bga-preview-oct7`: T-451 separate
  BGA/Layer/Layer 2/Poor lanes and compositing, now unblocked by T-450.
- `wormedit-stem-pool-oct7`, branch `batch/stem-pool-oct7`: T-443 budgeted stable
  stem caches, now unblocked by T-439.

Each has a GPT-6.1 Sol medium coordinator and a GPT-6 Luna medium executor.
The four-agent limit means the BGA executor runs first; the stem coordinator
prepares a brief and reviews existing behavior meanwhile, then starts its Luna
when the BGA executor releases the slot. No shared edits or build directories.

BGA owns `src/ui/frontend.h` and `src/ui/nuklear/compose_canvas.cpp` first.
The stem coordinator must not edit those paths until BGA commits, root reviews
and integrates, and its worker receives the updated main commit. Root then
transfers those two paths to the stem worker. Both coordinators record exact
supporting source/test paths on their own card before editing. Existing files
owned by the other card must be coordinated with root, not edited concurrently.
Root alone owns build manifests and status/index documents unless it explicitly
assigns a narrow change. Coordinators edit only their own task card section.

Use workspace scratch `wormedit-scratch-builds/next-oct7`, never rely on `/tmp`.
Build with normal dependency-aware CMake, not `/fast`; serialize build writes,
use isolated build directories, and run configured complexity checks (new
functions and tests <=25). Preserve source originals. No desktop windows, pushes
or app restarts. The established isolated Xvfb restriction remains; use real
windowless renderer/event/projection checks and report the pending visual gate.
Source dependency symlinks may point to main's immutable libotoworm directory
for builds, but restore the empty gitlink directory before status/commit.

Do stale-plan checks, preserve out-of-scope limits. T-438/T-453 STOP audits and
T-440/T-444/T-455 are outside this implementation batch. Root will run a normal
application build, relevant independent checks, and the full suite after serial
integration, then update resume/card statuses and remove completed worktrees.

Agent thread limits rejected the second new coordinator and a new Luna thread.
Root reuses `/root/budget_recovery_61` as the T-443 Sol coordinator; T-451 Sol
reuses `/root/budget_recovery_61/budget_luna` for its executor. No old worktree
remains active. If repeated Luna model-capacity failures prevent implementation,
root authorizes the assigned Sol 6.1 medium coordinator to implement the narrow
brief and records the fallback; root still independently reviews final diffs.

## Narrow contract ownership update

To avoid an idle stem coordinator while BGA implements its wider card, root
allows the T-443 Sol coordinator to implement step 1's `stem_key` addition
ONLY inside `WormUiComposeStemWaveformView` in its isolated frontend.h, plus the
already owned BMSON projection and tests. BGA does not touch that struct. This
is an explicit exception to the earlier whole-file wait; root serially merges
the nonoverlapping contract edits. Compose canvas stays BGA-owned until handoff.
The stem coordinator can then implement renderer step 2 in its exclusively
owned field files, preserving card step order. The existing Luna slot remains
BGA-owned; a released Luna reviews/executes the remaining stem slice later.

The broader T-451 Luna task produced only the queue-state slice after a long
implementation interval. Root authorized a narrower handoff: the BGA Sol
coordinator interrupts it, confirms source ownership release, implements lane
projection and renderer slices in card order, and returns a concrete gesture
or test slice to Luna. The stem Sol independently implemented step 1 stable
ready/pending keys; root reviewed the actual diff and 41 assertions in three
production projection cases. It proceeds with exclusively owned renderer step 2.
No parallel writes to the same worktree paths are allowed.

Root further narrows shared canvas ownership: T-443 may edit only stem/waveform
envelope drawing and its key-pinning setup in its isolated compose_canvas.cpp;
T-451 owns only BGA lane/event drawing and gesture ghosts. The two feature
regions are independent. Any common helper required by both must be escalated
before edits. Root serially merges both canvas diffs, preserving both contracts.
This replaces the earlier whole-canvas wait without permitting same-worktree
concurrent writes. T-443's step 3 runtime source is ready for narrow Luna tests;
its Sol coordinator may proceed to step 4 cap removal/wiring in card order.

## Root integration and cleanup

T-443 accepted as `2c05e78` and T-451 as `7790258`. Sol 6.1 medium
coordinators completed and reviewed the implementation, with Luna medium
contributing the bounded runtime tests and queue state slice. Root reviewed
the actual diffs, required transient texture reservation and selected-waveform
priority, preserved stacked moves and storyboard ordering, and checked the
actual Add BGA lane dropdown. Shared frontend/canvas regions merged cleanly.

Before BGA integration, the normal main application build and all 2,475 tests
passed. Root's independent production Compose canvas software probe submits
300 textured stems, then reverses their order and changes selected audio with
zero regenerated rows or budget fallbacks (4,368,000 resident bytes).
`next-oct7/root/canvas-stems-probe.log` records the passing result.

Both worker trees are removed after clean commits and ownership release; their
branches remain. Only main is registered. Three older generated build trees
plus both completed isolated worker builds were removed, reclaiming about
13 GB. Worker test/tidy logs and root probe sources remain under
`/home/zardoru/devel/wormedit-scratch-builds/next-oct7/`; removed worker build
paths in earlier evidence refer to the completed runs, not reusable builds.
Original-chart GUI captures remain pending under the established Xvfb
restriction. No original chart, user desktop session or remote branch changed.

Final normal main application build passes. Full CTest passes **2,495/2,495**
(`root/final-main-ctest-verified.log`); final merged canvas probe also passes
(`root/final-canvas-stems-probe.log`). The first full integration run caught
the new dropdown bypassing the central chrome guard and an image test retaining
the fixture's video resource/dimensions. Root and the BGA coordinator corrected
those within the previously owned paths; all three focused regressions pass.
Correction clang-tidy reports only unchanged existing test scores.
