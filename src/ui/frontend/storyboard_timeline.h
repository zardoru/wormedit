#pragma once

#include <wormedit/core/storyboard_vocabulary.h>
#include "ui/frontend/storyboard_inspector.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct storyboard_timeline_key_t {
    std::string id;
    std::int64_t time_ms{}; // Authored keys use signed integer milliseconds.
    double position{}; // Pixels from the time lane's left edge in the shared viewport.
    osbx_easing_t easing{osbx_linear_easing_t{}};
    std::string easing_label;
    bool selected{}, editable{true};
    auto operator==(const storyboard_timeline_key_t&) const -> bool = default;
};
struct storyboard_timeline_row_t {
    osbx_track_target_t track;
    std::string label;
    std::size_t depth{};
    bool editable{};
    std::string reason;
    std::vector<storyboard_timeline_key_t> keys;
    std::optional<storyboard_lua_band_view_t> lua;
    auto operator==(const storyboard_timeline_row_t&) const -> bool = default;
};
struct storyboard_easing_choice_t {
    osbx_easing_t easing{osbx_linear_easing_t{}};
    std::string label;
    std::array<float, 33> preview{};
    auto operator==(const storyboard_easing_choice_t&) const -> bool = default;
};
struct storyboard_timeline_line_t {
    double time_ms{};
    bool measure{};
    auto operator==(const storyboard_timeline_line_t&) const -> bool = default;
};
struct storyboard_timeline_view_t {
    std::string message;
    std::string_view status; // Frame-borrowed diagnostic from the shared scene presenter.
    std::vector<storyboard_timeline_row_t> rows;
    std::vector<storyboard_easing_choice_t> easings;
    std::vector<storyboard_timeline_line_t> lines;
    storyboard_time_t time; // Shared fractional playhead; independent of authored key snapping.
    double playhead_position{};
    std::optional<std::int64_t> preview_delta_ms;
    std::string preview_reason;
    bool grid_available{}, grid_enabled{};
    auto operator==(const storyboard_timeline_view_t&) const -> bool = default;
};
enum class storyboard_timeline_action_kind_t { none, select_keys, seek, add_key, erase_keys, easing, move_keys, set_grid };
struct storyboard_timeline_action_t {
    storyboard_timeline_action_kind_t kind{storyboard_timeline_action_kind_t::none};
    storyboard_inspector_phase_t phase{storyboard_inspector_phase_t::commit};
    osbx_track_target_t track;
    std::vector<osbx_key_target_t> keys;
    storyboard_time_t time; // Captured shared playhead for K; sample before authored-time snapping.
    double pointer_time_ms{}; // Unsnapped pointer coordinate in signed milliseconds.
    double pointer_delta_ms{}; // Drag displacement; add to integer anchor before canonical snapping.
    std::int64_t anchor_time_ms{}; // Captured original key time; revisions never rebase it.
    osbx_easing_t easing{osbx_linear_easing_t{}};
    bool shift{}, grid_enabled{};
    auto operator==(const storyboard_timeline_action_t&) const -> bool = default;
};
} // namespace wormedit::ui
