#include "ui/nuklear/compose_bmson_tab.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/compose_panel_internal.h"
#include "ui/nuklear/render/theme.h"

#include <algorithm>
#include <cstdint>

namespace {

struct BmsonTab {
    nk_context* context;
    const wormedit::ui::compose_view_t* view;
    wormedit::ui::workspace_action_t* action;
    worm_ui_nuklear_compose_state_t* state;
};

auto can_move_selected_bmson_bgm(const wormedit::ui::compose_view_t& view) -> bool {
    return view.scene_available && view.selected_bmson_bgm_event_id.value != 0;
}

void render_bmson_object_actions(const BmsonTab& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* action = panel.action;
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto move_clicked = nk_button_label(context, "Move BMSON BGM to cursor");
    const auto delete_clicked = nk_button_label(context, "Delete BMSON BGM");
    if (!can_move) nk_widget_disable_end(context);
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (move_clicked && can_move) {
            action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
            action->compose_action = WORM_UI_COMPOSE_ACTION_MOVE_BMSON_BGM_TO_CURSOR;
        } else if (delete_clicked && can_move) {
            action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
            action->compose_action = WORM_UI_COMPOSE_ACTION_DELETE_BMSON_BGM_MARKER;
        }
    }

    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto assign_key_clicked = nk_button_label(context, "BGM to active key lane");
    if (!can_move) nk_widget_disable_end(context);
    const auto can_assign_to_bgm = view.selected_bmson_note_event_id.value != 0;
    if (!can_assign_to_bgm) nk_widget_disable_begin(context);
    const auto assign_bgm_clicked = nk_button_label(context, "Selected tap to BGM");
    if (!can_assign_to_bgm) nk_widget_disable_end(context);
    if (action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (assign_key_clicked && can_move) {
            action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_BGM_TO_ACTIVE_LANE;
        } else if (assign_bgm_clicked && can_assign_to_bgm) {
            action->compose_bmson_bgm_event_id.value = view.selected_bmson_note_event_id.value;
            action->compose_action = WORM_UI_COMPOSE_ACTION_ASSIGN_BMSON_NOTE_TO_BGM;
        }
    }
}

void render_bmson_marker_actions(const BmsonTab& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* action = panel.action;
    nk_layout_row_dynamic(context, 12, 1);
    nk_rule_horizontal(context, wormedit::ui::theme::ui(
                                    wormedit::ui::theme::UiColor::border), false);
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto insert_continue = nk_button_label(context, "Insert continue at cursor");
    const auto insert_restart = nk_button_label(context, "Insert restart at cursor");
    if (!can_move) nk_widget_disable_end(context);
    if ((insert_continue || insert_restart) && can_move &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
        action->compose_action = insert_restart
            ? WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_INSERT_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Staged BMSON markers: %zu",
              view.staged_bmson_marker_restarts.size());
    nk_layout_row_dynamic(context, 24, 2);
    if (!can_move) nk_widget_disable_begin(context);
    const auto stage_continue = nk_button_label(context, "Stage continue");
    const auto stage_restart = nk_button_label(context, "Stage restart");
    if (!can_move) nk_widget_disable_end(context);
    if ((stage_continue || stage_restart) && can_move &&
        action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
        action->compose_action = stage_restart
            ? WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_RESTART_AT_CURSOR
            : WORM_UI_COMPOSE_ACTION_STAGE_BMSON_BGM_CONTINUE_AT_CURSOR;
    }
}

void render_bmson_razor_mode(const BmsonTab& panel, bool can_move) {
    auto* context = panel.context;
    const auto& view = *panel.view;
    auto* state = panel.state;
    nk_layout_row_dynamic(context, 12, 1);
    nk_rule_horizontal(context, wormedit::ui::theme::ui(
                                    wormedit::ui::theme::UiColor::border), false);
    const auto can_razor = can_move && view.selected_bmson_source_lane_id.value != 0 &&
                           !view.note_paint_mode_active &&
                           !view.semantic_layer_paint_mode_active &&
                           !view.timing_stage_active;
    if (!can_razor) {
        state->bmson_input.bmson_razor_mode = worm_ui_nuklear_bmson_razor_mode_t::off;
        nk_widget_disable_begin(context);
    }
    nk_layout_row_dynamic(context, 24, 2);
    const auto continue_clicked = wormedit::ui::chrome::mode_button(
        context, "Razor continue", "Slice the selected BGM lane without restarting",
        state->bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::continuation);
    const auto restart_clicked = wormedit::ui::chrome::mode_button(
        context, "Razor restart", "Slice the selected BGM lane and restart playback",
        state->bmson_input.bmson_razor_mode == worm_ui_nuklear_bmson_razor_mode_t::restart);
    if (!can_razor) nk_widget_disable_end(context);
    if (can_razor && (continue_clicked || restart_clicked)) {
        const auto choice = restart_clicked ? worm_ui_nuklear_bmson_razor_mode_t::restart
                                             : worm_ui_nuklear_bmson_razor_mode_t::continuation;
        state->bmson_input.bmson_razor_mode = state->bmson_input.bmson_razor_mode == choice
            ? worm_ui_nuklear_bmson_razor_mode_t::off : choice;
        if (state->bmson_input.bmson_razor_mode != worm_ui_nuklear_bmson_razor_mode_t::off)
            state->resource_picker.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    }
}

