#pragma once

#include <wormedit/formats/native_project.h>

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace wormedit::native_project_internal {

// Rewrites a document at `from_version` into the shape of `from_version + 1`.
// Returns an empty string on success, otherwise a message for the diagnostic.
using migrate_fn_t = std::string (*)(nlohmann::json& document);

struct migration_t {
    std::uint32_t from_version{};
    migrate_fn_t migrate{};
};

enum class migration_status_t {
    unchanged,
    migrated,
    // Version field is missing or is not an unsigned integer.
    malformed_version,
    // Version is newer than the target.
    unsupported_version,
    // A step in the chain is missing from the table.
    missing_step,
    // A migration step reported an error; the document must not be used.
    failed,
};

struct migration_outcome_t {
    migration_status_t status{migration_status_t::unchanged};
    std::string message;
};

// Migrations shipped with the reader: one `migrate_vN_to_vN+1` per released
// schema bump. Schema 2 added `expression_source` to SV recipes.
[[nodiscard]] auto default_migrations() -> std::span<const migration_t>;

// Repairs BMS BGM data saved before `00` slots stopped being events and BGM
// lines became per-measure columns: drops sound-less events that came from a
// `00` slot, then maps the k-th legacy line of every measure onto column k.
// Idempotent and content-detected, so it needs no schema bump. Returns true
// when the document changed.
[[nodiscard]] auto normalize_legacy_bms_bgm(song_document_t& song) -> bool;

// Walks `document.schema_version` up to `target_version`, one table step at a
// time, and sets the version after each successful step.
[[nodiscard]] auto migrate_document(nlohmann::json& document, std::uint32_t target_version,
                                    std::span<const migration_t> table) -> migration_outcome_t;

// Same as parse_native_project with an explicit target schema version and
// migration table, so tests can exercise the chain before a real v2 exists.
[[nodiscard]] auto parse_native_project_with_migrations(std::string_view bytes,
                                                        native_project_limits_t limits,
                                                        std::uint32_t target_version,
                                                        std::span<const migration_t> table)
    -> native_project_parse_result_t;

} // namespace wormedit::native_project_internal
