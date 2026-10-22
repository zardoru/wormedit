#pragma once
#include "ui/frontend.h"
#include "wormedit/nuklear_config.h"
#include <array>
#include <cstdint>

namespace wormedit::ui::nuklear {
enum class notification_history_pointer_route_t { none, blocked, interactive };
struct notification_panel_state_t {
    int severity_filter{}; // All, then hint/info/success/warning/error.
    std::uint64_t expanded_id{};
    std::array<char, 4097> detail_text{};
    std::array<notification_history_pointer_route_t, 32> pointer_routes{};
    bool pointer_owned_this_frame{};
    nk_context* last_context{};
    struct nk_rect bounds{};
    nk_uint scroll_x{}, scroll_y{};
    nk_uint detail_scroll_x{}, detail_scroll_y{};
};
auto notification_history_pointer_edge(notification_panel_state_t& state, struct nk_rect bounds,
    float x, float y, std::uint32_t button, bool down, bool allow_capture, bool interactive)
    -> notification_history_pointer_route_t;
auto notification_history_view_valid(const wormedit::ui::notification_history_view_t& view) -> bool;
auto notification_row_visible(const notification_panel_state_t& state,
                               const wormedit::ui::notification_history_row_view_t& row) -> bool;
void render_notification_panel(nk_context* context, const wormedit::ui::notification_history_view_t& view,
                                notification_panel_state_t& state,
                                wormedit::ui::notification_action_t& action, int width, int height,
                                bool interactive = true);
} // namespace wormedit::ui::nuklear
