#include "sys/compose_scroll_geometry.h"
#include "ui/nuklear/compose_area_menu.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_note_menu.h"
#include "ui/nuklear/context_menu.h"
#include "wormedit/nuklear_config.h"
#include "ui/nuklear/compose_state.h"

namespace {
namespace chrome = wormedit::ui::chrome;

ComposeAreaCapture capture;
bool consumed{};
bool band_visible{};
chrome::ContextMenu menu{.title = "Area actions"};

auto menu_stamp(const WormUiComposeView& view) -> chrome::PopupContextStamp {
    return {view.project_id, view.chart_id, view.document_revision, 0};
}

void close_menu(nk_context* context) {
    chrome::context_menu_close(menu, context);
    consumed = true;
}

void menu_entry(nk_context* context, const char* title, const char* disabled_reason,
                WormUiComposeActionKind kind, WormUiWorkspaceAction& action) {
    if (chrome::context_menu_item(menu, context, title, disabled_reason)) {
        action.compose_action = kind;
        consumed = true;
    }
}

auto reason_unless(bool enabled, const char* reason) -> const char* {
    return enabled ? nullptr : reason;
}

void render_menu(nk_context* context, const WormUiComposeView& view,
                 WormUiWorkspaceAction& action) {
    if (!menu.open)
        return;
    if (!view.area_range_available) {
        close_menu(context);
        return;
    }
    if (!chrome::context_menu_begin(menu, context, menu_stamp(view),
                                    nk_vec2(chrome::context_menu_width,
                                            chrome::context_menu_height(6)))) {
        consumed = true;
        return;
    }
    constexpr const char* materialize = "Materialize the selected measures first";
    menu_entry(context, "Quantize selection",
               reason_unless(view.selected_note_count != 0, "Select notes first"),
               WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE, action);
    menu_entry(context, "Create section",
               reason_unless(view.beat_section_selection_span_available, materialize),
               WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION, action);
    menu_entry(context, "Loop playback", "Range looping is not available yet",
               WORM_UI_COMPOSE_ACTION_NONE, action);
    menu_entry(context, "Delete contents",
               reason_unless(view.area_delete_supported,
                             view.area_range_virtual ? materialize
                                                     : "Area deletion supports notes and BGM only"),
               WORM_UI_COMPOSE_ACTION_AREA_DELETE, action);
    menu_entry(context, "Add SV recipe", reason_unless(!view.area_range_virtual, materialize),
               WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE, action);
    menu_entry(context, "Add SPEEDS span", "Speed span creation from an area is not available yet",
               WORM_UI_COMPOSE_ACTION_NONE, action);
    chrome::context_menu_end(menu, context);
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
    return consumed || chrome::context_menu_owns_input(menu);
}
auto compose_area_menu_open() -> bool {
    return menu.open;
}
auto compose_area_menu_consumed() -> bool {
    return consumed || chrome::context_menu_owns_input(menu);
}
auto compose_area_band_visible() -> bool {
    return band_visible;
}
auto compose_area_capture_active() -> bool {
    return capture.active || chrome::context_menu_owns_input(menu);
}
void reset_compose_area_menu() {
    capture = {};
    chrome::context_menu_reset(menu);
    consumed = false;
    band_visible = false;
}

void cancel_compose_area_capture(WormUiWorkspaceAction& action) {
    if (capture.active)
        capture.restore(action);
    else
        action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR;
    capture.active = false;
    chrome::context_menu_close(menu);
    consumed = true;
}

void process_compose_area_menu(nk_context* context, const WormUiComposeView& view,
                               WormUiWorkspaceAction& action, struct nk_rect viewport,
                               double input_right, bool focused) {
    (void)chrome::context_menu_update(menu, context);
    if (compose_note_menu_consumed()) { consumed = false; return; }
    band_visible = view.scene_available && view.area_range_available;
    consumed = capture.active || menu.open;
    if (!view.scene_available || view.area_rows_per_measure == 0 ||
        view.viewport_height_pixels <= 0 || view.viewport_width_pixels <= 0 ||
        !std::isfinite(view.pixels_per_standard_measure) || view.pixels_per_standard_measure <= 0 ||
        view.area_canvas_rows == 0) {
        capture.active = false;
        chrome::context_menu_close(menu, context);
        consumed = false;
        return;
    }
    if (!focused && menu.open)
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
    if (worm_ui_nuklear_compose_context_click(context, view) && inside) {
        chrome::context_menu_open(menu, mouse.pos, menu_stamp(view));
        consumed = true;
    }
    if (!menu.open && nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT)) {
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
