#include "sys/compose_actions_internal.h"
namespace wormedit::sys::compose_action_detail {
auto finish_marker_edit_feedback(compose_action_context_t& context,
                                 const wormedit::document_command_result_t& applied,
                                 std::string success, std::string unchanged,
                                 std::string rebind_failure, notification_source_t source)
    -> std::string {
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front(), source);
    if (!applied.applied)
        return compose_action_info(context, std::move(unchanged), source);
    const auto rebound = wormedit::rebind_workspace(context.environment.workspace_ref,
                                                    context.environment.history_ref);
    if (!rebound)
        return compose_mutation_rebind_failure(context, rebound, std::move(success),
                                               std::move(rebind_failure), source);
    return compose_action_success(context, std::move(success), source);
}

beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures)
        return {.error = "The chart measure geometry cannot define a section span."};
    position_span_t span;
    bool from_selection{};
    if (context.state.retained_selection_range) {
        const auto& selection = *context.state.retained_selection_range;
        const auto begin = absolute_row(*measures, selection.begin);
        const auto end = absolute_row(*measures, selection.end);
        if (!begin || !end || *begin >= *end)
            return {.error =
                        "The retained area/Space interval is no longer valid; select it again."};
        span = selection;
        from_selection = true;
    } else {
        const auto begin = absolute_row(*measures, context.state.cursor);
        const auto denominator = context.state.active_snap_denominator;
        if (!begin || denominator == 0 || chart.divisions_per_standard_measure % denominator != 0)
            return {.error = "The active Compose grid cannot define a section span."};
        const auto step = chart.divisions_per_standard_measure / denominator;
        if (step == 0 || *begin >= measures->total_rows || step >= measures->total_rows - *begin)
            return {.error = "Move the cursor earlier to create a one-snap section."};
        const auto end = timing_position_at_row(*measures, *begin + step);
        if (!end)
            return {.error = "The next active-snap row is outside the chart."};
        span = {context.state.cursor, *end};
    }
    return {.edit =
                insert_section_action_t{context.state.chart, context.action.compose_marker_label,
                                        marker_anchor_t::beat, span, std::nullopt},
            .from_selection = from_selection};
}

auto make_existing_beat_section_edit(compose_action_context_t& context,
                                     const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    if (!context.action.compose_section_id)
        return "Choose a beat-anchored section before editing it.";
    const auto section = std::ranges::find(
        chart.sections, object_id_t{context.action.compose_section_id}, &section_t::id);
    if (section == chart.sections.end() || section->anchor != marker_anchor_t::beat ||
        !section->beat_span)
        return "That beat-anchored section is no longer available.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, section->id}};
    auto span = *section->beat_span;
    auto label = section->label;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL)
        label = context.action.compose_marker_label;
    else if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN)
        span.begin = context.state.cursor;
    else
        span.end = context.state.cursor;
    return marker_edit_action_t{update_section_action_t{context.state.chart, section->id,
                                                        std::move(label), marker_anchor_t::beat,
                                                        span, std::nullopt}};
}

std::string beat_section_success(const WormUiWorkspaceAction& action, bool from_selection) {
    switch (action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
        return from_selection ? "Section added from the retained area/Space interval."
                              : "One-snap section added at the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
        return "Section renamed.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
        return "Section start moved to the Compose cursor.";
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
        return "Section end moved to the Compose cursor.";
    default:
        return "Section deleted.";
    }
}

std::optional<std::string> edit_beat_section(compose_action_context_t& context) {
    using namespace wormedit;
    const bool adding = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION;
    if (context.state.virtual_bms_cursor_row && adding)
        return compose_action_warning(context,
                                      "Beat sections require a materialized cursor measure.");
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing sections.");
    const auto* chart =
        selected_chart(context.environment.history_ref, context.environment.workspace_ref);
    if (!chart)
        return compose_action_warning(context, "Choose a chart before editing sections.");
    marker_edit_action_t edit;
    bool from_selection{};
    if (adding) {
        auto made = make_beat_section_insertion(context, *chart);
        if (!made.error.empty())
            return compose_action_warning(context, made.error);
        edit = std::move(*made.edit);
        from_selection = made.from_selection;
    } else {
        auto made = make_existing_beat_section_edit(context, *chart);
        if (const auto* error = std::get_if<std::string>(&made))
            return compose_action_warning(context, *error);
        edit = std::get<marker_edit_action_t>(std::move(made));
    }
    const auto applied =
        apply_marker_edit(context.environment.history_ref,
                          context.action.compose_input_document_revision, std::move(edit));
    return finish_marker_edit_feedback(
        context, applied, beat_section_success(context.action, from_selection),
        "Section edit made no changes.", "Section changed, but workspace rebinding failed.");
}

