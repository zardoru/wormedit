# Core edit contracts and storage support — 2026-10-29

Scope: T-777/T-778, lane B, base `d154284`. No sys/UI, build registrations, collection owners, formats or schema edits. Other lanes own those changes. This report records structural evidence; it does not claim measured developer-speed or build-time improvement.

## Before and after dependency reach

The first quick scan gave command/document reach 368/660. Its quoted-relative paths were not normalized, so it missed test-support edges. The reviewed comparison normalizes `../` include paths for BOTH snapshots, resolves local/include/src first-party includes, takes the transitive closure per unique tracked `.cpp`, and counts each source once. Before text comes from `git show d154284:<path>`; after text comes from this checkout, including new first-party headers. Third-party and generated headers are excluded. This explains the small difference from the earlier audit's 375/660 and avoids mixing denominators.

| Header | Before unique `.cpp` dependents | After |
| --- | ---: | ---: |
| command.h | 376 | 376 |
| document.h | 663 | 656 |
| interaction_types.h | 379 | 379 |

The final global result is modest: command and interaction-type reach are unchanged, while document reach falls by seven unique sources. Fixtures in grid_tests.cpp, snap_tests.cpp and lane_transform_tests.cpp genuinely require heavy types and now include those owners directly; counting their temporarily broken compilation as a dependency improvement would be misleading. Many consumers still independently require the heavy owners. The intended bounded benefit is visible in the migrated public-header closures (including the header itself):

| Public header | Before first-party headers | After |
| --- | ---: | ---: |
| interaction_types.h | 16 | 4 |
| snap.h | 16 | 7 |
| grid.h | 17 | 8 |
| notifications.h | 16 | 5 |
| sound_groups.h | 21 | 4 |
| proposal_apply_result.h | 21 | 1 |

`notifications.cpp`'s complete first-party include closure falls from 18 to 7 files. `document_command_types.h` owns the existing stamps, semantic target key, command result/payload, publication options and effect. `position_types.h` owns measure-local positions/spans. The proposal issue payload only needs `<string>` and now includes that directly. Types retain their identities and methods; method implementations remain at existing implementation owners. Heavy command callers keep command.h; no compatibility aliases or duplicate vocabulary are introduced. Implementations of snap/grid/lane transforms/sound groups explicitly include the document/transaction they use.

## Build graph and rebuild churn

The local Debug/Ninja application configuration has `WORMEDIT_ENABLE_LUA=OFF`. It has 1,202 first-party compile commands for 743 distinct source files: 459 duplicate compilation instances beyond the first. This lane changes no source registrations, targets or compiler settings, so duplicate compilation count is unchanged. Lane A owns target reuse. Include reach is a structural dependency estimate, not evidence that every dependency is compiled in every target. Weighting the same lexical source graph by these compile-command instances predicts command-header reach 634→634 and document-header reach 1,063→1,052. These are predictions, including conditional include edges, not measured baseline object rebuilds.

After the complete build, `ninja -C build -t deps` reports actual header dependencies in 627 object files for command.h, 1,050 for document.h, 1,061 for position_types.h, and 627 for document_command_types.h. In particular, separating the command values has not reduced the heavy command header's current compiled fanout. A complete baseline compiler graph was not built, so no exact whole-build savings or elapsed-time reduction is claimed.

For a representative compiler check, archived `d154284` source text into a plain temporary source directory (no checkout/worktree). Took each real source's existing compile_commands.json command, removed `-o`/`-c`, added `-M -MF - -MT probe`, and changed only first-party source/include roots between snapshots. Compiler, flags, working directory and external dependency sources stayed fixed. Counted distinct first-party source/header dependencies in GCC's output:

| Real translation unit | Before | After |
| --- | ---: | ---: |
| src/core/notifications.cpp | 18 | 7 |
| src/core/sound_groups.cpp | 26 | 29 |
| src/core/interaction.cpp | 55 | 58 |

The two heavy controls gain three light owner headers; they do not become cheaper include graphs. No synthetic consumer or toy edit is used. Raw observations: `/tmp/core-contracts-preprocessor.json`, `/tmp/core-contracts-ninja-deps.json`; compile-command/source closure counts are in `/tmp/core-contracts-measurement.json`.

