#include <wormedit/core/interaction.h>

#include <wormedit/core/rational.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <ranges>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
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
    const auto found =
        std::ranges::find_if(song.charts, [id](const auto& chart) { return chart.id == id; });
    return found == song.charts.end() ? nullptr : &*found;
}

auto find_chart(song_document_t& song, chart_id_t id) -> chart_document_t* {
    const auto found =
        std::ranges::find_if(song.charts, [id](const auto& chart) { return chart.id == id; });
    return found == song.charts.end() ? nullptr : &*found;
}

auto position_at(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    if (absolute >= index.total_rows) {
        return std::nullopt;
    }
    const auto after = std::ranges::upper_bound(index.entries, absolute, {},
                                                &measure_index_entry_t::absolute_begin_row);
    if (after == index.entries.begin()) {
        return std::nullopt;
    }
    const auto entry = std::prev(after);
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
    const auto found = std::ranges::find_if(
        layout->lanes, [id](const auto& lane) { return lane.id == id && editable_lane(lane); });
    return found == layout->lanes.end() ? nullptr : &*found;
}

auto transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to, lane_id_t current)
    -> std::optional<lane_id_t> {
    const auto* source_layout = effective_layout(chart, index, from);
    const auto* target_layout = effective_layout(chart, index, to);
    if (source_layout == nullptr || target_layout == nullptr) {
        return std::nullopt;
    }
    const auto source = std::ranges::find(source_layout->lanes, current, &lane_definition_t::id);
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

auto maximum_viewport_scroll(const chart_document_t& chart, const measure_index_t& index,
                             double scale, double viewport_height) -> std::optional<double> {
    const auto content_height = static_cast<double>(index.total_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 || !std::isfinite(viewport_height) ||
        viewport_height <= 0.0) {
        return std::nullopt;
    }
    return std::max(0.0, content_height - viewport_height);
}

auto make_view(const document_history_t& history, const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    const auto* chart = find_chart(history.document, state.chart);
    if (chart == nullptr) {
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "editor interaction refers to an unknown chart")};
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
        std::ranges::find(state.supported_snap_denominators, state.active_snap_denominator) ==
            state.supported_snap_denominators.end() ||
        chart->divisions_per_standard_measure % state.active_snap_denominator != 0) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "active editor snap is invalid for this chart")};
    }
    const auto maximum_scroll = maximum_viewport_scroll(
        *chart, index, state.pixels_per_standard_measure, state.viewport_height_pixels);
    if (!std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        state.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !maximum_scroll.has_value() || !std::isfinite(state.viewport_scroll_pixels) ||
        state.viewport_scroll_pixels < 0.0 || state.viewport_scroll_pixels > *maximum_scroll) {
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_resolution, "editor viewport geometry is invalid")};
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
        .pending_hold = state.pending_hold,
        .pending_note_duration_gesture = state.pending_note_duration_gesture,
        .selected_notes = state.selected_notes,
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
    if (remainder > distance_up &&
        distance_up <= std::numeric_limits<std::uint64_t>::max() - *absolute) {
        snapped = *absolute + distance_up;
    }
    return position_at(index, snapped);
}

auto colliding_note(const chart_document_t& chart, const measure_index_t& index, lane_id_t lane,
                    local_position_t begin, local_position_t end) -> std::optional<object_id_t> {
    const auto begin_row = absolute_row(index, begin);
    const auto end_row = absolute_row(index, end);
    if (!begin_row.has_value() || !end_row.has_value() || *end_row < *begin_row) {
        return std::nullopt;
    }
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != lane)
                continue;
            const auto note_begin = absolute_row(index, note.position);
            const auto note_end = note.end ? absolute_row(index, *note.end) : note_begin;
            if (note_begin.has_value() && note_end.has_value() && *note_begin <= *end_row &&
                *note_end >= *begin_row) {
                return note.id;
            }
        }
    }
    return std::nullopt;
}

auto chart_note_ids(const chart_document_t& chart) -> std::unordered_set<std::uint64_t> {
    auto count = std::size_t{};
    for (const auto& measure : chart.measures) {
        count += measure.notes.size();
    }
    std::unordered_set<std::uint64_t> result;
    result.reserve(count);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            result.insert(note.id.value);
        }
    }
    return result;
}

auto find_note(const chart_document_t& chart, object_id_t id) -> const note_t* {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end())
            return &*found;
    }
    return nullptr;
}

auto find_note(chart_document_t& chart, object_id_t id) -> note_t* {
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end())
            return &*found;
    }
    return nullptr;
}

struct resized_note_t {
    object_id_t id;
    lane_id_t lane;
    std::uint64_t begin{};
    std::uint64_t end{};
    std::optional<local_position_t> end_position;
};

struct duration_collision_interval_t {
    lane_id_t lane;
    std::uint64_t begin{};
    std::uint64_t end{};
    object_id_t note;
    bool resizing{};
};

struct active_duration_collision_t {
    std::uint64_t end{};
    object_id_t note;
};

struct earliest_duration_collision_t {
    auto operator()(const active_duration_collision_t& left,
                    const active_duration_collision_t& right) const -> bool {
        return left.end > right.end;
    }
};

struct duration_resize_plan_t {
    object_id_t reference_note;
    lane_id_t reference_lane;
    local_position_t reference_end;
    std::vector<object_id_t> notes;
    std::vector<resized_note_t> resized;
};

