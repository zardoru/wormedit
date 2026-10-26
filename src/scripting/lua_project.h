#pragma once
#include "lua_generator.h"

namespace wormedit::scripting {
// Freezes one project-relative include graph and asset hashes into ordinary API-v1 source.
// No live-file reads remain in the resulting recipe, and no project data is published here.
struct lua_project_source_t {
    std::string source;
    auto operator==(const lua_project_source_t&) const -> bool = default;
    [[nodiscard]] static auto is_frozen(std::string_view) -> bool;
    [[nodiscard]] static auto from(const song_document_t&, const chart_document_t&,
        const generator_recipe_t&, std::span<const editor_selection_object_t>,
        const std::filesystem::path& base, lua_sandbox_limits_t = {}) -> diagnosed_t<lua_project_source_t>;
};
}
