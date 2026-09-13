#include <wormedit/core/interaction.h>

#include <wormedit/core/rational.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>

namespace wormedit {
namespace {

auto error(diagnostic_code_t code, std::string message,
          std::optional<object_id_t> object = std::nullopt) -> diagnostic_t {
    return {
        .severity = diagnostic_severity_t::error,
        .code = code,
        .message = std::move(message),
        .object = object,
    };
}

auto find_chart(const song_document_t& song, chart_id_t id) -> const chart_document_t* {
    const auto found = std::ranges::find_if(song.charts,
                                           [id](const auto& chart) { return chart.id == id; });
    return found == song.charts.end() ? nullptr : &*found;
}

auto find_chart(song_document_t& song, chart_id_t id) -> chart_document_t* {
    const auto found = std::ranges::find_if(song.charts,
                                           [id](const auto& chart) { return chart.id == id; });
    return found == song.charts.end() ? nullptr : &*found;
}

auto position_at(const measure_index_t& index, std::uint64_t absolute) ->
    std::optional<local_position_t> {
    if (absolute >= index.total_rows) {
        return std::nullopt;
    }
    const auto entry = std::ranges::find_if(
        std::views::reverse(index.entries), [absolute](const auto& candidate) {
            return candidate.absolute_begin_row <= absolute;
        });
    if (entry == index.entries.rend()) {
        return std::nullopt;
    }
    return local_position_t{
        .measure = entry->id,
        .row = absolute - entry->absolute_begin_row,
    };
}

auto effective_layout(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position) -> const lane_layout_t* {
    const auto event_row = absolute_row(index, position);
    if (!event_row.has_value()) {
        return nullptr;
    }
    const lane_layout_segment_t* selected{};
    std::uint64_t selected_row{};
    for (const auto& segment : chart.lane_layouts) {
        const auto segment_row = absolute_row(index, segment.position);
        if (segment_row.has_value() && *segment_row <= *event_row &&
            (selected == nullptr || *segment_row >= selected_row)) {
            selected = &segment;
            selected_row = *segment_row;
        }
    }
    return selected == nullptr ? nullptr : &selected->layout;
}

auto editable_lane(const lane_definition_t& lane) -> bool {
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
}

auto find_active_lane(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, lane_id_t id) -> const lane_definition_t* {
    const auto* layout = effective_layout(chart, index, position);
    if (layout == nullptr) {
        return nullptr;
    }
    const auto found = std::ranges::find_if(layout->lanes, [id](const auto& lane) {
        return lane.id == id && editable_lane(lane);
    });
    return found == layout->lanes.end() ? nullptr : &*found;
}

auto transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to,
                     lane_id_t current) -> std::optional<lane_id_t> {
    const auto* source_layout = effective_layout(chart, index, from);
    const auto* target_layout = effective_layout(chart, index, to);
    if (source_layout == nullptr || target_layout == nullptr) {
        return std::nullopt;
    }
    const auto source = std::ranges::find(source_layout->lanes, current,
                                          &lane_definition_t::id);
    if (source == source_layout->lanes.end()) {
        return std::nullopt;
    }
    const auto exact_column = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.display_order == source->display_order &&
               lane.role == source->role;
    });
    if (exact_column != target_layout->lanes.end()) {
        return exact_column->id;
    }
    const auto same_column = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.display_order == source->display_order;
    });
    if (same_column != target_layout->lanes.end()) {
        return same_column->id;
    }
    const auto same_role = std::ranges::find_if(target_layout->lanes, [&](const auto& lane) {
        return editable_lane(lane) && lane.role == source->role;
    });
    if (same_role != target_layout->lanes.end()) {
        return same_role->id;
    }
    const lane_definition_t* first{};
    for (const auto& lane : target_layout->lanes) {
        if (editable_lane(lane) &&
            (first == nullptr || lane.display_order < first->display_order)) {
            first = &lane;
        }
    }
    return first == nullptr ? std::nullopt : std::optional{first->id};
}

