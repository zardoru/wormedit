#include "ui/nuklear/compose_measures_panel.h"
#include "ui/nuklear/content_layout.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/compose_action_stamp.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/tooltip.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <nuklear.h>
#include <span>

namespace {
constexpr float row_height = 26.0f;

auto raise(WormUiWorkspaceAction& action, const WormUiComposeView& view,
           WormUiComposeActionKind kind) -> bool {
    return wormedit::ui::raise_compose_action(action, view, kind,
                                              wormedit::ui::compose_project_stamp_t::preserve);
}

auto ids_of(const WormUiComposeView& view) -> std::span<const std::uint64_t> {
    return {view.measure_row_ids, view.measure_row_count};
}

void format_row(const WormUiComposeMeasureRowView& value, char* label, std::size_t size) {
    if (value.has_meter)
        std::snprintf(label, size, "%llu    %u/%u    %llu/%llu", static_cast<unsigned long long>(value.number),
                      value.meter_numerator, value.meter_denominator,
                      static_cast<unsigned long long>(value.length_numerator),
                      static_cast<unsigned long long>(value.length_denominator));
    else
        std::snprintf(label, size, "%llu    --    %llu/%llu", static_cast<unsigned long long>(value.number),
                      static_cast<unsigned long long>(value.length_numerator),
                      static_cast<unsigned long long>(value.length_denominator));
}

void row(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action,
         worm_ui_nuklear_compose_state_t& state, std::size_t index) {
    nk_layout_row_dynamic(context, row_height, 1);
    const auto hovered = nk_widget_is_hovered(context);
    const auto clicked = nk_widget_is_mouse_clicked(context, NK_BUTTON_LEFT) && !view.input_occluded;
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID || index >= view.measure_row_count) return;
    const auto& value = view.measure_rows[index];
    const auto selected = worm_ui_keysound_is_selected(state.measures_dock.measure_selection, value.measure_id);
    auto* canvas = nk_window_get_canvas(context);
    if (selected || hovered) nk_fill_rect(canvas, bounds, 3, nk_rgb(44, 72, 109));
    char label[96];
    format_row(value, label, sizeof(label));
    nk_draw_text(canvas, nk_rect(bounds.x + 6, bounds.y + 4, bounds.w - 8, 20), label,
        static_cast<int>(std::strlen(label)), context->style.font, nk_rgba(0, 0, 0, 0), nk_rgb(222, 228, 238));
    if (!clicked) return;
    const auto modifiers = SDL_GetModState();
    worm_ui_keysound_click(state.measures_dock.measure_selection, ids_of(view), index,
        (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0);
    // A plain or Shift click also moves the cursor, so the field follows the list.
    if (value.measure_id == state.measures_dock.measure_selection.primary &&
        raise(action, view, WORM_UI_COMPOSE_ACTION_GO_TO_MEASURE))
        action.compose_measure_number = value.number;
}

void meter_controls(nk_context* context, const WormUiComposeView& view, WormUiWorkspaceAction& action,
                    worm_ui_nuklear_compose_state_t& state) {
    nk_layout_row_template_begin(context, 27);
    nk_layout_row_template_push_static(context, 38.0f);
    nk_layout_row_template_push_variable(context, 40.0f);
    nk_layout_row_template_push_static(context, 10.0f);
    nk_layout_row_template_push_variable(context, 40.0f);
    nk_layout_row_template_push_static(context, 58.0f);
    nk_layout_row_template_end(context);
    nk_label(context, "Meter", NK_TEXT_LEFT);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.measures_dock.measures_meter_numerator.data(), static_cast<int>(state.measures_dock.measures_meter_numerator.size()),
        nk_filter_decimal);
    nk_label(context, "/", NK_TEXT_CENTERED);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        state.measures_dock.measures_meter_denominator.data(), static_cast<int>(state.measures_dock.measures_meter_denominator.size()),
        nk_filter_decimal);
    const auto none = state.measures_dock.measure_selection.selected.empty();
    if (none) nk_widget_disable_begin(context);
    const auto apply = nk_button_label(context, "Apply");
    if (none) nk_widget_disable_end(context);
    if (!apply || none || !raise(action, view, WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER)) return;
    state.measures_dock.measures_staged_ids = state.measures_dock.measure_selection.selected;
    action.compose_measure_ids = state.measures_dock.measures_staged_ids.data();
    action.compose_measure_id_count = state.measures_dock.measures_staged_ids.size();
    std::snprintf(action.timing_meter_numerator_text, sizeof(action.timing_meter_numerator_text), "%s",
                  state.measures_dock.measures_meter_numerator.data());
    std::snprintf(action.timing_meter_denominator_text, sizeof(action.timing_meter_denominator_text), "%s",
                  state.measures_dock.measures_meter_denominator.data());
}
} // namespace

void worm_ui_nuklear_render_compose_measures(nk_context* context, const WormUiComposeView* view,
    WormUiWorkspaceAction* action, worm_ui_nuklear_compose_state_t* state) {
    nk_layout_row_dynamic(context, 20, 1);
    char title[64];
    std::snprintf(title, sizeof(title), "Measures  ·  %zu selected", state->measures_dock.measure_selection.selected.size());
    nk_label(context, title, NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 27, 2);
    if (nk_button_label(context, "Select all")) worm_ui_keysound_select_all(state->measures_dock.measure_selection, ids_of(*view));
    if (nk_button_label(context, "Clear")) worm_ui_keysound_clear(state->measures_dock.measure_selection);
    meter_controls(context, *view, *action, *state);
    worm_ui_keysound_prune(state->measures_dock.measure_selection, ids_of(*view));
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "No.   Meter   Length", NK_TEXT_LEFT);
    const auto height = wormedit::ui::layout::remaining_content_height(context,
        {.minimum = 60.0f, .reserved = 5.0f});
    nk_layout_row_dynamic(context, height, 1);
    struct nk_list_view list{};
    if (nk_list_view_begin(context, &list, "Compose measures list", NK_WINDOW_BORDER,
                           static_cast<int>(row_height),
                           static_cast<int>(std::max<std::size_t>(view->measure_row_count, 1)))) {
        for (auto index = list.begin; index < std::min(list.end, static_cast<int>(view->measure_row_count)); ++index)
            row(context, *view, *action, *state, static_cast<std::size_t>(index));
        nk_list_view_end(&list);
    }
    if (view->measure_rows_truncated) {
        nk_layout_row_dynamic(context, 18, 1);
        nk_label(context, "Only the first measures are listed.", NK_TEXT_LEFT);
    }
}
