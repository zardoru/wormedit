# Task cards: recovery journal size and cost

[Back to the task-card index](../tasks.md).

Release blocker (owner, 2026-10-27). The T-121 journal appends a complete native post-state per
accepted publication. A one-note move in `0_fgd_I` (23k items) writes a 24.2 MB record and costs
about 1.4 s of worker time; the 128 MiB journal holds five such records. The editor targets
charts of about 50,000 items (owner, 2026-10-27), where a record is about 57 MB and an append
takes about 3.4 s. Measurements and an independent review:
`/tmp/claude-1000/-home-zardoru-devel-wormedit/2038fa19-7aa5-4648-8c6f-387e2b0c4552/scratchpad/codex/report.txt`.
The [recovery design](../../references/recovery-design.md) "Command journal" section is
the contract these cards change. The owner decision "the command journal replays edits"
([open questions](../decisions/open-questions.md)) stays: records persist accepted post-state
values, never command intent.

## T-769 Make journal flushes and retries durable

- **Status:** done (2026-10-27; lane wyrm, report `/tmp/wormedit-lanes/journal/wyrm/report.txt`; main working-tree GATE 5,452/5,452 with build, whitespace and complexity (`/tmp/wormedit-lanes/journal/main-gate-1.log`); the pre-save call stays wait-only because `mark_explicit_save(saved, current)` performs the durable flush after the save)
- **Outcome:** an identical retry succeeds at capacity, `append` no longer re-parses its own record, journal payloads are compact JSON, the explicit flush publishes any required checkpoint and reports durability, and application shutdown flushes before stopping the autosave scheduler.
- **Read first:** `src/core/command_journal.cpp`, `src/core/command_journal_runtime.cpp`, `src/core/autosave_loop.cpp`, `src/sys/app/application.cpp` (`shutdown()`), `docs/references/recovery-design.md`.
- **Files owned:** `include/wormedit/core/command_journal.h`, `src/core/command_journal.cpp`, `src/core/command_journal_runtime.cpp`, `include/wormedit/core/autosave_loop.h`, `src/core/autosave_loop.cpp`, `src/sys/app/application.cpp` (shutdown only), `include/wormedit/formats/native_project.h` and `src/formats/native_project.cpp` (compact write option only), `tests/core/command_journal_tests.cpp`, `tests/core/autosave_loop_tests.cpp`, `docs/architecture.md` (journal paragraph).
- **Steps:** the five numbered items of the lane brief, in order, each with a regression test that fails first.
- **Invariants:** the record format version and the recovery reader stay unchanged; explicit saves stay pretty-printed; ordinary edits never wait for recovery I/O.
- **Reject/failure behavior:** a failed append or flush leaves the previous valid prefix and backup untouched and requests a checkpoint.
- **Verify:** `ctest --test-dir build -R 'journal|autosave|recovery' --output-on-failure`, then GATE.
- **Out of scope:** delta records (T-772); scan budget (T-770).

## T-770 Keep unrelated files from hiding recovery candidates

- **Status:** done (2026-10-27; lane wyvern, only prefix-matching names count against the budget, 512 by default, hard cap 1,024; main working-tree GATE 5,452/5,452 with build, whitespace and complexity (`/tmp/wormedit-lanes/journal/main-gate-1.log`))
- **Outcome:** a valid journal and adjacent backup in a directory with 600+ unrelated files are still discovered, and the scan stays bounded.
- **Read first:** `src/formats/native_project_storage.cpp` (`inspect_caller_recovery_directory` and the second directory loop), `docs/references/recovery-design.md`.
- **Files owned:** `src/formats/native_project_storage.cpp`, `include/wormedit/formats/native_project_storage.h`, the recovery-scan tests.
- **Steps:** 1. Add the 513-unrelated-files regression. 2. Count or look up only recovery-shaped names against the budget. 3. Apply the same fix to every loop of that shape.
- **Invariants:** candidate, byte and diagnostic budgets still bound the scan; scanning never mutates files.
- **Reject/failure behavior:** a directory over the remaining budget reports `resource_limit` without discarding candidates already matched.
- **Verify:** the new regression and `ctest --test-dir build -R recovery --output-on-failure`, then GATE.
- **Out of scope:** moving recovery artifacts to a dedicated directory.

