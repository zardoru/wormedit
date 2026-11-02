#include "ui/nuklear/storyboard/transport.h"
#include "ui/nuklear/chrome/chrome.h"

namespace wormedit::ui {
namespace {
void render_frame_cost(nk_context* context, const storyboard_transport_view_t& view) {
    if (view.frame_ms) nk_labelf(context, NK_TEXT_RIGHT, "Frame %.2f ms", *view.frame_ms);
    else nk_label(context, "Frame —", NK_TEXT_RIGHT);
}
} // namespace

auto render_storyboard_transport(nk_context* context, const storyboard_transport_view_t& view,
    storyboard_transport_action_t& action, bool input_available) -> bool {
    if (!context) return false;
    nk_layout_row_template_begin(context, 26);
    chrome::IconButton::layout_columns(context, 1);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 120);
    nk_layout_row_template_end(context);
    const auto* hint = view.loop_enabled ? "Disable storyboard selection loop"
        : "Loop selected storyboard key times";
    const bool clicked = chrome::IconButton{.glyph = lucide::Glyph::rotate_ccw,
        .tooltip = hint, .selected = view.loop_enabled,
        .enabled = input_available && view.can_toggle_loop()}.render(context);
    const auto* guidance = !view.available ? "Select a chart with audio output to loop"
        : view.loop_enabled ? "Shared transport loop enabled"
        : view.selection && view.selection->is_playable() ? "Loop selected key interval"
        : "Select two key times at or after 0 ms to loop";
    nk_label(context, guidance, NK_TEXT_LEFT);
    render_frame_cost(context, view);
    if (!clicked || !input_available || !view.can_toggle_loop()) return false;
    action.loop = view.loop_enabled ? std::nullopt : view.selection;
    return true;
}

} // namespace wormedit::ui
