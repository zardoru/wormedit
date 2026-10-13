#include "ui/field/rect.h"
#include "ui/nuklear/notification_panel.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cmath>
#include <string_view>

namespace wormedit::ui::nuklear {
namespace {
constexpr const char* severity_names[]{"Hint", "Info", "Success", "Warning", "Error"};
constexpr const char* filter_names[]{"All severities", "Hint", "Info", "Success", "Warning", "Error"};
constexpr const char* source_names[]{"Document", "Edit", "Audio", "Export", "Import", "Recovery",
                                    "Format", "Input", "System"};

void emit_notification_action(WormUiNotificationAction& action,
                              WormUiNotificationActionKind kind, std::uint64_t id = 0) {
    if (action.kind == WORM_UI_NOTIFICATION_ACTION_NONE) action = {kind, id};
}

void render_row_buttons(nk_context* context, const WormUiNotificationHistoryRow& row,
                        WormUiNotificationAction& action) {
    nk_layout_row_dynamic(context, 26, 3);
    if (nk_button_label(context, "Copy details"))
        emit_notification_action(action, WORM_UI_NOTIFICATION_ACTION_COPY_DETAILS, row.id);
    const bool positional = row.target == WORM_UI_NOTIFICATION_TARGET_POSITION;
    const bool missing = row.target == WORM_UI_NOTIFICATION_TARGET_MISSING;
    if (!positional && !missing) nk_widget_disable_begin(context);
    const char* label = row.target == WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL
        ? "Sin posición en el chart" : "Go to";
    const bool go = nk_button_label(context, label) != 0;
    if (!positional && !missing) nk_widget_disable_end(context);
    if (go && (positional || missing))
        emit_notification_action(action, WORM_UI_NOTIFICATION_ACTION_GO_TO, row.id);
    if (nk_button_label(context, "Dismiss"))
        emit_notification_action(action, WORM_UI_NOTIFICATION_ACTION_DISMISS, row.id);
}

auto detail_line_length(const nk_user_font& font, std::string_view text, float available) -> int {
    int length = 0;
    float width = 0;
    while (length < static_cast<int>(text.size()) && text[length] != '\n') {
        nk_rune rune{};
        const int bytes = std::max(1, nk_utf_decode(text.data() + length, &rune,
            static_cast<int>(text.size()) - length));
        const float next = font.width(font.userdata, font.height, text.data() + length, bytes);
        if (length && width + next > available) break;
        width += next;
        length += bytes;
    }
    return length;
}

void render_detail_lines(nk_context* context, std::string_view text) {
    const auto& font = *context->style.font;
    const float available = std::max(1.0f, nk_window_get_content_region_size(context).x -
        2 * context->style.text.padding.x);
    while (!text.empty()) {
        const int length = detail_line_length(font, text, available);
        nk_layout_row_dynamic(context, font.height + 2 * context->style.text.padding.y, 1);
        nk_text(context, text.data(), length, NK_TEXT_LEFT);
        text.remove_prefix(static_cast<std::size_t>(length));
        if (!text.empty() && text.front() == '\n') text.remove_prefix(1);
    }
}

void render_expanded_detail(nk_context* context, const WormUiNotificationHistoryRow& row,
                            notification_panel_state_t& state) {
    // The pinned READ_ONLY edit disables all input, including scrolling.
    // Static text in a scroll group keeps the entire bounded payload accessible.
    std::snprintf(state.detail_text.data(), state.detail_text.size(), "%s",
                  row.detail[0] ? row.detail : row.message);
    nk_layout_row_dynamic(context, 150, 1);
    if (nk_group_scrolled_offset_begin(context, &state.detail_scroll_x, &state.detail_scroll_y,
                                      "Notification detail", NK_WINDOW_BORDER)) {
        render_detail_lines(context, state.detail_text.data());
        nk_group_end(context);
    }
}

void render_history_row(nk_context* context, const WormUiNotificationHistoryRow& row,
                        notification_panel_state_t& state, WormUiNotificationAction& action) {
    nk_layout_row_dynamic(context, 23, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%s · %s · ×%u · %llus ago%s",
        severity_names[row.severity], source_names[row.source], row.repeat_count,
        static_cast<unsigned long long>(row.age_seconds), row.read ? "" : " · unread");
    nk_layout_row_dynamic(context, 70, 1);
    nk_label_wrap(context, row.message);
    nk_layout_row_dynamic(context, 24, 1);
    if (nk_button_label(context, state.expanded_id == row.id ? "Collapse details" : "Expand details"))
        state.expanded_id = state.expanded_id == row.id ? 0 : row.id;
    if (state.expanded_id == row.id) render_expanded_detail(context, row, state);
    render_row_buttons(context, row, action);
    nk_layout_row_dynamic(context, 8, 1);
    nk_spacing(context, 1);
}

void render_history_body(nk_context* context, const WormUiNotificationHistoryView& view,
                         notification_panel_state_t& state, WormUiNotificationAction& action) {
    nk_layout_row_dynamic(context, 28, 2);
    state.severity_filter = chrome::safe_combo(context, filter_names, 6,
        std::clamp(state.severity_filter, 0, 5), 24, nk_vec2(190, 170));
    if (nk_button_label(context, "Clear all"))
        emit_notification_action(action, WORM_UI_NOTIFICATION_ACTION_DISMISS_ALL);
    if (view.row_count == 0) {
        nk_layout_row_dynamic(context, 30, 1);
        nk_label(context, "No notifications", NK_TEXT_LEFT);
    }
    for (std::size_t index = 0; index < view.row_count; ++index) {
        if (notification_row_visible(state, view.rows[index]))
            render_history_row(context, view.rows[index], state, action);
    }
}
} // namespace

auto notification_history_pointer_edge(notification_panel_state_t& state, struct nk_rect bounds,
    float x, float y, std::uint32_t button, bool down, bool allow_capture, bool interactive)
    -> notification_history_pointer_route_t {
    using route_t = notification_history_pointer_route_t;
    if (button == 0 || button > state.pointer_routes.size()) return route_t::none;
    auto& captured = state.pointer_routes[button - 1];
    if (captured != route_t::none) {
        const auto route = captured;
        if (!down) captured = route_t::none;
        state.pointer_owned_this_frame = true;
        return route;
    }
    const bool inside = std::isfinite(x) && std::isfinite(y) &&
                        is_point_in_rect({bounds.x, bounds.y, bounds.w, bounds.h}, x, y);
    if (!down || !allow_capture || !inside) return route_t::none;
    captured = interactive ? route_t::interactive : route_t::blocked;
    state.pointer_owned_this_frame = true;
    return captured;
}

auto notification_history_view_valid(const WormUiNotificationHistoryView& view) -> bool {
    if (view.row_count > WORM_UI_NOTIFICATION_HISTORY_CAPACITY || (view.row_count && !view.rows))
        return false;
    for (std::size_t index = 0; index < view.row_count; ++index) {
        const auto& row = view.rows[index];
        if (!row.message || !row.detail || row.id == 0 || row.severity < WORM_UI_NOTIFICATION_HINT ||
            row.severity > WORM_UI_NOTIFICATION_ERROR || row.source >= std::size(source_names) ||
            row.target < WORM_UI_NOTIFICATION_NO_TARGET || row.target > WORM_UI_NOTIFICATION_TARGET_MISSING)
            return false;
    }
    return true;
}

auto notification_row_visible(const notification_panel_state_t& state,
                               const WormUiNotificationHistoryRow& row) -> bool {
    return state.severity_filter == 0 || state.severity_filter == static_cast<int>(row.severity) + 1;
}

void render_notification_panel(nk_context* context, const WormUiNotificationHistoryView& view,
                                notification_panel_state_t& state,
                                WormUiNotificationAction& action, int width, int height,
                                bool interactive) {
    if (!context || !view.visible || !notification_history_view_valid(view)) return;
    const auto panel_width = std::max(1.0f, std::min(760.0f, static_cast<float>(width) - 24));
    const auto panel_height = std::max(1.0f, std::min(560.0f, static_cast<float>(height) - 60));
    const nk_flags flags = NK_WINDOW_BORDER | NK_WINDOW_TITLE |
        (interactive ? NK_WINDOW_MOVABLE | NK_WINDOW_SCALABLE | NK_WINDOW_CLOSABLE
                     : NK_WINDOW_NO_INPUT);
    const bool switching = state.last_context && state.last_context != context;
    const auto bounds = state.last_context ? state.bounds : nk_rect(12, 40, panel_width, panel_height);
    if (switching) nk_window_set_bounds(context, "Notifications", bounds);
    const bool open = nk_begin(context, "Notifications", bounds, flags) != 0;
    if (switching) nk_window_set_scroll(context, state.scroll_x, state.scroll_y);
    if (open) render_history_body(context, view, state, action);
    state.bounds = nk_window_get_bounds(context);
    nk_window_get_scroll(context, &state.scroll_x, &state.scroll_y);
    state.last_context = context;
    nk_end(context);
    if (interactive && (nk_window_is_closed(context, "Notifications") ||
                        nk_window_is_hidden(context, "Notifications")))
        emit_notification_action(action, WORM_UI_NOTIFICATION_ACTION_CLOSE_HISTORY);
}
} // namespace wormedit::ui::nuklear
