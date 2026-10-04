#include "sys/compose_area_actions.h"
#include <algorithm>
#include <ranges>
#include <utility>
#include <wormedit/core/snap.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/virtual_canvas.h>

namespace wormedit::sys {
namespace {
struct area_action_context_t {
    const WormUiWorkspaceAction& action;
    wormedit::document_history_t& history;
    wormedit::editor_interaction_state_t& state;
    std::optional<wormedit::notification_sink_t> notifications;
};
auto area_feedback(area_action_context_t& context, std::string message,
                   wormedit::notification_severity_t severity,
                   wormedit::notification_source_t source) -> std::string {
    if (context.notifications && !message.empty()) {
        if (severity == notification_severity_t::hint) context.notifications->post_transient_hint(source, message);
        else context.notifications->post(severity, source, message);
    }
    return message;
}
auto area_rejection(area_action_context_t& context, std::string message) -> std::string {
    return area_feedback(context, std::move(message), wormedit::notification_severity_t::warning,
                         wormedit::notification_source_t::edit);
}
auto area_diagnostic(area_action_context_t& context,
                     const wormedit::diagnostic_t& diagnostic) -> std::string {
    if (context.notifications)
        context.notifications->post_diagnostics(wormedit::notification_source_t::edit, {diagnostic});
    return diagnostic.message;
}
auto area_position_at_row(const wormedit::measure_index_t& index, std::uint64_t row)
    -> std::optional<wormedit::local_position_t> {
    for (const auto& entry : index.entries)
        if (row >= entry.absolute_begin_row && row - entry.absolute_begin_row < entry.row_capacity)
            return wormedit::local_position_t{entry.id, row - entry.absolute_begin_row};
    return std::nullopt;
}
std::string area_result_status(area_action_context_t& context,
                               const wormedit::editor_interaction_result_t& result,
                               const char* success) {
    if (result.accepted)
        return area_feedback(context, success,
            result.document_changed ? wormedit::notification_severity_t::success
                                    : wormedit::notification_severity_t::info,
            wormedit::notification_source_t::edit);
    return result.diagnostics.empty() ? area_rejection(context, "Area action rejected.")
                                      : area_diagnostic(context, result.diagnostics.front());
}
auto snap_area_row(const wormedit::chart_document_t& chart, std::uint64_t row,
                   std::uint64_t denominator) -> std::optional<std::uint64_t> {
    return wormedit::snap_absolute_row(chart, row, denominator);
}

bool area_quantization_ids_overlap(const wormedit::chart_document_t& chart,
                                   const std::vector<wormedit::object_id_t>& selected) {
    const auto overlaps = [&](const auto& values) {
        return std::ranges::any_of(values, [&](const auto& value) {
            return std::ranges::find(selected, value.id) != selected.end();
        });
    };
    for (const auto& measure : chart.measures)
        if (overlaps(measure.bgm_events))
            return true;
    return overlaps(chart.lane_layouts) || overlaps(chart.bpms) || overlaps(chart.pauses) ||
           overlaps(chart.scrolls) || overlaps(chart.speeds) || overlaps(chart.kiai) ||
           overlaps(chart.timing_anchors) || overlaps(chart.bga_events) || overlaps(chart.warps) ||
           overlaps(chart.bookmarks) || overlaps(chart.sections);
}

std::string quantize_compose_area_selection(area_action_context_t& context,
                                            const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    if (area_quantization_ids_overlap(chart, context.state.selected_notes))
        return area_rejection(context, "Selection quantization is unavailable because object identities overlap.");

    if (context.state.selected_notes.empty())
        return area_rejection(context, "Select notes before quantizing.");
    const auto& action = context.action;
    if (action.compose_quantize_space > static_cast<std::uint8_t>(quantization_space_t::authored_measure) ||
        action.compose_quantize_tie > static_cast<std::uint8_t>(quantization_tie_t::reject) ||
        action.compose_quantize_holds > static_cast<std::uint8_t>(quantization_hold_t::both_endpoints))
        return area_rejection(context, "Selection quantization has invalid settings.");
    const auto legacy_request = action.compose_quantize_divisions == 0;
    const auto divisions = legacy_request ? context.state.active_snap_denominator
                                          : action.compose_quantize_divisions;
    const auto request = quantize_request_t{.divisions = divisions,
                                            .space = static_cast<quantization_space_t>(
                                                action.compose_quantize_space),
                                            .tie = static_cast<quantization_tie_t>(
                                                action.compose_quantize_tie),
                                            .holds = legacy_request
                                                         ? quantization_hold_t::both_endpoints
                                                         : static_cast<quantization_hold_t>(
                                                               action.compose_quantize_holds),
                                            .objects = context.state.selected_notes};
    const auto result = apply_quantize_command(context.history, context.state.chart, request);
    if (!result.command.diagnostics.empty() && !result.command.applied)
        return area_diagnostic(context, result.command.diagnostics.front());
    return result.command.applied
        ? area_feedback(context, "Selected notes quantized.", notification_severity_t::success,
                        notification_source_t::edit)
        : area_rejection(context, "Selection quantization rejected.");
}

// A new SV recipe over the retained area: linear 1x to 2x on the generic SV output, so it is
// visible and editable straight away. Its values and sampling are edited afterwards.
std::string add_sv_recipe_over_area(area_action_context_t& context,
                                    const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    if (!context.state.retained_selection_range)
        return area_rejection(context, "Select an area first.");
    sv_recipe_t recipe{.name = "Recipe " + std::to_string(chart.sv_recipes.size() + 1),
                       .range = *context.state.retained_selection_range,
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::linear,
                       .parameters = {.value_from = {1, 1}, .value_to = {2, 1}}};
    const auto inserted = apply_sv_recipe_edit(context.history, context.history.document.revision,
                                               insert_sv_recipe_action_t{chart.id, std::move(recipe)});
    if (inserted.applied)
        return area_feedback(context, "SV recipe added over the selected area.",
                             notification_severity_t::success, notification_source_t::edit);
    return inserted.diagnostics.empty() ? area_rejection(context, "SV recipe rejected.")
                                        : area_diagnostic(context, inserted.diagnostics.front());
}

auto select_compose_area_contents(wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state,
                                  const wormedit::measure_index_t& index)
    -> std::optional<wormedit::editor_interaction_state_t> {
    using namespace wormedit;
    const auto* measures = &index;
    if (!state.retained_selection_range)
        return std::nullopt;
    const auto span = *state.retained_selection_range;
    const auto begin = absolute_row(*measures, span.begin);
    const auto end = absolute_row(*measures, span.end);
    if (!begin || !end || *begin >= *end)
        return std::nullopt;
    auto temporary = state;
    temporary.selected_notes.clear();
    temporary.selected_objects.clear();
    place_cursor_at(temporary, span.begin);
    const space_range_selection_action_t selection{editor_area_selection_mode_t::replace,
                                                   {true, true, true, true}};
    const auto started = dispatch_editor_action(history, temporary, selection);
    if (!started.accepted)
        return std::nullopt;
    const auto last = area_position_at_row(*measures, *end);
    if (!last)
        return std::nullopt;
    place_cursor_at(temporary, *last);
    const auto selected = dispatch_editor_action(history, temporary, selection);
    if (!selected.accepted)
        return std::nullopt;
    return temporary;
}

bool supported_area_contents(const wormedit::editor_interaction_state_t& selected) {
    using kind = wormedit::editor_selection_object_kind_t;
    return std::ranges::all_of(selected.selected_objects, [](const auto& object) {
        return object.kind == kind::note || object.kind == kind::bgm_event;
    });
}

std::string delete_compose_area_contents(area_action_context_t& context,
                                         const wormedit::measure_index_t& index) {
    using namespace wormedit;
    auto selected = select_compose_area_contents(context.history, context.state, index);
    if (!selected)
        return area_rejection(context, "Select an authored area first.");
    if (!supported_area_contents(*selected))
        return area_rejection(context, "Area deletion supports notes and BGM only; the area contains other objects.");
    const auto result =
        dispatch_editor_action(context.history, *selected, delete_editor_selection_action_t{});
    if (result.accepted) {
        context.state.selected_notes = std::move(selected->selected_notes);
        context.state.selected_objects = std::move(selected->selected_objects);
    }
    return area_result_status(context, result, "Selected area contents deleted.");
}

std::string resize_compose_area(area_action_context_t& context,
                                const wormedit::chart_document_t& chart,
                                const wormedit::measure_index_t& measures) {
    using namespace wormedit;
    const auto kind = context.action.compose_action;
    auto begin = context.action.compose_area_begin_row;
    auto end = context.action.compose_area_end_row;
    const auto bounds = context.action.compose_area_virtual
                            ? build_virtual_canvas(chart)
                            : std::variant<virtual_canvas_t, diagnostic_list_t>{virtual_canvas_t{}};
    const auto* virtual_geometry = std::get_if<virtual_canvas_t>(&bounds);
    const auto total = context.action.compose_area_virtual && virtual_geometry
                           ? virtual_geometry->total_rows
                           : measures.total_rows;
    if (begin >= total || end >= total)
        return area_rejection(context, "The area is outside the chart geometry.");
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET) {
        auto& moving = context.action.compose_area_begin_edge ? begin : end;
        const auto snapped = snap_area_row(chart, moving, wormedit::edit_snap_denominator(chart, context.state));
        if (!snapped)
            return area_rejection(context, "The active grid cannot resize this area.");
        moving = *snapped;
        if (begin > end)
            std::swap(begin, end);
    }
    if (begin >= end)
        return area_rejection(context, "Area edges must retain a non-empty interval.");
    if (context.action.compose_area_virtual) {
        const auto canvas = build_virtual_canvas(chart);
        const auto* geometry = std::get_if<virtual_canvas_t>(&canvas);
        if (!geometry || end >= geometry->total_rows)
            return area_rejection(context, "The area is outside the BMS canvas.");
        context.state.retained_selection_range.reset();
        context.state.retained_canvas_selection_range = canvas_row_span_t{begin, end};
    } else {
        const auto first = area_position_at_row(measures, begin);
        const auto last = area_position_at_row(measures, end);
        if (!first || !last)
            return area_rejection(context, "The area is outside the authored chart.");
        context.state.retained_selection_range = position_span_t{*first, *last};
        context.state.retained_canvas_selection_range = canvas_row_span_t{begin, end};
    }
    return area_feedback(context,
        kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE ? "Area resize canceled."
                                                         : "Selected area resized.",
        notification_severity_t::hint, notification_source_t::input);
}

std::optional<std::string> dispatch_compose_area_action(area_action_context_t& context) {
    using namespace wormedit;
    const auto kind = context.action.compose_action;
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR) {
        context.state.retained_selection_range.reset();
        context.state.retained_canvas_selection_range.reset();
        return area_feedback(context, "Selected area cleared.", notification_severity_t::hint,
                             notification_source_t::input);
    }
    if (kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_DELETE && kind != WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)
        return std::nullopt;
    const auto chart = std::ranges::find(context.history.document.charts, context.state.chart,
                                         &chart_document_t::id);
    if (chart == context.history.document.charts.end())
        return area_rejection(context, "The chart is no longer available.");
    const auto built = build_measure_index(*chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures)
        return area_rejection(context, "The selected area has invalid measure geometry.");
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE)
        return quantize_compose_area_selection(context, *chart);
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)
        return add_sv_recipe_over_area(context, *chart);
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_DELETE)
        return delete_compose_area_contents(context, *measures);
    return resize_compose_area(context, *chart, *measures);
}

} // namespace

