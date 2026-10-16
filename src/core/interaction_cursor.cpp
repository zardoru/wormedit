#include "interaction_gesture_runner.h"
#include "interaction_internal.h"
#include "interaction_geometry.h"
#include "interaction_cursor.h"

#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/snap.h>

#include "wide_math.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <string>

namespace wormedit::interaction_detail {
namespace {

constexpr auto is_remainder_rounding_up(std::uint64_t remainder, std::uint64_t capacity) -> bool {
    return remainder > capacity - remainder;
}

auto cursor_transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to, lane_id_t current,
                     const editor_scratch_presentation_t& sides)
    -> std::optional<lane_id_t> {
    const auto* source = effective_layout(chart, index, from);
    const auto* target = effective_layout(chart, index, to);
    return source && target ? editor_lane_across_presented_layouts(*source, *target, current, sides)
                            : std::nullopt;
}



auto cursor_make_view(const document_history_t& history, const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    return inspect_editor_interaction(history, state);
}

auto cursor_diagnostic_result(const document_history_t& history,
                       const editor_interaction_state_t& state,
                       diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    editor_interaction_result_t result{.diagnostics = std::move(diagnostics)};
    const auto view = cursor_make_view(history, state);
    if (const auto* model = std::get_if<editor_interaction_view_model_t>(&view))
        result.view = *model;
    return result;
}



} // namespace

static auto apply_cursor_navigation_target(cursor_navigation_context_t& context,
                                           std::optional<local_position_t> target,
                                           std::uint64_t canvas_row,
                                           std::string_view description)
    -> editor_interaction_result_t {
    auto& state = context.edit.state;
    if (state.hold() &&
        state.hold()->gesture_source == hold_gesture_source_t::keyboard_lane) {
        if (!target || state.hold()->anchor_canvas_row ||
            std::ranges::any_of(state.additional_keyboard_holds(),
                                [](const auto& hold) { return hold.anchor_canvas_row.has_value(); }))
            return revise_gesture<hold_gesture_t>(context.edit, canvas_row);
        return revise_gesture<hold_gesture_t>(context.edit, *target);
    }
    auto proposed = state;
    place_cursor_on_canvas_row(proposed, target, canvas_row);
    if (!keep_cursor_visible(context.edit.scope.chart, context.edit.scope.measures, proposed))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   std::string(description) + " cannot keep the viewport valid")});
    auto view = cursor_make_view(context.edit.history, proposed);
    if (target && std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = cursor_transition_lane(context.edit.scope.chart, context.edit.scope.measures, state.cursor,
                                                 *target, state.active_lane, state.scratch_on_right);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = cursor_make_view(context.edit.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return cursor_diagnostic_result(context.edit.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true,
            .view = std::get<editor_interaction_view_model_t>(std::move(view))};
}

auto move_edit_cursor(cursor_navigation_context_t& context,
                      const move_edit_cursor_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::invalid_id, "unknown cursor direction")});
    auto canvas_rows = context.edit.scope.measures.total_rows;
    if (has_chart_virtual_tail(context.edit.scope.chart)) {
        const auto canvas = build_virtual_canvas(context.edit.scope.chart);
        const auto* value = std::get_if<virtual_canvas_t>(&canvas);
        if (!value) return cursor_diagnostic_result(context.edit.history, context.edit.state,
                                               std::get<diagnostic_list_t>(canvas));
        canvas_rows = value->total_rows;
    }
    const auto next_row = adjacent_snap_canvas_row(context.edit.scope.chart, context.edit.scope.measures,
        static_cast<long double>(context.absolute) + context.edit.state.cursor_subrow,
        context.edit.state.active_snap_denominator, canvas_rows,
        action.direction == interaction_direction_t::forward);
    if (!next_row)
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor movement would leave chart geometry")});
    const auto target = *next_row < context.edit.scope.measures.total_rows
        ? position_at(context.edit.scope.measures, *next_row) : std::nullopt;
    return apply_cursor_navigation_target(context, target, *next_row, "cursor movement");
}

auto apply_page_distance_floor(std::uint64_t origin, std::uint64_t target,
                                std::uint64_t total_rows,
                                const page_edit_cursor_action_t& action) -> std::uint64_t {
    if (action.minimum_rows == 0) return target;
    if (action.direction == interaction_direction_t::forward)
        return std::max(target, origin + std::min(action.minimum_rows, total_rows - 1 - origin));
    return std::min(target, origin - std::min(action.minimum_rows, origin));
}

