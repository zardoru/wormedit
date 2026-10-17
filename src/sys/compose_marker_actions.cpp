#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {

namespace {
constexpr auto marker_route_entries = std::array{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL,
                           .operation =
                               run_routed_compose_action<search_selected_note_labels_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL,
                           .operation =
                               run_routed_compose_action<search_selected_note_labels_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL,
                           .operation =
                               run_routed_compose_action<search_selected_note_labels_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL,
                           .operation =
                               run_routed_compose_action<edit_selected_note_labels_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL,
                           .operation =
                               run_routed_compose_action<edit_selected_note_labels_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER,
                           .operation = run_routed_compose_action<navigate_audio_marker_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER,
                           .operation = run_routed_compose_action<navigate_audio_marker_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE,
                           .operation = run_routed_compose_action<move_compose_bar_line_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER,
                           .operation = run_routed_compose_action<set_measures_meter_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER,
                           .operation =
                               run_routed_compose_action<set_cursor_measure_meter_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM,
                           .operation = run_routed_compose_action<propose_recipe_form_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM,
                           .operation = run_routed_compose_action<apply_recipe_form_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW,
                           .operation =
                               run_routed_compose_action<clear_recipe_form_preview_action_t>},
    compose_action_route_t{
        .kind = WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS,
        .operation = run_routed_compose_action<edit_cursor_measure_action_t,
                                               cursor_measure_command_t::remove_with_contents>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR,
                           .operation = run_routed_compose_action<transform_selected_notes_action_t,
                                                                  selection_transform_t::mirror>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT,
                           .operation =
                               run_routed_compose_action<transform_selected_notes_action_t,
                                                         selection_transform_t::rotate_left>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT,
                           .operation =
                               run_routed_compose_action<transform_selected_notes_action_t,
                                                         selection_transform_t::rotate_right>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER,
                           .operation =
                               run_routed_compose_action<transform_selected_notes_action_t,
                                                         selection_transform_t::shift_earlier>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER,
                           .operation =
                               run_routed_compose_action<transform_selected_notes_action_t,
                                                         selection_transform_t::shift_later>},
};
} // namespace
static_assert(is_valid_route_module(marker_route_entries));
constinit const std::span<const compose_action_route_t> compose_marker_routes{marker_route_entries};
} // namespace wormedit::sys::compose_action_detail