std::optional<std::string> dispatch_compose_beat_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION:
        return edit_beat_section(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_bookmark_edit(compose_action_context_t& context,
                              const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK;
    const auto milliseconds = deleting
                                  ? std::optional<std::int64_t>{}
                                  : parse_timing_signed(context.action.compose_marker_audio_begin);
    if (!deleting && !milliseconds)
        return "Enter the audio bookmark position as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK)
        return marker_edit_action_t{
            insert_bookmark_action_t{context.state.chart, context.action.compose_marker_label,
                                     marker_anchor_t::audio_time, std::nullopt, milliseconds}};
    if (!context.action.compose_bookmark_id)
        return "Choose an audio-time bookmark before editing it.";
    const auto found = std::ranges::find(
        chart.bookmarks, object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_milliseconds || found->position)
        return "That audio-time bookmark is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, milliseconds}};
}

std::optional<std::string> edit_audio_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing bookmarks.",
            notification_source_t::edit);
    const auto* chart =
        selected_chart(context.environment.history_ref, context.environment.workspace_ref);
    if (!chart)
        return compose_action_warning(context,
                                      "Choose a chart before editing audio-time bookmarks.",
                                      notification_source_t::edit);
    auto built = make_audio_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_warning(context, *error, notification_source_t::edit);
    const auto applied = apply_marker_edit(context.environment.history_ref,
                                           context.action.compose_input_document_revision,
                                           std::get<marker_edit_action_t>(std::move(built)));
    const auto success =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK
            ? "Audio-time bookmark added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK
            ? "Audio-time bookmark deleted."
            : "Audio-time bookmark updated.";
    return finish_marker_edit_feedback(
        context, applied, success, "Audio bookmark edit made no changes.",
        "Audio bookmark changed, but workspace rebinding failed.", notification_source_t::edit);
}

std::optional<std::string>
dispatch_compose_audio_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK:
        return edit_audio_bookmark(context);
    default:
        return std::nullopt;
    }
}

auto make_audio_section_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto deleting =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION;
    const auto begin = deleting ? std::optional<std::int64_t>{}
                                : parse_timing_signed(context.action.compose_marker_audio_begin);
    const auto end = deleting ? std::optional<std::int64_t>{}
                              : parse_timing_signed(context.action.compose_marker_audio_end);
    if (!deleting && (!begin || !end))
        return "Enter both audio section bounds as exact signed milliseconds.";
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION)
        return marker_edit_action_t{insert_section_action_t{
            context.state.chart, context.action.compose_marker_label, marker_anchor_t::audio_time,
            std::nullopt, std::pair{*begin, *end}}};
    if (!context.action.compose_section_id)
        return "Choose an audio-time section before editing it.";
    const auto found = std::ranges::find(
        chart.sections, object_id_t{context.action.compose_section_id}, &section_t::id);
    if (found == chart.sections.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_millisecond_span || found->beat_span)
        return "That audio-time section is no longer available.";
    if (deleting)
        return marker_edit_action_t{delete_section_action_t{context.state.chart, found->id}};
    return marker_edit_action_t{update_section_action_t{
        context.state.chart, found->id, context.action.compose_marker_label,
        marker_anchor_t::audio_time, std::nullopt, std::pair{*begin, *end}}};
}

std::optional<std::string> edit_audio_section(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing sections.",
            notification_source_t::edit);
    const auto* chart =
        selected_chart(context.environment.history_ref, context.environment.workspace_ref);
    if (!chart)
        return compose_action_warning(context, "Choose a chart before editing audio-time sections.",
                                      notification_source_t::edit);
    auto built = make_audio_section_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_warning(context, *error, notification_source_t::edit);
    const auto applied = apply_marker_edit(context.environment.history_ref,
                                           context.action.compose_input_document_revision,
                                           std::get<marker_edit_action_t>(std::move(built)));
    const auto success =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION
            ? "Audio-time section added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION
            ? "Audio-time section deleted."
            : "Audio-time section updated.";
    return finish_marker_edit_feedback(
        context, applied, success, "Audio section edit made no changes.",
        "Audio section changed, but workspace rebinding failed.", notification_source_t::edit);
}

