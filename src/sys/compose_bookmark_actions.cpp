#include <wormedit/core/timing.h>
#include "sys/compose_actions_internal.h"
#include "sys/native_chart_edit_action.h"
#include "sys/projection_notification_feedback.h"
namespace wormedit::sys::compose_action_detail {
beat_section_edit_t make_beat_section_insertion(compose_action_context_t& context,
                                                const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    const auto& indexed = context.environment.document.history.indices(chart.id)->measures;
    const auto* measures = indexed ? &*indexed : nullptr;
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
                wormedit::insert_edit_t<wormedit::section_edit_value_t>{
                    .chart = context.state.chart,
                    .value = {.label = context.action.compose_marker_label,
                              .anchor = marker_anchor_t::beat,
                              .beat_span = span,
                              .audio_millisecond_span = std::nullopt}},
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
        return marker_edit_action_t{wormedit::delete_edit_t<wormedit::section_edit_value_t>{
            .chart = context.state.chart, .id = section->id}};
    auto span = *section->beat_span;
    auto label = section->label;
    if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL)
        label = context.action.compose_marker_label;
    else if (context.action.compose_action == WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN)
        span.begin = context.state.cursor;
    else
        span.end = context.state.cursor;
    return marker_edit_action_t{wormedit::update_edit_t<wormedit::section_edit_value_t>{
        .chart = context.state.chart,
        .id = section->id,
        .value = {.label = std::move(label),
                  .anchor = marker_anchor_t::beat,
                  .beat_span = span,
                  .audio_millisecond_span = std::nullopt}}};
}

std::string_view beat_section_success(const WormUiWorkspaceAction& action, bool from_selection) {
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

auto edit_beat_section_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    adding = context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION;
    if (context.state.virtual_bms_cursor_row && adding)
        return compose_action_outcome_t::blocked(
            "Beat sections require a materialized cursor measure.");
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing sections."))
        return std::move(*blocked);
    chart = selected_chart(context.environment.document.history,
                           context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("Choose a chart before editing sections.");
    return std::nullopt;
}

auto edit_beat_section_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    marker_edit_action_t edit;
    bool from_selection{};
    if (adding) {
        auto made = make_beat_section_insertion(context, *chart);
        if (!made.error.empty())
            return compose_action_outcome_t::blocked(made.error);
        edit = std::move(*made.edit);
        from_selection = made.from_selection;
    } else {
        auto made = make_existing_beat_section_edit(context, *chart);
        if (const auto* error = std::get_if<std::string>(&made))
            return compose_action_outcome_t::blocked(*error);
        edit = std::get<marker_edit_action_t>(std::move(made));
    }

    return run_native_chart_edit<marker_edit_action_t>(
        context.environment.document.history, std::move(edit),
        compose_action_description_t{
            .success = beat_section_success(context.action, from_selection),
            .unchanged = "Section edit made no changes.",
            .rebind_failure = "Section changed, but workspace rebinding failed."});
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
        return marker_edit_action_t{wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>{
            .chart = context.state.chart,
            .value = {.label = context.action.compose_marker_label,
                      .anchor = marker_anchor_t::audio_time,
                      .position = std::nullopt,
                      .audio_milliseconds = milliseconds}}};
    if (!context.action.compose_bookmark_id)
        return "Choose an audio-time bookmark before editing it.";
    const auto found = std::ranges::find(
        chart.bookmarks, object_id_t{context.action.compose_bookmark_id}, &bookmark_t::id);
    if (found == chart.bookmarks.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_milliseconds || found->position)
        return "That audio-time bookmark is no longer available.";
    if (deleting)
        return marker_edit_action_t{wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>{
            .chart = context.state.chart, .id = found->id}};
    return marker_edit_action_t{wormedit::update_edit_t<wormedit::bookmark_edit_value_t>{
        .chart = context.state.chart,
        .id = found->id,
        .value = {.label = context.action.compose_marker_label,
                  .anchor = marker_anchor_t::audio_time,
                  .position = std::nullopt,
                  .audio_milliseconds = milliseconds}}};
}

auto edit_audio_bookmark_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing bookmarks."))
        return std::move(*blocked);
    chart = selected_chart(context.environment.document.history,
                           context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked(
            "Choose a chart before editing audio-time bookmarks.", notification_source_t::edit);
    return std::nullopt;
}

