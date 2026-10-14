#include "sys/compose_area_actions.h"
#include "sys/compose_action_context.h"
#include "sys/compose_quantize_request.h"
#include <algorithm>
#include <numeric>
#include <ranges>
#include <utility>
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/command.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/virtual_canvas.h>

namespace wormedit::sys {
using namespace compose_action_detail;
auto can_apply_compose_quantize(const application_command_state_t& application,
                                const editor_interaction_state_t& state) -> bool {
    return !application.project.compose_view.compose_timing_focus &&
           !application.project.clipboard.paste_session &&
           !wormedit::has_pending_editor_gesture(state);
}

auto apply_compose_quantize_selection(document_history_t& history,
                                      editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      const WormUiWorkspaceAction& action)
    -> compose_action_outcome_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return compose_action_outcome_t::blocked("The chart is no longer available.");
    const auto geometry = build_measure_index(*chart);
    if (!std::holds_alternative<measure_index_t>(geometry))
        return compose_action_outcome_t::blocked("The selected area has invalid measure geometry.");
    auto request = quantize_request_from_action(action, {});
    if (!request)
        return compose_action_outcome_t::blocked("Selection quantization has invalid settings.");
    request->selection = editor_quantize_selection(state);
    auto report = preview_quantize_selection(*chart, request->selection->notes, *request);
    auto result = apply_quantize_command(history, state.chart, *request);
    if (!result.command.applied)
        return result.command.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Selection quantization rejected.")
                   : compose_action_outcome_t::from(std::move(result.command.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::first_diagnostic);
    application.project.quantize.quantize_applied_report =
        application_command_state_t::quantize_applied_report_t{
            .song = history.document.id,
            .chart = state.chart,
            .rows = std::move(report.report_rows),
            .total_rows = report.report_total_rows,
            .truncated = report.report_truncated};
    return compose_action_outcome_t::from(std::move(result.command), compose_action_description_t{.success = "Selected objects quantized.", .unchanged = {}, .rebind_failure = "Selected objects quantized, but workspace rebinding failed.", .source = notification_source_t::edit, .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
}

namespace {
auto area_position_at_row(const wormedit::measure_index_t& index, std::uint64_t row)
    -> std::optional<wormedit::local_position_t> {
    for (const auto& entry : index.entries)
        if (row >= entry.absolute_begin_row && row - entry.absolute_begin_row < entry.row_capacity)
            return wormedit::local_position_t{entry.id, row - entry.absolute_begin_row};
    return std::nullopt;
}

auto snap_area_row(const wormedit::chart_document_t& chart, std::uint64_t row,
                   std::uint64_t denominator) -> std::optional<std::uint64_t> {
    return wormedit::snap_absolute_row(chart, row, denominator);
}

// A new SV recipe over the retained area: linear 1x to 2x on the generic SV output, so it is
// visible and editable straight away. Its values and sampling are edited afterwards.
compose_action_outcome_t add_sv_recipe_over_area(compose_action_context_t& context,
                                                 const wormedit::chart_document_t& chart) {
    using namespace wormedit;
    if (!context.state.retained_selection_range)
        return compose_action_outcome_t::blocked("Select an area first.");
    sv_recipe_t recipe{.name = "Recipe " + std::to_string(chart.sv_recipes.size() + 1),
                       .range = *context.state.retained_selection_range,
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::linear,
                       .parameters = {.value_from = {1, 1}, .value_to = {2, 1}}};
    auto inserted = apply_sv_recipe_edit(context.environment.document.history,
                                         wormedit::insert_edit_t<wormedit::sv_recipe_t>{
                                             .chart = chart.id, .value = std::move(recipe)});
    if (inserted.applied)
        return compose_action_outcome_t::from(std::move(inserted), compose_action_description_t{.success = "SV recipe added over the selected area.", .unchanged = {}, .rebind_failure = "Area action applied, but workspace rebinding failed.", .source = notification_source_t::edit, .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
    return inserted.diagnostics.empty()
               ? compose_action_outcome_t::blocked("SV recipe rejected.")
               : compose_action_outcome_t::from(std::move(inserted.diagnostics),
                                                notification_source_t::edit,
                                                compose_feedback_delivery_t::first_diagnostic);
}

// A new SPEEDS event at the area start. Its transition spans the area, measured in beats (four
// per standard measure, exact), and its multiplier starts neutral so it can be edited afterwards.
compose_action_outcome_t add_speed_span_over_area(compose_action_context_t& context,
                                                  const wormedit::chart_document_t& chart,
                                                  const wormedit::measure_index_t& measures) {
    using namespace wormedit;
    if (!context.state.retained_selection_range)
        return compose_action_outcome_t::blocked("Select an area first.");
    const auto span = *context.state.retained_selection_range;
    const auto begin = absolute_row(measures, span.begin);
    const auto end = absolute_row(measures, span.end);
    if (!begin || !end || *begin >= *end || chart.divisions_per_standard_measure == 0)
        return compose_action_outcome_t::blocked("The area must cover at least one row.");
    const auto numerator = (*end - *begin) * 4;
    const auto divisor = std::gcd(numerator, chart.divisions_per_standard_measure);
    const rational_t beats{numerator / divisor, chart.divisions_per_standard_measure / divisor};
    auto inserted =
        apply_scroll_speed_edit(context.environment.document.history,
                                wormedit::insert_edit_t<wormedit::speed_edit_value_t>{
                                    .chart = chart.id,
                                    .value = {.position = span.begin,
                                              .settings = {.multiplier = {1, 1},
                                                           .transition_beats = beats,
                                                           .transition_is_seconds = false}}});
    if (inserted.applied)
        return compose_action_outcome_t::from(std::move(inserted), compose_action_description_t{.success = "SPEEDS span added over the selected area.", .unchanged = {}, .rebind_failure = "Area action applied, but workspace rebinding failed.", .source = notification_source_t::edit, .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
    return inserted.diagnostics.empty()
               ? compose_action_outcome_t::blocked("SPEEDS span rejected.")
               : compose_action_outcome_t::from(std::move(inserted.diagnostics),
                                                notification_source_t::edit,
                                                compose_feedback_delivery_t::first_diagnostic);
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

compose_action_outcome_t delete_compose_area_contents(compose_action_context_t& context,
                                                      const wormedit::measure_index_t& index) {
    using namespace wormedit;
    auto selected =
        select_compose_area_contents(context.environment.document.history, context.state, index);
    if (!selected)
        return compose_action_outcome_t::blocked("Select an authored area first.");
    if (!supported_area_contents(*selected))
        return compose_action_outcome_t::blocked(
            "Area deletion supports notes and BGM only; the area contains other objects.");
    auto result = delete_selection_all(
        context.environment.document.history, *selected,
        {.timing_locked = context.environment.editor.commands.project.timing_recipe.timing_locked});
    if (result.accepted) {
        context.state.selected_notes = std::move(selected->selected_notes);
        context.state.selected_objects = std::move(selected->selected_objects);
    }
    if (!result.accepted)
        return result.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Area action rejected.")
                   : compose_action_outcome_t::from(std::move(result.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::first_diagnostic);
    auto outcome = compose_action_outcome_t::from(
        "Selected area contents deleted.",
        result.document_changed ? notification_severity_t::success : notification_severity_t::info,
        notification_source_t::edit, result.document_changed);
    outcome.rebind_failure = "Selected area contents deleted, but workspace rebinding failed.";
    return outcome;
}

compose_action_outcome_t resize_compose_area(compose_action_context_t& context,
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
        return compose_action_outcome_t::blocked("The area is outside the chart geometry.");
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET) {
        auto& moving = context.action.compose_area_begin_edge ? begin : end;
        const auto snapped = snap_area_row(chart, moving, wormedit::edit_snap_denominator(chart, context.state));
        if (!snapped)
            return compose_action_outcome_t::blocked("The active grid cannot resize this area.");
        moving = *snapped;
        if (begin > end)
            std::swap(begin, end);
    }
    if (begin >= end)
        return compose_action_outcome_t::blocked("Area edges must retain a non-empty interval.");
    if (context.action.compose_area_virtual) {
        const auto canvas = build_virtual_canvas(chart);
        const auto* geometry = std::get_if<virtual_canvas_t>(&canvas);
        if (!geometry || end >= geometry->total_rows)
            return compose_action_outcome_t::blocked("The area is outside the BMS canvas.");
        context.state.retained_selection_range.reset();
        context.state.retained_canvas_selection_range = canvas_row_span_t{begin, end};
    } else {
        const auto first = area_position_at_row(measures, begin);
        const auto last = area_position_at_row(measures, end);
        if (!first || !last)
            return compose_action_outcome_t::blocked("The area is outside the authored chart.");
        context.state.retained_selection_range = position_span_t{*first, *last};
        context.state.retained_canvas_selection_range = canvas_row_span_t{begin, end};
    }
    return compose_action_outcome_t::from(
        kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE ? "Area resize canceled."
                                                          : "Selected area resized.",
        notification_severity_t::hint, notification_source_t::input);
}

compose_action_outcome_t convert_compose_area_to_pause(compose_action_context_t& context) {
    using namespace wormedit;
    if (context.environment.editor.commands.project.timing_recipe.timing_locked)
        return compose_action_outcome_t::blocked("Timing is locked.");
    if (auto blocked =
            context.require_no_gesture("Finish the current edit before converting the area."))
        return std::move(*blocked);
    if (!context.state.retained_selection_range)
        return compose_action_outcome_t::blocked("Select a nonempty authored row range first.");
    const auto kind = context.action.compose_action;
    const bool delay = kind == WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY ||
                       kind == WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE;
    const bool resize = kind == WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE ||
                        kind == WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_DELAY_RESIZE;
    auto result =
        apply_measure_edit(context.environment.document.history,
                           convert_range_to_pause_action_t{
                               context.state.chart, *context.state.retained_selection_range,
                               delay ? pause_kind_t::delay : pause_kind_t::stop, resize});
    if (!result.applied)
        return result.diagnostics.empty()
                   ? compose_action_outcome_t::blocked("Area conversion rejected.")
                   : compose_action_outcome_t::from(std::move(result.diagnostics),
                                                    notification_source_t::edit,
                                                    compose_feedback_delivery_t::first_diagnostic);
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    place_cursor_at(context.state, chart->pauses.back().position);
    context.state.retained_selection_range.reset();
    context.state.retained_canvas_selection_range.reset();
    context.state.selected_canvas_measure.reset();
    return compose_action_outcome_t::from(std::move(result), compose_action_description_t{.success = delay ? "Selected rows converted to Delay." : "Selected rows converted to Stop.", .unchanged = {}, .rebind_failure = "Area conversion applied, but workspace rebinding failed.", .source = notification_source_t::edit, .diagnostic_policy = compose_diagnostic_feedback_policy_t::rejected_only});
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

namespace compose_action_detail {
std::optional<compose_action_outcome_t>
dispatch_compose_area_action(compose_action_context_t& context) {
    using namespace wormedit;
    const auto kind = context.action.compose_action;
    if (compose_area_pause_conversion(kind) &&
        context.environment.document.history.document.id.value !=
            context.action.compose_input_project_id)
        return compose_action_outcome_t::blocked(wormedit::stale_action_message("Area conversion"));
    if (context.action.compose_area_clear_requested) {
        context.state.retained_selection_range.reset();
        context.state.retained_canvas_selection_range.reset();
    }
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_RANGE_CLEAR) {
        context.state.retained_selection_range.reset();
        context.state.retained_canvas_selection_range.reset();
        return compose_action_outcome_t::from(
            "Selected area cleared.", notification_severity_t::hint, notification_source_t::input);
    }
    if (kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_SET &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_RANGE_RESTORE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_DELETE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE &&
        kind != WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN && !compose_area_pause_conversion(kind))
        return std::nullopt;
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE)
        return apply_compose_quantize_selection(context.environment.document.history, context.state,
                                                context.environment.editor.commands,
                                                context.action);
    const auto chart = std::ranges::find(context.environment.document.history.document.charts,
                                         context.state.chart, &chart_document_t::id);
    if (chart == context.environment.document.history.document.charts.end())
        return compose_action_outcome_t::blocked("The chart is no longer available.");
    const auto built = build_measure_index(*chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures)
        return compose_action_outcome_t::blocked("The selected area has invalid measure geometry.");
    if (compose_area_pause_conversion(kind))
        return convert_compose_area_to_pause(context);
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE)
        return add_sv_recipe_over_area(context, *chart);
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_ADD_SPEED_SPAN)
        return add_speed_span_over_area(context, *chart, *measures);
    if (kind == WORM_UI_COMPOSE_ACTION_AREA_DELETE)
        return delete_compose_area_contents(context, *measures);
    return resize_compose_area(context, *chart, *measures);
}
} // namespace compose_action_detail
} // namespace wormedit::sys