auto maximum_viewport_scroll(const chart_document_t& chart,
                             const measure_index_t& index, double scale,
                             double viewport_height) -> std::optional<double> {
    const auto content_height = static_cast<double>(index.total_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 ||
        !std::isfinite(viewport_height) || viewport_height <= 0.0) {
        return std::nullopt;
    }
    return std::max(0.0, content_height - viewport_height);
}

auto make_view(const document_history_t& history, const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    const auto* chart = find_chart(history.document, state.chart);
    if (chart == nullptr) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "editor interaction refers to an unknown chart")};
    }
    const auto built_index = build_measure_index(*chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::get<diagnostic_list_t>(built_index);
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = absolute_row(index, state.cursor);
    if (!absolute.has_value()) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "editor cursor lies outside chart geometry")};
    }
    const auto* lane = find_active_lane(*chart, index, state.cursor, state.active_lane);
    if (lane == nullptr) {
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "active editor lane is unavailable at the cursor")};
    }
    if (state.active_snap_denominator == 0 ||
        std::ranges::find(state.supported_snap_denominators,
                          state.active_snap_denominator) ==
            state.supported_snap_denominators.end() ||
        chart->divisions_per_standard_measure % state.active_snap_denominator != 0) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "active editor snap is invalid for this chart")};
    }
    const auto maximum_scroll = maximum_viewport_scroll(
        *chart, index, state.pixels_per_standard_measure,
        state.viewport_height_pixels);
    if (!std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        state.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !maximum_scroll.has_value() || !std::isfinite(state.viewport_scroll_pixels) ||
        state.viewport_scroll_pixels < 0.0 ||
        state.viewport_scroll_pixels > *maximum_scroll) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "editor viewport geometry is invalid")};
    }

    const auto step = chart->divisions_per_standard_measure / state.active_snap_denominator;
    return editor_interaction_view_model_t{
        .chart = state.chart,
        .active_lane = state.active_lane,
        .active_lane_name = lane->name,
        .cursor = state.cursor,
        .active_snap_denominator = state.active_snap_denominator,
        .supported_snap_denominators = state.supported_snap_denominators,
        .document_revision = history.document.revision,
        .pixels_per_standard_measure = state.pixels_per_standard_measure,
        .viewport_scroll_pixels = state.viewport_scroll_pixels,
        .viewport_height_pixels = state.viewport_height_pixels,
        .can_move_backward = *absolute >= step,
        .can_move_forward = step < index.total_rows - *absolute,
    };
}

auto next_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    auto maximum = std::uint64_t{};
    const auto scan = [&maximum](const auto& values) {
        for (const auto& value : values) {
            maximum = std::max(maximum, value.id.value);
        }
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) {
        scan(channel.events);
    }
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    return object_id_t{maximum + 1};
}

auto snapped_position(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t target, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto absolute = absolute_row(index, target);
    if (!absolute.has_value() || denominator == 0 ||
        chart.divisions_per_standard_measure % denominator != 0) {
        return std::nullopt;
    }
    const auto step = chart.divisions_per_standard_measure / denominator;
    if (step == 0) {
        return std::nullopt;
    }
    const auto remainder = *absolute % step;
    auto snapped = *absolute - remainder;
    const auto distance_up = step - remainder;
    if (remainder > distance_up && distance_up <=
                                       std::numeric_limits<std::uint64_t>::max() - *absolute) {
        snapped = *absolute + distance_up;
    }
    return position_at(index, snapped);
}

auto diagnostic_result(const document_history_t& history,
                       const editor_interaction_state_t& state,
                       diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    editor_interaction_result_t result{
        .diagnostics = std::move(diagnostics),
    };
    const auto view = make_view(history, state);
    if (std::holds_alternative<editor_interaction_view_model_t>(view)) {
        result.view = std::get<editor_interaction_view_model_t>(view);
    }
    return result;
}

} // namespace

