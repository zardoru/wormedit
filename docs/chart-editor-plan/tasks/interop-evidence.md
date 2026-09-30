# Task cards: interop evidence

[Back to the task-card index](../tasks.md).

## T-30 Record BMSE and iBMSC clipboard samples

- **Status:** blocked-on-user
- **Outcome:** checked-in fixture files plus a README table recording, for each sample, the tool name, version, OS, and the exact editor action that produced it.
- **Read first:** `docs/references/bms-editor-clipboard.md`, `docs/chart-editor-plan/stages/foundation.md` (stage 0, clipboard bullet), `tests/fixtures/README.md`.
- **Files owned:** new `tests/fixtures/bms/clipboard/` (samples and `README.md`), `tests/fixtures/README.md`.
- **Steps:**
  1. STOP and ask the user to supply clipboard captures, or to confirm they will run BMSE and iBMSC themselves. A model cannot produce these; do not synthesize them from the source audit.
  2. For each supplied capture, save the raw bytes unmodified with a name of the form `<tool>-<version>-<case>.bin`. Cases: BGM, long note, BPM, STOP, free-grid position.
  3. Add one table row per sample to the fixture `README.md` with tool, version, OS, action, and byte length.
  4. Add the new directory to `tests/fixtures/README.md`.
- **Invariants:** captured bytes are never re-encoded or reformatted.
- **Reject/failure behavior:** if a sample's tool or version is unknown, do not add it.
- **Verify:** `ctest --test-dir build --output-on-failure -R 'core\..*(fixtures|Stage 0)'`; `sha256sum` each file and record it in the README.
- **Out of scope:** writing a clipboard parser or paste command.
