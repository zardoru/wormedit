#pragma once

#include "ui/compose_mode.h"
#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/panel_input.h"
#include "wormedit/nuklear_config.h"

#include <optional>

static auto& state = worm_ui_nuklear_compose_state();
static constexpr double compose_measure_rail_width = 42.0;

[[nodiscard]] inline auto compose_mode_of(const WormUiComposeView& view)
    -> wormedit::ui::compose_mode_t {
    return view.timing_focus ? wormedit::ui::compose_mode_t::timing_focus
                             : wormedit::ui::compose_mode_t::full;
}

struct ComposePaintContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    struct nk_command_buffer* canvas;
    struct nk_rect viewport;
    struct nk_rect rail;
};


struct ComposeToolbarContext {
    nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
};

void update_compose_pointer_navigation(
    WormUiWorkspaceAction* action,
    std::optional<WormUiComposeNavigationAction> desired_action);

struct ComposeInspectorContext {
    nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
};

void append_compose_input(
    WormUiWorkspaceAction* action, WormUiComposeInputKind kind, double x_pixels = 0.0,
    double y_pixels = 0.0, uint64_t lane_id = 0, bool successive = false,
    WormUiComposeSelectionMode selection_mode = WORM_UI_COMPOSE_SELECTION_REPLACE,
    WormUiComposeNavigationAction navigation_action = WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD,
    uint64_t object_id = 0, uint8_t resource_role = 0,
    bool razor_stroke_start = false,
    WormUiTimingLaneKind timing_lane = WORM_UI_TIMING_LANE_BPM,
    uint64_t timing_chart_id = 0, uint64_t timing_document_revision = 0,
    uint64_t timing_anchor_chart_row = 0);

struct ComposeOverviewInput {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect overview;
    bool available;
    bool pressed;
    bool window_focused;
    bool beat_zero_was_active;
};

struct ComposeOverviewContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_command_buffer* canvas;
    struct nk_rect bounds;
};

struct ComposePointerFocusContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    struct nk_rect overview;
    float editor_input_right;
    bool overview_available;
    bool window_focused;
};

struct ComposePointerFocusState {
    bool pointer_inside;
    bool pointer_clicked;
    bool overview_pressed;
};

struct ComposeViewportInputContext {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    float input_right;
    bool pointer_inside;
    bool window_focused;
};

struct ComposeViewportCursor {
    double x;
    double y;
    double note_field_left;
    double note_field_right;
    bool in_note_field;
};

struct ComposeBeatFitInput {
    struct nk_context* context;
    const WormUiComposeView* view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    double pointer_y;
    double pointer_started_x;
    double pointer_started_y;
    std::uint16_t pointer_modifiers;
    std::uint16_t blocked_modifiers;
    std::size_t shortcut_event_count;
    bool window_focused;
    bool pointer_started_in_view;
    bool left_down;
    bool left_released;
};

struct ComposeDoubleClickContext {
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    const worm_ui_nuklear_pointer_click_t* clicks;
    std::size_t click_count;
    struct nk_rect viewport;
    double editor_input_right;
    double note_field_left;
    double note_field_right;
};

struct ComposePointerEventContext {
    nk_context* context;
    const WormUiComposeView* view;
    const WormUiTimingView* timing_view;
    WormUiWorkspaceAction* action;
    struct nk_rect viewport;
    double editor_input_right;
    double pointer_x;
    double pointer_y;
    double note_field_left;
    double note_field_right;
    double pointer_started_x;
    double pointer_started_y;
    double paint_anchor_x;
    double paint_anchor_y;
    double paint_pointer_x;
    double paint_pointer_y;
    double left_clicked_screen_x;
    double left_clicked_screen_y;
    uint16_t pointer_modifiers;
    uint16_t any_pointer_modifier;
    std::size_t shortcut_event_count;
    bool window_focused;
    bool pointer_inside;
    bool pointer_clicked;
    bool pointer_in_note_field;
    bool pointer_started_in_view;
    bool pointer_started_in_note_field;
    bool left_down;
    bool left_released;
    bool alt_down;
    bool compose_pointer_captured;
    bool compose_note_double_click_in_view;
};