The cold app build was deliberately interrupted when the coordinator reported memory/swap contention across four lanes. Completed local objects remain; root build outputs are never reused or mutated. Sources in root build/_deps and libotoworm are read-only overrides. No elapsed build or benchmark times from that run are meaningful.

## Real extension and navigation routes

T-637's completed typed edit route remains `bpm_editor.cpp` operation → transaction typed `stage/execute` in command.h → prepared native adapter in bms_program_edit.cpp → shared publication. Adding an edit still needs its public value/action declaration, domain operation and appropriate native collection mapping. It now ALSO declares `storage_support`: objects or deliberate legacy-only. That extra declaration is a real one-time/extension cost, accepted because unsupported prepared storage can no longer be inferred solely from a mutable legacy chart assumption. It is not a reduction in required edit sites for adding a new typed family.

The concrete `c596924` lane-key repair changed interaction.cpp and bms_program_edit_tests.cpp (two files). Before this refactor, stage_assign_note_lanes contained the domain algorithm PLUS the has_bms_program branch and native-object adapter call. After it declares objects and the selected identities in one stage call. Domain authoring remains in interaction.cpp, and storage dispatch is in command.cpp. Domain→runner→adapter still spans three implementation owners; it has not become a one-file route.

The concrete `72f342d` sound-membership repair changed semantic_layer_editor.cpp, bms_program_edit_tests.cpp and the edit-execution allowance (three files). Before, a separate domain helper resolved the target/catalog projection, staged settings, collected affected objects, replayed native-object mutation and combined effects. After, sound membership declares layer_settings_and_objects and supplies only its existing domain validation/mutation/effect. The transaction owns that adapter sequence, exact selected catalog lifetime and combined effect. It retains the existing immutable read_projection handle rather than adding another detached projection copy. The separate domain storage helper and domain entry branch are removed. Other existing domain adapters are deliberately not migrated without evidence.

The pilot itself touches more files than either original bug fix because it migrates six existing typed operation declarations and introduces shared contracts. Future changes to the two existing domain algorithms still require their domain implementation and regression file: no reduction in necessary edit-file count is claimed. The concrete gain is compile-checked storage policy and shared adapter orchestration, plus the seven source-level document dependency removals above.

Objects, difficulty layer settings, combined settings+objects and geometry name distinct existing authoritative adapters. Legacy-only uses the existing atomic prepared refusal. Typed operations allow objects/legacy-only; object operations without a native collection mapping still refuse prepared mutation. No callback can publish a detached projection or implicitly authorize multiple semantic Charts. The shared adapter retains the same two deterministic callback executions for combined settings+objects as the repaired sound-membership path; it is not a generic graph framework.

These are deterministic source-route observations, not a fresh agent navigation trial. The fixed exercises in workflow-navigation-baseline-2026-10-19.md used Luna medium and read-only contexts; this implementation used lead high and Luna-high hands. No before/after agent speed, hop or wrong-turn comparison is claimed. T-722 itself recorded 13 versus 9 inspected files despite fewer wrong guesses: moved lines and smaller entry files are insufficient evidence of faster navigation. T-638's existing traversal is reused, not reopened.

## Other recent-change controls