auto edit_audio_bookmark_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto built = make_audio_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_outcome_t::blocked(*error, notification_source_t::edit);

    const auto success =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK
            ? "Audio-time bookmark added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK
            ? "Audio-time bookmark deleted."
            : "Audio-time bookmark updated.";
    return run_native_chart_edit<marker_edit_action_t>(
        context.environment.document.history, std::get<marker_edit_action_t>(std::move(built)),
        compose_action_description_t{.success = success,
                                     .unchanged = "Audio bookmark edit made no changes.",
                                     .rebind_failure =
                                         "Audio bookmark changed, but workspace rebinding failed.",
                                     .source = notification_source_t::edit});
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
        return marker_edit_action_t{wormedit::insert_edit_t<wormedit::section_edit_value_t>{
            .chart = context.state.chart,
            .value = {.label = context.action.compose_marker_label,
                      .anchor = marker_anchor_t::audio_time,
                      .beat_span = std::nullopt,
                      .audio_millisecond_span = std::pair{*begin, *end}}}};
    if (!context.action.compose_section_id)
        return "Choose an audio-time section before editing it.";
    const auto found = std::ranges::find(
        chart.sections, object_id_t{context.action.compose_section_id}, &section_t::id);
    if (found == chart.sections.end() || found->anchor != marker_anchor_t::audio_time ||
        !found->audio_millisecond_span || found->beat_span)
        return "That audio-time section is no longer available.";
    if (deleting)
        return marker_edit_action_t{wormedit::delete_edit_t<wormedit::section_edit_value_t>{
            .chart = context.state.chart, .id = found->id}};
    return marker_edit_action_t{wormedit::update_edit_t<wormedit::section_edit_value_t>{
        .chart = context.state.chart,
        .id = found->id,
        .value = {.label = context.action.compose_marker_label,
                  .anchor = marker_anchor_t::audio_time,
                  .beat_span = std::nullopt,
                  .audio_millisecond_span = std::pair{*begin, *end}}}};
}

auto edit_audio_section_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing sections."))
        return std::move(*blocked);
    chart = selected_chart(context.environment.document.history,
                           context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked(
            "Choose a chart before editing audio-time sections.", notification_source_t::edit);
    return std::nullopt;
}

auto edit_audio_section_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto built = make_audio_section_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_outcome_t::blocked(*error, notification_source_t::edit);

    const auto success =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION
            ? "Audio-time section added."
        : context.action.compose_action == WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION
            ? "Audio-time section deleted."
            : "Audio-time section updated.";
    return run_native_chart_edit<marker_edit_action_t>(
        context.environment.document.history, std::get<marker_edit_action_t>(std::move(built)),
        compose_action_description_t{.success = success,
                                     .unchanged = "Audio section edit made no changes.",
                                     .rebind_failure =
                                         "Audio section changed, but workspace rebinding failed.",
                                     .source = notification_source_t::edit});
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
        return marker_edit_action_t{wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>{
            .chart = context.state.chart, .id = found->id}};
    const bool rename = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK;
    return marker_edit_action_t{wormedit::update_edit_t<wormedit::bookmark_edit_value_t>{
        .chart = context.state.chart,
        .id = found->id,
        .value = {.label = rename ? context.action.compose_marker_label : found->label,
                  .anchor = marker_anchor_t::beat,
                  .position = rename ? found->position : std::optional{context.state.cursor},
                  .audio_milliseconds = std::nullopt}}};
}

auto edit_beat_bookmark_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    operation = context.action.compose_action;
    if (context.state.virtual_bms_cursor_row &&
        operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION)
        return compose_action_outcome_t::blocked(
            "Bookmarks can only go inside existing measures. Add measures up to the cursor first.");
    if (auto blocked = context.require_no_gesture(
            "Finish or cancel the active Compose gesture before editing bookmarks."))
        return std::move(*blocked);
    chart = selected_chart(context.environment.document.history,
                           context.environment.document.workspace);
    if (!chart)
        return compose_action_outcome_t::blocked("Choose a chart before editing bookmarks.");
    return std::nullopt;
}