auto compose_area_delete_supported(const wormedit::chart_document_t& chart,
                                   const wormedit::measure_index_t& index,
                                   const wormedit::editor_interaction_state_t& state) -> bool {
    using namespace wormedit;
    if (!state.retained_selection_range)
        return false;
    const auto begin = absolute_row(index, state.retained_selection_range->begin);
    const auto end = absolute_row(index, state.retained_selection_range->end);
    if (!begin || !end || *begin >= *end)
        return false;
    const auto point_inside = [&](local_position_t position) {
        const auto row = absolute_row(index, position);
        return row && *row >= *begin && *row < *end;
    };
    const auto positioned = [&](const auto& values) {
        return std::ranges::any_of(values,
                                   [&](const auto& value) { return point_inside(value.position); });
    };
    const auto span_inside = [&](position_span_t span) {
        const auto first = absolute_row(index, span.begin);
        const auto last = absolute_row(index, span.end);
        return first && last && *first < *end && *last >= *begin;
    };
    const auto warp = std::ranges::any_of(
        chart.warps, [&](const auto& value) { return span_inside(value.span); });
    const auto bookmark = std::ranges::any_of(chart.bookmarks, [&](const auto& value) {
        return value.position && point_inside(*value.position);
    });
    const auto section = std::ranges::any_of(chart.sections, [&](const auto& value) {
        return value.beat_span && span_inside(*value.beat_span);
    });
    return !(positioned(chart.bpms) || positioned(chart.pauses) || positioned(chart.scrolls) ||
             positioned(chart.speeds) || positioned(chart.kiai) ||
             positioned(chart.timing_anchors) || positioned(chart.bga_events) || warp || bookmark ||
             section);
}

