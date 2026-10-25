#include "ui/nuklear/project/compare_panel.h"
#include "ui/nuklear/chrome/content_layout.h"
#include "ui/nuklear/compose/compose_canvas.h"
#include "ui/nuklear/app/sdl_backend.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/render/theme.h"

#include <algorithm>

namespace {
void render_diagnostics(nk_context* context, const wormedit::ui::compare_view_t& view,
                        wormedit::ui::workspace_action_t& action) {
    if (view.diagnostics.size() == 0) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No validation diagnostics.", NK_TEXT_LEFT);
        return;
    }
    const auto group_height = std::min(154.0f, 44.0f +
        22.0f * static_cast<float>(view.diagnostics.size()));
    nk_layout_row_dynamic(context, group_height, 1);
    if (!nk_group_begin(context, "Validation diagnostics", NK_WINDOW_BORDER)) return;
    for (size_t index = 0; index < view.diagnostics.size(); ++index) {
        const auto& diagnostic = view.diagnostics[index];
        const char* label = diagnostic.label.data();
        nk_layout_row_dynamic(context, 22, 1);
        if (diagnostic.has_row) {
            if (nk_button_label(context, label)) {
                action.kind = WORM_UI_WORKSPACE_ACTION_COMPARE_DIAGNOSTIC_JUMP;
                action.compare_diagnostic_row = diagnostic.row;
                action.compare_diagnostic_measure.value = diagnostic.measure.value;
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
                                          const wormedit::ui::compare_view_t* view,
                                          wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action || !view->visible) return;
    render_diagnostics(context, *view, *action);
    if (!view->has_two_difficulties || !view->first_pane || !view->second_pane) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "Choose two difficulties to compare.", NK_TEXT_LEFT);
        return;
    }
    int selected{};
    for (size_t index = 0; index < view->charts.size(); ++index)
        if (view->charts[index].id == view->second_chart_id) selected = static_cast<int>(index);
    if (!view->charts.empty()) {
        nk_layout_row_dynamic(context, 28, 1);
        const auto next = wormedit::ui::chrome::safe_combo(context,
            std::span<const wormedit::ui::compare_chart_view_t>{view->charts},
            selected, 24, nk_vec2(220, 300), &wormedit::ui::compare_chart_view_t::label);
        if (next != selected && static_cast<std::size_t>(next) < view->charts.size()) {
            action->kind = WORM_UI_WORKSPACE_ACTION_COMPARE_SELECT_CHART;
            action->compare_chart_id = view->charts[next].id;
        }
    }
    nk_layout_row_dynamic(context, 18, 2);
    nk_label(context, view->first_difficulty.data(), NK_TEXT_CENTERED);
    nk_label(context, view->second_difficulty.data(), NK_TEXT_CENTERED);
    struct nk_rect first{}, second{};
    const auto field_height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 160.0f, .include_spacing = true});
    nk_layout_row_dynamic(context, field_height, 2);
    (void)nk_widget(&first, context);
    (void)nk_widget(&second, context);
    auto* canvas = nk_window_get_canvas(context);
    if (!canvas || first.w <= 1.0f || second.w <= 1.0f) return;
    const wormedit::ui::nuklear::direct_compose_fields_scope_t direct_fields(context);
    draw_compose_scene_canvas(context, view->first_pane, canvas, first);
    draw_compose_scene_canvas(context, view->second_pane, canvas, second);
    const auto& scroll = context->input.mouse.scroll_delta;
    if ((nk_input_is_mouse_hovering_rect(&context->input, first) ||
         nk_input_is_mouse_hovering_rect(&context->input, second)) && scroll.y != 0.0f) {
        action->kind = WORM_UI_WORKSPACE_ACTION_COMPARE_SCROLL;
        action->compare_scroll_delta = static_cast<double>(scroll.y) * 72.0;
    }
}