std::optional<std::string>
dispatch_compose_audio_section_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION:
    case WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION:
        return edit_audio_section(context);
    default:
        return std::nullopt;
    }
}

auto make_beat_bookmark_edit(compose_action_context_t& context,
                             const wormedit::chart_document_t& chart)
    -> std::variant<wormedit::marker_edit_action_t, std::string> {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (!context.action.compose_bookmark_id)
        return "Choose a beat-anchored bookmark before editing it.";
    const auto found = std::ranges::find(
        chart.bookmarks, object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::beat ||
        !found->position)
        return "That beat-anchored bookmark is no longer available.";
    if (operation == WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK)
        return marker_edit_action_t{delete_bookmark_action_t{context.state.chart, found->id}};
    const bool rename = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK;
    return marker_edit_action_t{update_bookmark_action_t{
        context.state.chart, found->id, rename ? context.action.compose_marker_label : found->label,
        marker_anchor_t::beat, rename ? found->position : std::optional{context.state.cursor},
        std::nullopt}};
}

std::optional<std::string> edit_beat_bookmark(compose_action_context_t& context) {
    using namespace wormedit;
    const auto operation = context.action.compose_action;
    if (context.state.virtual_bms_cursor_row &&
        operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION)
        return compose_action_warning(
            context,
            "Bookmarks can only go inside existing measures. Add measures up to the cursor first.");
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing bookmarks.");
    const auto* chart =
        selected_chart(context.environment.history_ref, context.environment.workspace_ref);
    if (!chart)
        return compose_action_warning(context, "Choose a chart before editing bookmarks.");
    auto built = make_beat_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_warning(context, *error);
    const auto applied = apply_marker_edit(context.environment.history_ref,
                                           context.action.compose_input_document_revision,
                                           std::get<marker_edit_action_t>(std::move(built)));
    const auto success = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                             ? "Bookmark renamed."
                         : operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
                             ? "Bookmark moved to the Compose cursor."
                             : "Bookmark deleted.";
    const auto unchanged = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                               ? "Bookmark label was unchanged."
                               : "Bookmark edit made no changes.";
    return finish_marker_edit_feedback(context, applied, success, unchanged,
                                       "Bookmark changed, but workspace rebinding failed.");
}

std::optional<std::string>
dispatch_compose_beat_bookmark_action(compose_action_context_t& context) {
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK:
    case WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION:
    case WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK:
        return edit_beat_bookmark(context);
    default:
        return std::nullopt;
    }
}

std::optional<std::string> edit_selected_note_labels(compose_action_context_t& context) {
    using namespace wormedit;
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing note labels.");
    if (context.state.selected_notes.empty())
        return compose_action_warning(context,
                                      "Select one or more notes before editing their label.");
    const auto label = context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL
                           ? std::optional<std::string>{context.action.compose_note_label}
                           : std::nullopt;
    const auto applied = apply_note_label_edit(
        context.environment.history_ref, context.action.compose_input_document_revision,
        {.chart = context.state.chart, .notes = context.state.selected_notes, .label = label});
    if (!applied.diagnostics.empty())
        return publish_compose_diagnostic(context, applied.diagnostics.front());
    if (!applied.applied)
        return compose_action_info(context,
                                   label ? "Selected notes already have that label."
                                         : "Selected notes already have no label.",
                                   notification_source_t::edit);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound)
        return compose_mutation_rebind_failure(
            context, rebound, label ? "Note label applied." : "Note labels cleared.",
            "Note label changed, but workspace rebinding failed.", notification_source_t::edit);
    return compose_action_success(context, label ? "Note label applied." : "Note labels cleared.");
}