auto plan_duration_resize(const chart_document_t& chart, const measure_index_t& index,
                          std::vector<object_id_t> targets, object_id_t reference_id,
                          local_position_t reference_end, std::uint64_t snap_denominator,
                          bool reference_end_is_exact = false)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t> {
    const auto* reference = find_note(chart, reference_id);
    if (reference == nullptr ||
        (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold)) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "duration resize requires a tap or hold reference note",
                                       reference_id)};
    }
    const auto snapped =
        reference_end_is_exact
            ? (absolute_row(index, reference_end).has_value() ? std::optional{reference_end}
                                                              : std::nullopt)
            : snapped_position(chart, index, reference_end, snap_denominator);
    const auto reference_begin = absolute_row(index, reference->position);
    const auto reference_old_end =
        reference->end ? absolute_row(index, *reference->end) : reference_begin;
    const auto reference_new_end = snapped ? absolute_row(index, *snapped) : std::nullopt;
    if (!snapped.has_value() || !reference_begin.has_value() || !reference_old_end.has_value() ||
        !reference_new_end.has_value() || *reference_new_end < *reference_begin) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "reference duration cannot end before its note head",
                                       reference_id)};
    }
    const auto old_reference_duration = *reference_old_end - *reference_begin;
    const auto new_reference_duration = *reference_new_end - *reference_begin;
    const auto shrinking = new_reference_duration < old_reference_duration;
    const auto delta = shrinking ? old_reference_duration - new_reference_duration
                                 : new_reference_duration - old_reference_duration;
    if (std::ranges::find(targets, reference_id) == targets.end()) {
        targets = {reference_id};
    }

    std::unordered_set<std::uint64_t> target_ids;
    target_ids.reserve(targets.size());
    for (const auto id : targets) {
        if (id.value == 0 || !target_ids.insert(id.value).second) {
            return diagnostic_list_t{
                error(diagnostic_code_t::duplicate_id,
                      "duration resize contains an invalid or duplicate note ID", id)};
        }
    }
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> measure_entries;
    measure_entries.reserve(index.entries.size());
    for (const auto& entry : index.entries) {
        measure_entries.emplace(entry.id.value, &entry);
    }
    const auto row_for = [&](local_position_t position) -> std::optional<std::uint64_t> {
        const auto found = measure_entries.find(position.measure.value);
        if (found == measure_entries.end() || position.row >= found->second->row_capacity ||
            position.row >
                std::numeric_limits<std::uint64_t>::max() - found->second->absolute_begin_row) {
            return std::nullopt;
        }
        const auto row = found->second->absolute_begin_row + position.row;
        return row < index.total_rows ? std::optional{row} : std::nullopt;
    };

    auto note_count = std::size_t{};
    for (const auto& measure : chart.measures) {
        note_count += measure.notes.size();
    }
    std::unordered_map<std::uint64_t, const note_t*> target_notes;
    target_notes.reserve(targets.size());
    std::vector<duration_collision_interval_t> collision_intervals;
    collision_intervals.reserve(note_count);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (target_ids.contains(note.id.value)) {
                target_notes.emplace(note.id.value, &note);
                continue;
            }
            const auto begin = row_for(note.position);
            const auto end = note.end ? row_for(*note.end) : begin;
            if (!begin.has_value() || !end.has_value()) {
                return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                               "existing note lies outside chart geometry",
                                               note.id)};
            }
            collision_intervals.push_back({note.lane, *begin, *end, note.id, false});
        }
    }

    duration_resize_plan_t plan{
        .reference_note = reference_id,
        .reference_lane = reference->lane,
        .reference_end = *snapped,
        .notes = std::move(targets),
    };
    plan.resized.reserve(plan.notes.size());
    for (const auto id : plan.notes) {
        const auto found = target_notes.find(id.value);
        const auto* note = found == target_notes.end() ? nullptr : found->second;
        if (note == nullptr ||
            (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold)) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           "selected duration resize contains a non-tap/hold note",
                                           id)};
        }
        const auto begin = row_for(note->position);
        const auto old_end = note->end ? row_for(*note->end) : begin;
        if (!begin.has_value() || !old_end.has_value()) {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "selected note duration lies outside chart geometry",
                                           id)};
        }
        const auto old_duration = *old_end - *begin;
        if (shrinking && delta > old_duration) {
            return diagnostic_list_t{
                error(diagnostic_code_t::invalid_hold,
                      "duration delta would move a selected tail before its head", id)};
        }
        std::uint64_t new_duration{};
        if (shrinking) {
            new_duration = old_duration - delta;
        } else if (delta > std::numeric_limits<std::uint64_t>::max() - old_duration) {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "selected note duration would overflow", id)};
        } else {
            new_duration = old_duration + delta;
        }
        if (new_duration > index.total_rows - *begin) {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "selected note tail would leave chart geometry", id)};
        }
        const auto end = *begin + new_duration;
        const auto end_position =
            new_duration == 0 ? std::optional<local_position_t>{} : position_at(index, end);
        if (new_duration != 0 && !end_position.has_value()) {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "selected note tail cannot be represented in the chart",
                                           id)};
        }
        if (end_position.has_value() &&
            find_active_lane(chart, index, *end_position, note->lane) == nullptr) {
            return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                           "selected note lane is unavailable at its resized tail",
                                           id)};
        }
        plan.resized.push_back({id, note->lane, *begin, end, end_position});
        collision_intervals.push_back({note->lane, *begin, end, id, true});
    }

    std::ranges::sort(collision_intervals, [](const auto& left, const auto& right) {
        if (left.lane != right.lane) {
            return left.lane.value < right.lane.value;
        }
        if (left.begin != right.begin) {
            return left.begin < right.begin;
        }
        if (left.end != right.end) {
            return left.end < right.end;
        }
        return left.resizing < right.resizing;
    });
    using active_collision_queue_t =
        std::priority_queue<active_duration_collision_t, std::vector<active_duration_collision_t>,
                            earliest_duration_collision_t>;
    auto active_resized = active_collision_queue_t{};
    auto active_existing = active_collision_queue_t{};
    auto active_lane_id = lane_id_t{};
    auto has_active_lane = false;
    for (const auto& interval : collision_intervals) {
        if (!has_active_lane || interval.lane != active_lane_id) {
            active_resized = active_collision_queue_t{};
            active_existing = active_collision_queue_t{};
            active_lane_id = interval.lane;
            has_active_lane = true;
        }
        while (!active_resized.empty() && active_resized.top().end < interval.begin) {
            active_resized.pop();
        }
        while (!active_existing.empty() && active_existing.top().end < interval.begin) {
            active_existing.pop();
        }
        if (interval.resizing) {
            if (!active_resized.empty()) {
                return diagnostic_list_t{error(diagnostic_code_t::collision,
                                               "resized selected notes overlap on one lane",
                                               interval.note)};
            }
            if (!active_existing.empty()) {
                return diagnostic_list_t{error(diagnostic_code_t::collision,
                                               "resized note overlaps an unselected same-lane note",
                                               active_existing.top().note)};
            }
            active_resized.push({interval.end, interval.note});
        } else {
            if (!active_resized.empty()) {
                return diagnostic_list_t{error(diagnostic_code_t::collision,
                                               "resized note overlaps an unselected same-lane note",
                                               interval.note)};
            }
            active_existing.push({interval.end, interval.note});
        }
    }
    return plan;
}

