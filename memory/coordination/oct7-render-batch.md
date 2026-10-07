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