auto page_edit_cursor(cursor_navigation_context_t& context,
                      const page_edit_cursor_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::invalid_id, "unknown cursor page direction")});
    if (action.measure_count == 0 || context.edit.scope.measures.entries.empty())
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::invalid_resolution,
                   "cursor paging requires a positive measure count")});
    const auto built_canvas = build_virtual_canvas(context.edit.scope.chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas);
    if (!canvas) return cursor_diagnostic_result(context.edit.history, context.edit.state,
                                            std::get<diagnostic_list_t>(built_canvas));
    const auto source = canvas->measure_containing(context.absolute);
    if (!source || source->row_capacity == 0)
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor page source lies outside measure geometry")});
    const auto last_index = canvas->measure_count - 1;
    const auto available = action.direction == interaction_direction_t::backward
        ? source->number : last_index - source->number;
    const auto offset = static_cast<std::size_t>(
        std::min<std::uint64_t>(action.measure_count, available));
    const auto target_index = action.direction == interaction_direction_t::backward
        ? source->number - offset : source->number + offset;
    const auto target_measure = canvas->measure_at(target_index);
    if (!target_measure)
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::row_out_of_range, "cursor page target is unavailable")});
    const auto scaled = wide_math::multiply(context.absolute - source->begin_row,
                                            target_measure->row_capacity);
    const auto division = wide_math::divide(scaled, {0, source->row_capacity});
    const auto rounded = wide_math::add(
        division.quotient,
        is_remainder_rounding_up(division.remainder.low, source->row_capacity) ? 1U : 0U);
    if (!wide_math::can_fit_u64(rounded) ||
        (action.minimum_rows == 0 && rounded.low >= target_measure->row_capacity))
        return cursor_diagnostic_result(context.edit.history, context.edit.state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor page target cannot represent its fractional row")});
    auto row = target_measure->begin_row + std::min(rounded.low, target_measure->row_capacity - 1);
    row = apply_page_distance_floor(context.absolute, row, canvas->total_rows, action);
    const auto target = row < context.edit.scope.measures.total_rows ? position_at(context.edit.scope.measures, row) : std::nullopt;
    return apply_cursor_navigation_target(context, target, row, "cursor paging");
}

auto keep_cursor_visible(const chart_document_t& chart, const measure_index_t& index,
                         editor_interaction_state_t& candidate) -> bool {
    const auto row = candidate.virtual_bms_cursor_row
        ? candidate.virtual_bms_cursor_row : index.row_of(candidate.cursor);
    const auto maximum = maximum_viewport_scroll(chart, index,
        candidate.pixels_per_standard_measure, candidate.viewport_height_pixels);
    if (!row || !maximum) return false;
    const auto pixels_per_row = candidate.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    const auto cursor_top = static_cast<double>(*row) * pixels_per_row;
    const auto cursor_bottom = cursor_top + pixels_per_row;
    if (candidate.fixed_cursor_screen_y_pixels) {
        const auto anchor = candidate.scroll_direction == editor_field_scroll_direction_t::down
            ? candidate.viewport_height_pixels - *candidate.fixed_cursor_screen_y_pixels
            : *candidate.fixed_cursor_screen_y_pixels;
        set_viewport_scroll_pixels(candidate, std::clamp(cursor_top - anchor,
            minimum_viewport_scroll(candidate.viewport_height_pixels), *maximum));
        return std::isfinite(candidate.viewport_scroll_pixels);
    }
    constexpr double top_fraction = 0.2;
    constexpr double bottom_fraction = 0.8;
    const auto comfort_top = candidate.viewport_scroll_pixels +
                             candidate.viewport_height_pixels * top_fraction;
    const auto comfort_bottom = candidate.viewport_scroll_pixels +
                                candidate.viewport_height_pixels * bottom_fraction;
    if (cursor_top < comfort_top)
        set_viewport_scroll_pixels(candidate, cursor_top -
            candidate.viewport_height_pixels * top_fraction);
    else if (cursor_bottom > comfort_bottom)
        set_viewport_scroll_pixels(candidate, cursor_bottom -
            candidate.viewport_height_pixels * bottom_fraction);
    set_viewport_scroll_pixels(candidate, std::clamp(candidate.viewport_scroll_pixels,
        minimum_viewport_scroll(candidate.viewport_height_pixels), *maximum));
    return std::isfinite(candidate.viewport_scroll_pixels);
}

auto jump_edit_cursor(cursor_navigation_context_t& context,
                      const jump_edit_cursor_action_t& action) -> editor_interaction_result_t {
    auto& state = context.edit.state;
    if (!document_stamp_t{.project = context.edit.history.document.id,
                          .revision = action.expected_document_revision}
             .is_current(context.edit.history.document))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::invalid_id,
                   stale_action_message("Cursor jump"))});
    if (!context.edit.scope.measures.row_of(action.position))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor jump target lies outside chart geometry")});
    auto proposed = state;
    place_cursor_on_picked_row(proposed, action.position);
    if (!keep_cursor_visible(context.edit.scope.chart, context.edit.scope.measures, proposed))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "cursor jump cannot keep the viewport valid")});
    auto view = cursor_make_view(context.edit.history, proposed);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = cursor_transition_lane(context.edit.scope.chart, context.edit.scope.measures, state.cursor,
                                                 action.position, state.active_lane, state.scratch_on_right);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = cursor_make_view(context.edit.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return cursor_diagnostic_result(context.edit.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(view)};
}

