#include "sys/compose_actions_internal.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context) {
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE)
        return finish_compose_action(context, move_bar_line(context));
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER)
        return finish_compose_action(context, set_measures_meter(context));
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER)
        return finish_compose_action(context, set_cursor_measure_meter(context));
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM)
        return finish_compose_action(context, propose_recipe_form_action(context));
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM)
        return finish_compose_action(context, apply_recipe_form_action(context));
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW) {
        context.environment.editor.commands.project.timing_recipe.recipe_preview.reset();
        return compose_action_hint(context, "SV recipe preview cleared.");
    }
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS)
        return finish_compose_action(
            context, edit_cursor_measure(context, cursor_measure_command_t::remove_with_contents));
    if (const auto transform = selection_transform_for(context.action.compose_action))
        return finish_compose_action(context, transform_selected_notes(context, *transform));
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL:
        return finish_compose_action(context, search_selected_note_labels(context));
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL:
        return finish_compose_action(context, edit_selected_note_labels(context));
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return dispatch_compose_beat_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return dispatch_compose_beat_section_action(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return dispatch_compose_audio_bookmark_action(context);
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER:
    case WORM_UI_COMPOSE_ACTION_NAVIGATE_NEXT_AUDIO_MARKER:
        return navigate_audio_marker(context);
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return dispatch_compose_audio_section_action(context);
    default:
        return std::nullopt;
    }
}
} // namespace wormedit::sys::compose_action_detail