std::string navigate_audio_marker(compose_action_context_t& context) {
    using namespace wormedit;
    auto& app = context.environment.application_command_state_ref;
    if (!app.transport_audio_milliseconds || !app.transport_seek_audio_milliseconds)
        return compose_action_warning(
            context, "Start chart preview once before navigating audio-time markers.",
            notification_source_t::audio);
    const auto cursor = app.transport_audio_milliseconds();
    if (!cursor)
        return compose_action_warning(context,
                                      "Start chart preview once to compile its seekable timeline.",
                                      notification_source_t::audio);
    const auto direction =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER
            ? marker_navigation_direction_t::previous
            : marker_navigation_direction_t::next;
    const auto marker = navigate_markers(context.environment.history_ref.document,
                                         context.environment.history_ref.document.revision,
                                         context.state.chart, *cursor, direction);
    if (marker.status == marker_navigation_status_t::no_result)
        return compose_action_hint(context,
                                   direction == marker_navigation_direction_t::previous
                                       ? "No previous audio-time bookmark or section boundary."
                                       : "No next audio-time bookmark or section boundary.",
                                   notification_source_t::audio);
    if (!marker || !marker.item->audio_milliseconds)
        return marker.diagnostics.empty()
                   ? compose_action_warning(
                         context,
                         "Audio-time marker navigation could not resolve an exact millisecond.",
                         notification_source_t::audio)
                   : publish_compose_diagnostic(context, marker.diagnostics.front(),
                                                notification_source_t::audio);
    const auto status = app.transport_seek_audio_milliseconds(*marker.item->audio_milliseconds);
    return app.transport.transport_seek_posts_feedback
               ? status
               : compose_action_hint(context, status, notification_source_t::audio);
}

std::string search_selected_note_labels(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before searching labels.");
    const auto step =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL
            ? label_search_step_t::next
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL
            ? label_search_step_t::previous
            : label_search_step_t::first;
    auto severity = notification_severity_t::warning;
    auto status = run_label_search(context.environment.history_ref, context.state,
                                   context.action.compose_note_label, step, &severity);
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::input);
}

auto selection_transform_for(WormUiComposeActionKind kind) -> std::optional<selection_transform_t> {
    switch (kind) {
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_MIRROR:
        return selection_transform_t::mirror;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_LEFT:
        return selection_transform_t::rotate_left;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_ROTATE_RIGHT:
        return selection_transform_t::rotate_right;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_EARLIER:
        return selection_transform_t::shift_earlier;
    case WORM_UI_COMPOSE_ACTION_TRANSFORM_SHIFT_LATER:
        return selection_transform_t::shift_later;
    default:
        return std::nullopt;
    }
}

