# Autosave and crash-recovery design

Status: reviewed planning contract; atomic primary/recovery publication, the
headless five-minute autosave coordinator, bounded startup scanning,
recovery-action/quarantine execution, the generation-checked recovery banner,
SDL-based per-user recovery-directory discovery, and the periodic application
autosave loop are implemented. The loop checks dirty revisions against the
monotonic five-minute coordinator, captures one immutable document snapshot,
and submits bounded serialization/publication to a one-worker, one-task
scheduler. Checksummed command journaling, valid-prefix replay and startup recovery
are implemented. Crash-marker backends remain planned.

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

## Five-minute autosave contract

The application loop checks the coordinator using a monotonic clock and
document dirty state. Saved native projects use an adjacent
`<project-filename>.bak`; untitled and import-only sessions use
`project-<stable-project-id>.wormedit.bak` in SDL's per-user recovery directory.
At most one immutable snapshot is queued at a time. The document copy happens
on the caller thread; bounded serialization, validation, and publication run on
the scheduler worker. A failed publication leaves the previous backup intact,
reports its diagnostic in the application status, and retries after 30 seconds.
A long-running export or waveform job does not reset the cadence. Recovery into
the live session is held until an autosave worker finishes, then starts a new
autosave cadence using the recovered artifact's session sequence and explicit
save baseline.

Write `<name>.bak.tmp` in the same directory, serialize completely, flush it, read/validate its envelope and payload, flush the containing directory where supported, and atomically replace `<name>.bak`. If serialization, space, permission, flush, checksum, or validation fails, retain the previous known-good `.bak` and show a non-modal diagnostic. Stale temporary files are recognized and validated separately at startup; they never outrank a valid completed backup merely by timestamp.

An explicit successful project save records its revision and is flushed/validated before any backup cleanup. On a clean close, a backup no newer than that saved revision may be removed or retained according to the user's recovery-retention setting. A newer backup is never silently deleted.

## Reopening and recovery

Opening a project and application startup both search for a matching `.bak`, untitled recovery entries, a crash marker, and any command journal. Validate each candidate independently before comparing it with the primary file. Use project ID and revision first and timestamps only as supporting evidence, since clocks can move.

The implemented scanner covers the primary, adjacent `.bak`, caller-supplied recovery entries and directories, and interrupted same-directory primary/recovery temporary files. At application startup SDL resolves and creates Wormedit's per-user preference directory, appends its `recovery` subdirectory to the bounded scan, and the headless scanner examines its regular files. The scanner validates raw native projects separately from checksummed recovery envelopes, applies bounded candidate/file/total-byte/diagnostic limits, ranks only within one project identity, and attaches a size plus SHA-256 scan ticket to each fully-read artifact. The headless action executor rereads and validates that exact content before returning a read-only/session document or mutating anything. Recover-into-session requires the caller's expected current ID/revision (or an explicitly empty session) and confirms it still matches the scanned primary. Save-as is create-only and cannot target the primary. Discard and recommended quarantine move only the selected artifact to a unique same-directory quarantine name, verify the moved content, and attempt restoration if verification fails; the scanner itself never mutates files. A bounded backend-neutral controller converts scans into clipped scalar banner rows and typed action tokens. Every token is bound to a scan generation and displayed row/candidate identity; replacing a scan or successfully moving an artifact invalidates prior tokens before another action can reach storage.

If a valid backup is newer than the explicit save, show a recovery banner before editing with actions to inspect it read-only, recover it into the session, save it as a new project, or discard it. Never overwrite the primary project merely because a backup exists. Show the saved and backup revisions/times plus a document-level change summary when both can be decoded. If the primary is missing or corrupt and the backup is valid, recommend recovery while retaining the corrupt original for inspection.

If a crash marker exists but the primary save validates and no newer valid backup exists, atomically create the `.bak` from that known-good primary before continuing, as requested. Do not overwrite a newer valid recovery snapshot. Corrupt backups are only quarantined after an explicit action and never loaded or used to replace a primary file. Choosing discard is explicit and moves only the selected, scan-ticket-matching recovery generation into quarantine after confirming the primary/session baseline; the original remains recoverable under its quarantine name.

## Command journal

The live history publication boundary captures every accepted command and Undo/Redo
as a complete native post-state, prior revision, command label and monotonic journal
sequence. Each record's SHA-256 covers its payload and metadata. Full post-states
represent replacement deltas, so replay covers every command family without a second
command serializer. Audio and other external assets remain referenced; relative paths
are anchored on the worker using the same recovery target as autosave.

Serialization and file publication run through `async_job_t` on the autosave scheduler.
Before accepting another publication, the history boundary waits for the previous
record's file and directory flushes. With healthy storage, the current publication is
the only accepted edit that may still be in flight. The writer owns its generation and
validated prefix, checks the expected file length before appending, and avoids parsing
the growing file for each note. Recovery independently verifies every record.
The default bounds are four queued snapshots, 64 MiB per file, 16 MiB per record and
1,024 records. Reaching a bound or encountering a write failure requests an early full
checkpoint using the existing autosave coordinator and retry policy. Prior artifacts
remain available when storage fails, with a recovery warning.

Saved projects use an adjacent `<project>.bak.journal-<generation>`; untitled/imported
sessions use the corresponding recovery-directory target. New generations preserve
unresolved artifacts. Opening/startup discovers journals under the existing candidate,
directory and byte budgets. JSON parsing uses the dependency with record depth, node,
duplicate-key and payload bounds. A bad checksum, malformed post-state, revision gap,
sequence gap or torn final record stops replay at the validated prefix. Identical
duplicates are idempotent. A corrupt journal never prevents another valid backup from
being offered.

Autosave captures a path/sequence cursor with its immutable document. Recovery
envelope version 2 adds that cursor and its checksum; envelopes without a cursor retain
version 1, and both are readable. Only a successfully flushed backup acknowledges
compaction, and directory-sync warnings retain the journal. Records newer than the
captured cursor survive. A completed checkpoint can remove its covered valid prefix
and unusable tail; an unread limit refuses compaction. Recovery sequence floors continue
across generations, and cursor-aware ranking handles Undo's decreasing numeric revision.
Explicit Save records the resulting baseline, including edits made while Save ran.

The scanner's ticket covers the entire journal, including a corrupt tail. Existing
Inspect/Recover/Save-as/Discard actions revalidate that exact artifact before using its
validated post-state; journal replay adds no automatic primary overwrite or new replay UI.

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