auto make_editor_interaction(
    const document_history_t& history, chart_id_t chart_id, lane_id_t lane_id,
    local_position_t cursor, std::span<const std::uint64_t> ordered_snap_denominators,
    std::uint64_t requested_snap_denominator,
    double viewport_height_pixels) -> editor_interaction_setup_t {
    editor_interaction_setup_t result;
    const auto* chart = find_chart(history.document, chart_id);
    if (chart == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "editor interaction refers to an unknown chart"));
        return result;
    }
    const auto built_index = build_measure_index(*chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        result.diagnostics = std::get<diagnostic_list_t>(built_index);
        return result;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    if (!absolute_row(index, cursor).has_value()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::row_out_of_range, "initial editor cursor is outside chart"));
        return result;
    }
    if (find_active_lane(*chart, index, cursor, lane_id) == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unknown_lane, "initial editor lane is unavailable at cursor"));
        return result;
    }
    const auto snap = filter_snap_denominators(chart->divisions_per_standard_measure,
                                               ordered_snap_denominators,
                                               requested_snap_denominator);
    if (!snap) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_resolution, "cannot configure editor snaps"));
        return result;
    }
    result.state = editor_interaction_state_t{
        .chart = chart_id,
        .active_lane = lane_id,
        .cursor = cursor,
        .supported_snap_denominators = snap.supported_denominators,
        .active_snap_denominator = snap.active_denominator,
        .active_snap_was_fallback = snap.active_was_fallback,
        .viewport_height_pixels = viewport_height_pixels,
    };
    const auto view = make_view(history, *result.state);
    if (std::holds_alternative<editor_interaction_view_model_t>(view)) {
        result.view = std::get<editor_interaction_view_model_t>(view);
    } else {
        result.diagnostics = std::get<diagnostic_list_t>(view);
        result.state.reset();
    }
    return result;
}

auto editor_position_y_pixels(const chart_document_t& chart,
                              const editor_interaction_view_model_t& view,
                              local_position_t position) -> std::optional<double> {
    if (view.chart != chart.id || !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels)) {
        return std::nullopt;
    }
    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::nullopt;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = absolute_row(index, position);
    if (!absolute.has_value()) {
        return std::nullopt;
    }
    const auto y = static_cast<double>(*absolute) * view.pixels_per_standard_measure /
                       static_cast<double>(chart.divisions_per_standard_measure) -
                   view.viewport_scroll_pixels;
    if (!std::isfinite(y)) {
        return std::nullopt;
    }
    return y;
}

