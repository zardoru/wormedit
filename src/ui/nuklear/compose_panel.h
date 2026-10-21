#pragma once

#include "ui/frontend.h"
#include "ui/nuklear/panel_input.h"

#include <cstddef>
#include <cstdint>

struct nk_context;
struct nk_rect;

[[nodiscard]] int worm_ui_nuklear_compose_view_valid(const wormedit::ui::compose_view_t* view);
// Overview publication failures must not invalidate the editable chart scene.
[[nodiscard]] int worm_ui_nuklear_compose_overview_valid(const wormedit::ui::compose_view_t* view);
[[nodiscard]] const char* worm_ui_nuklear_compose_overview_error(const wormedit::ui::compose_view_t* view);

void worm_ui_nuklear_reset_compose_panel();

void worm_ui_nuklear_queue_compose_key(std::uint32_t keycode, std::uint32_t scancode,
                                       std::uint16_t modifiers, bool down, bool repeat);

void worm_ui_nuklear_queue_compose_wheel(double delta_x, double delta_y, double mouse_x,
                                         double mouse_y, std::uint16_t modifiers,
                                         bool coarse_steps);

void worm_ui_nuklear_queue_compose_middle_click(float x, float y);

void worm_ui_nuklear_compose_handle_focus_lost();

void worm_ui_nuklear_compose_discard_frame_input();

void worm_ui_nuklear_prepare_compose_panel(const wormedit::ui::compose_view_t* view, bool compose_tab_active,
                                           bool window_focused, wormedit::ui::workspace_action_t* action);

void worm_ui_nuklear_render_compose_panel(struct nk_context* context, const wormedit::ui::compose_view_t* view,
                                          const wormedit::ui::timing_view_t* timing_view,
                                          wormedit::ui::workspace_action_t* action, bool window_focused,
                                          bool ime_composition_active,
                                          const worm_ui_nuklear_pointer_click_t* double_clicks,
                                          std::size_t double_click_count,
                                          std::size_t shortcut_event_count);

[[nodiscard]] bool worm_ui_nuklear_compose_lane_focused(const wormedit::ui::compose_view_t* view,
                                                        bool compose_tab_active,
                                                        bool window_focused);

void worm_ui_nuklear_render_compose_minimap(struct nk_context* context,
    const wormedit::ui::compose_view_t* view, wormedit::ui::workspace_action_t* action,
    struct nk_rect bounds, bool window_focused);

// The song-length bar under the transport seek bar: waveform of the zoomed window, measure
// grid, playhead and the Compose viewport. Seeking and the beat-zero drag follow the window.
void worm_ui_nuklear_render_compose_bgm_bar(struct nk_context* context,
    const wormedit::ui::compose_view_t* view, wormedit::ui::workspace_action_t* action,
    struct nk_rect bounds, bool window_focused);
