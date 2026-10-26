#pragma once
#include "ui/frontend/compose.h"

struct nk_context;
namespace wormedit::ui { struct compose_view_t; struct workspace_action_t; }

namespace wormedit::ui {
// Drawing and pointer admission share the visible edge grips, including when
// both edges occupy the same screen row or sit at a viewport boundary.
struct compose_export_cut_handle_t {
    object_id_t cut;
    std::uint8_t edge{};
    compose_rect_t bounds;
    double y_pixels{};

    [[nodiscard]] static auto from(const compose_view_t& view,
        const compose_export_cut_view_t& cut, std::uint8_t edge)
        -> std::optional<compose_export_cut_handle_t>;
    [[nodiscard]] static auto at(const compose_view_t& view, double x, double y,
        object_id_t preferred = {}) -> std::optional<compose_export_cut_handle_t>;
    [[nodiscard]] auto contains(double x, double y) const -> bool;
    auto operator==(const compose_export_cut_handle_t&) const -> bool = default;
};
}

void worm_ui_nuklear_render_compose_export_cuts(nk_context* context,
    const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action);