auto dispatch_editor_action(document_history_t& history, editor_interaction_state_t& state,
                            const editor_ui_action_t& action) -> editor_interaction_result_t {
    const auto initial_view = make_view(history, state);
    if (std::holds_alternative<diagnostic_list_t>(initial_view)) {
        return diagnostic_result(history, state,
                                 std::get<diagnostic_list_t>(initial_view));
    }
    const auto* chart = find_chart(history.document, state.chart);
    const auto built_index = build_measure_index(*chart);
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = *absolute_row(index, state.cursor);

    return std::visit(
        [&](const auto& typed_action) -> editor_interaction_result_t {
            using action_t = std::decay_t<decltype(typed_action)>;
            if constexpr (std::is_same_v<action_t, move_edit_cursor_action_t>) {
                if (typed_action.direction != interaction_direction_t::backward &&
                    typed_action.direction != interaction_direction_t::forward) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id, "unknown cursor direction")});
                }
                const auto step = chart->divisions_per_standard_measure /
                                  state.active_snap_denominator;
                const auto remainder = absolute % step;
                std::optional<local_position_t> next;
                if (typed_action.direction == interaction_direction_t::backward) {
                    const auto distance = remainder == 0 ? step : remainder;
                    if (absolute >= distance) {
                        next = position_at(index, absolute - distance);
                    }
                } else {
                    const auto distance = remainder == 0 ? step : step - remainder;
                    if (distance < index.total_rows - absolute) {
                        next = position_at(index, absolute + distance);
                    }
                }
                if (!next.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "cursor movement would leave chart geometry")});
                }
                auto proposed_state = state;
                proposed_state.cursor = *next;
                auto next_view = make_view(history, proposed_state);
                if (std::holds_alternative<diagnostic_list_t>(next_view)) {
                    const auto replacement = transition_lane(
                        *chart, index, state.cursor, *next, state.active_lane);
                    if (replacement.has_value()) {
                        proposed_state.active_lane = *replacement;
                        next_view = make_view(history, proposed_state);
                    }
                }
                if (std::holds_alternative<diagnostic_list_t>(next_view)) {
                    return diagnostic_result(
                        history, state, std::get<diagnostic_list_t>(next_view));
                }
                state = std::move(proposed_state);
                return {
                    .accepted = true,
                    .view = std::get<editor_interaction_view_model_t>(next_view),
                };
            } else if constexpr (std::is_same_v<action_t, cycle_edit_snap_action_t>) {
                if (typed_action.direction != interaction_direction_t::backward &&
                    typed_action.direction != interaction_direction_t::forward) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id, "unknown snap direction")});
                }
                const auto found = std::ranges::find(state.supported_snap_denominators,
                                                     state.active_snap_denominator);
                if (found == state.supported_snap_denominators.end()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "active editor snap is not in the supported snap list")});
                }
                if (!state.supported_snap_denominators.empty()) {
                    const auto size = static_cast<std::ptrdiff_t>(
                        state.supported_snap_denominators.size());
                    const auto index = std::distance(state.supported_snap_denominators.begin(), found);
                    const auto delta = typed_action.direction == interaction_direction_t::forward ? 1
                                                                                                   : -1;
                    const auto next = (index + delta + size) % size;
                    const auto next_denominator = state.supported_snap_denominators[
                        static_cast<std::size_t>(next)];
                    if (next_denominator != state.active_snap_denominator) {
                        state.active_snap_denominator = next_denominator;
                        state.active_snap_was_fallback = false;
                    }
                }
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                                : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, place_tap_action_t>) {
                const auto snapped = snapped_position(*chart, index, typed_action.position,
                                                      state.active_snap_denominator);
                if (!snapped.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "note placement target cannot be snapped inside chart geometry")});
                }
                if (find_active_lane(*chart, index, *snapped, state.active_lane) == nullptr) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_lane,
                               "active editor lane is unavailable at snapped position")});
                }
                auto proposed = history.document;
                auto* proposed_chart = find_chart(proposed, state.chart);
                const auto target_measure = std::ranges::find_if(
                    proposed_chart->measures,
                    [&](const auto& measure) { return measure.id == snapped->measure; });
                if (target_measure == proposed_chart->measures.end()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_measure,
                               "snapped note position refers to an unknown measure")});
                }
                const auto existing = std::ranges::find_if(
                    target_measure->notes, [&](const auto& note) {
                        return note.position == *snapped && note.lane == state.active_lane;
                    });
                const auto target_absolute = *absolute_row(index, *snapped);
                std::optional<object_id_t> overlap;
                for (const auto& candidate_measure : proposed_chart->measures) {
                    for (const auto& note : candidate_measure.notes) {
                        if (note.lane != state.active_lane || !note.end.has_value() ||
                            (existing != target_measure->notes.end() && note.id == existing->id)) {
                            continue;
                        }
                        const auto begin = absolute_row(index, note.position);
                        const auto end = absolute_row(index, *note.end);
                        if (begin.has_value() && end.has_value() &&
                            target_absolute >= *begin && target_absolute <= *end) {
                            overlap = note.id;
                            break;
                        }
                    }
                    if (overlap.has_value()) {
                        break;
                    }
                }
                if (overlap.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::collision,
                               "tap placement overlaps a same-lane hold", *overlap)});
                }
                std::string label;
                if (existing != target_measure->notes.end()) {
                    if (existing->kind != note_kind_t::tap || existing->end.has_value()) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::collision,
                                   "tap placement overlaps a non-tap note", existing->id)});
                    }
                    target_measure->notes.erase(existing);
                    label = "Remove tap note";
                } else {
                    const auto id = next_object_id(*proposed_chart);
                    if (!id.has_value()) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_id,
                                   "chart object ID sequence is exhausted")});
                    }
                    target_measure->notes.push_back({
                        .id = *id,
                        .position = *snapped,
                        .lane = state.active_lane,
                        .kind = note_kind_t::tap,
                    });
                    label = "Place tap note";
                }
                auto command = commit_document_command(history, std::move(proposed),
                                                      std::move(label));
                if (!command) {
                    return diagnostic_result(history, state, std::move(command.diagnostics));
                }
                state.cursor = *snapped;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = true,
                    .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                                : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, select_editor_lane_action_t>) {
                if (find_active_lane(*chart, index, state.cursor, typed_action.lane) == nullptr) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_lane,
                               "selected editor lane is unavailable at the cursor")});
                }
                state.active_lane = typed_action.lane;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                                : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, adjust_editor_zoom_action_t>) {
                if (!std::isfinite(typed_action.factor) || !(typed_action.factor > 0.0)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "editor zoom factor must be finite and positive")});
                }
                const auto current_scale = state.pixels_per_standard_measure;
                double next_scale{};
                if (typed_action.factor > maximum_pixels_per_standard_measure / current_scale) {
                    next_scale = maximum_pixels_per_standard_measure;
                } else if (typed_action.factor < minimum_pixels_per_standard_measure / current_scale) {
                    next_scale = minimum_pixels_per_standard_measure;
                } else {
                    next_scale = std::clamp(
                        current_scale * typed_action.factor,
                        minimum_pixels_per_standard_measure,
                        maximum_pixels_per_standard_measure);
                }
                auto anchor_screen_y =
                    static_cast<double>(absolute) * current_scale /
                        static_cast<double>(chart->divisions_per_standard_measure) -
                    state.viewport_scroll_pixels;
                if (typed_action.anchor_screen_y_pixels.has_value()) {
                    anchor_screen_y = *typed_action.anchor_screen_y_pixels;
                    if (!std::isfinite(anchor_screen_y) || anchor_screen_y < 0.0 ||
                        anchor_screen_y > state.viewport_height_pixels) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_resolution,
                                   "zoom anchor lies outside the editor viewport")});
                    }
                }
                const auto anchor_absolute_rows =
                    (state.viewport_scroll_pixels + anchor_screen_y) *
                    static_cast<double>(chart->divisions_per_standard_measure) /
                    current_scale;
                const auto unclamped_scroll =
                    anchor_absolute_rows * next_scale /
                        static_cast<double>(chart->divisions_per_standard_measure) -
                    anchor_screen_y;
                const auto maximum_scroll = maximum_viewport_scroll(
                    *chart, index, next_scale, state.viewport_height_pixels);
                const auto next_scroll = maximum_scroll.has_value()
                                             ? std::clamp(unclamped_scroll, 0.0,
                                                          *maximum_scroll)
                                             : std::numeric_limits<double>::quiet_NaN();
                if (!std::isfinite(next_scroll)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "editor zoom would overflow viewport coordinates")});
                }
                state.pixels_per_standard_measure = next_scale;
                state.viewport_scroll_pixels = next_scroll;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                                : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, scroll_editor_view_action_t>) {
                if (!std::isfinite(typed_action.vertical_pixels) ||
                    !std::isfinite(state.viewport_scroll_pixels)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "viewport scroll must remain finite")});
                }
                const auto next_scroll =
                    state.viewport_scroll_pixels + typed_action.vertical_pixels;
                if (!std::isfinite(next_scroll)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "viewport scroll would overflow")});
                }
                const auto maximum_scroll = maximum_viewport_scroll(
                    *chart, index, state.pixels_per_standard_measure,
                    state.viewport_height_pixels);
                if (!maximum_scroll.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "editor viewport geometry is invalid")});
                }
                state.viewport_scroll_pixels = std::clamp(next_scroll, 0.0,
                                                          *maximum_scroll);
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                                : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            }
        },
        action);
}

} // namespace wormedit
