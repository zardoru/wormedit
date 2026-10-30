# Frontend boundary guard — 2026-10-29

Round two, T-784, base `23e113b557bb5a41fa753a32bbcda5ba0661cfb2`, continuing PR81.

The original toolkit guard scanned public core/audio/formats headers and their `.cpp`
implementations but omitted private headers. It also did not enforce frontend contract
imports. The new inventory includes private headers and frontend contracts. A reviewed
allowlist admits existing shared core value vocabulary; document/history/interaction state,
formats/audio services and resolved private application/domain paths are rejected. Relative
paths resolving outside the frontend cannot hide a first-party service import.

This is a direct source-include guard. It does not claim to parse macro-generated includes,
prove arbitrary aliases safe, or validate every future transitive dependency of an approved
value header. A new shared public include needs a seam review rather than a blanket exception.

## Concrete dependency correction

`src/ui/frontend/bga_usage.h` imported `document.h` for IDs, a local position and a visual
kind. Those values already have owners: `position_types.h` and `vocabulary.h`. Reusing them
removes the model import without parallel enums, new value types or changed view/action data.
The shared frontend target and its support dependencies build successfully after this change
(`/tmp/wormedit-round2-frontend-build.log`). No direct-include fallout was needed in that
bounded target; the remaining consuming targets await combined integration verification.

Frozen pre-build root Make dependencies cover 1,076 of 1,078 first-party compile entries.
`bga_usage.h` reaches 460 objects/284 sources; `actions.h` reaches 452/281; `document.h`
reaches 972/656. These are existing compiled dependency counts, not savings. Raw per-object
coverage is saved in `/tmp/wormedit-round2-before-dependencies.json`; a subsequent comparison
must use the same covered object set and explicitly account for newly registered sources.

For a same-command compiler control, archived only base `src/` and `include/` into
`/tmp/wormedit-round2-baseline-source`. Each source uses its current real compile command,
working directory and external/generated includes. Removed only `-o` plus its output and
the `-c` switch, changed the first-party source/include roots for the baseline snapshot,
and requested GCC `-MM`. Counts include the source itself and distinct first-party headers:

| Translation unit | Before files | After files | document.h before → after |
| --- | ---: | ---: | --- |
| ui/nuklear/chrome/components.cpp | 58 | 51 | yes → no |
| ui/nuklear/compose/compose_input_panel.cpp | 120 | 120 | yes → yes |
| ui/nuklear/compose/compose_recipe_form.cpp | 76 | 76 | yes → yes |
| sys/compose/input/compose_input.cpp | 223 | 223 | yes → yes |

The three neutral controls still independently need the model. No general build-time or
whole-frontend fanout gain is inferred from the component result. Reproduction helper:
`/tmp/wormedit-round2-include-probes.py`; exact commands/dependencies:
`/tmp/wormedit-round2-include-probes.json`; summary:
`/tmp/wormedit-round2-include-probes.log`.

## Verification checkpoint

`architecture.ui_frontend_boundary`, the new guard regressions and
`documentation.chart_editor_state` pass 3/3 (`/tmp/wormedit-round2-guard-tests.log`).
The regression script exercises toolkit imports from private core/audio/formats headers,
frontend model/history/interaction imports, a relative private-header escape, and legal
vocabulary/position/standard-library includes. Rejections require the correct diagnostic
family, not merely a failed command. It creates only its dedicated fixture directory and
cleans each case.

Explicit matching Clang/clang-tidy/libclang 23.1.3 complexity passes all four compiler-control
consumers (`/tmp/wormedit-round2-boundary-complexity.log`, details
`/tmp/wormedit-complexity.592s3ljd`). Final combined GATE and matched post-build dependency
counts are pending. No allowance, complexity baseline or runtime schema changes.

## Combined acceptance and dependency result

The initial combined build exposed the sys BGA adapter's retired transitive import.
`bga_usage_view.h` now declares its document argument and its `.cpp` directly includes the
model it queries. The frontend contract stays light; no model allowance was introduced.

Final combined Lua ON GATE passes build, 5,602/5,602 tests, whitespace and 10 changed-TU
complexity checks (`/tmp/wormedit-round2-gate-verified.log`, `/tmp/wormedit-gate.LyyJ9B`).
Eight explicit additional consumers pass with matching 23.1.3, including three frontend
controls, application/same-sound consumers and the two unchanged internal input consumers.
Summary: `/tmp/wormedit-round2-header-complexity.log`; list:
`/tmp/wormedit-round2-header-consumers.txt`. Focused workflows pass 847 assertions/58 cases;
the event harness passes 721/721; an isolated Compose smoke is opened and inspected.

Re-reading the same 1,076 pre-existing object dependency files after the successful build:

| Header | Before objects/sources | After objects/sources |
| --- | ---: | ---: |
| document.h | 972 / 656 | 884 / 580 |
| frontend/bga_usage.h | 460 / 284 | 460 / 284 |
| frontend/actions.h | 452 / 281 | 452 / 281 |

This is a matched object set, not the entire configured database. The two originally
unbuilt entries belong to the excluded BMS edit benchmark; the four new policy-owner
objects are excluded from both sides. No earlier object disappeared from the comparison.
The unchanged BGA/action-header reach shows that the gain is removing their dependency
on the heavy model, rather than shrinking the transport's own reach. New policy objects
also depend on the model, so the matched 88-object reduction must not be reported as an
unadjusted whole-build net reduction.

The final same-command compiler controls are 58→51 for components, 120→120 for the input
panel, 76→76 for the recipe form, and 223→224 for input dispatch. The extra input header
is the admission owner; it is a negative include-count result. All compiler-control paths
have no parent-directory aliases. The earlier table isolates BGA cleanup before admission;
final source/commands/dependencies are in `/tmp/wormedit-round2-include-probes-final.json`.

Measurements and reproduction helper: `/tmp/wormedit-round2-rebuild-measurements.json`,
`/tmp/wormedit-round2-measure-edits.py`; all simulated source timestamps are restored.
No wall-time or general navigation-speed improvement is claimed.
