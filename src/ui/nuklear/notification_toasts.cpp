#include "ui/nuklear/notification_toasts.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <array>

namespace wormedit::ui::nuklear {
namespace {

auto severity_label(WormUiNotificationSeverity severity) -> const char* {
    return severity == WORM_UI_NOTIFICATION_ERROR ? "Error" : "Warning";
}

auto severity_color(WormUiNotificationSeverity severity) -> nk_color {
    return theme::ui(severity == WORM_UI_NOTIFICATION_ERROR ? theme::UiColor::accent
                                                           : theme::UiColor::text_bright);
}

void render_message_detail(nk_context* context, const char* message,
                           notification_rect_t bounds, int width, int height) {
    const auto mouse = context->input.mouse.pos;
    if (!message || !notification_contains(bounds, mouse.x, mouse.y)) return;
    const auto detail_width = std::min(420.0f, static_cast<float>(width) - 24.0f);
    const auto padding = 4 * std::max({context->style.window.padding.y,
        context->style.window.popup_padding.y, context->style.window.menu_padding.y}) +
        2 * context->style.window.border + context->style.window.spacing.y;
    const auto detail_height = std::min(180.0f, static_cast<float>(height) - 42.0f - padding);
    if (detail_width < 80.0f || detail_height < context->style.font->height) return;
    const auto outer_height = detail_height + padding;
    const auto left = std::clamp(bounds.x, 0.0f, static_cast<float>(width) - detail_width);
    const auto top = std::clamp(bounds.y - outer_height - 8.0f, 0.0f,
                               static_cast<float>(height) - 42.0f - outer_height);
    const auto offset = nk_vec2(left - mouse.x - 1, top - mouse.y - 1);
    if (!nk_tooltip_begin_offset(context, detail_width, NK_TOP_LEFT, offset)) return;
    nk_layout_row_dynamic(context, detail_height, 1);
    nk_label_wrap(context, message);
    nk_tooltip_end(context);
}

void render_one_toast(nk_context* context, const WormUiNotificationToastView& toast,
                      notification_toast_state_t& state, std::size_t index,
                      int width, int height) {
    constexpr std::array names{"notification-toast-0", "notification-toast-1",
                               "notification-toast-2"};
    const auto bounds = notification_toast_bounds(width, height, index);
    state.bounds[index] = bounds;
    state.ids[index] = toast.id;
    if (nk_begin(context, names[index], nk_rect(bounds.x, bounds.y, bounds.width, bounds.height),
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR | NK_WINDOW_NO_INPUT)) {
        nk_layout_row_begin(context, NK_DYNAMIC, 18, 2);
        nk_layout_row_push(context, 0.92f);
        if (toast.repeat_count > 1)
            nk_labelf_colored(context, NK_TEXT_LEFT, severity_color(toast.severity), "%s ×%u",
                              severity_label(toast.severity), toast.repeat_count);
        else
            nk_label_colored(context, severity_label(toast.severity), NK_TEXT_LEFT,
                              severity_color(toast.severity));
        nk_layout_row_push(context, 0.08f);
        const auto close = nk_widget_bounds(context);
        state.close_bounds[index] = {close.x, close.y, close.w, close.h};
        nk_label(context, "×", NK_TEXT_CENTERED);
        nk_layout_row_end(context);
        nk_layout_row_dynamic(context, std::max(0.0f, bounds.height - 38.0f), 1);
        nk_label_wrap(context, toast.message ? toast.message : "");
        render_message_detail(context, toast.message, bounds, width, height);
    }
    nk_end(context);
}

} // namespace

void render_notification_toasts(nk_context* context, const WormUiNotificationView& view,
                                notification_toast_state_t& state, int width, int height) {
    state.bounds.fill({});
    state.close_bounds.fill({});
    state.ids.fill(0);
    state.count = 0;
    if (!context || width <= 0 || height <= 0) return;
    const auto count = std::min(view.toast_count, std::size_t{WORM_UI_NOTIFICATION_TOAST_CAPACITY});
    for (std::size_t index = 0; index < count; ++index) {
        if (notification_toast_bounds(width, height, index).height == 0) break;
        render_one_toast(context, view.toasts[index], state, index, width, height);
        ++state.count;
    }
}

void render_notification_overlay(nk_context* primary, nk_context* overlay,
                                  const WormUiNotificationView& view,
                                  notification_toast_state_t& state, int width, int height) {
    if (!primary || !overlay) {
        notification_focus_lost(state);
        render_notification_toasts(nullptr, view, state, width, height);
        return;
    }
    overlay->style = primary->style;
    nk_input_begin(overlay);
    nk_input_motion(overlay, static_cast<int>(primary->input.mouse.pos.x),
                    static_cast<int>(primary->input.mouse.pos.y));
    nk_input_end(overlay);
    render_notification_toasts(overlay, view, state, width, height);
}

} // namespace wormedit::ui::nuklear