void render_staged_bmson_actions(const BmsonTab& panel) {
    const auto& view = *panel.view;
    auto* context = panel.context;
    auto* action = panel.action;
    const auto has_staged = view.staged_bmson_marker_restarts.size() != 0;
    nk_layout_row_dynamic(context, 24, 2);
    if (!has_staged) nk_widget_disable_begin(context);
    const auto commit_clicked = nk_button_label(context, "Commit staged");
    const auto cancel_clicked = nk_button_label(context, "Cancel staged");
    if (!has_staged) nk_widget_disable_end(context);
    if (has_staged && action->compose_action == WORM_UI_COMPOSE_ACTION_NONE) {
        if (commit_clicked) action->compose_action = WORM_UI_COMPOSE_ACTION_COMMIT_STAGED_BMSON_BGM;
        else if (cancel_clicked) action->compose_action = WORM_UI_COMPOSE_ACTION_CANCEL_STAGED_BMSON_BGM;
    }
}

void render_bmson_object_and_marker_tools(const BmsonTab& panel) {
    const auto can_move = can_move_selected_bmson_bgm(*panel.view);
    render_bmson_object_actions(panel, can_move);
    render_bmson_marker_actions(panel, can_move);
    render_bmson_razor_mode(panel, can_move);
    render_staged_bmson_actions(panel);
}


void render_selected_bmson_slice_controls(const BmsonTab& inspector) {
    const auto& view = *inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    const bool enabled = view.scene_available && view.selected_bmson_bgm_event_id.value != 0 &&
                         view.selected_bmson_source_lane_id.value != 0;
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "BMSON slice", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 2);
    if (!enabled) nk_widget_disable_begin(context);
    const auto restart_clicked = nk_button_label(context, "Restart");
    const auto continue_clicked = nk_button_label(context, "Continue");
    if (!enabled) nk_widget_disable_end(context);
    if (!enabled || action->compose_action != WORM_UI_COMPOSE_ACTION_NONE ||
        (!restart_clicked && !continue_clicked)) return;
    action->compose_action = restart_clicked ? WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_RESTART
                                            : WORM_UI_COMPOSE_ACTION_SET_BMSON_BGM_CONTINUE;
    action->compose_bmson_bgm_event_id.value = view.selected_bmson_bgm_event_id.value;
    action->compose_input_chart_id.value = view.chart_id.value;
    action->compose_input_document_revision = view.document_revision;
}

void render_staged_bmson_regions(const BmsonTab& inspector) {
    const auto* view = inspector.view;
    auto* context = inspector.context;
    auto* action = inspector.action;
    if (view->staged_bmson_marker_restarts.size() == 0 ||
        !nk_tree_push(context, NK_TREE_TAB, "Staged BMSON regions", NK_MAXIMIZED))
        return;
    inspector.state->sounds_dock.bmson_audition_index = std::clamp(inspector.state->sounds_dock.bmson_audition_index, 1,
                                            static_cast<int>(view->staged_bmson_marker_restarts.size()));
    nk_layout_row_dynamic(context, 24, 1);
    nk_property_int(context, "Region", 1, &inspector.state->sounds_dock.bmson_audition_index,
                    static_cast<int>(view->staged_bmson_marker_restarts.size()), 1, 1.0f);
    const auto selected_index = static_cast<std::size_t>(inspector.state->sounds_dock.bmson_audition_index - 1);
    const auto assigned_lane = view->staged_bmson_marker_playable_lanes[selected_index];
    nk_labelf(context, NK_TEXT_LEFT, "%s · %s",
              view->staged_bmson_marker_restarts[selected_index] ? "Restart" : "Continue",
              assigned_lane == 0 ? "BGM" : "Playable key lane");
    const auto selected = static_cast<std::uint64_t>(inspector.state->sounds_dock.bmson_audition_index);
    if (nk_button_label(context, "Audition staged region"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_layout_row_dynamic(context, 24, 2);
    if (nk_button_label(context, "Assign key"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_ACTIVE_LANE,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Assign BGM"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_ASSIGN_DRAFT_TO_BGM,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Restart"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_TOGGLE_DRAFT_RESTART,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    if (nk_button_label(context, "Remove"))
        append_compose_input(action, WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT,
                             0.0, 0.0, 0, false, WORM_UI_COMPOSE_SELECTION_REPLACE,
                             WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD, selected);
    nk_tree_pop(context);
}


} // namespace

void worm_ui_nuklear_render_compose_bmson_tab(struct nk_context* context,
                                              const wormedit::ui::compose_view_t* view,
                                              wormedit::ui::workspace_action_t* action,
                                              worm_ui_nuklear_compose_state_t* state) {
    if (!context || !view || !action || !state) return;
    const BmsonTab panel{context, view, action, state};
    render_bmson_object_and_marker_tools(panel);
    render_selected_bmson_slice_controls(panel);
    render_staged_bmson_regions(panel);
}
