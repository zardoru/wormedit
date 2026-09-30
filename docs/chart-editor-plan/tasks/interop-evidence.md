# Task cards: interop evidence

[Back to the task-card index](../tasks.md).

## T-30 Record BMSE and iBMSC clipboard samples

- **Status:** done (2026-09-30; source-derived at the user's request from BMSE `26826aff6d51` and pBMSC/iBMSC 3.5.5.16 `06aaf7438dbc`; runtime captures optional, see T-31)
- **Outcome:** checked-in fixture files plus a README table recording, for each sample, the tool name, version, source commit, derivation, byte length, and SHA-256.
- **Read first:** `docs/references/bms-editor-clipboard.md`, `tests/fixtures/bms/clipboard/README.md`, `tests/fixtures/README.md`.
- **Files owned:** `tests/fixtures/bms/clipboard/` (samples and `README.md`), `tests/fixtures/README.md`, `tests/core/fixture_inventory_tests.cpp`, `.gitattributes`.
- **Steps:**
  1. Read each editor's copy and paste routines at the pinned commits (done; the grammar is in the reference).
  2. Emit one shared ten-object scenario in the four grammars: BMSE 3.0, BMSE legacy, iBMSC normal, iBMSC xNT (done).
  3. Record each file with its SHA-256 in the fixture README, and list it in the fixture inventory test (done).
- **Invariants:** fixture bytes keep CRLF; `*.clip` is `-text` in `.gitattributes`.
- **Reject/failure behavior:** if a later runtime capture disagrees with a source-derived file, the capture wins and the difference is recorded in the reference.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'Stage 0'`; `sha256sum tests/fixtures/bms/clipboard/*.clip` matches the README.
- **Out of scope:** writing a clipboard parser or paste command (T-63).

## T-31 Confirm the source-derived clipboard grammar with runtime captures

- **Status:** blocked-on-user
- **Outcome:** at least one real capture per grammar, plus one from a comma-decimal locale, checked in beside the source-derived files, with any disagreement written into the reference.
- **Read first:** `docs/references/bms-editor-clipboard.md` (hazards and cross-editor table), `tests/fixtures/bms/clipboard/README.md`.
- **Files owned:** `tests/fixtures/bms/clipboard/`, `docs/references/bms-editor-clipboard.md`.
- **Steps:**
  1. STOP: only the user can run BMSE and iBMSC on Windows. Ask whether they want to provide captures; if not, leave this card blocked. It does not block T-63.
  2. Save each capture unmodified as `<tool>-<version>-capture-<case>.clip` and add a README row.
  3. Diff each capture against the matching source-derived file and record the result.
- **Invariants:** captured bytes are never re-encoded.
- **Reject/failure behavior:** a capture with unknown tool or version is not added.
- **Verify:** as T-30.
- **Out of scope:** parser changes.
