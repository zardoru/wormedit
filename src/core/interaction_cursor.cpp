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

constexpr auto remainder_rounds_up(std::uint64_t remainder, std::uint64_t capacity) -> bool {
    return remainder > capacity - remainder;
}

auto error(diagnostic_code_t code, std::string message) -> diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message)};
}

auto position_at(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    return position_at_absolute_row(index, absolute);
}

auto editable_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto row = absolute_row(index, position);
    if (!row) return nullptr;
    const lane_layout_segment_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto candidate = absolute_row(index, segment.position);
        if (candidate && *candidate <= *row &&
            (selected == nullptr || *candidate >= selected_row)) {
            selected = &segment;
            selected_row = *candidate;
        }
    }
    return selected == nullptr ? nullptr : &selected->layout;
}

auto transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to, lane_id_t current)
    -> std::optional<lane_id_t> {
    const auto* source_layout = effective_layout(chart, index, from);
    const auto* target_layout = effective_layout(chart, index, to);
    if (!source_layout || !target_layout) return std::nullopt;
    const auto source = std::ranges::find(source_layout->lanes, current, &lane_definition_t::id);
    if (source == source_layout->lanes.end()) return std::nullopt;
    const auto exact = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.display_order == source->display_order &&
               lane.role == source->role;
    });
    if (exact != target_layout->lanes.end()) return exact->id;
    const auto column = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.display_order == source->display_order;
    });
    if (column != target_layout->lanes.end()) return column->id;
    const auto role = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.role == source->role;
    });
    if (role != target_layout->lanes.end()) return role->id;
    const lane_definition_t* first{};
    for (const auto& lane : target_layout->lanes)
        if (editable_lane(lane) && (!first || lane.display_order < first->display_order))
            first = &lane;
    return first ? std::optional{first->id} : std::nullopt;
}

auto normalized_position_span(const measure_index_t& index, local_position_t first,
                              local_position_t second) -> std::optional<position_span_t> {
    const auto first_row = absolute_row(index, first);
    const auto second_row = absolute_row(index, second);
    if (!first_row || !second_row || *first_row == *second_row) return std::nullopt;
    return *first_row < *second_row ? position_span_t{first, second}
                                    : position_span_t{second, first};
}

auto make_view(const document_history_t& history, const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    return inspect_editor_interaction(history, state);
}

auto diagnostic_result(const document_history_t& history,
                       const editor_interaction_state_t& state,
                       diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    editor_interaction_result_t result{.diagnostics = std::move(diagnostics)};
    const auto view = make_view(history, state);
    if (const auto* model = std::get_if<editor_interaction_view_model_t>(&view))
        result.view = *model;
    return result;
}

auto maximum_viewport_scroll(const chart_document_t& chart, const measure_index_t& index,
                             double scale, double viewport_height) -> std::optional<double> {
    auto total_rows = index.total_rows;
    if (chart_has_virtual_tail(chart)) {
        const auto canvas = build_virtual_canvas(chart);
        const auto* virtual_canvas = std::get_if<virtual_canvas_t>(&canvas);
        if (!virtual_canvas) return std::nullopt;
        total_rows = virtual_canvas->total_rows;
    }
    const auto content_height = static_cast<double>(total_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 ||
        !std::isfinite(viewport_height) || viewport_height <= 0.0)
        return std::nullopt;
    return editor_maximum_viewport_scroll(content_height, viewport_height);
}

auto minimum_viewport_scroll(double viewport_height) -> double {
    return -viewport_height * editor_cursor_edge_margin_fraction;
}

} // namespace

