# Autosave and crash-recovery design

Status: reviewed planning contract; atomic primary/recovery publication, the
headless five-minute coordinator, the bounded startup scanner, and headless
recovery-action/quarantine execution are implemented. Recovery UI, command
journaling, and crash-marker backends remain planned.

## Recovery artifacts

Recovery always serializes the versioned native project document, even when the session began from a standalone BMS, SM/SSC, osu!mania, BMSON, or later Quaver file. This retains multiple difficulties, editor metadata, semantic layers, selections needed for recovery, format provenance, and generator recipes that an interchange file cannot carry.

For a saved native project, the primary autosave is adjacent to it and named `<project-filename>.bak`. An untitled or import-only session uses `<stable-project-id>.bak` in the platform application-recovery directory and records the original source path. External audio/image/video assets are referenced with identity hashes rather than copied every five minutes.

Each backup envelope contains:

- recovery format and native project schema versions;
- stable project/session IDs;
- source and intended project paths;
- monotonically increasing document revision and last explicit-save revision;
- UTC write time and monotonic session sequence;
- payload length and a strong payload checksum;
- a complete native project payload;
- optional validated command-journal position.

## Five-minute autosave

While a document is dirty, the autosave coordinator snapshots an immutable document revision every five minutes. The cadence uses a monotonic clock and continues during ordinary idle time; a long-running export or waveform job does not reset it. Autosave work runs outside the UI/audio threads, but completion is committed only if its revision and project identity still match the request.

Write `<name>.bak.tmp` in the same directory, serialize completely, flush it, read/validate its envelope and payload, flush the containing directory where supported, and atomically replace `<name>.bak`. If serialization, space, permission, flush, checksum, or validation fails, retain the previous known-good `.bak` and show a non-modal diagnostic. Stale temporary files are recognized and validated separately at startup; they never outrank a valid completed backup merely by timestamp.

An explicit successful project save records its revision and is flushed/validated before any backup cleanup. On a clean close, a backup no newer than that saved revision may be removed or retained according to the user's recovery-retention setting. A newer backup is never silently deleted.

## Reopening and recovery

Opening a project and application startup both search for a matching `.bak`, untitled recovery entries, a crash marker, and any command journal. Validate each candidate independently before comparing it with the primary file. Use project ID and revision first and timestamps only as supporting evidence, since clocks can move.

The implemented scanner covers the primary, adjacent `.bak`, caller-supplied recovery entries, and interrupted same-directory primary/recovery temporary files. It validates raw native projects separately from checksummed recovery envelopes, applies bounded candidate/file/total-byte/diagnostic limits, ranks only within one project identity, and attaches a size plus SHA-256 scan ticket to each fully-read artifact. The headless action executor rereads and validates that exact content before returning a read-only/session document or mutating anything. Recover-into-session requires the caller's expected current ID/revision (or an explicitly empty session) and confirms it still matches the scanned primary. Save-as is create-only and cannot target the primary. Discard and recommended quarantine move only the selected artifact to a unique same-directory quarantine name, verify the moved content, and attempt restoration if verification fails; the scanner itself never mutates files.

If a valid backup is newer than the explicit save, show a recovery banner before editing with actions to inspect it read-only, recover it into the session, save it as a new project, or discard it. Never overwrite the primary project merely because a backup exists. Show the saved and backup revisions/times plus a document-level change summary when both can be decoded. If the primary is missing or corrupt and the backup is valid, recommend recovery while retaining the corrupt original for inspection.

If a crash marker exists but the primary save validates and no newer valid backup exists, atomically create the `.bak` from that known-good primary before continuing, as requested. Do not overwrite a newer valid recovery snapshot. Corrupt backups are only quarantined after an explicit action and never loaded or used to replace a primary file. Choosing discard is explicit and moves only the selected, scan-ticket-matching recovery generation into quarantine after confirming the primary/session baseline; the original remains recoverable under its quarantine name.

## Crash handling

Use a small `crash_backend_t` platform boundary:

- Windows installs an unhandled-exception boundary and may write a minidump through a separately tested optional backend.
- Linux and macOS install handlers for the fatal signals that can be handled meaningfully and retain the platform's normal crash reporting/core-dump behavior.
- The application entry point catches otherwise-unhandled C++ exceptions and reports them through the same recovery coordinator.

A fatal signal/exception handler must not traverse the document, allocate memory, take ordinary mutexes, run Lua, call UI/audio code, or invoke the project serializer. Those operations are unsafe when process state may already be corrupt. The lowest-level handler writes only a fixed-size crash marker and minimal preallocated diagnostic data using platform-safe operations, then terminates or chains to the platform handler.

For failures caught at the application boundary while the process remains coherent, request and validate an emergency native-project backup through the normal recovery coordinator before shutdown. For hard faults, recovery comes from the last validated five-minute snapshot plus an optional checksummed append-only command journal maintained during normal operation. A later watchdog/helper process may own the last prevalidated snapshot if testing shows that a tighter hard-crash recovery window is required.

Crash reports contain build ID, platform, exception/signal code, thread identifier, and stack/minidump data where available. Project content and absolute asset paths are not uploaded automatically. On the next start, the UI links the crash marker to the matching recovery candidates and clears it only after the user resolves or dismisses recovery.

## Required tests

- fake-clock tests at just before, exactly at, and just after five minutes, repeated intervals, clean/dirty transitions, long-running jobs, and clock discontinuities;
- atomic-write fault injection for serialization, short write, disk full, permission, flush, rename, checksum, and re-open validation failures;
- proof that every failure retains both the primary project and previous known-good `.bak`;
- startup matrices for missing/valid/corrupt primary, backup, temporary file, journal, and crash marker with older/equal/newer revisions and misleading timestamps;
- recovery of untitled and imported standalone-format sessions into a complete native project;
- command-journal truncation, corrupt record, duplicate record, revision gap, replay failure, and idempotent replay cases;
- clean-close cleanup and newer-backup retention;
- application-boundary exception tests that produce an emergency backup only after validation;
- isolated subprocess crash tests for supported signals/exceptions, including crashes while the document lock, allocator, serializer, audio callback, and autosave worker are active;
- verification that the low-level handler performs only its allowed fixed-size operations, does not deadlock, and leaves the last validated backup readable;
- Windows, macOS, and Linux startup/recovery integration tests, plus optional Windows minidump and POSIX core/crash-report checks where CI permits them.