auto compose_area_delete_supported(wormedit::document_history_t& history,
                                   const wormedit::editor_interaction_state_t& state) -> bool {
    const auto chart =
        std::ranges::find(history.document.charts, state.chart, &wormedit::chart_document_t::id);
    if (chart == history.document.charts.end())
        return false;
    const auto built = wormedit::build_measure_index(*chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&built);
    if (!index)
        return false;
    const auto selected = select_compose_area_contents(history, state, *index);
    return selected && supported_area_contents(*selected);
}

auto dispatch_compose_area_action(const WormUiWorkspaceAction& action,
                                  wormedit::document_history_t& history,
                                  wormedit::editor_interaction_state_t& state,
                                  std::optional<wormedit::notification_sink_t> notifications)
    -> std::optional<std::string> {
    area_action_context_t context{action, history, state, notifications};
    const auto kind = action.compose_action;
    if (!action.compose_area_clear_requested && kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_DELETE && kind != WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)
        return std::nullopt;
    if (state.chart.value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision)
        return area_rejection(context, "Area action ignored because the chart or document revision changed.");
    if (action.compose_area_clear_requested) {
        state.retained_selection_range.reset();
        state.retained_canvas_selection_range.reset();
    }
    return dispatch_compose_area_action(context);
}
} // namespace wormedit::sys
