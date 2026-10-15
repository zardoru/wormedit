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
| `build_<x>_view` | the same, only when the view is a C ABI struct or a plain container (`std::vector`, `std::array`), which cannot own a `from` |
| `create_` / `open_` | has side effects (files, sessions, OS resources) |
| `plan_` / `prepare_` | computes a proposal; changes nothing |
| `validate_` | checks and returns diagnostics; changes nothing |
| `apply_` | an undoable command, run through the document command transaction |
| `propose_`/`begin_` → `revise_` → `apply_`/`cancel_` | the lifecycle of a staged proposal |
| `is_` / `has_` / `can_` | yes/no checks (comparators keep `*_less`, `*_equal`) |
| `scope.x()` | inside a scoped owner (a transaction, a staged editor), the unqualified accessor is that scope's own object: `transaction.document()` is the document of the transaction. Only the exception is qualified (`source_chart(id)` for the committed version) |
| `owner.<x>_for(key)` | find-or-add accessor |

- Names must not lie: `folded_name` must fold; `fold` is reserved for real Unicode case folding
  (ASCII-only lowercasing is `ascii_lowercase`).
- One concept, one name across layers (a "Keysound" is "Keysound" everywhere; one tab table owns
  its labels).

## Commands, revisions and results

- **Document commands run through `document_command_transaction_t`:** build it with
  `::from(history)`, reject through it, copy the document lazily and only once, commit through it.
  Never hand-write the stamp/copy/commit sequence. Staged edits are orchestration around it.
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
  C ABI; `static_assert` enum counts against tables. Constants (object limits, default snap set)
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
position crossing the ABI? 8. A context unpacked and rebuilt? 9. A name breaking the table above?
10. Hand-rolled code a dependency provides? 11. Background work with its own generation, in-flight or poll/wait plumbing instead of the async shell? 12. A third copy of a skeleton instead of a shared runner? 13. A scaffold without a removal card? 14. A ratchet count raised?
