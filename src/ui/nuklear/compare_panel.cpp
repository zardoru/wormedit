#include "ui/nuklear/compare_panel.h"
#include "ui/nuklear/content_layout.h"
#include "ui/nuklear/compose_canvas.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/theme.h"

#include <algorithm>

namespace {
void render_diagnostics(nk_context* context, const WormUiCompareView& view,
                        WormUiWorkspaceAction& action) {
    if (view.diagnostic_count == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No validation diagnostics.", NK_TEXT_LEFT);
        return;
    }
    const auto group_height = std::min(154.0f, 44.0f +
        22.0f * static_cast<float>(view.diagnostic_count));
    nk_layout_row_dynamic(context, group_height, 1);
    if (!nk_group_begin(context, "Validation diagnostics", NK_WINDOW_BORDER)) return;
    for (size_t index = 0; index < view.diagnostic_count; ++index) {
        const char* label = view.diagnostic_labels ? view.diagnostic_labels[index]
                                                    : "Validation diagnostic";
        nk_layout_row_dynamic(context, 22, 1);
        if (view.diagnostic_has_rows && view.diagnostic_has_rows[index]) {
            if (nk_button_label(context, label)) {
                action.kind = WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP;
                action.compare_diagnostic_row = view.diagnostic_rows[index];
                action.compare_diagnostic_measure = view.diagnostic_measures[index];
                action.compare_diagnostic_chart = view.diagnostic_chart_id;
            }
        } else {
            nk_label(context, label, NK_TEXT_LEFT);
        }
    }
    nk_group_end(context);
}
} // namespace

void worm_ui_nuklear_render_compare_panel(nk_context* context,
                                          const WormUiCompareView* view,
                                          WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !view->visible) return;
    render_diagnostics(context, *view, *action);
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
    const auto field_height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 160.0f, .include_spacing = true});
    nk_layout_row_dynamic(context, field_height, 2);
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
