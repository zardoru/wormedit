#pragma once

#include <wormedit/core/storyboard_vocabulary.h>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct storyboard_hierarchy_row_t {
    std::string id;
    std::optional<std::string> parent;
    std::size_t depth{};
    osbx_layer_t layer{osbx_layer_t::background};
    bool is_group{}, selected{}, editor_hidden{}, editable{}, lua_generated{};
    std::string warning;
    auto operator==(const storyboard_hierarchy_row_t&) const -> bool = default;
};
struct storyboard_hierarchy_layer_t {
    osbx_layer_t layer{osbx_layer_t::background};
    std::string_view title;
    std::vector<storyboard_hierarchy_row_t> rows;
    bool shown_in_state{true}; // Preview overlay, independent of authored/editor visibility.
    auto operator==(const storyboard_hierarchy_layer_t&) const -> bool = default;
};
struct storyboard_hierarchy_view_t {
    std::string_view message;
    std::array<storyboard_hierarchy_layer_t, osbx_layer_names.size()> layers;
    auto operator==(const storyboard_hierarchy_view_t&) const -> bool = default;
};
// Capture lives only on the enclosing storyboard_action_t. These fields describe
// one hierarchy intent; source/destination IDs are resolved at the backend edge.
enum class storyboard_hierarchy_mutation_kind_t {
    none, add_group, add_sprite, erase, rename, duplicate, drop, set_layer, toggle_editor_visibility
};
struct storyboard_hierarchy_mutation_t {
    storyboard_hierarchy_mutation_kind_t kind{storyboard_hierarchy_mutation_kind_t::none};
    std::string node;
    std::optional<std::string> parent, before, image;
    std::string text;
    osbx_layer_t layer{osbx_layer_t::background};
    auto operator==(const storyboard_hierarchy_mutation_t&) const -> bool = default;
};
} // namespace wormedit::ui