## T-771 Benchmark journal costs at 50,000 items

- **Status:** done (2026-10-27; Debug at 50k notes: write 2.46 s, parse 8.15 s, append 20.70 s, read of three records 41.17 s; main working-tree GATE 5,452/5,452 with build, whitespace and complexity (`/tmp/wormedit-lanes/journal/main-gate-1.log`))
- **Outcome:** `tests/core/command_journal_benchmarks.cpp`, hidden behind `[.benchmark]`, reports write, parse, one-note-move append and journal read times for a synthetic 50,000-item document.
- **Read first:** `tests/core/command_journal_tests.cpp`, `tests/CMakeLists.txt`.
- **Files owned:** `tests/core/command_journal_benchmarks.cpp`, `tests/CMakeLists.txt`.
- **Steps:** 1. Build the synthetic document in memory. 2. Add Catch2 `BENCHMARK` cases. 3. Document the invocation in the file header.
- **Invariants:** ctest and GATE never run the benchmark; it needs no external files.
- **Reject/failure behavior:** none; measurement only.
- **Verify:** GATE (compiles, not run), then the explicit `"[.benchmark]"` invocation.
- **Out of scope:** pass/fail thresholds (T-774).

## T-772 Journal accepted post-states as id-keyed deltas between seeds

- **Status:** done (2026-10-27; lane wyrm; merged into main's working tree. Real `0_fgd_I` Release check by the coordinator: one-note move in the scoped program writes a 434-byte delta whose replay serializes identically, but the diff costs 528 ms (seed write 182 ms) and each replayed delta 740 ms; follow-up T-776)
- **Outcome:** with the T-771 benchmark at 50,000 items, a one-note-move record is at most 16 KiB and its append takes at most 10% of a full seed append, and recovery reconstructs a document whose canonical native serialization equals the accepted post-state.
- **Read first:** `docs/references/recovery-design.md` (Command journal), `src/core/command_journal.cpp`, `src/formats/native_project.cpp` (value serializers), `include/wormedit/core/document.h` (`song_document_t`), `docs/chart-editor-plan/decisions/code-style.md`.
- **Files owned:** `include/wormedit/core/command_journal.h`, `src/core/command_journal.cpp`, `src/core/command_journal_runtime.cpp`, `src/formats/native_project_storage.cpp` (journal header detection only; coordinator-approved supporting-file extension), new `include/wormedit/formats/native_project_delta.h`, new `src/formats/native_project_delta.cpp`, `src/formats/native_project.cpp` and `src/formats/native_project_value_codec.h` (shared existing value serializers, validation and collection definitions), new `tests/formats/native_project_delta_tests.cpp`, `tests/core/command_journal_tests.cpp`, `tests/core/command_journal_benchmarks.cpp`, `CMakeLists.txt`, `tests/CMakeLists.txt`, `docs/architecture.md`, `docs/references/recovery-design.md`.
- **Steps:**
  1. Bump the journal header to version 2: record kinds `seed` (compact full post-state, as v1) and `delta`. Version 1 journals stay readable.
  2. Add `native_project_delta_t::from(before, after)` and `apply_on(document)`. Top-level fields that are unchanged are omitted; collections with stable IDs (Charts, prepared BMS programs, resources, assets, groups, recipes, and inside them measures, objects, scoped nodes and content items) get upsert/remove by ID plus explicit order when the order changes; other fields are replaced whole. Shared `shared_ptr<const>` owners compare by pointer first. Values are encoded with the native codec's own serializers, never a second serializer.
  3. The worker diffs against the last accepted post-state the writer already caches; the main thread and the publication observer are unchanged.
  4. Write a seed for the first record of a segment, after any failed, skipped or overflowed publication, after compaction leaves no seed, when a delta would exceed half the seed, and every 128 deltas (replay-work budget). Never bridge a gap with a delta.
  5. Chain records: each delta names its base sequence and the base record's checksum; seeds carry their payload checksum.
  6. Reader: decode the seed, apply deltas in order, stop at the first bad record, run full native validation on the reconstructed final state; on failure fall back to the latest seed state with a diagnostic. Scanner, ranking and recovery actions keep consuming one validated snapshot.
  7. Compaction through a cursor keeps the newest seed at or before the first retained delta.
  8. Tests: `apply_on(from(a, b), a)` equals `b` by canonical serialization for every command family, including Undo/Redo, divergent edits after Undo, timing propagation, BMS inactive branches and catalog edits; truncation at every record boundary and inside records; corrupt checksum, gap, duplicate and wrong-base records; overflow forcing a reseed; v1 journal compatibility.
  9. Extend the T-771 benchmark with the delta append and record size; record the numbers here.
- **Invariants:** no journal code in domain edits; recovered documents are lossless (inactive bodies, exact fractions, provenance, retained-source flags); an unsupported difference falls back to a seed, never drops data.
- **Reject/failure behavior:** a damaged record leaves the preceding valid state recoverable; a journal that cannot be reconstructed never hides another valid backup.
- **Verify:** `ctest --test-dir build -R 'journal|delta|recovery|native' --output-on-failure`, the benchmark figures against the Outcome thresholds, then GATE.
- **Scope approval (lane coordinator, 2026-10-27):** the internal codec bridge also exposes existing validation and shared collection definitions, so delta admission reuses native checks and collection field names have one owner. Also approved `src/formats/native_project_storage.cpp` header detection: both scanner classification and recovery parsing otherwise recognize only the new v2 header and hide valid v1 journals. No native persisted field or schema changed.
- **Regression evidence (2026-10-27):** 26 new cases; complete names in `/tmp/wormedit-lanes/journal/delta/regression-tests.txt`. Failing-before logs: `red-version.log`, `red-delta.log` (56,633-byte full record), `red-wrapper.log`, `red-zero.log`, `red-schema.log`, `red-v1-scan.log` in that directory. Key cases: "A one-note journal delta stores accepted values by identity"; "Native deltas recover inactive scoped BMS content exact fractions and source provenance"; "Native deltas preserve linked propagation Undo Redo and divergent edits"; "Journal recovery stops before wrong-base gaps and conflicting duplicate deltas"; "Journal truncation at each record boundary and within seeds and deltas retains its prefix"; "Native validation failure after deltas recovers the latest complete seed"; "Journal seeds bound replay to 128 deltas"; "Queue overflow checkpoints the skipped publications and the next edit starts a seed"; "Native deltas preserve signed zero in persisted storyboard numbers".
- **Debug benchmark evidence (five samples, 50,000 notes):** `write_native_project` 2.46644 s; parse 7.99477 s; seed append 4.97955 s; delta append 194.316 ms (3.9023% of seed, below 10%); seed record 13,784,195 bytes; one-note delta 740 bytes (below 16 KiB); three-record read 28.3801 s. Invocation: `build/tests/wormedit_command_journal_benchmarks '[.benchmark]' --benchmark-samples 5 --benchmark-no-analysis --benchmark-warmup-time 0`. Output tail: `All tests passed (33 assertions in 1 test case)`; full log `/tmp/wormedit-lanes/journal/delta/benchmark-final-debug.log`.
- **Implementation evidence:** ID-keyed project/chart/measure/event collections, scoped difficulty/node/content IDs, generated object `value/id`, generated-chart/bookmark `chart`, storyboard `stable_id`; other lists replace whole. Every seed fallback and the values lacking IDs are enumerated in `/tmp/wormedit-lanes/journal/delta/report.txt`. Recovery validates intermediate states as well as the final state; changed BMS owners still encode canonical owner values before diffing. T-774 retains the scoped/main-thread-copy performance decision.
- **Verify evidence:** final focused filter: `100% tests passed out of 240`, `Total Test time (real) = 38.64 sec` (`/tmp/wormedit-lanes/journal/delta/verify-focused-release.log`). GATE: `scripts/gate.sh --jobs 4 --complexity-base HEAD`; 5,478/5,478 tests, build, whitespace and complexity passed; `complexity: checked 7/7`, `complexity: passed (7 translation units)`. Full logs in `/tmp/wormedit-lanes/journal/delta/`; gate output tail:

  ```text
  gate: 4 job(s), set explicitly; logs in /tmp/wormedit-lanes/journal/delta/wormedit-gate.wr8xtH
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 5478
  gate: passed
  ```
- **Out of scope:** native schema changes (T-773); shared change tracking (T-774); command-intent replay (rejected above).

## T-773 Store repeated provenance once in native projects

- **Status:** dropped (2026-10-27; implemented in lane wyrm, merged, then reverted after the owner approved compact saves (T-775), which shrink more with no schema change. Pretty saves were 25.05% smaller and compact 31.6%, but Release writes went from 180 to 295 ms on `0_fgd_I`. Patch kept at `/tmp/wormedit-lanes/journal/provenance/coordinator.patch`; report `/tmp/wormedit-lanes/journal/provenance/report.txt`)
- **Outcome:** `0_fgd_I` converted with `wormedit-convert` saves at least 25% smaller than schema 26 output, round-trips byte-identically, and schema 26 files migrate losslessly to schema 27.
- **Read first:** `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `include/wormedit/core/document.h` (`source_provenance_t`), `docs/chart-editor-plan/decisions/code-style.md` (schema rule, one table per fact).
- **Files owned:** `include/wormedit/core/document.h` (schema constant only), `src/formats/native_project.cpp`, `src/formats/native_project_migrations.cpp`, `src/formats/native_project_delta.cpp` (encoding follow-up), `src/formats/native_project_value_codec.h`, `tests/formats/native_project_tests.cpp`, `tests/formats/native_project_delta_tests.cpp`, `tests/formats/bms_program_migration_tests.cpp` (current-schema assertion only), `docs/architecture.md`, `docs/lua-api/README.md` if it names the schema.
- **Scope approval (lane coordinator, 2026-10-27):** extend `src/formats/native_project_value_codec.h` with the shared codec-only provenance table expansion/packing boundary, so migrations and encoded journal replay use one implementation. `tests/formats/bms_program_migration_tests.cpp` replaces its literal current-schema expectation with the shared schema constant, retaining its schema25 boundary-preservation fixture. This card file records evidence; Status stays for the collecting coordinator.
- **Steps:** 1. Measure which repeated fields dominate `0_fgd_I` (provenance is about a third of each content item). 2. Encode them as a document-level table referenced by index, in the codec only; the in-memory model is unchanged. 3. Bump to schema 27 with a 26→27 migration. 4. Keep journal deltas encoding the table changes they need.
- **Invariants:** the in-memory document and every older fixture are unchanged; saves stay deterministic.
- **Reject/failure behavior:** an out-of-range table index rejects the file with a diagnostic and no partial document.
- **Verify:** `ctest --test-dir build -R 'native|migration|delta' --output-on-failure`, the size comparison, then GATE.
- **Measurement evidence (2026-10-27):** the source BMS is readable; the actual schema26 converter reproduces the supplied 22,501,881-byte fixture. Its 23,817 provenance objects take 2,504,324 compact bytes; there are 1,219 tuples without order but only 2,953 complete records. Source order repeats across items from one source line and is retained in every distinct record. Two family/profile/spelling groups and parallel source-field/order columns avoid the flat table's roughly 23% pretty saving.
- **Size and losslessness evidence:** actual `wormedit-convert` schema27 output is 16,865,880 bytes, **25.0468% smaller**; compact 7,547,706 → 5,165,224 bytes (**31.5656% smaller**). Gzip9 pretty/compact: schema26 356,066/271,637; schema27 312,248/268,273 bytes. Independent table expansion reconstructs schema26 JSON in every field. Direct conversion, migration and repeated parse/write produce identical schema27 bytes (`fixture-compare-final.log` under the log directory below).
- **Debug timing evidence (three-sample medians, codec only, no disk I/O):** native parse 5865.34 → 5559.87 ms; pretty write 1792.91 → 2622.54 ms; compact write 1732.04 → 2580.85 ms. Schema26 load through migration is 7220.89 ms. Smaller output trades for **46.27% slower Debug pretty writes**; retaining row references removed one redundant lookup pass. Raw samples: `bench26.log`, `bench27-final.log`, `bench27-migration-final.log`.
- **Regression evidence:** six new cases listed in `/tmp/wormedit-lanes/journal/provenance/regression-tests.txt`. Observed failures before fixes: `red.log`, `red-schema26-deltas.log`, `red-write-expanded-budget.log`. Coverage includes shared/distinct source orders, deterministic round trips, lossless migration, malformed index/column/group data, expansion limits, new provenance inserted before existing table indices, two-delta replay, and schema26 replay before migration. Value serializers and patches stay self-contained; seeds expand once. Both saved and expanded forms obey codec bounds. In-memory provenance is unchanged.
- **Verify evidence:** GATE checks all 5,488 tests plus build, whitespace and complexity (6/6 translation units); all passed. The final exact native/migration/delta filter passed 216/216 in 2.43 s; the broader journal/recovery filter passed 268/268. Documentation passed 6/6 after recording the evidence. Final regression/rollback filter: 187 assertions in seven cases. Logs, probes, file list and report: `/tmp/wormedit-lanes/journal/provenance/`; final gate log `gate-final-3.log`, detailed directory `wormedit-gate.njY3Bo`. Status is left for the collecting coordinator. Actual gate tail:

  ```text
  gate: 4 job(s), set explicitly; logs in /tmp/wormedit-lanes/journal/provenance/wormedit-gate.njY3Bo
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 5488
  gate: passed
  ```
- **Out of scope:** pretty versus compact saves ([open question 28](../decisions/open-questions.md), T-775); folder or archive layout (open question 12).

## T-774 Decide whether journal diffing needs shared change tracking

- **Status:** done (2026-10-27; owner chose option (a): "skipping delta validation sounds fine". Delta appends no longer validate the whole post-state; seeds and the reader's final replayed state still do)
- **Outcome:** the T-771 benchmark at 50,000 items shows whether the T-772 diff, plus the document copy on the main thread, stays under 25 ms per commit in the Debug build; the result is recorded here.
- **Read first:** `tests/core/command_journal_benchmarks.cpp`, this file's T-772 numbers.
- **Files owned:** this card.
- **Steps:** 1. Run the benchmark for both flat 50k-note and scoped BMS 50k-item documents. 2. If both stay under the budget, set Status to `done (not needed: <numbers>)`. 3. Otherwise **STOP** and report the numbers to the owner with a proposal (change sets from the transaction, or immutable chunks).
- **Invariants:** measurement only.
- **Reject/failure behavior:** none.
- **Verify:** the benchmark output tail pasted here.
- **Out of scope:** implementing change tracking before the owner approves it.
- **Evidence (2026-10-27, Debug, one sample, from `/tmp/wormedit-lanes/journal/replay/benchmark-final.log`):**
  - Flat 50k notes: the delta append is 179.9 ms. `native_project_delta_t::from` takes 167.5 ms, of which full native validation of the post-state is 157.8 ms. Checksum 0.02 ms, file append 0.04 ms.
  - Scoped BMS 50k items: the delta append is 3,555 ms (seed 14,255 ms). `from` takes 3,549 ms, of which validation is 3,466 ms.
  - Without validation, the diff itself is about 10 ms flat and about 85 ms scoped. The main-thread document copy is 0.07 ms (shared owners), so change tracking from the transaction or immutable chunks would save almost nothing.
  - Real `0_fgd_I` in Release (coordinator): `from` takes 54.3 ms with the edited program and 44.1 ms for a metadata-only change, again mostly validation.
- **Proposal for the owner:** decide whether delta appends must re-validate the whole document.
  - (a) Skip writer-side validation for deltas. The reader already validates the final replayed state and falls back to the latest seed. An invalid in-memory state would also fail an explicit save, so recovery is never worse than saving.
  - (b) Validate only the owners the delta touches. This needs validation that can run per owner.
  - (c) Keep it. It runs on the journal worker, never on the main thread.

  Recommendation: (a), keeping full validation on seeds.

## T-776 Replay journal deltas on the encoded tree and diff scoped programs by item

- **Status:** done (2026-10-27; lane wyrm, report `/tmp/wormedit-lanes/journal/replay/report.txt`; merged into main's working tree, main GATE 5,482/5,482 with build, whitespace and complexity (`/tmp/wormedit-lanes/journal/main-gate-3.log`); coordinator Release check on real `0_fgd_I`: one-note diff 54.3 ms, down from 528 ms, of which a metadata-only diff is 44.1 ms; delta 434 bytes; canonical round trip equal)
- **Outcome:** in the T-771 benchmark at 50,000 items, reading a seed plus 128 one-note deltas takes at most 1.5 times reading the seed alone, and a one-note-move delta append inside a scoped BMS program of 50,000 content items takes at most 25% of that document's seed append; recovered documents still serialize identically to the accepted post-state.
- **Read first:** T-772 above, `src/formats/native_project_delta.cpp`, `src/core/command_journal.cpp` (`decode_post_state`, `read_journal_line`, `checkpoint`), `include/wormedit/core/bms_program.h` (prepared program nodes and content items), `docs/chart-editor-plan/decisions/code-style.md`.
- **Files owned:** T-772's files plus `src/formats/native_project_bms_codec.inc` (supporting-file extension below).
- **Scope approval (lane coordinator, 2026-10-27):** `src/formats/native_project_bms_codec.inc` is included in the native codec ownership: factor its existing owner/node field serializers so typed diffs reuse the single encoder without encoding unchanged content. `tests/CMakeLists.txt` links the benchmark to its existing JSON dependency for component measurements. No native persisted fields or schema change.
- **Steps:**
  1. Benchmark first: add a synthetic scoped BMS fixture of 50,000 content items to the T-771 benchmark, plus a read of seed + 128 deltas, and record the T-772 numbers and a cost breakdown of the delta append (validation, encoding, diff, checksum, file write).
  2. Replay deltas on the encoded native tree: parse a segment's seed JSON once, apply each delta's patch to the tree, and run the native reader (full validation) once on the final state. Structural, checksum and chain failures still stop at the preceding record; a final-state validation failure still falls back to the latest seed. Compaction and the writer's reopen path use the same replay.
  3. Diff scoped BMS programs by typed node and content-item identity, comparing typed values before encoding, so a changed program encodes only its changed items and its small owner-level fields; keep the native codec's pool-reference form for what is encoded. Unsupported or ambiguous comparisons still fall back to a seed.
  4. Tests: replayed state equals the canonical serialization of the writer's accepted state for the T-772 command-family cases, including scoped BMS item edits, inserts, removes and reorders; a delta tail whose final state fails validation recovers the latest seed.
  5. Re-run the benchmark and record the numbers here.
- **Invariants:** T-772's invariants; intermediate states are never exposed as recovered snapshots unless validated.
- **Reject/failure behavior:** as T-772.
- **Verify:** `ctest --test-dir build -R 'journal|delta|recovery|native' --output-on-failure`, the benchmark figures against the Outcome, then GATE.
- **Implementation evidence (lane wyrm, 2026-10-27):** `native_project_replay_t` retains the bounded parsed tree and cached collection positions; transactional patches roll back on structural/budget failures. The shared native reader accepts that parsed tree, so no full domain encode/dump/parse runs per delta. Reader, reopened writer and compaction share segment replay. Typed scoped node/item comparisons encode changed values through the native codec; unchanged canonical catalog inputs avoid copying the graph. Changed catalog inputs use the codec's canonical pool-reference fields. Native schema and journal format are unchanged.
- **Regression evidence:** new cases "Journal replay validates the final delta state before publishing a snapshot", "Native scoped deltas preserve content insertion removal and explicit order", "Encoded native replay rolls back partial patches and codec budget failures", and "Reopened migrated journal seeds reseed before accepting another publication". Every command-family delta round trip also exercises encoded-tree replay in both directions. Observed failing-before evidence: `red-final-validation.log`, `red-scoped-threshold.log`, and `red-reopened-migrated-seed.log` under `/tmp/wormedit-lanes/journal/replay/`; `red-scoped-threshold.log` measured 16.878 s against the 3.686 s scoped-append budget. Existing final-validation fallback, corruption, truncation, compaction, reopen, inactive-body and catalog tests remain covered.
- **Real-chart cost evidence (Debug):** instrumenting the HEAD delta algorithm on `0_fgd_I` measured 3,642.1 ms total: validation 433.416 ms, encoding both owners 1,198.526 ms, DOM diff 1,692.777 ms, remaining field work/destruction 317.381 ms. Catalog refresh was 21.6 ms and owner reconstruction/projection 88.2 ms per owner: encoding and DOM diff dominate. The typed implementation measured 542.2 ms (validation 433.6 ms, small owner fields 66.0 ms, remaining typed diff/encoding about 42.6 ms), retained the 434-byte delta, and serialized identically after replay. Log: `/tmp/wormedit-lanes/journal/replay/real-profile.log`.
- **Recovery fallback evidence:** writer seed policies are unchanged except that reopening a seed requiring native migration now forces a new seed (its migrated domain snapshot cannot be used as the old encoded base). Final graph, enum, schema, numeric or field-shape failure recovers the latest seed; an invalid seed recovers the prior validated segment. Structural/checksum/chain failures retain the preceding patch prefix, which is validated before exposure. Encoded-tree byte/node/depth failures roll back the patch and retain that prefix. A delta cannot extend an unmigrated seed. No intermediate unvalidated snapshot is exposed.
- **Debug benchmark evidence (one sample, exact scoped count):** flat 50,000 notes: seed-only read 10.6441 s, seed + 128 one-note deltas 10.7202 s (1.00715×). Scoped 50,000 content items (49,600 notes + 400 measure definitions): seed-only read 29.4623 s, seed + 128 deltas 29.6194 s (1.00533×); seed append 14.2554 s, delta append 3.55529 s (24.94%, below 25%). All recovered states serialized identically. Seed/delta records: flat 13,784,195/740 bytes; scoped 29,721,649/923 bytes. The exact-count HEAD-algorithm baseline took 16.4367 s per scoped delta vs 14.1132 s per seed and failed the threshold (`benchmark-baseline-exact.log`). Its measured phases were validation 3.43010 s, encoding 6.00980 s, DOM diff 5.82021 s, remainder 1.17663 s. Current scoped `from`: validation 3.46591 s, small-field encoding 0.000866 s, typed comparison/changed encoding/envelope work about 0.08262 s; checksum 0.0363 ms and direct payload file append 0.0467 ms. Original T-771 cases also passed: write 2.46981 s, parse 7.98001 s, seed append 4.88561 s, delta append 179.693 ms, three-record read 10.6544 s. Invocation: `build/tests/wormedit_command_journal_benchmarks '[.replay-benchmark],[.delta-breakdown],[.benchmark]' --benchmark-samples 1 --benchmark-no-analysis --benchmark-warmup-time 0`; tail: `All tests passed (100 assertions in 3 test cases)`. Log: `/tmp/wormedit-lanes/journal/replay/benchmark-final.log`.
- **Harness correction:** the initial manual real-chart probe aborted because it omitted the library's `JSON_DIAGNOSTIC_POSITIONS=1` definition at internal JSON return-value boundaries. Recompiling with that definition completed with exit 0 and canonical equality (`real-corrected.log`, `real-profile.log`). The CMake benchmark already receives that definition from its JSON target. The eight-job gate was OOM-killed; subsequent builds/tests/gates use at most four jobs and measurements run separately.
- **Verify evidence:** final focused command `ctest --test-dir build -j 4 -R 'journal|delta|recovery|native' --output-on-failure`: `100% tests passed out of 244`, `Total Test time (real) = 38.68 sec` (`/tmp/wormedit-lanes/journal/replay/focused-final.log`). Final GATE command `scripts/gate.sh --jobs 4 --complexity-base HEAD`: all 5,482 tests passed; build, whitespace and complexity passed; complexity checked 6/6 translation units (`gate-final.log`, `wormedit-gate.ZyOOpc/complexity.log` in that directory). Status remains for the coordinator. Actual gate tail:

  ```text
  gate: 4 job(s), set explicitly; logs in /tmp/wormedit-lanes/journal/replay/wormedit-gate.ZyOOpc
    pass  build
    pass  ctest
    pass  whitespace
    pass  complexity
    100% tests passed out of 5482
  gate: passed
  ```
- **Out of scope:** shared change tracking from the command transaction (T-774); native schema changes (T-773).

## T-775 Write compact native saves

- **Status:** done (2026-10-27; owner answered open question 28: compact)
- **Outcome:** explicit saves use compact JSON, about three times smaller than the current indented output.
- **Read first:** [open question 28](../decisions/open-questions.md), `src/formats/native_project.cpp`.
- **Files owned:** `src/formats/native_project.cpp`, `tests/formats/native_project_tests.cpp`.
- **Steps:** 1. **STOP** until open question 28 is answered. 2. Switch the save writer to the T-769 compact option. 3. Update byte-identical fixtures.
- **Invariants:** readers accept both forms; no schema change (layout only).
- **Reject/failure behavior:** unchanged.
- **Verify:** `ctest --test-dir build -R native --output-on-failure`, then GATE.
- **Out of scope:** journal payloads (already compact in T-769).