auto edit_beat_bookmark_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto built = make_beat_bookmark_edit(context, *chart);
    if (const auto* error = std::get_if<std::string>(&built))
        return compose_action_outcome_t::blocked(*error);

    const auto success = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                             ? "Bookmark renamed."
                         : operation == WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION
                             ? "Bookmark moved to the Compose cursor."
                             : "Bookmark deleted.";
    const auto unchanged = operation == WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK
                               ? "Bookmark label was unchanged."
                               : "Bookmark edit made no changes.";
    return run_native_chart_edit<marker_edit_action_t>(
        context.environment.document.history, std::get<marker_edit_action_t>(std::move(built)),
        compose_action_description_t{.success = success,
                                     .unchanged = unchanged,
                                     .rebind_failure =
                                         "Bookmark changed, but workspace rebinding failed."});
}

auto navigate_audio_marker_action_t::validate(compose_action_context_t& context)
    -> std::optional<compose_action_outcome_t> {
    auto& app = context.environment.editor.commands;
    if (!app.services.transport_audio_milliseconds ||
        !app.services.transport_seek_audio_milliseconds)
        return compose_action_outcome_t::blocked(
            "Start chart preview once before navigating audio-time markers.",
            notification_source_t::audio);
    cursor = app.services.transport_audio_milliseconds();
    if (!cursor)
        return compose_action_outcome_t::blocked(
            "Start chart preview once to compile its seekable timeline.",
            notification_source_t::audio);
    return std::nullopt;
}

auto navigate_audio_marker_action_t::perform(compose_action_context_t& context) const
    -> compose_action_outcome_t {
    using namespace wormedit;
    auto& app = context.environment.editor.commands;
    const auto direction =
        context.action.compose_action == WORM_UI_COMPOSE_ACTION_NAVIGATE_PREVIOUS_AUDIO_MARKER
            ? marker_navigation_direction_t::previous
            : marker_navigation_direction_t::next;
    const auto marker =
        marker_navigation_match_t::from(context.environment.document.history.document,
                                        context.environment.document.history.document.revision,
                                        context.state.chart, *cursor, direction);
    if (marker.value.status == marker_navigation_status_t::no_result)
        return compose_action_outcome_t::from(
            direction == marker_navigation_direction_t::previous
                ? "No previous audio-time bookmark or section boundary."
                : "No next audio-time bookmark or section boundary.",
            notification_severity_t::hint, notification_source_t::audio);
    if (!marker.value.is_found() || !marker.diagnostics.empty() ||
        !marker.value.item->audio_milliseconds)
        return marker.diagnostics.empty()
                   ? compose_action_outcome_t::blocked(
                         "Audio-time marker navigation could not resolve an exact millisecond.",
                         notification_source_t::audio)
                   : compose_action_outcome_t::from(marker.diagnostics.front(),
                                                    notification_source_t::audio);
    return app.services.transport_seek_audio_milliseconds(*marker.value.item->audio_milliseconds);
}

namespace {
constexpr auto bookmark_route_entries = std::array{
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ADD_BEAT_SECTION,
                           .operation = run_routed_compose_action<edit_beat_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL,
                           .operation = run_routed_compose_action<edit_beat_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_BEGIN,
                           .operation = run_routed_compose_action<edit_beat_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BEAT_SECTION_END,
                           .operation = run_routed_compose_action<edit_beat_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_BEAT_SECTION,
                           .operation = run_routed_compose_action<edit_beat_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ADD_AUDIO_BOOKMARK,
                           .operation = run_routed_compose_action<edit_audio_bookmark_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_BOOKMARK,
                           .operation = run_routed_compose_action<edit_audio_bookmark_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_BOOKMARK,
                           .operation = run_routed_compose_action<edit_audio_bookmark_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_ADD_AUDIO_SECTION,
                           .operation = run_routed_compose_action<edit_audio_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_UPDATE_AUDIO_SECTION,
                           .operation = run_routed_compose_action<edit_audio_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_AUDIO_SECTION,
                           .operation = run_routed_compose_action<edit_audio_section_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_BOOKMARK,
                           .operation = run_routed_compose_action<edit_beat_bookmark_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_SET_BEAT_BOOKMARK_POSITION,
                           .operation = run_routed_compose_action<edit_beat_bookmark_action_t>},
    compose_action_route_t{.kind = WORM_UI_COMPOSE_ACTION_DELETE_BEAT_BOOKMARK,
                           .operation = run_routed_compose_action<edit_beat_bookmark_action_t>},
};
} // namespace
constinit const std::span<const compose_action_route_t> compose_bookmark_routes{
    bookmark_route_entries};
} // namespace wormedit::sys::compose_action_detail