[PR75](https://github.com/zardoru/wormedit/pull/75) (latched selection Copy) remains a sys/event-harness workflow change: this lane does not reduce its four necessary edit files. [PR65](https://github.com/zardoru/wormedit/pull/65) (Wasm) touches platform/build/storage/UI; light contracts do not eliminate its platform-specific files. [PR64](https://github.com/zardoru/wormedit/pull/64)'s unused dependency removal belongs to build work and is not merged/taken over here. [PR63](https://github.com/zardoru/wormedit/pull/63)'s Quaver parser/export/fixtures remain format work; no format-registration or parsing simplification is claimed. These pending PRs were inspected as evidence only.

The 68-file sample-control change `c6eadce` still needs its semantic family and UI plumbing; this lane adds an intentional legacy-only declaration to its existing operation, not a replacement feature framework. Solo-layer selection `4c5fade` remains a selection/hit-test/sys concern, with no claimed reduction in its ten changed files. Recent prepared repairs, not synthetic toy edits, are the storage pilot.

## Verification and review

Configuration: default native application, Debug, Ninja, GNU C++ compiler unchanged, `WORMEDIT_ENABLE_LUA=OFF`. This is **not** the coordinator/A lane's Lua ON configuration or its 5,585-test universe. Lua-only consumers were not compiled here. The coordinator's combined Lua ON gate remains required for integration; the relevant small targets are `wormedit_scripting` (six translation units, including lua_document_api.cpp and lua_generator.cpp) and `wormedit_lua_tests`. No unrelated configuration was silently changed.

The final full gate ran with `PATH=/tmp/wormedit-clang-23.1.3/usr/bin:$PATH` and matching `LD_LIBRARY_PATH=/tmp/wormedit-clang-23.1.3/usr/lib` (preserving any existing library path), using `scripts/gate.sh build --jobs 2 --complexity-base d154284`. Real output tail:

```text
gate: 2 job(s), set explicitly; logs in /tmp/wormedit-gate.OktHbm
  pass  build
  pass  ctest
  pass  whitespace
  pass  complexity
  100% tests passed out of 5466
gate: passed
```

The gate's matching clang-tidy/clang/libclang 23.1.3 check passed all **19 changed translation units**. An explicit `scripts/check-complexity.py --build-dir build --jobs 2 --clang-tidy /tmp/wormedit-clang-23.1.3/usr/bin/clang-tidy --clang /tmp/wormedit-clang-23.1.3/usr/bin/clang src/core/notifications.cpp src/core/interaction_selection.cpp` with the same library path passed **2/2 additional header-consuming units** (`/tmp/core-contracts-header-complexity.log`). Thus 21 distinct units cover changed code and representative unmodified consumers. The checker runs the configured clang-tidy cognitive-complexity check; no allowances or NOLINT suppressions changed.

The real core binary filter `[bms-program-edit],*sound layer membership*,*lane assignment*,[edit-commit],[edit-staging]` passed **1,564 assertions in 89 test cases** (`/tmp/core-contracts-focused-final.log`). It includes ordinary edits; flat and conditional prepared lane assignment (tap/hold identities, WAV references, selection, Undo/Redo); flat and conditional sound membership (settings and content in one Undo); inherited/mixed-owner/collision refusals; same-lane no-ops; and deliberate legacy-only sample-control refusal with unchanged revision, owner, history and no legacy mirror. It reuses domain fixtures and existing runner coverage.

The first complete gate found one new test with complexity 29, a regression rather than a baseline exception. Extracting its coherent prepared-history invariant assertions retained every assertion and storage variant. The affected check, focused tests and complete gate were rerun successfully. Cold-build missing manual outputs and fixture transitive-include failures were resolved before this final gate; none is waived as a baseline failure. Test-generated `.agent/osu-stepmania-publication/song.wormedit` output was restored and excluded from the commit.

Dedicated documentation checks after the final card/report update passed 6/6 (`/tmp/core-contracts-docs-final.log`); final staged whitespace validation also passed. Review checklist: one definition per concept; no parallel result/effect vocabulary or aliases; existing transaction/gesture runners retained; scoped authority stays in prepared adapters; no hand-rolled parser, schema, frontend or build-registration changes; no complexity allowances raised. The bounded verdict is a smaller public contract surface and explicit storage safety, with modest document dependency savings and neutral command fanout, not evidence of generally faster navigation or builds.

Coordinator integration: [PR81](https://github.com/zardoru/wormedit/pull/81) narrows the new
composite API to exercised object and combined layer/object modes and documents deterministic
replay. It also fixes the direct Lua document declaration and fixture include exposed by
lighter headers. Combined Lua-enabled GATE passes 5,596/5,596, including all 109 Lua tests,
and 45 changed-TU complexity checks. The standalone measurements above remain scoped to B;
see the [integration audit](navigation-modifiability-2026-10-29.md) for combined acceptance.
