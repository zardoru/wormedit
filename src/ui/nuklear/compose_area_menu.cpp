#include "sys/compose_scroll_geometry.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/popup_lifecycle.h"
#include "wormedit/nuklear_config.h"
#include <cstring>

namespace {
ComposeAreaCapture capture;
bool consumed{};
bool band_visible{};
bool menu_open{};
bool menu_release_pending{};
bool opened_this_frame{};
wormedit::ui::chrome::PopupContextStamp menu_context{};
struct nk_rect menu_bounds{};

void close_menu(nk_context* context, bool dismiss_existing_popup = true) {
    menu_open = false;
    consumed = true;
    wormedit::ui::chrome::popup_arm_release(menu_release_pending);
    if (context && dismiss_existing_popup)
        wormedit::ui::chrome::dismiss_popup(context, "Area actions", menu_bounds);
}


void menu_entry(nk_context* context, const char* title, bool enabled, const char* reason,
                WormUiComposeActionKind kind, WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 24, 1);
    if (!enabled)
        nk_widget_disable_begin(context);
    if (nk_button_label(context, title)) {
        action.compose_action = kind;
        close_menu(context, false);
        nk_popup_close(context);
    }
    if (!enabled) {
        nk_widget_disable_end(context);
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, reason);
    }
}

void render_menu(nk_context* context, const WormUiComposeView& view,
                 WormUiWorkspaceAction& action) {
    if (!menu_open)
        return;
    const auto current = wormedit::ui::chrome::PopupContextStamp{
        view.project_id, view.chart_id, view.document_revision, 0};
    const auto bounds = wormedit::ui::chrome::popup_screen_bounds(context, menu_bounds);
    if (!wormedit::ui::chrome::popup_context_matches(menu_context, current) ||
        !view.area_range_available || wormedit::ui::chrome::popup_dismiss_requested(
            context, bounds, !opened_this_frame)) {
        close_menu(context);
        opened_this_frame = false;
        return;
    }
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Area actions",
                        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, menu_bounds)) {
        close_menu(context);
        return;
    }
    menu_entry(context, "Quantize selection", view.selected_note_count != 0, "Select notes first",
               WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE, action);
    menu_entry(context, "Create section", view.beat_section_selection_span_available,
               "Materialize the selected measures first", WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION,
               action);
    menu_entry(context, "Loop playback", false, "Range looping is not available yet",
               WORM_UI_COMPOSE_ACTION_NONE, action);
    menu_entry(context, "Delete contents", view.area_delete_supported,
               view.area_range_virtual ? "Materialize the selected measures first"
                                       : "Area deletion supports notes and BGM only",
               WORM_UI_COMPOSE_ACTION_AREA_DELETE, action);
    menu_entry(context, "Add SV recipe", !view.area_range_virtual,
               "Materialize the selected measures first", WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE,
               action);
    menu_entry(context, "Add SPEEDS span", false,
               "Speed span creation from an area is not available yet", WORM_UI_COMPOSE_ACTION_NONE,
               action);
    nk_popup_end(context);
}

void update_capture(nk_context* context, const WormUiComposeView& view,
                    WormUiWorkspaceAction& action, struct nk_rect viewport, bool focused) {
    const auto& mouse = context->input.mouse;
    if (!capture.current(view)) {
        capture.active = false;
        return;
    }
    if (!focused || (!mouse.buttons[NK_BUTTON_LEFT].down &&
                     !nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))) {
        capture.restore(action);
        capture.active = false;
        return;
    }
    const auto y = (mouse.pos.y - viewport.y) * view.viewport_height_pixels / viewport.h;
    const auto source_y = wormedit::sys::compose_source_y(y, view.viewport_height_pixels, view.downscroll);
    const auto row = std::clamp((source_y + view.area_scroll_pixels) *
                                    static_cast<double>(view.area_rows_per_measure) /
                                    view.pixels_per_standard_measure,
                                0.0, static_cast<double>(view.area_canvas_rows - 1));
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET;
    action.compose_area_begin_row =
        capture.begin_edge ? static_cast<std::uint64_t>(std::llround(row)) : capture.begin;
    action.compose_area_end_row =
        capture.begin_edge ? capture.end : static_cast<std::uint64_t>(std::llround(row));
    action.compose_area_virtual = capture.virtual_range;
    action.compose_area_begin_edge = capture.begin_edge;
    if (nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT))
        capture.active = false;
}
} // namespace

