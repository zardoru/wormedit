#pragma once

// Test-only adapters: tests build the whole workspace_action_t while the Compose Nuklear panels
// take their narrower window. The frontend builds the window explicitly in app/frontend.cpp.
#include "ui/nuklear/compose/compose_bookmark_chooser.h"
#include "ui/nuklear/compose/compose_sound_picker.h"
#include "ui/nuklear/compose/compose_state.h"

inline void worm_ui_nuklear_update_inline_offset(worm_ui_nuklear_compose_state_t& state,
    const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action,
    bool committed, bool deactivated) {
    auto window = wormedit::ui::compose_ui_window_of(action);
    worm_ui_nuklear_update_inline_offset(state, view, window, committed, deactivated);
}

[[nodiscard]] inline auto worm_ui_nuklear_inline_timing_review_new_stage(
    worm_ui_nuklear_compose_state_t& state, const wormedit::ui::timing_view_t& view,
    wormedit::ui::workspace_action_t& action, float anchor_x, float anchor_y) -> bool {
    const auto window = wormedit::ui::compose_ui_window_of(action);
    return worm_ui_nuklear_inline_timing_review_new_stage(state, view, window, anchor_x, anchor_y);
}

inline void worm_ui_nuklear_inline_timing_cancel(worm_ui_nuklear_compose_state_t& state,
                                                 wormedit::ui::workspace_action_t& action) {
    auto window = wormedit::ui::compose_ui_window_of(action);
    worm_ui_nuklear_inline_timing_cancel(state, window);
}

inline void worm_ui_nuklear_inline_timing_update(worm_ui_nuklear_compose_state_t& state,
    const wormedit::ui::timing_view_t& view, wormedit::ui::workspace_action_t& action,
    bool committed, bool deactivated) {
    auto window = wormedit::ui::compose_ui_window_of(action);
    worm_ui_nuklear_inline_timing_update(state, view, window, committed, deactivated);
}

inline void worm_ui_nuklear_pick_draw_source(worm_ui_nuklear_compose_state_t& state,
    wormedit::ui::workspace_action_t& action, uint8_t kind, uint64_t id) {
    auto window = wormedit::ui::compose_ui_window_of(action);
    worm_ui_nuklear_pick_draw_source(state, window, kind, id);
}

inline void render_compose_sound_picker(nk_context* context, const wormedit::ui::compose_view_t& view,
    wormedit::ui::workspace_action_t& action, worm_ui_nuklear_compose_state_t& state, bool focused) {
    auto window = wormedit::ui::compose_ui_window_of(action);
    render_compose_sound_picker(context, view, window, state, focused);
}

[[nodiscard]] inline auto worm_ui_nuklear_render_compose_bookmark_chooser(nk_context* context,
    const wormedit::ui::bookmark_chooser_view_t& view, wormedit::ui::workspace_action_t* action,
    bool window_focused, bookmark_chooser_state_t* state) -> bool {
    auto window = wormedit::ui::compose_ui_window_of(*action);
    return worm_ui_nuklear_render_compose_bookmark_chooser(context, view, &window, window_focused, state);
}