static auto apply_cursor_navigation_target(cursor_navigation_context_t& context,
                                           std::optional<local_position_t> target,
                                           std::uint64_t canvas_row,
                                           std::string_view description)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold &&
        state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) {
        if (!target || state.pending_hold->anchor_canvas_row ||
            std::ranges::any_of(state.additional_keyboard_holds,
                                [](const auto& hold) { return hold.anchor_canvas_row.has_value(); }))
            return context.update_canvas_keyboard_holds(canvas_row);
        return context.update_keyboard_holds(*target);
    }
    auto proposed = state;
    place_cursor_on_canvas_row(proposed, target, canvas_row);
    if (!context.keep_cursor_visible(proposed))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   std::string(description) + " cannot keep the viewport valid")});
    auto view = make_view(context.history, proposed);
    if (target && std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = transition_lane(context.chart, context.index, state.cursor,
                                                 *target, state.active_lane);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = make_view(context.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return diagnostic_result(context.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true,
            .view = std::get<editor_interaction_view_model_t>(std::move(view))};
}

static auto next_cursor_snap_row(const cursor_navigation_context_t& context,
                                 interaction_direction_t direction, std::uint64_t step,
                                 std::uint64_t canvas_rows) -> std::optional<std::uint64_t> {
    const auto row = static_cast<long double>(context.absolute) + context.state.cursor_subrow;
    const auto position = row / static_cast<long double>(step);
    const auto target = (direction == interaction_direction_t::forward
        ? std::floor(position) + 1.0L : std::ceil(position) - 1.0L) * step;
    if (!std::isfinite(target) || target < 0.0L || target >= canvas_rows) return std::nullopt;
    return static_cast<std::uint64_t>(target);
}

auto move_edit_cursor(cursor_navigation_context_t& context,
                      const move_edit_cursor_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_id, "unknown cursor direction")});
    const auto step = context.chart.divisions_per_standard_measure /
                      context.state.active_snap_denominator;
    auto canvas_rows = context.index.total_rows;
    if (chart_has_virtual_tail(context.chart)) {
        const auto canvas = build_virtual_canvas(context.chart);
        const auto* value = std::get_if<virtual_canvas_t>(&canvas);
        if (!value) return diagnostic_result(context.history, context.state,
                                               std::get<diagnostic_list_t>(canvas));
        canvas_rows = value->total_rows;
    }
    const auto next_row = next_cursor_snap_row(context, action.direction, step, canvas_rows);
    if (!next_row)
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor movement would leave chart geometry")});
    const auto target = *next_row < context.index.total_rows
        ? position_at(context.index, *next_row) : std::nullopt;
    return apply_cursor_navigation_target(context, target, *next_row, "cursor movement");
}

auto page_edit_cursor(cursor_navigation_context_t& context,
                      const page_edit_cursor_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_id, "unknown cursor page direction")});
    if (action.measure_count == 0 || context.index.entries.empty())
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_resolution,
                   "cursor paging requires a positive measure count")});
    const auto built_canvas = build_virtual_canvas(context.chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas);
    if (!canvas) return diagnostic_result(context.history, context.state,
                                            std::get<diagnostic_list_t>(built_canvas));
    const auto source = canvas->measure_containing(context.absolute);
    if (!source || source->row_capacity == 0)
        return diagnostic_result(context.history, context.state,
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
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::row_out_of_range, "cursor page target is unavailable")});
    const auto scaled = wide_math::multiply(context.absolute - source->begin_row,
                                            target_measure->row_capacity);
    const auto division = wide_math::divide(scaled, {0, source->row_capacity});
    const auto rounded = wide_math::add(
        division.quotient,
        remainder_rounds_up(division.remainder.low, source->row_capacity) ? 1U : 0U);
    if (!wide_math::fits_u64(rounded) || rounded.low >= target_measure->row_capacity)
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor page target cannot represent its fractional row")});
    const auto row = target_measure->begin_row + rounded.low;
    const auto target = target_measure->authored ? position_at(context.index, row) : std::nullopt;
    return apply_cursor_navigation_target(context, target, row, "cursor paging");
}

