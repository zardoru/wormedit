# Code style and structure rules

[Back to technical decisions](../decisions.md). Owner-approved 2026-10-12/13 during the
[state ownership audit](../../audits/state-ownership-2026-10-12.md). The codebase is large enough
that free functions spread across headers, copied helpers and implicit ownership became the main
cost of reading it. These rules keep it navigable. Use them when writing code **and** as the
review checklist. Items marked *(T-6xx)* describe the shape that card is putting in place; follow
it in new code even before the card lands.

## Ownership and lifetimes

- **One owner per concept.** A concept's state, settings and operations live in one type (for
  example `metronome_t`, `project_audio_sources_t`, `bookmark_navigation_t`), not in loose locals,
  `std::function` services and long parameter lists. *(T-614, T-615, T-619)*
- **Application state has an owner.** `application_t` holds subsystems declared in dependency
  order; `main()` only constructs, runs and shuts down. Never add a reference mirror struct
  (`*_ref` members pointing back at locals). *(T-603, T-605)*
- **Group fields by lifetime** (app, session preference, project, chart, gesture, frame, cache), so
  a reset is `group = {}`. Never write "fresh object, copy the survivors back" resets or slice a
  base class to keep fields.
- **Shutdown order is explicit and RAII.** An idempotent `shutdown()` that the destructor also
  calls; workers are joined before platform teardown.
- **Stored callbacks capture `this` or one subsystem** and call named member functions; never
  capture locals by reference in a callback that outlives the statement.

## Types

- **Construction and basic queries live on the type.** Aggregates stay mostly public, built with
  designated initializers. A type with a single way to build it gets a static `T::from(...)` or
  `T::with_defaults()`, returning `std::optional<T>` or a result when it can fail. Never add a
  user-declared constructor to an aggregate. Classes with private invariants: a public constructor
  when construction cannot fail; a static `T::from` returning a result over a private constructor
  only when it can fail (core code does not throw). A class never needs a `from` that always succeeds.
  Types with many factories (lane profiles) belong in a catalog instead.
- **Name the members you set (owner, 2026-10-17).** Build aggregates with designated initializers
  and leave the rest to their default member initializers (`{.diagnostics = d}`); the build does not
  warn about the omitted members. A positional brace list (`{a, b}`) must set every member: clang
  reports one that stops early, and warnings are errors on our targets (`cmake/warnings.cmake`,
  T-669). Only clang tells the two forms apart, so build the `linux-clang` preset after adding
  positional initializers.
- **Members with rules are private.** When reaching a member has a rule (created lazily, finished once,
  resets something), the type is a class: the storage is private with a trailing `_` (`document_`) and
  the accessor that enforces the rule takes the plain name (`document()`), so code outside the type
  cannot reach past it.
- **Queries are members:** `measure_index_t::row_of`, `timing_index_t::seconds_at`,
  `chart_document_t::find_note`, `lane_definition_t::is_editable`. Find-or-add accessors are
  `owner.<x>_for(key)` members on the container's owner. *(T-617, T-623, T-626)*
- **Value types declare `operator==` (defaulted).** Never compare their fields by hand in callers.
- **One vocabulary per set of values.** Don't keep parallel enums or structs that code translates
  between; map them exhaustively (no `default:`) or `static_assert` the correspondence.
- Methods on structs are welcome when they express behaviour local to that data; free functions
  are for operations spanning several types, catalogs and commands.

## Functions and parameters

- If a function takes several objects that one owner already groups, take the owner (or one
  subsystem) instead. Seven or more parameters is a review flag.
- No `auto&` parameters or `template <typename Context>` used to avoid naming state types.
- Contexts are small stack bundles of references, built once per dispatch or frame and passed by
  reference. Never unpack a context into loose arguments only to rebuild a smaller one.
- Keep each function at cognitive complexity 25 or below by extracting named functions or members,
  not by moving branches into lambdas or `std::function` (see `AGENTS.md`).

## Naming