auto select_bms_canvas_measure(cursor_navigation_context_t& context,
                               const select_bms_canvas_measure_action_t& action)
    -> editor_interaction_result_t {
    auto& state = context.edit.state;
    const auto& chart = context.edit.scope.chart;
    if (!has_chart_virtual_tail(chart) ||
        action.number >= virtual_tail_measure_limit(chart))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "BMS measure selection is outside #000-#999")});
    const auto built = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    if (!canvas) return cursor_diagnostic_result(context.edit.history, state,
                                            std::get<diagnostic_list_t>(built));
    const auto measure = canvas->measure_at(action.number);
    if (!measure || measure->row_capacity == 0)
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "BMS measure selection has no geometry")});
    auto proposed = state;
    proposed.selected_canvas_measure = action.number;
    proposed.retained_canvas_selection_range = canvas_row_span_t{
        measure->begin_row, measure->begin_row + measure->row_capacity - 1};
    proposed.selected_notes.clear();
    proposed.selected_objects.clear();
    if (measure->authored) {
        const auto first = position_at(context.edit.scope.measures, measure->begin_row);
        const auto last = position_at(context.edit.scope.measures, measure->begin_row + measure->row_capacity - 1);
        if (!first || !last)
            return cursor_diagnostic_result(context.edit.history, state,
                {error(diagnostic_code_t::row_out_of_range,
                       "selected measure is missing authored geometry")});
        place_cursor_on_picked_row(proposed, *first);
        proposed.retained_selection_range = normalized_position_span(context.edit.scope.measures, *first, *last);
    } else {
        place_cursor_on_canvas_row(proposed, std::nullopt, measure->begin_row);
        proposed.retained_selection_range.reset();
    }
    if (!keep_cursor_visible(context.edit.scope.chart, context.edit.scope.measures, proposed))
        return cursor_diagnostic_result(context.edit.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "measure selection cannot keep the viewport valid")});
    auto view = cursor_make_view(context.edit.history, proposed);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto target = measure->authored ? proposed.cursor : local_position_t{
            context.edit.scope.measures.entries.back().id, context.edit.scope.measures.entries.back().row_capacity - 1};
        const auto replacement = cursor_transition_lane(chart, context.edit.scope.measures, state.cursor, target,
                                                 state.active_lane, state.scratch_on_right);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = cursor_make_view(context.edit.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return cursor_diagnostic_result(context.edit.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(view)};
}

} // namespace wormedit::interaction_detail

namespace wormedit {
namespace {
auto cursor_canvas_rows(const chart_document_t& chart, const measure_index_t& index)
    -> std::optional<std::uint64_t> {
    if (!has_chart_virtual_tail(chart)) return index.total_rows;
    const auto built = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    return canvas ? std::optional{canvas->total_rows} : std::nullopt;
}
}
auto synchronize_editor_cursor_to_viewport(const chart_document_t& chart,
    const measure_index_t& index, editor_interaction_state_t& state) -> bool {
    if (!state.fixed_cursor_screen_y_pixels) return true;
    const auto rows = cursor_canvas_rows(chart, index);
    if (!rows || !*rows || !state.active_snap_denominator || !chart.divisions_per_standard_measure ||
        chart.divisions_per_standard_measure % state.active_snap_denominator) return false;
    const auto source = state.scroll_direction == editor_field_scroll_direction_t::down
        ? state.viewport_height_pixels - *state.fixed_cursor_screen_y_pixels
        : *state.fixed_cursor_screen_y_pixels;
    const auto raw = (state.viewport_scroll_pixels + source) *
        static_cast<long double>(chart.divisions_per_standard_measure) / state.pixels_per_standard_measure;
    if (!std::isfinite(raw)) return false;
    const auto bounded = std::clamp(raw, 0.0L, static_cast<long double>(*rows - 1));
    const auto snapped = snap_canvas_row(chart, index, bounded, state.active_snap_denominator, *rows);
    if (!snapped) return false;
    const auto row = *snapped;
    const auto position = index.position_at(row);
    const auto from = state.cursor;
    place_cursor_on_canvas_row(state, position, row, static_cast<double>(bounded - row));
    if (position) {
        const auto lane = interaction_detail::cursor_transition_lane(chart, index, from, *position, state.active_lane, state.scratch_on_right);
        if (lane) state.active_lane = *lane;
    }
    return true;
}
} // namespace wormedit
