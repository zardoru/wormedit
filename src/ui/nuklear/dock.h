#pragma once
#include "ui/frontend/compose.h"
#include <wormedit/core/ui_layout_preferences.h>
#include <array>
#include <optional>
struct nk_context;

namespace wormedit::ui {
struct dock_pane_t {
    dock_panel_t panel;
    compose_rect_t bounds;
    auto operator==(const dock_pane_t&) const -> bool = default;
};
struct dock_geometry_t {
    std::array<std::optional<dock_pane_t>, 2> panes;
    compose_rect_t strip;
    std::optional<compose_rect_t> width_handle;
    std::optional<compose_rect_t> split_handle;
    float reserved_width{};
    [[nodiscard]] static auto from(float width, float height, const ui_layout_preferences_t& layout,
                                  const std::array<bool, 6>& available, float reserved_left_width = 0) -> dock_geometry_t;
    [[nodiscard]] auto is_visible(dock_panel_t panel) const -> bool;
    [[nodiscard]] auto is_side_by_side() const -> bool;
    [[nodiscard]] auto split_extent() const -> double;
    [[nodiscard]] auto widget_count() const -> int {
        auto count = 1 + static_cast<int>(width_handle.has_value()) + static_cast<int>(split_handle.has_value());
        for (const auto& pane : panes) if (pane) ++count;
        return count;
    }
};
enum class dock_capture_t { none, width, split, panel };
struct dock_pointer_t {
    dock_capture_t capture{};
    std::optional<dock_panel_t> panel;
    float x{}, y{}, width{}, split{};
    [[nodiscard]] auto is_active() const -> bool { return capture != dock_capture_t::none; }
};
// Geometry uses the current layout-space coordinates. Captures consume only view controls.
void render_dock_controls(nk_context* context, const dock_geometry_t& geometry,
                          ui_layout_preferences_t& layout, dock_pointer_t& pointer,
                          const std::array<bool, 6>& available, bool input_available);
} // namespace wormedit::ui
