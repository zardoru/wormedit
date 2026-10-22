#include "ui/nuklear/compose_snap_grid.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/render/theme.h"
#include <algorithm>
#include <array>
#include <cstdio>

namespace {
using namespace wormedit::ui::chrome;
void emit_snap(nk_context* context, const wormedit::ui::compose_view_t& view,
               wormedit::ui::workspace_action_t& action, std::uint64_t denominator) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = view.chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_snap_denominator = denominator;
    context_menu_close(worm_ui_nuklear_compose_state().snap_grid.snap_grid_menu, context);
}
void move_choice(nk_context* context, int count, int& selected) {
    int delta{};
    if (nk_input_is_key_pressed(&context->input, NK_KEY_LEFT)) --delta;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_RIGHT)) ++delta;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_UP)) delta -= 4;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_DOWN)) delta += 4;
    selected = std::clamp(selected + delta, 0, count - 1);
}
void render_grid_popup(nk_context* context, const wormedit::ui::compose_view_t& view,
                       wormedit::ui::workspace_action_t& action, bool available) {
    auto& state = worm_ui_nuklear_compose_state();
    auto& menu = state.snap_grid.snap_grid_menu;
    const auto count = static_cast<int>(std::min<std::size_t>(view.supported_snap_denominators.size(), 32));
    if (!available || count == 0) context_menu_close(menu, context);
    const auto height = context_menu_height(static_cast<float>((count + 3) / 4 + 3));
    if (!context_menu_begin(menu, context, wormedit::ui::chrome::PopupContextStamp::from(view), nk_vec2(280, height))) return;
    std::array<std::array<char, 24>, 32> storage{};
    std::array<const char*, 32> labels{};
    for (int index = 0; index < count; ++index) {
        std::snprintf(storage[index].data(), storage[index].size(), "1/%llu",
                      static_cast<unsigned long long>(view.supported_snap_denominators[index]));
        labels[index] = storage[index].data();
    }
    move_choice(context, count, state.snap_grid.snap_grid_choice);
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Editing grid", NK_TEXT_LEFT);
    const auto chosen = ChipGrid{labels.data(), count, state.snap_grid.snap_grid_choice, 4}.render(context).chosen;
    if (chosen >= 0) emit_snap(context, view, action, view.supported_snap_denominators[chosen]);
    else if (nk_input_is_key_pressed(&context->input, NK_KEY_ENTER))
        emit_snap(context, view, action, view.supported_snap_denominators[state.snap_grid.snap_grid_choice]);
    nk_layout_row_dynamic(context, 22, 1);
    if (Button{"Find fitting snap", ButtonKind::secondary, nullptr,
               view.selected_note_count + view.selected_bgm_event_count != 0}.render(context)) {
        emit_snap(context, view, action, 0);
        if (action.compose_action == WORM_UI_COMPOSE_ACTION_SET_EDIT_SNAP)
            action.compose_action = WORM_UI_COMPOSE_ACTION_FIND_FITTING_SNAP;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Arrows · Enter · Esc", NK_TEXT_LEFT);
    context_menu_end(menu, context);
}
}

void render_compose_snap_badge(nk_context* context, const wormedit::ui::compose_view_t& view,
                               wormedit::ui::workspace_action_t& action, bool available) {
    auto& state = worm_ui_nuklear_compose_state();
    auto& menu = state.snap_grid.snap_grid_menu;
    (void)context_menu_update(menu, context);
    char label[24];
    std::snprintf(label, sizeof(label), "1/%llu",
                  static_cast<unsigned long long>(view.area_snap_denominator));
    const auto bounds = nk_widget_bounds(context);
    if (Chip{label, menu.open, available, context_menu_owns_input(menu) ? nullptr : "Choose editing grid"}.render(context)) {
        state.snap_grid.snap_grid_choice = 0;
        const auto count = std::min<std::size_t>(view.supported_snap_denominators.size(), 32);
        for (std::size_t index = 0; index < count; ++index)
            if (view.supported_snap_denominators[index] == view.area_snap_denominator)
                state.snap_grid.snap_grid_choice = static_cast<int>(index);
        context_menu_open(menu, nk_vec2(bounds.x, bounds.y), wormedit::ui::chrome::PopupContextStamp::from(view));
        menu.shared_keyboard_navigation = false;
    }
    render_grid_popup(context, view, action, available);
}