auto keep_cursor_visible(const chart_document_t& chart, const measure_index_t& index,
                         editor_interaction_state_t& candidate) -> bool {
    const auto row = candidate.virtual_bms_cursor_row
        ? candidate.virtual_bms_cursor_row : absolute_row(index, candidate.cursor);
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
    auto& state = context.state;
    if (action.expected_document_revision != context.history.document.revision)
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_id,
                   "cursor jump refers to a stale document revision")});
    if (!absolute_row(context.index, action.position))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "cursor jump target lies outside chart geometry")});
    auto proposed = state;
    place_cursor_on_picked_row(proposed, action.position);
    if (!context.keep_cursor_visible(proposed))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "cursor jump cannot keep the viewport valid")});
    auto view = make_view(context.history, proposed);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = transition_lane(context.chart, context.index, state.cursor,
                                                 action.position, state.active_lane);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = make_view(context.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return diagnostic_result(context.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(view)};
}

auto select_bms_canvas_measure(cursor_navigation_context_t& context,
                               const select_bms_canvas_measure_action_t& action)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto& chart = context.chart;
    if (!chart_has_virtual_tail(chart) ||
        action.number >= virtual_tail_measure_limit(chart))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "BMS measure selection is outside #000-#999")});
    const auto built = build_virtual_canvas(chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    if (!canvas) return diagnostic_result(context.history, state,
                                            std::get<diagnostic_list_t>(built));
    const auto measure = canvas->measure_at(action.number);
    if (!measure || measure->row_capacity == 0)
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "BMS measure selection has no geometry")});
    auto proposed = state;
    proposed.selected_canvas_measure = action.number;
    proposed.retained_canvas_selection_range = canvas_row_span_t{
        measure->begin_row, measure->begin_row + measure->row_capacity - 1};
    proposed.selected_notes.clear();
    proposed.selected_objects.clear();
    if (measure->authored) {
        const auto first = position_at(context.index, measure->begin_row);
        const auto last = position_at(context.index, measure->begin_row + measure->row_capacity - 1);
        if (!first || !last)
            return diagnostic_result(context.history, state,
                {error(diagnostic_code_t::row_out_of_range,
                       "selected measure is missing authored geometry")});
        place_cursor_on_picked_row(proposed, *first);
        proposed.retained_selection_range = normalized_position_span(context.index, *first, *last);
    } else {
        place_cursor_on_canvas_row(proposed, std::nullopt, measure->begin_row);
        proposed.retained_selection_range.reset();
    }
    if (!context.keep_cursor_visible(proposed))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "measure selection cannot keep the viewport valid")});
    auto view = make_view(context.history, proposed);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto target = measure->authored ? proposed.cursor : local_position_t{
            context.index.entries.back().id, context.index.entries.back().row_capacity - 1};
        const auto replacement = transition_lane(chart, context.index, state.cursor, target,
                                                 state.active_lane);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = make_view(context.history, proposed);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return diagnostic_result(context.history, state, *diagnostics);
    state = std::move(proposed);
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(view)};
}

} // namespace wormedit::interaction_detail

namespace wormedit {
namespace {
auto cursor_canvas_rows(const chart_document_t& chart, const measure_index_t& index)
    -> std::optional<std::uint64_t> {
    if (!chart_has_virtual_tail(chart)) return index.total_rows;
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
    const auto step = chart.divisions_per_standard_measure / state.active_snap_denominator;
    auto row = static_cast<std::uint64_t>(std::floor(bounded / step)) * step;
    if (step < *rows - row && bounded - row > static_cast<long double>(step) / 2) row += step;
    const auto position = position_at_absolute_row(index, row);
    const auto from = state.cursor;
    place_cursor_on_canvas_row(state, position, row, static_cast<double>(bounded - row));
    if (position) {
        const auto lane = interaction_detail::transition_lane(chart, index, from, *position, state.active_lane);
        if (lane) state.active_lane = *lane;
    }
    return true;
}
} // namespace wormedit