struct nk_rect compose_area_band_rect(const WormUiComposeView& view, struct nk_rect viewport) {
    const auto scale = viewport.h / static_cast<float>(view.viewport_height_pixels);
    const auto a =
        viewport.y + static_cast<float>(compose_area_y(view, view.area_range_begin_row)) * scale;
    const auto b =
        viewport.y + static_cast<float>(compose_area_y(view, view.area_range_end_row)) * scale;
    const auto left =
        viewport.x + 42.0f * viewport.w / static_cast<float>(view.viewport_width_pixels);
    return nk_rect(left, std::min(a, b), viewport.x + viewport.w - left, std::abs(b - a));
}


auto compose_area_pointer_consumed() -> bool {
    return consumed || menu_open || menu_release_pending;
}
auto compose_area_menu_open() -> bool {
    return menu_open;
}
auto compose_area_menu_consumed() -> bool {
    return menu_open || menu_release_pending || consumed;
}
auto compose_area_band_visible() -> bool {
    return band_visible;
}
auto compose_area_capture_active() -> bool {
    return capture.active || menu_open || menu_release_pending;
}
void reset_compose_area_menu() {
    capture = {};
    menu_open = false;
    menu_release_pending = false;
    opened_this_frame = false;
    consumed = false;
    band_visible = false;
}

void cancel_compose_area_capture(WormUiWorkspaceAction& action) {
    if (capture.active)
        capture.restore(action);
    else
        action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR;
    capture.active = false;
    if (menu_open) wormedit::ui::chrome::popup_arm_release(menu_release_pending);
    menu_open = false;
    consumed = true;
}

void process_compose_area_menu(nk_context* context, const WormUiComposeView& view,
                               WormUiWorkspaceAction& action, struct nk_rect viewport,
                               double input_right, bool focused) {
    opened_this_frame = false;
    (void)wormedit::ui::chrome::popup_update_release_pending(context, menu_release_pending);
    if (compose_note_menu_consumed()) { consumed = false; return; }
    band_visible = view.scene_available && view.area_range_available;
    consumed = capture.active || menu_open;
    if (!view.scene_available || view.area_rows_per_measure == 0 ||
        view.viewport_height_pixels <= 0 || view.viewport_width_pixels <= 0 ||
        !std::isfinite(view.pixels_per_standard_measure) || view.pixels_per_standard_measure <= 0 ||
        view.area_canvas_rows == 0) {
        capture.active = false;
        menu_open = false;
        consumed = false;
        return;
    }
    if (!focused && menu_open)
        close_menu(context);
    if (capture.active) {
        update_capture(context, view, action, viewport, focused);
        return;
    }
    if (!focused || !view.area_range_available || view.area_canvas_rows == 0) {
        render_menu(context, view, action); // Closes an open menu whose range went away.
        return;
    }
    const auto rect = compose_area_band_rect(view, viewport);
    const auto& mouse = context->input.mouse;
    const bool inside =
        nk_input_is_mouse_hovering_rect(&context->input, rect) && mouse.pos.x < input_right;
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) && inside) {
        menu_open = true;
        menu_context = {view.project_id, view.chart_id, view.document_revision, 0};
        opened_this_frame = true;
        const auto content = nk_window_get_content_region(context);
        menu_bounds = nk_rect(
            std::clamp(mouse.pos.x - content.x, 0.0f, std::max(0.0f, content.w - 340)),
            std::clamp(mouse.pos.y - content.y, 0.0f, std::max(0.0f, content.h - 390)), 340, 390);
        consumed = true;
    }
    if (!menu_open && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
        const auto a = viewport.y + compose_area_y(view, view.area_range_begin_row) * viewport.h /
                                        view.viewport_height_pixels;
        const auto b = viewport.y + compose_area_y(view, view.area_range_end_row) * viewport.h /
                                        view.viewport_height_pixels;
        const bool horizontal = mouse.pos.x >= rect.x && mouse.pos.x < input_right;
        if (horizontal && (std::abs(mouse.pos.y - a) <= 5 || std::abs(mouse.pos.y - b) <= 5)) {
            capture.start(view, std::abs(mouse.pos.y - a) <= std::abs(mouse.pos.y - b));
            consumed = true;
        } else if (!inside) {
            action.compose_area_clear_requested = true;
        }
    }
    render_menu(context, view, action);
}