| Form | Meaning |
|---|---|
| `make_` / `T::from` | builds a new value, no side effects |
| `build_` | computes a derived index or cache from existing data |
| `<x>_view_t::from` | a read-only view of the document for the frontend, when the view is its own C++ type (never `project_`, which means the song project) |
| `build_<x>_view` | the same, only when the view is a `WormUi*` C struct not yet migrated (see "Frontend boundary") or a plain container (`std::vector`, `std::array`), which cannot own a `from` |
| `create_` / `open_` | has side effects (files, sessions, OS resources) |
| `plan_` / `prepare_` | computes a proposal; changes nothing |
| `validate_` | checks and returns diagnostics; changes nothing |
| `apply_` | an undoable command, run through the document command transaction |
| `propose_`/`begin_` → `revise_` → `apply_`/`cancel_` | the lifecycle of a staged proposal |
| `is_` / `has_` / `can_` | yes/no checks (comparators keep `*_less`, `*_equal`) |
| `scope.x()` | inside a scoped owner (a transaction, a staged editor), the unqualified accessor is that scope's own object: `transaction.document()` is the document of the transaction. Only the exception is qualified (`source_chart(id)` for the committed version) |
| `owner.<x>_for(key)` | find-or-add accessor |
| `ui_codec<WormUiX>::from` / `::into` | the `WormUi*` C-struct surfaces still in place (owner, 2026-10-16; see "Frontend boundary" for the migration away from C structs): `from(core value…)` builds the ABI struct; `into(abi, …)` turns it into a core value (a result when parsing can fail). One specialization per ABI type, in sys next to its users, like `std::hash`/`std::formatter`: neither the flat C struct nor the core type changes. No `<x>_from_ui`/`<x>_to_ui`/`<x>_from_form` free names. Core-to-core construction stays `T::from` *(T-653)* |

- Names must not lie: `folded_name` must fold; `fold` is reserved for real Unicode case folding
  (ASCII-only lowercasing is `ascii_lowercase`).
- One concept, one name across layers (a "Keysound" is "Keysound" everywhere; one tab table owns
  its labels).

## Commands, revisions and results

- **A chart edit is only its data change** (owner, 2026-10-15): the [edit pipeline](../../architecture.md)
  is the one model to learn. Two paths come in, direct commands and gesture dispatch, and one
  transaction changes the document. Adding a chart edit means writing its validation, the data it
  changes and its effect, plus one route entry or gesture kind. The stamp, copy, ready/no-op checks,
  commit, Undo record, stale message, exclusivity and result construction belong to the shared
  runners, which keep every one of these safeguards. The goal is that the right way is also the
  least work, not a restriction; when an edit needs a safeguard the runner lacks, add it to the
  runner so every edit gets it. Keep `docs/architecture.md` true
  when a runner changes. No scripting layer or reified command objects: undo is snapshots.
- **Document commands run through `document_command_transaction_t`:** build it with
  a constructor, reject through it, copy the document lazily and only once, commit through it.
  Never hand-write the stamp/copy/commit sequence. Staged edits are orchestration around it.
  Three reads, three guarantees: `document()` is the working copy (the first call copies the
  committed document, once); `current_document()` reads what the transaction sees now (the working
  copy if one exists, otherwise the committed document) without copying, for validation, ID
  allocation and stale checks; `source_document()`/`source_chart(id)` are always the committed
  version. Read with `current_document()` unless you are about to change something.
- **Check a revision once, at the boundary.** Each entry point (and each item of an input batch)
  checks the action's stamp once; handlers below it don't. Synchronous core calls take no stamp.
  State captured earlier (gestures, previews, proposals, async results) carries a
  `document_stamp_t`/`chart_stamp_t` and is checked when used. Stale messages come from one helper.
  *(T-609, T-610)*
- **sys handlers return an outcome** (`compose_action_outcome_t`); one finisher rebinds and
  publishes feedback. No local outcome structs or per-file feedback helpers. *(T-618)*
- **Expected failures are values:** `result_t<T, E>` (an alias of `std::expected` under C++23) and
  `diagnosed_t<T>` for results that carry warnings. One success rule per type, no per-file
  `fail()`/`reject()`/`rejected()` builders, and no new bespoke `operator bool`. *(T-620)*
- **Outcome aliases name shared contracts or complex types.** Keep an alias when several
  signatures share it or it makes a hard-to-read type clearer. Spell a simple type used by one
  operation directly in its signature. An effect returned on both success and failure carries
  its status and partial work directly; do not wrap it in `result_t<T, T>`. *(T-633)*

- **Background work runs through one async shell** (owner, 2026-10-15): every job that leaves
  the UI thread is an `async_job_t` instance over the shared `task_scheduler_t`, with one
  `request` (a new request supersedes and cancels older generations), `poll`, `wait`, `cancel`,
  in-flight bound, generation and captured identity/stamp. An owner supplies only its request
  type, its work function and its payload; it never re-implements generations, in-flight lists or
  poll/wait plumbing. Real-time threads (audio callback, preview refill) are not jobs. *(T-635)*