std::string transform_selected_notes(compose_action_context_t& context,
                                     selection_transform_t transform) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before transforming notes.");
    if (context.environment.history_ref.document.revision !=
        context.action.compose_input_document_revision)
        return compose_action_warning(context, "Transform ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    auto status = apply_selection_transform(context.environment.history_ref, context.state,
                                            transform, &severity);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Transform applied, but workspace rebinding failed.";
        severity = notification_severity_t::error;
        return publish_compose_feedback(context, std::move(status), severity,
                                        notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

std::string edit_cursor_measure(compose_action_context_t& context,
                                cursor_measure_command_t command) {
    if (const auto error = compose_measure_edit_rejection(
            context.environment.history_ref, context.state,
            context.environment.application_command_state_ref.clipboard.paste_session.has_value(),
            context.action.compose_input_document_revision))
        return compose_action_warning(context, *error);
    auto severity = notification_severity_t::warning;
    auto status = apply_cursor_measure_command(context.environment.history_ref, context.state,
                                               command, &severity);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Measure edited, but workspace rebinding failed.";
        severity = notification_severity_t::error;
        return publish_compose_feedback(context, std::move(status), severity,
                                        notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

auto recipe_form_target(compose_action_context_t& context) -> std::optional<recipe_form_target_t> {
    if (context.environment.history_ref.document.revision !=
        context.action.compose_input_document_revision)
        return std::nullopt;
    const auto chart = std::ranges::find(context.environment.history_ref.document.charts,
                                         context.state.chart, &chart_document_t::id);
    if (chart == context.environment.history_ref.document.charts.end() ||
        chart->id.value != context.action.compose_input_chart_id)
        return std::nullopt;
    const auto recipe = std::ranges::find(
        chart->sv_recipes, recipe_id_t{context.action.compose_recipe_id}, &sv_recipe_t::id);
    if (recipe == chart->sv_recipes.end())
        return std::nullopt;
    return recipe_form_target_t{&*chart, &*recipe};
}

std::string preview_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target)
        return compose_action_warning(
            context, "SV recipe form ignored because the recipe or the document changed.");
    auto preview = wormedit::sys::preview_recipe_form(
        *target->chart, context.environment.history_ref.document.revision, *target->recipe,
        context.action.compose_recipe_form);
    const auto status =
        preview.error.empty() ? std::string{"SV recipe preview updated."} : preview.error;
    const auto severity = preview.error.empty() ? notification_severity_t::hint : preview.severity;
    const auto source =
        preview.error.empty() ? notification_source_t::input : notification_source_t::edit;
    context.environment.application_command_state_ref.timing_recipe.recipe_preview = std::move(preview);
    return publish_compose_feedback(context, status, severity, source);
}

std::string apply_recipe_form_action(compose_action_context_t& context) {
    const auto target = recipe_form_target(context);
    if (!target)
        return compose_action_warning(
            context, "SV recipe form ignored because the recipe or the document changed.");
    if (context.environment.application_command_state_ref.timing_recipe.timing_locked)
        return compose_action_warning(context, "Timing is locked.");
    bool applied = false;
    auto severity = notification_severity_t::warning;
    auto status = wormedit::sys::apply_recipe_form(
        context.environment.history_ref, target->chart->id, target->recipe->id,
        context.action.compose_recipe_form, applied, &severity);
    if (!applied)
        return publish_compose_feedback(context, std::move(status), severity,
                                        notification_source_t::edit);
    context.environment.application_command_state_ref.timing_recipe.recipe_preview.reset();
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "SV recipe updated, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

std::string set_cursor_measure_meter(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.environment.history_ref.document.revision !=
        context.action.compose_input_document_revision)
        return compose_action_warning(context, "Meter edit ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    auto status = apply_cursor_measure_meter(
        context.environment.history_ref, context.state, context.action.timing_meter_numerator_text,
        context.action.timing_meter_denominator_text, &severity);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Meter edited, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

std::string set_measures_meter(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.environment.history_ref.document.revision !=
        context.action.compose_input_document_revision)
        return compose_action_warning(context, "Meter edit ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    const auto ids = std::span<const std::uint64_t>{context.action.compose_measure_ids,
                                                    context.action.compose_measure_id_count};
    auto status = apply_measures_meter(context.environment.history_ref, context.state, ids,
                                       context.action.timing_meter_numerator_text,
                                       context.action.timing_meter_denominator_text, &severity);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Meter edited, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

std::string move_bar_line(compose_action_context_t& context) {
    if (editor_interaction_has_pending_gesture(context.state))
        return compose_action_warning(
            context, "Finish or cancel the active Compose gesture before editing measures.");
    if (context.environment.history_ref.document.revision !=
            context.action.compose_input_document_revision ||
        context.action.compose_measure_id_count != 1 || !context.action.compose_measure_ids)
        return compose_action_warning(context,
                                      "Bar line move ignored because the document changed.");
    auto severity = notification_severity_t::warning;
    const auto& action = context.action;
    const auto parse = [](const char* text) {
        return static_cast<std::uint32_t>(std::strtoul(text, nullptr, 10));
    };
    auto status = apply_bar_line_move(
        context.environment.history_ref, context.state, action.compose_measure_ids[0],
        {parse(action.timing_meter_numerator_text), parse(action.timing_meter_denominator_text)},
        {action.compose_second_meter_numerator, action.compose_second_meter_denominator},
        &severity);
    const auto rebound =
        rebind_workspace(context.environment.workspace_ref, context.environment.history_ref);
    if (!rebound) {
        static_cast<void>(
            publish_compose_feedback(context, status, severity, notification_source_t::edit));
        status = "Bar line moved, but workspace rebinding failed.";
        return compose_action_error(context, std::move(status), notification_source_t::document);
    }
    return publish_compose_feedback(context, std::move(status), severity,
                                    notification_source_t::edit);
}

std::optional<std::string> dispatch_compose_marker_action(compose_action_context_t& context) {
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE)
        return move_bar_line(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER)
        return set_measures_meter(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_MEASURE_METER)
        return set_cursor_measure_meter(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_PREVIEW_SV_RECIPE_FORM)
        return preview_recipe_form_action(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_APPLY_SV_RECIPE_FORM)
        return apply_recipe_form_action(context);
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_CLEAR_SV_RECIPE_PREVIEW) {
        context.environment.application_command_state_ref.timing_recipe.recipe_preview.reset();
        return compose_action_hint(context, "SV recipe preview cleared.");
    }
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_REMOVE_MEASURE_WITH_CONTENTS)
        return edit_cursor_measure(context, cursor_measure_command_t::remove_with_contents);
    if (const auto transform = selection_transform_for(context.action.compose_action))
        return transform_selected_notes(context, *transform);
    switch (context.action.compose_action) {
    case WORM_UI_COMPOSE_ACTION_SEARCH_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_NEXT_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_SEARCH_PREVIOUS_NOTE_LABEL:
        return search_selected_note_labels(context);
    case WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL:
    case WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL:
        return edit_selected_note_labels(context);
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
