#pragma once

#include <wormedit/core/result.h>
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

enum class migration_phase_t : std::uint8_t { unchanged, migrated };

// Migrations shipped with the reader: one `migrate_vN_to_vN+1` per released
// schema bump. Schema 2 added `expression_source` to SV recipes.
[[nodiscard]] auto default_migrations() -> std::span<const migration_t>;

// Repairs BMS BGM data saved before `00` slots stopped being events and BGM
// lines became per-measure columns: drops sound-less events that came from a
// `00` slot, then maps the k-th legacy line of every measure onto column k.
// Idempotent and content-detected, so it needs no schema bump. Returns true
// when the document changed.
[[nodiscard]] auto normalize_legacy_bms_bgm(song_document_t& song) -> bool;

// Success retains whether a migration ran; failures carry a shared code and message.
// Walks `document.schema_version` up to `target_version`, one table step at a
// time, and sets the version after each successful step.
[[nodiscard]] auto migrate_document(nlohmann::json& document, std::uint32_t target_version,
                                    std::span<const migration_t> table)
    -> result_t<migration_phase_t, error_t<failure_code_t>>;

[[nodiscard]] auto build_native_project_parse(std::string_view bytes, native_project_limits_t limits,
    std::uint32_t target_version, std::span<const migration_t> table) -> native_project_parse_outcome_t;

// Same as parse_native_project with an explicit target schema version and
// migration table, so tests can exercise the chain before a real v2 exists.
[[nodiscard]] auto parse_native_project_with_migrations(std::string_view bytes,
                                                        native_project_limits_t limits,
                                                        std::uint32_t target_version,
                                                        std::span<const migration_t> table)
    -> native_project_parse_outcome_t;

} // namespace wormedit::native_project_internal
