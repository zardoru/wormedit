#pragma once
#include "ui/frontend/storyboard_lua_links.h"
#include <algorithm>

#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/visual_layout.h>
#include <wormedit/core/storyboard_spline.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::ui {

struct storyboard_choice_view_t {
    std::string id, label;
    auto operator==(const storyboard_choice_view_t&) const -> bool = default;
};
struct storyboard_numeric_track_view_t {
    std::vector<double> value;
    bool keyed{}, at_key{}, editable{};
    std::string reason;
    std::optional<std::string> key; // Occupied snapped edit time, independent of the fractional marker.
    auto operator==(const storyboard_numeric_track_view_t&) const -> bool = default;
};
struct storyboard_op_view_t {
    std::string id;
    std::optional<std::string> parent;
    osbx_transform_kind_t kind{osbx_transform_kind_t::translate};
    std::string label;
    std::size_t depth{}, ordinal{};
    bool can_move_up{}, can_move_down{};
    storyboard_numeric_track_view_t track;
    std::optional<storyboard_spline_t> spline;
    auto operator==(const storyboard_op_view_t&) const -> bool = default;
};
struct storyboard_transform_choice_t {
    osbx_transform_kind_t kind{osbx_transform_kind_t::translate};
    std::string label;
    auto operator==(const storyboard_transform_choice_t&) const -> bool = default;
};
struct storyboard_inspector_view_t {
    std::string message, node, reason;
    bool editable{}, is_sprite{}, can_convert{};
    std::optional<std::string> image;
    std::string origin;
    osbx_layer_t layer{osbx_layer_t::background};
    int order{};
    std::optional<osbx_lifetime_t> lifetime;
    std::vector<storyboard_choice_view_t> images, origins;
    std::vector<storyboard_transform_choice_t> transform_choices;
    std::vector<storyboard_op_view_t> ops;
    std::vector<storyboard_lua_link_view_t> lua_links;
    [[nodiscard]] auto lua_link_for(const storyboard_lua_target_t& target) const -> const storyboard_lua_link_view_t* {
        const auto found = std::ranges::find(lua_links, target, &storyboard_lua_link_view_t::target);
        return found == lua_links.end() ? nullptr : &*found;
    }
    storyboard_numeric_track_view_t opacity, color;
    osbx_blend_t blend{osbx_blend_t::normal};
    bool blend_editable{}, blend_keyed{}, blend_at_key{};
    std::optional<std::string> blend_key;
    storyboard_time_t time;
    std::optional<std::int64_t> key_time_ms;
    std::optional<visual_affine_t> world;
    auto operator==(const storyboard_inspector_view_t&) const -> bool = default;
};

enum class storyboard_inspector_field_t {
    none, id, image, origin, layer, order, blend, transform, opacity, color,
    lifetime, add_op, remove_op, move_op, convert, bake_spline
};
// Typed field commits are direct edits; label scrubs use the shared gesture path.
enum class storyboard_inspector_phase_t { focus, commit, begin, revise, apply, cancel };
struct storyboard_inspector_action_t {
    storyboard_inspector_field_t field{storyboard_inspector_field_t::none};
    storyboard_inspector_phase_t phase{storyboard_inspector_phase_t::commit};
    std::string node, op, text;
    std::optional<std::string> key;
    std::optional<std::string> parent_op;
    std::vector<double> value;
    std::int64_t key_time_ms{};
    osbx_transform_kind_t op_kind{osbx_transform_kind_t::translate};
    osbx_layer_t layer{osbx_layer_t::background};
    osbx_blend_t blend{osbx_blend_t::normal};
    int order{}, move{};
    std::optional<osbx_lifetime_t> lifetime;
    bool has_valid_value{true}; // Text parsing failed: reject through the shared outcome/feedback path.
    storyboard_spline_t spline;
    auto operator==(const storyboard_inspector_action_t&) const -> bool = default;
};

} // namespace wormedit::ui