## Shared helpers, facts and policies

- **Look for the home before writing a helper:** `text_util.h` (UTF-8, trim, ASCII case,
  bounded text), `rational.h` (conversions, `nearest_row`), `checked_math.h`,
  `timing_arithmetic.h`, `numeric_text.h` (`parse_integer`, `parse_exact_decimal`), the BMS
  base-36 helpers, and type members. A copy under a new name is still a copy.
- **Edge cases are named policies, not silent variants:** zero denominators, row clamping and
  chart-end behaviour take an explicit policy argument (`chart_end_policy_t`, clamp policy) instead
  of slightly different copies.
- **One table per fact.** Lists of values and labels live in one core catalog with stable ids.
  The UI receives labels through the view; actions carry ids, never list positions across the
  frontend boundary; `static_assert` enum counts against tables. Constants (object limits, default snap set)
  are defined once.
- **Use the dependency instead of hand-rolling:** FFmpeg, miniaudio, iconv, nlohmann::json,
  stb_image and SDL3 already cover their formats and utilities. A new hand-written binary or
  file-format parser needs a stated reason in this file.

## Repetition and scaffolding (owner, 2026-10-15)

- **Rule of three, by shape:** before adding a type, handler, job or action, find its siblings by
  what they do, not by name. If yours would be the third with the same skeleton (guards, stamp
  checks, unwraps, lifecycle status, poll/wait plumbing, preamble), extract the shared runner,
  shell or trait first and write only the part that differs; if that is out of your scope, stop
  and report it. A reader should reach the work (how it works, what it proposes, when it is valid,
  the commit) without unwrapping ceremony.
- **Every scaffold has a removal owner:** a temporary adapter, forwarder or compatibility alias
  is allowed only with the card that deletes it, named in a comment beside it
  (`// Temporary: removed by T-NNN`). Unowned scaffolds are bugs.
- **Complexity is not a reason to fragment:** when a function exceeds the cognitive-complexity
  limit because of boilerplate, move the boilerplate into the shared owner instead of splitting
  the function into single-use fragments.
- **Ratchets:** `cmake/ceremony_ratchet.txt` lists known ceremony patterns with a maximum count;
  the `documentation.ceremony_ratchet` test fails when a count grows. Lower the maximum in the same
  commit that lowers a count; raising one needs the owner's approval. When an audit finds a new
  ceremony pattern, add a row.
- **A migration finishes in its card, or it ratchets** (owner, 2026-10-18). "Convert it when you
  touch it" is a judgment call that people resolve differently, so both forms live on and new code
  copies whichever is nearby. A migration whose sites can be counted converts every site in one
  card (or one ordered lane of cards), and that card adds a ratchet row whose maximum ends at the
  remaining legitimate count. A migration too large for that may go gradually only if a ratchet
  row can detect the old form: new code cannot add it, and the count only falls. A migration no
  regex can detect is not ready to be gradual; sharpen the old form's definition or schedule a
  closing card. The "Frontend boundary" migration below is the gradual
  kind and has its rows.

## Tests (owner, 2026-10-15)

- **Review test value in bounded checkpoints** (owner, 2026-10-18). Name the family/shared owner,
  baseline, reviewed scope, evidence and remaining work. Distinguish bodies reviewed from tests
  merely indexed or retained conservatively. Limit conclusions to the reviewed cases; passing
  counts or a sample's removal rate do not establish the value or nonredundancy of the whole suite.
- **Test a safeguard where it lives.** A shared runner's checks (exclusivity, stale, not-ready,
  no-op, single commit, routing, completion) are tested once, at the runner. An owner tests its
  domain behavior plus one wiring test that it goes through the runner; it does not re-test the
  runner's safeguards. *(T-644)*
- **Assert the contract.** Assert a diagnostic's code; assert its text only where the text is the
  contract. Rules about source shape belong in the ceremony ratchet, not in a test that scans files.
- **Size limits small.** Prove a bound at the smallest size that exercises it; anything over a
  second needs a reason in the test or the `slow` label.

## Frontend boundary (owner, 2026-10-17)

- **The goal is a replaceable frontend, not a C ABI.** The frontend reads view models and emits
  actions; it never reaches core types, the document or history directly, and backend names stay
  out of core (`cmake/check_ui_boundary.cmake`). That seam is what keeps a migration to another
  frontend possible. C layout is not required for it: no consumer outside the C++ build reads the
  `WormUi*` structs, and the planned alternative backends (ImGui, Clay) would be C++ adapters.
