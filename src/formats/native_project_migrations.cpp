#include "native_project_migrations.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace wormedit::native_project_internal {

auto default_migrations() -> std::span<const migration_t> {
    return {};
}

namespace {

auto find_step(std::span<const migration_t> table, std::uint32_t from_version)
    -> const migration_t* {
    const auto found = std::ranges::find(table, from_version, &migration_t::from_version);
    return found == table.end() ? nullptr : &*found;
}

} // namespace

auto migrate_document(nlohmann::json& document, std::uint32_t target_version,
                      std::span<const migration_t> table) -> migration_outcome_t {
    if (!document.is_object() || !document.contains("schema_version") ||
        !document.at("schema_version").is_number_unsigned()) {
        return {migration_status_t::malformed_version,
                "document schema version must be an unsigned integer"};
    }
    const auto start = document.at("schema_version").get<std::uint64_t>();
    if (start > target_version) {
        return {migration_status_t::unsupported_version,
                "document schema version is newer than this build supports"};
    }
    if (start == target_version) {
        return {};
    }
    // Work on a copy so a failing step never leaves a half-migrated document.
    auto working = document;
    for (auto version = static_cast<std::uint32_t>(start); version < target_version; ++version) {
        const auto* step = find_step(table, version);
        if (step == nullptr || step->migrate == nullptr) {
            return {migration_status_t::missing_step,
                    "no migration from document schema version " + std::to_string(version)};
        }
        if (auto message = step->migrate(working); !message.empty()) {
            return {migration_status_t::failed, std::move(message)};
        }
        working["schema_version"] = version + 1;
    }
    document = std::move(working);
    return {migration_status_t::migrated, {}};
}

} // namespace wormedit::native_project_internal
