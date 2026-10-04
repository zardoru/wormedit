#include "ui/nuklear/compare_panel.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <algorithm>

void worm_ui_nuklear_render_compare_panel(nk_context* context,
                                          const WormUiCompareView* view,
                                          WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !view->visible) return;
    if (!view->has_two_difficulties || !view->first_pane || !view->second_pane) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Choose two difficulties to compare.", NK_TEXT_LEFT);
        return;
    }
    int selected{};
    for (size_t index = 0; index < view->chart_count; ++index)
        if (view->chart_ids[index] == view->second_chart_id) selected = static_cast<int>(index);
    if (view->chart_names && view->chart_count > 0) {
        const auto* labels = const_cast<const char**>(view->chart_names);
        nk_layout_row_dynamic(context, 28, 1);
        const int next = wormedit::ui::chrome::safe_combo(
            context, labels, static_cast<int>(view->chart_count), selected, 24, nk_vec2(220, 300));
        if (next != selected && static_cast<size_t>(next) < view->chart_count) {
            action->kind = WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART;
            action->compare_chart_id = view->chart_ids[next];
        }
    }
    nk_layout_row_dynamic(context, 18, 2);
    nk_label(context, view->first_difficulty, NK_TEXT_CENTERED);
    nk_label(context, view->second_difficulty, NK_TEXT_CENTERED);
    struct nk_rect first{}, second{};
    nk_layout_row_dynamic(context, std::max(160.0f, nk_window_get_content_region(context).h - 56.0f), 2);
    (void)nk_widget(&first, context);
    (void)nk_widget(&second, context);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas || first.w <= 1.0f || second.w <= 1.0f) return;
    draw_compose_scene_canvas(context, view->first_pane, canvas, first);
    draw_compose_scene_canvas(context, view->second_pane, canvas, second);
    const auto& scroll = context->input.mouse.scroll_delta;
    if ((nk_input_is_mouse_hovering_rect(&context->input, first) ||
         nk_input_is_mouse_hovering_rect(&context->input, second)) && scroll.y != 0.0f) {
        action->kind = WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL;
        action->compare_scroll_delta = static_cast<double>(scroll.y) * 72.0;
    }
}