- **Boundary types may be plain C++:** `std::span`, `std::string_view`, `enum class`, strong IDs
  and owned containers, in headers that include no backend. They still carry only presentation data
  and action payloads, never core document types.
- **What may cross:** core *vocabulary* (enums such as `note_kind_t` or `visual_event_kind_t`,
  strong IDs, `rational_t`, and small pure queries over them such as `editor_bga_lane_index`) may be
  used on both sides; reuse it instead of a parallel raw integer. Documents, history, indices and
  editor state may not. Today's frontend follows this in what it uses, but four Nuklear files
  included all of `document.h` or the old `interaction.h` for a few enums until T-670 gave the vocabulary a
  light header, `core/vocabulary.h`.
- **Migrate gradually, under a ratchet** (owner, 2026-10-17; ratchet added 2026-10-18). No
  wholesale conversion card. A change that adds a view surface or action payload writes it in C++
  types. A change that substantially reworks an existing `WormUi*` surface may convert that surface
  when the conversion is contained in the files it already owns; record it on the card. Do not
  convert surfaces a change does not otherwise touch, and do not break an in-flight card's promise
  of an unchanged layout. Four `cmake/ceremony_ratchet.txt` rows hold the old form's count so it
  only falls: C view structs (`typedef struct WormUi`), fixed capacities
  (`WORM_UI_*_CAPACITY`), pointer-plus-count fields in `src/ui/frontend`, and `ui_codec`
  specializations. A conversion lowers the rows it touches in the same commit.
- **What a conversion removes:** fixed capacities and their truncation paths, the frame-storage
  flattening that keeps pointer-plus-count arrays alive, raw integer kinds and IDs, and that
  surface's `ui_codec` specialization once the frontend can read the view type directly.
- **Worked example (T-672):** `compose_measure_lane_view_t` in `src/ui/frontend/compose_measure_lane.h`
  is a C++ member of the C struct `WormUiComposeView`, so the surface converts without touching
  frame ownership. It uses core vocabulary (`bar_line_time_t`, `measure_id_t` from
  `core/vocabulary.h`), owns its list instead of a pointer and count into side storage, and puts
  its query on the type (`view.measure_lane.is_selected(id)`). Sys refills it in place while
  building the view. A member with an owned container makes the enclosing struct non-trivially
  copyable; check that nothing copies that view per frame.
- **If a non-C++ frontend ever becomes real,** give it its own C or serialized layer outside the
  C++ seam, shaped by that frontend's needs. Do not pre-build one without a consumer.

## Files and folders

- Group files in folders by subsystem and keep the file-name prefix
  (`sys/compose/compose_input.cpp`), so file names stay unique for grep, editor tabs and
  `#include` lines. *(T-621)*
- Internal headers declare shared helpers once; a split must not leave copies in each `.cpp`.

## Working practice

- **Find copies by what they do** (bodies, field sets, call shapes), not only by name. The
  name-based sweeps missed `fail`/`rejected`/`fold_ascii`/`folded`.
- **Rename with AST-aware tools** (`clang-refactor local-rename`, clangd rename), then grep for
  leftovers in macros, comments and docs. Never use plain text search-and-replace for symbols.
- **Unifying drifted copies** goes to the most careful variant, with a regression test per
  behaviour difference, and records the change on the card.

## Review checklist (pattern catalog)

1. A helper copied under another name? 2. The same struct repeated per kind? 3. Parallel
vocabularies translated by hand? 4. A query that should be a member? 5. A local outcome or result
type, or a failure builder? 6. State with no owner? 7. A fact written in two places, or a list
position crossing the frontend boundary? 8. A context unpacked and rebuilt? 9. A name breaking the table above?
10. Hand-rolled code a dependency provides? 11. Background work with its own generation, in-flight or poll/wait plumbing instead of the async shell? 12. A third copy of a skeleton instead of a shared runner? 13. A scaffold without a removal card? 14. A ratchet count raised? 15. A core edit or command surrounded by lifecycle, stamp, transaction or result plumbing that a shared runner should own, or a change to a runner without updating `docs/architecture.md`? 16. A new view surface or action payload written as a `WormUi*` C struct instead of C++ types, or a reworked C-struct surface left unconverted when the conversion was contained (see "Frontend boundary")? 17. A migration left half done with no ratchet row holding the old form's count?