auto duration_previews(const duration_resize_plan_t& plan) -> std::vector<note_duration_preview_t> {
    std::vector<note_duration_preview_t> result;
    result.reserve(plan.resized.size());
    for (const auto& note : plan.resized) {
        result.push_back(
            {note.id, note.end_position ? note_kind_t::hold : note_kind_t::tap, note.end_position});
    }
    return result;
}

auto diagnostic_result(const document_history_t& history, const editor_interaction_state_t& state,
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

auto make_editor_interaction(const document_history_t& history, chart_id_t chart_id,
                             lane_id_t lane_id, local_position_t cursor,
                             std::span<const std::uint64_t> ordered_snap_denominators,
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
    const auto snap =
        filter_snap_denominators(chart->divisions_per_standard_measure, ordered_snap_denominators,
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
    if (const auto* cancel = std::get_if<cancel_editor_gesture_action_t>(&action)) {
        switch (cancel->reason) {
        case editor_gesture_cancel_reason_t::escape:
        case editor_gesture_cancel_reason_t::focus_lost:
        case editor_gesture_cancel_reason_t::modifier_released:
        case editor_gesture_cancel_reason_t::pointer_capture_lost:
            break;
        default:
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "unknown editor gesture cancellation reason")});
        }
        state.pending_hold.reset();
        state.pending_note_duration_gesture.reset();
        const auto recovered_view = make_view(history, state);
        return {
            .accepted = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(recovered_view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(recovered_view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(recovered_view)
                               ? std::get<diagnostic_list_t>(recovered_view)
                               : diagnostic_list_t{},
        };
    }
    if (std::holds_alternative<cancel_hold_action_t>(action)) {
        state.pending_hold.reset();
        const auto recovered_view = make_view(history, state);
        return {
            .accepted = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(recovered_view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(recovered_view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(recovered_view)
                               ? std::get<diagnostic_list_t>(recovered_view)
                               : diagnostic_list_t{},
        };
    }
    if (state.pending_hold.has_value() && state.pending_hold->gesture_source.has_value()) {
        const auto source = *state.pending_hold->gesture_source;
        const auto keyboard_action =
            source == hold_gesture_source_t::keyboard_lane &&
            (std::holds_alternative<move_edit_cursor_action_t>(action) ||
             std::holds_alternative<release_lane_key_hold_gesture_action_t>(action));
        const auto pointer_action =
            source == hold_gesture_source_t::alt_pointer &&
            (std::holds_alternative<update_alt_pointer_hold_gesture_action_t>(action) ||
             std::holds_alternative<release_alt_pointer_hold_gesture_action_t>(action));
        if (!keyboard_action && !pointer_action) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "finish or cancel the active hold input gesture first")});
        }
    }
    if (state.pending_note_duration_gesture.has_value() &&
        !std::holds_alternative<update_note_duration_gesture_action_t>(action) &&
        !std::holds_alternative<release_note_duration_gesture_action_t>(action)) {
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active note duration gesture first")});
    }
    const auto initial_view = make_view(history, state);
    if (std::holds_alternative<diagnostic_list_t>(initial_view)) {
        return diagnostic_result(history, state, std::get<diagnostic_list_t>(initial_view));
    }
    const auto* chart = find_chart(history.document, state.chart);
    const auto built_index = build_measure_index(*chart);
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = *absolute_row(index, state.cursor);

    const auto make_duration_plan = [&](std::vector<object_id_t> targets, object_id_t reference,
                                        local_position_t end, bool exact_end = false) {
        return plan_duration_resize(*chart, index, std::move(targets), reference, end,
                                    state.active_snap_denominator, exact_end);
    };
    const auto apply_duration_plan = [&](const duration_resize_plan_t& plan) {
        auto proposed = history.document;
        auto* proposed_chart = find_chart(proposed, state.chart);
        auto changed = false;
        for (const auto& candidate : plan.resized) {
            auto* note = find_note(*proposed_chart, candidate.id);
            const auto kind = candidate.end_position ? note_kind_t::hold : note_kind_t::tap;
            changed = changed || note->kind != kind || note->end != candidate.end_position;
            note->kind = kind;
            note->end = candidate.end_position;
        }
        if (changed) {
            auto command = commit_document_command(
                history, std::move(proposed),
                plan.notes.size() == 1 ? "Resize note duration" : "Resize selected note durations");
            if (!command) {
                return diagnostic_result(history, state, std::move(command.diagnostics));
            }
        }
        state.pending_note_duration_gesture.reset();
        state.selected_notes = plan.notes;
        state.active_lane = plan.reference_lane;
        state.cursor = plan.reference_end;
        const auto next_view = make_view(history, state);
        return editor_interaction_result_t{
            .accepted = true,
            .document_changed = changed,
            .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                               ? std::get<diagnostic_list_t>(next_view)
                               : diagnostic_list_t{},
        };
    };

    const auto begin_hold_gesture = [&](lane_id_t lane, local_position_t position,
                                        hold_gesture_source_t source) {
        if (state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "another editor gesture is already active")});
        }
        const auto before = state;
        state.active_lane = lane;
        auto result = dispatch_editor_action(history, state, begin_hold_action_t{position});
        if (!result) {
            state = before;
            const auto view = make_view(history, state);
            result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                              ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                              : std::nullopt;
            return result;
        }
        if (state.pending_hold.has_value()) {
            state.pending_hold->gesture_source = source;
        }
        const auto view = make_view(history, state);
        result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                          ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                          : std::nullopt;
        result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                                 ? std::get<diagnostic_list_t>(view)
                                 : diagnostic_list_t{};
        return result;
    };
    const auto update_hold_gesture = [&](hold_gesture_source_t source, local_position_t position) {
        if (!state.pending_hold.has_value() || state.pending_hold->gesture_source != source) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "hold gesture update does not match the active input")});
        }
        const auto pending = *state.pending_hold;
        if (pending.document_revision != history.document.revision) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "pending hold gesture is stale after a document edit")});
        }
        auto target = snapped_position(*chart, index, position, state.active_snap_denominator);
        if (target.has_value()) {
            const auto target_row = absolute_row(index, *target);
            const auto start_row = absolute_row(index, pending.position);
            if (target_row.has_value() && start_row.has_value() && *target_row < *start_row) {
                target = pending.position;
            }
        }
        state.pending_hold->gesture_source.reset();
        auto result = dispatch_editor_action(
            history, state, update_hold_preview_action_t{target.value_or(position)});
        if (state.pending_hold.has_value()) {
            state.pending_hold->gesture_source = source;
        }
        if (result) {
            const auto view = make_view(history, state);
            result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                              ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                              : std::nullopt;
            result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                                     ? std::get<diagnostic_list_t>(view)
                                     : diagnostic_list_t{};
        }
        return result;
    };
    const auto release_hold_gesture = [&](hold_gesture_source_t source, local_position_t position) {
        if (!state.pending_hold.has_value() || state.pending_hold->gesture_source != source) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "hold gesture release does not match the active input")});
        }
        const auto pending = *state.pending_hold;
        auto target = snapped_position(*chart, index, position, state.active_snap_denominator);
        if (target.has_value()) {
            const auto target_row = absolute_row(index, *target);
            const auto start_row = absolute_row(index, pending.position);
            if (target_row.has_value() && start_row.has_value() && *target_row < *start_row) {
                target = pending.position;
            }
        }
        state.pending_hold->gesture_source.reset();
        auto result =
            dispatch_editor_action(history, state, commit_hold_action_t{target.value_or(position)});
        if (state.pending_hold.has_value()) {
            state.pending_hold->gesture_source = source;
        }
        if (result) {
            const auto view = make_view(history, state);
            result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                              ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                              : std::nullopt;
            result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                                     ? std::get<diagnostic_list_t>(view)
                                     : diagnostic_list_t{};
        }
        return result;
    };
    const auto begin_duration_gesture = [&](object_id_t reference_id) {
        if (state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "another editor gesture is already active")});
        }
        const auto* reference = find_note(*chart, reference_id);
        if (reference == nullptr ||
            (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold)) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id,
                       "duration gesture requires a tap or hold reference note", reference_id)});
        }
        auto targets = state.selected_notes;
        if (std::ranges::find(targets, reference_id) == targets.end()) {
            targets = {reference_id};
        }
        const auto initial_end = reference->end.value_or(reference->position);
        auto planned = make_duration_plan(targets, reference_id, initial_end, true);
        if (std::holds_alternative<diagnostic_list_t>(planned)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(planned)));
        }
        const auto& plan = std::get<duration_resize_plan_t>(planned);
        state.pending_note_duration_gesture = pending_note_duration_gesture_t{
            .reference_note = reference_id,
            .document_revision = history.document.revision,
            .notes = plan.notes,
            .preview_end = plan.reference_end,
            .preview = duration_previews(plan),
        };
        const auto view = make_view(history, state);
        return editor_interaction_result_t{
            .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
        };
    };
    const auto update_duration_gesture = [&](local_position_t requested_end) {
        if (!state.pending_note_duration_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "duration gesture update requires an active gesture")});
        }
        const auto pending = *state.pending_note_duration_gesture;
        if (pending.document_revision != history.document.revision) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "duration gesture is stale after a document edit")});
        }
        const auto* reference = find_note(*chart, pending.reference_note);
        if (reference == nullptr) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "duration gesture reference note no longer exists",
                                            pending.reference_note)});
        }
        auto snapped_end =
            snapped_position(*chart, index, requested_end, state.active_snap_denominator);
        if (!snapped_end.has_value()) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::row_out_of_range,
                       "duration gesture end cannot be snapped inside chart")});
        }
        const auto head = absolute_row(index, reference->position);
        const auto target = absolute_row(index, *snapped_end);
        auto exact_head = false;
        if (head.has_value() && target.has_value() && *target < *head) {
            snapped_end = reference->position;
            exact_head = true;
        }
        auto planned =
            make_duration_plan(pending.notes, pending.reference_note, *snapped_end, exact_head);
        if (std::holds_alternative<diagnostic_list_t>(planned)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(planned)));
        }
        const auto& plan = std::get<duration_resize_plan_t>(planned);
        state.pending_note_duration_gesture->preview_end = plan.reference_end;
        state.pending_note_duration_gesture->preview = duration_previews(plan);
        const auto view = make_view(history, state);
        return editor_interaction_result_t{
            .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
        };
    };
    const auto release_duration_gesture = [&](local_position_t requested_end) {
        const auto updated = update_duration_gesture(requested_end);
        if (!updated) {
            return updated;
        }
        const auto pending = *state.pending_note_duration_gesture;
        auto planned =
            make_duration_plan(pending.notes, pending.reference_note, pending.preview_end, true);
        if (std::holds_alternative<diagnostic_list_t>(planned)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(planned)));
        }
        return apply_duration_plan(std::get<duration_resize_plan_t>(planned));
    };

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
                const auto step =
                    chart->divisions_per_standard_measure / state.active_snap_denominator;
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
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::row_out_of_range,
                                                    "cursor movement would leave chart geometry")});
                }
                if (state.pending_hold.has_value() &&
                    state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane) {
                    return update_hold_gesture(hold_gesture_source_t::keyboard_lane, *next);
                }
                auto proposed_state = state;
                proposed_state.cursor = *next;
                auto next_view = make_view(history, proposed_state);
                if (std::holds_alternative<diagnostic_list_t>(next_view)) {
                    const auto replacement =
                        transition_lane(*chart, index, state.cursor, *next, state.active_lane);
                    if (replacement.has_value()) {
                        proposed_state.active_lane = *replacement;
                        next_view = make_view(history, proposed_state);
                    }
                }
                if (std::holds_alternative<diagnostic_list_t>(next_view)) {
                    return diagnostic_result(history, state,
                                             std::get<diagnostic_list_t>(next_view));
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
                    const auto size =
                        static_cast<std::ptrdiff_t>(state.supported_snap_denominators.size());
                    const auto index =
                        std::distance(state.supported_snap_denominators.begin(), found);
                    const auto delta =
                        typed_action.direction == interaction_direction_t::forward ? 1 : -1;
                    const auto next = (index + delta + size) % size;
                    const auto next_denominator =
                        state.supported_snap_denominators[static_cast<std::size_t>(next)];
                    if (next_denominator != state.active_snap_denominator) {
                        state.active_snap_denominator = next_denominator;
                        state.active_snap_was_fallback = false;
                    }
                }
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
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
                const auto target_measure =
                    std::ranges::find_if(proposed_chart->measures, [&](const auto& measure) {
                        return measure.id == snapped->measure;
                    });
                if (target_measure == proposed_chart->measures.end()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_measure,
                               "snapped note position refers to an unknown measure")});
                }
                const auto existing =
                    std::ranges::find_if(target_measure->notes, [&](const auto& note) {
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
                        if (begin.has_value() && end.has_value() && target_absolute >= *begin &&
                            target_absolute <= *end) {
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
                        return diagnostic_result(history, state,
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
                auto command =
                    commit_document_command(history, std::move(proposed), std::move(label));
                if (!command) {
                    return diagnostic_result(history, state, std::move(command.diagnostics));
                }
                state.cursor = *snapped;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = true,
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, begin_hold_action_t>) {
                const auto snapped = snapped_position(*chart, index, typed_action.position,
                                                      state.active_snap_denominator);
                if (!snapped.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "hold start cannot be snapped inside chart geometry")});
                }
                if (find_active_lane(*chart, index, *snapped, state.active_lane) == nullptr) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_lane,
                               "active editor lane is unavailable at the hold start")});
                }
                if (const auto collision =
                        colliding_note(*chart, index, state.active_lane, *snapped, *snapped)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::collision, "hold start overlaps a same-lane note",
                               *collision)});
                }
                state.pending_hold = pending_hold_t{
                    .position = *snapped,
                    .lane = state.active_lane,
                    .document_revision = history.document.revision,
                };
                state.cursor = *snapped;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, update_hold_preview_action_t>) {
                if (!state.pending_hold.has_value()) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_hold,
                                                    "hold preview requires a pending hold start")});
                }
                const auto pending = *state.pending_hold;
                if (pending.document_revision != history.document.revision ||
                    snapped_position(*chart, index, pending.position,
                                     state.active_snap_denominator) != pending.position) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "pending hold start is stale after a document or snap change")});
                }
                const auto snapped = snapped_position(*chart, index, typed_action.position,
                                                      state.active_snap_denominator);
                if (!snapped.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "hold preview cannot be snapped inside chart geometry")});
                }
                const auto begin_row = absolute_row(index, pending.position);
                const auto end_row = absolute_row(index, *snapped);
                if (!begin_row.has_value() || !end_row.has_value() || *end_row < *begin_row) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "hold preview cannot reverse before its start")});
                }
                if (*end_row > *begin_row) {
                    if (find_active_lane(*chart, index, *snapped, pending.lane) == nullptr) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::unknown_lane,
                                   "hold lane is unavailable at the previewed end")});
                    }
                    if (const auto collision = colliding_note(*chart, index, pending.lane,
                                                              pending.position, *snapped)) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::collision,
                                   "hold preview overlaps a same-lane note", *collision)});
                    }
                    state.pending_hold->preview_end = *snapped;
                } else {
                    state.pending_hold->preview_end.reset();
                }
                state.cursor = *snapped;
                state.active_lane = pending.lane;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, commit_hold_action_t>) {
                if (!state.pending_hold.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "hold completion requires a pending hold start")});
                }
                const auto pending = *state.pending_hold;
                if (pending.document_revision != history.document.revision ||
                    snapped_position(*chart, index, pending.position,
                                     state.active_snap_denominator) != pending.position) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "pending hold start is stale after a document or snap change")});
                }
                const auto snapped = snapped_position(*chart, index, typed_action.position,
                                                      state.active_snap_denominator);
                if (!snapped.has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "hold end cannot be snapped inside chart geometry")});
                }
                const auto begin_row = absolute_row(index, pending.position);
                const auto end_row = absolute_row(index, *snapped);
                if (!begin_row.has_value() || !end_row.has_value() || *end_row < *begin_row) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_hold,
                                                    "hold end cannot be before its start")});
                }
                if (find_active_lane(*chart, index, *snapped, pending.lane) == nullptr) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::unknown_lane,
                               "hold lane is unavailable at the snapped end")});
                }
                if (const auto collision =
                        colliding_note(*chart, index, pending.lane, pending.position, *snapped)) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::collision,
                                                    "hold overlaps a same-lane note", *collision)});
                }
                auto proposed = history.document;
                auto* proposed_chart = find_chart(proposed, state.chart);
                const auto target_measure =
                    std::ranges::find_if(proposed_chart->measures, [&](const auto& measure) {
                        return measure.id == pending.position.measure;
                    });
                if (target_measure == proposed_chart->measures.end()) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::unknown_measure,
                                                    "hold start refers to an unknown measure")});
                }
                const auto id = next_object_id(*proposed_chart);
                if (!id.has_value()) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_id,
                                                    "chart object ID sequence is exhausted")});
                }
                target_measure->notes.push_back({
                    .id = *id,
                    .position = pending.position,
                    .lane = pending.lane,
                    .kind = *end_row == *begin_row ? note_kind_t::tap : note_kind_t::hold,
                    .end = *end_row == *begin_row ? std::optional<local_position_t>{}
                                                  : std::optional{*snapped},
                });
                auto command = commit_document_command(history, std::move(proposed),
                                                       *end_row == *begin_row ? "Place tap note"
                                                                              : "Place hold note");
                if (!command) {
                    return diagnostic_result(history, state, std::move(command.diagnostics));
                }
                state.pending_hold.reset();
                state.cursor = *snapped;
                state.active_lane = pending.lane;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = true,
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, cancel_hold_action_t>) {
                return {};
            } else if constexpr (std::is_same_v<action_t, begin_lane_key_hold_gesture_action_t>) {
                return begin_hold_gesture(typed_action.lane, state.cursor,
                                          hold_gesture_source_t::keyboard_lane);
            } else if constexpr (std::is_same_v<action_t, release_lane_key_hold_gesture_action_t>) {
                const auto position =
                    state.pending_hold.has_value()
                        ? state.pending_hold->preview_end.value_or(state.pending_hold->position)
                        : state.cursor;
                return release_hold_gesture(hold_gesture_source_t::keyboard_lane, position);
            } else if constexpr (std::is_same_v<action_t,
                                                begin_alt_pointer_hold_gesture_action_t>) {
                if (!typed_action.alt_or_option_down) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "Alt/Option must be held to begin a pointer hold gesture")});
                }
                return begin_hold_gesture(typed_action.lane, typed_action.position,
                                          hold_gesture_source_t::alt_pointer);
            } else if constexpr (std::is_same_v<action_t,
                                                update_alt_pointer_hold_gesture_action_t>) {
                if (!typed_action.pointer_captured) {
                    return dispatch_editor_action(
                        history, state,
                        cancel_editor_gesture_action_t{
                            editor_gesture_cancel_reason_t::pointer_capture_lost});
                }
                if (!typed_action.alt_or_option_down) {
                    return dispatch_editor_action(
                        history, state,
                        cancel_editor_gesture_action_t{
                            editor_gesture_cancel_reason_t::modifier_released});
                }
                return update_hold_gesture(hold_gesture_source_t::alt_pointer,
                                           typed_action.position);
            } else if constexpr (std::is_same_v<action_t,
                                                release_alt_pointer_hold_gesture_action_t>) {
                if (!typed_action.pointer_captured) {
                    return dispatch_editor_action(
                        history, state,
                        cancel_editor_gesture_action_t{
                            editor_gesture_cancel_reason_t::pointer_capture_lost});
                }
                if (!typed_action.alt_or_option_down) {
                    return dispatch_editor_action(
                        history, state,
                        cancel_editor_gesture_action_t{
                            editor_gesture_cancel_reason_t::modifier_released});
                }
                return release_hold_gesture(hold_gesture_source_t::alt_pointer,
                                            typed_action.position);
            } else if constexpr (std::is_same_v<action_t, begin_note_duration_gesture_action_t>) {
                return begin_duration_gesture(typed_action.reference_note);
            } else if constexpr (std::is_same_v<action_t, update_note_duration_gesture_action_t>) {
                return update_duration_gesture(typed_action.reference_end);
            } else if constexpr (std::is_same_v<action_t, release_note_duration_gesture_action_t>) {
                return release_duration_gesture(typed_action.reference_end);
            } else if constexpr (std::is_same_v<action_t, cancel_editor_gesture_action_t>) {
                return {};
            } else if constexpr (std::is_same_v<action_t, select_editor_notes_action_t>) {
                const auto available_ids = chart_note_ids(*chart);
                std::unordered_set<std::uint64_t> selected_ids;
                selected_ids.reserve(typed_action.notes.size());
                for (const auto id : typed_action.notes) {
                    if (id.value == 0 || !available_ids.contains(id.value)) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_id,
                                   "note selection contains an unknown note ID", id)});
                    }
                    if (!selected_ids.insert(id.value).second) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::duplicate_id,
                                   "note selection contains a duplicate note ID", id)});
                    }
                }
                state.selected_notes = typed_action.notes;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, delete_editor_notes_action_t>) {
                std::unordered_set<std::uint64_t> selected_ids;
                if (!state.selected_notes.empty()) {
                    const auto available_ids = chart_note_ids(*chart);
                    selected_ids.reserve(state.selected_notes.size());
                    for (const auto id : state.selected_notes) {
                        if (!available_ids.contains(id.value)) {
                            return diagnostic_result(history, state,
                                                     {error(diagnostic_code_t::invalid_id,
                                                            "selected note no longer exists", id)});
                        }
                        if (!selected_ids.insert(id.value).second) {
                            return diagnostic_result(
                                history, state,
                                {error(diagnostic_code_t::duplicate_id,
                                       "selected note list contains a duplicate", id)});
                        }
                    }
                }
                auto proposed = history.document;
                auto* proposed_chart = find_chart(proposed, state.chart);
                std::size_t removed{};
                for (auto& measure : proposed_chart->measures) {
                    if (state.selected_notes.empty()) {
                        removed += std::erase_if(measure.notes, [&](const auto& note) {
                            return note.position == state.cursor && note.lane == state.active_lane;
                        });
                    } else {
                        removed += std::erase_if(measure.notes, [&](const auto& note) {
                            return selected_ids.contains(note.id.value);
                        });
                    }
                }
                if (removed == 0) {
                    return {
                        .accepted = true,
                        .view = std::get<editor_interaction_view_model_t>(initial_view),
                    };
                }
                if (!state.selected_notes.empty() && removed != state.selected_notes.size()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "selected note deletion did not resolve every note")});
                }
                auto command = commit_document_command(
                    history, std::move(proposed),
                    state.selected_notes.empty() ? "Delete note" : "Delete selected notes");
                if (!command) {
                    return diagnostic_result(history, state, std::move(command.diagnostics));
                }
                state.selected_notes.clear();
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = true,
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, resize_note_durations_action_t>) {
                const auto* reference = find_note(*chart, typed_action.reference_note);
                if (reference == nullptr ||
                    (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold)) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "duration resize requires a tap or hold reference note",
                               typed_action.reference_note)});
                }
                const auto reference_lane = reference->lane;
                const auto snapped = snapped_position(*chart, index, typed_action.reference_end,
                                                      state.active_snap_denominator);
                const auto reference_begin = absolute_row(index, reference->position);
                const auto reference_old_end =
                    reference->end ? absolute_row(index, *reference->end) : reference_begin;
                const auto reference_new_end =
                    snapped ? absolute_row(index, *snapped) : std::nullopt;
                if (!snapped.has_value() || !reference_begin.has_value() ||
                    !reference_old_end.has_value() || !reference_new_end.has_value() ||
                    *reference_new_end < *reference_begin) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "reference duration cannot end before its note head",
                               reference->id)});
                }
                const auto old_reference_duration = *reference_old_end - *reference_begin;
                const auto new_reference_duration = *reference_new_end - *reference_begin;
                const auto shrinking = new_reference_duration < old_reference_duration;
                const auto delta = shrinking ? old_reference_duration - new_reference_duration
                                             : new_reference_duration - old_reference_duration;

                auto targets = state.selected_notes;
                if (std::ranges::find(targets, reference->id) == targets.end()) {
                    targets = {reference->id};
                }
                struct resized_note_t {
                    object_id_t id;
                    lane_id_t lane;
                    std::uint64_t begin{};
                    std::uint64_t end{};
                    std::optional<local_position_t> end_position;
                };
                std::vector<resized_note_t> resized;
                resized.reserve(targets.size());
                for (const auto id : targets) {
                    const auto* note = find_note(*chart, id);
                    if (note == nullptr ||
                        (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold)) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_hold,
                                   "selected duration resize contains a non-tap/hold note", id)});
                    }
                    const auto begin = absolute_row(index, note->position);
                    const auto old_end = note->end ? absolute_row(index, *note->end) : begin;
                    if (!begin.has_value() || !old_end.has_value()) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::row_out_of_range,
                                   "selected note duration lies outside chart geometry", id)});
                    }
                    const auto old_duration = *old_end - *begin;
                    if (shrinking && delta > old_duration) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_hold,
                                   "duration delta would move a selected tail before its head",
                                   id)});
                    }
                    std::uint64_t new_duration{};
                    if (shrinking) {
                        new_duration = old_duration - delta;
                    } else if (delta > std::numeric_limits<std::uint64_t>::max() - old_duration) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::row_out_of_range,
                                   "selected note duration would overflow", id)});
                    } else {
                        new_duration = old_duration + delta;
                    }
                    if (new_duration > index.total_rows - *begin) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::row_out_of_range,
                                   "selected note tail would leave chart geometry", id)});
                    }
                    const auto end = *begin + new_duration;
                    const auto end_position = new_duration == 0 ? std::optional<local_position_t>{}
                                                                : position_at(index, end);
                    if (new_duration != 0 && !end_position.has_value()) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::row_out_of_range,
                                   "selected note tail cannot be represented in the chart", id)});
                    }
                    if (end_position.has_value() &&
                        find_active_lane(*chart, index, *end_position, note->lane) == nullptr) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::unknown_lane,
                                   "selected note lane is unavailable at its resized tail", id)});
                    }
                    resized.push_back({id, note->lane, *begin, end, end_position});
                }

                const auto is_target = [&](object_id_t id) {
                    return std::ranges::find(targets, id) != targets.end();
                };
                for (const auto& candidate : resized) {
                    for (const auto& measure : chart->measures) {
                        for (const auto& note : measure.notes) {
                            if (is_target(note.id) || note.lane != candidate.lane)
                                continue;
                            const auto begin = absolute_row(index, note.position);
                            const auto end = note.end ? absolute_row(index, *note.end) : begin;
                            if (begin.has_value() && end.has_value() && *begin <= candidate.end &&
                                *end >= candidate.begin) {
                                return diagnostic_result(
                                    history, state,
                                    {error(diagnostic_code_t::collision,
                                           "resized note overlaps an unselected same-lane note",
                                           note.id)});
                            }
                        }
                    }
                }
                for (std::size_t left = 0; left < resized.size(); ++left) {
                    for (std::size_t right = left + 1; right < resized.size(); ++right) {
                        if (resized[left].lane == resized[right].lane &&
                            resized[left].begin <= resized[right].end &&
                            resized[right].begin <= resized[left].end) {
                            return diagnostic_result(
                                history, state,
                                {error(diagnostic_code_t::collision,
                                       "resized selected notes overlap on one lane",
                                       resized[right].id)});
                        }
                    }
                }

                auto proposed = history.document;
                auto* proposed_chart = find_chart(proposed, state.chart);
                auto changed = false;
                for (const auto& candidate : resized) {
                    auto* note = find_note(*proposed_chart, candidate.id);
                    const auto kind = candidate.end_position ? note_kind_t::hold : note_kind_t::tap;
                    changed = changed || note->kind != kind || note->end != candidate.end_position;
                    note->kind = kind;
                    note->end = candidate.end_position;
                }
                if (!changed) {
                    state.selected_notes = targets;
                    state.active_lane = reference_lane;
                    state.cursor = *snapped;
                    const auto next_view = make_view(history, state);
                    return {
                        .accepted = true,
                        .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                    ? std::optional{std::get<editor_interaction_view_model_t>(
                                          next_view)}
                                    : std::nullopt,
                        .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                           ? std::get<diagnostic_list_t>(next_view)
                                           : diagnostic_list_t{},
                    };
                }
                auto command =
                    commit_document_command(history, std::move(proposed),
                                            targets.size() == 1 ? "Resize note duration"
                                                                : "Resize selected note durations");
                if (!command) {
                    return diagnostic_result(history, state, std::move(command.diagnostics));
                }
                state.selected_notes = targets;
                state.active_lane = reference_lane;
                state.cursor = *snapped;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = true,
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
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
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
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
                } else if (typed_action.factor <
                           minimum_pixels_per_standard_measure / current_scale) {
                    next_scale = minimum_pixels_per_standard_measure;
                } else {
                    next_scale = std::clamp(current_scale * typed_action.factor,
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
                    static_cast<double>(chart->divisions_per_standard_measure) / current_scale;
                const auto unclamped_scroll =
                    anchor_absolute_rows * next_scale /
                        static_cast<double>(chart->divisions_per_standard_measure) -
                    anchor_screen_y;
                const auto maximum_scroll = maximum_viewport_scroll(*chart, index, next_scale,
                                                                    state.viewport_height_pixels);
                const auto next_scroll = maximum_scroll.has_value()
                                             ? std::clamp(unclamped_scroll, 0.0, *maximum_scroll)
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
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                };
            } else if constexpr (std::is_same_v<action_t, scroll_editor_view_action_t>) {
                if (!std::isfinite(typed_action.vertical_pixels) ||
                    !std::isfinite(state.viewport_scroll_pixels)) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_resolution,
                                                    "viewport scroll must remain finite")});
                }
                const auto next_scroll =
                    state.viewport_scroll_pixels + typed_action.vertical_pixels;
                if (!std::isfinite(next_scroll)) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_resolution,
                                                    "viewport scroll would overflow")});
                }
                const auto maximum_scroll = maximum_viewport_scroll(
                    *chart, index, state.pixels_per_standard_measure, state.viewport_height_pixels);
                if (!maximum_scroll.has_value()) {
                    return diagnostic_result(history, state,
                                             {error(diagnostic_code_t::invalid_resolution,
                                                    "editor viewport geometry is invalid")});
                }
                state.viewport_scroll_pixels = std::clamp(next_scroll, 0.0, *maximum_scroll);
                const auto next_view = make_view(history, state);
                return {
                    .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
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
