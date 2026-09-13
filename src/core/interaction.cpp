#include <wormedit/core/interaction.h>

#include <wormedit/core/rational.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <ranges>
#include <set>
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

auto rational_label(signed_rational_t value) -> std::string {
    return std::to_string(value.numerator) +
           (value.denominator == 1 ? std::string{} : "/" + std::to_string(value.denominator));
}

auto position_label(local_position_t position) -> std::string {
    return std::to_string(position.measure.value) + ":" + std::to_string(position.row);
}

auto note_kind_label(note_kind_t kind) -> std::string_view {
    switch (kind) {
    case note_kind_t::tap:
        return "Tap";
    case note_kind_t::hold:
        return "Hold";
    case note_kind_t::roll:
        return "Roll";
    case note_kind_t::invisible:
        return "Invisible note";
    case note_kind_t::mine:
        return "Mine";
    case note_kind_t::lift:
        return "Lift";
    case note_kind_t::fake:
        return "Fake note";
    }
    return "Note";
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

auto scope_accepts(editor_selection_scope_t scope, editor_selection_object_kind_t kind) -> bool {
    switch (kind) {
    case editor_selection_object_kind_t::note:
        return scope.notes;
    case editor_selection_object_kind_t::bgm_event:
        return scope.bgm;
    case editor_selection_object_kind_t::bga_event:
        return scope.bga;
    case editor_selection_object_kind_t::bpm_event:
    case editor_selection_object_kind_t::stop_event:
    case editor_selection_object_kind_t::delay_event:
    case editor_selection_object_kind_t::warp_event:
    case editor_selection_object_kind_t::scroll_event:
    case editor_selection_object_kind_t::speed_event:
    case editor_selection_object_kind_t::kiai_event:
    case editor_selection_object_kind_t::timing_anchor:
    case editor_selection_object_kind_t::bookmark:
    case editor_selection_object_kind_t::section:
        return scope.timing;
    }
    return false;
}

auto notes_from_objects(const std::vector<editor_selection_object_t>& objects)
    -> std::vector<object_id_t> {
    std::vector<object_id_t> notes;
    for (const auto& object : objects) {
        if (object.kind == editor_selection_object_kind_t::note)
            notes.push_back(object.id);
    }
    return notes;
}

auto set_selected_objects(editor_interaction_state_t& state,
                          std::vector<editor_selection_object_t> objects) -> void {
    state.selected_notes = notes_from_objects(objects);
    state.selected_objects = std::move(objects);
}

auto set_selected_notes(editor_interaction_state_t& state, std::vector<object_id_t> notes) -> void {
    std::vector<editor_selection_object_t> objects;
    objects.reserve(notes.size());
    for (const auto id : notes)
        objects.push_back({editor_selection_object_kind_t::note, id});
    state.selected_notes = std::move(notes);
    state.selected_objects = std::move(objects);
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

auto absolute_row_at_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels)
    -> std::optional<std::uint64_t> {
    if (view.chart != chart.id || !std::isfinite(y_pixels) || y_pixels < 0.0 ||
        y_pixels >= view.viewport_height_pixels ||
        !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels) || chart.divisions_per_standard_measure == 0 ||
        index.total_rows == 0) {
        return std::nullopt;
    }
    const auto row = (static_cast<long double>(y_pixels) + view.viewport_scroll_pixels) *
                     static_cast<long double>(chart.divisions_per_standard_measure) /
                     static_cast<long double>(view.pixels_per_standard_measure);
    if (!std::isfinite(row) || row < 0.0L || row >= static_cast<long double>(index.total_rows)) {
        return std::nullopt;
    }
    // Pointer positions are continuous; map to the nearest stored row and make
    // exact half-row ties stable by resolving them toward the earlier row.
    const auto nearest = std::floor(row + 0.5L);
    if (nearest >= static_cast<long double>(index.total_rows)) {
        return index.total_rows - 1;
    }
    return static_cast<std::uint64_t>(nearest);
}

auto position_at_row_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels)
    -> std::optional<local_position_t> {
    const auto row = absolute_row_at_y_pixels(chart, index, view, y_pixels);
    return row ? position_at(index, *row) : std::nullopt;
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

    std::optional<space_range_selection_preview_t> space_range_preview;
    if (state.pending_space_range_selection.has_value()) {
        const auto& pending = *state.pending_space_range_selection;
        if (pending.document_revision != history.document.revision ||
            pending.hit_index == nullptr ||
            pending.hit_index->document_revision != history.document.revision ||
            pending.hit_index->chart != chart->id) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           "Space range selection is stale after a document edit")};
        }
        const auto anchor_row = absolute_row(index, pending.anchor);
        if (!anchor_row.has_value()) {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "Space range anchor lies outside chart geometry")};
        }
        const auto first_row = std::min(*anchor_row, *absolute);
        const auto last_row = std::max(*anchor_row, *absolute);
        auto queried = editor_objects_in_row_range(*chart, *pending.hit_index, first_row, last_row,
                                                   pending.scope);
        if (std::holds_alternative<diagnostic_list_t>(queried)) {
            return std::get<diagnostic_list_t>(std::move(queried));
        }
        auto objects = std::get<std::vector<editor_selection_object_t>>(std::move(queried));
        auto notes = notes_from_objects(objects);
        space_range_preview = space_range_selection_preview_t{
            .document_revision = pending.document_revision,
            .anchor = pending.anchor,
            .cursor = state.cursor,
            .first_row = first_row,
            .last_row = last_row,
            .mode = pending.mode,
            .notes = std::make_shared<const std::vector<object_id_t>>(std::move(notes)),
            .objects =
                std::make_shared<const std::vector<editor_selection_object_t>>(std::move(objects)),
        };
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
        .pending_note_paint_gesture =
            state.pending_note_paint_gesture
                ? std::optional{note_paint_gesture_preview_t{
                      .document_revision = state.pending_note_paint_gesture->document_revision,
                      .targets = state.pending_note_paint_gesture->targets,
                  }}
                : std::nullopt,
        .pending_editor_area_selection =
            state.pending_editor_area_selection
                ? std::optional{editor_area_selection_preview_t{
                      .document_revision = state.pending_editor_area_selection->document_revision,
                      .left_pixels =
                          std::min(state.pending_editor_area_selection->anchor_x_pixels,
                                   state.pending_editor_area_selection->current_x_pixels),
                      .top_pixels = std::min(state.pending_editor_area_selection->anchor_y_pixels,
                                             state.pending_editor_area_selection->current_y_pixels),
                      .right_pixels =
                          std::max(state.pending_editor_area_selection->anchor_x_pixels,
                                   state.pending_editor_area_selection->current_x_pixels),
                      .bottom_pixels =
                          std::max(state.pending_editor_area_selection->anchor_y_pixels,
                                   state.pending_editor_area_selection->current_y_pixels),
                      .notes = state.pending_editor_area_selection->notes,
                      .objects = state.pending_editor_area_selection->objects,
                  }}
                : std::nullopt,
        .pending_space_range_selection = std::move(space_range_preview),
        .selected_notes = state.selected_notes,
        .selected_objects = state.selected_objects,
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

struct note_paint_plan_t {
    std::vector<note_paint_target_t> additions;
    std::vector<object_id_t> removals;
};

auto plan_note_paint(const chart_document_t& chart, const measure_index_t& index,
                     std::span<const note_paint_target_t> targets)
    -> std::variant<note_paint_plan_t, diagnostic_list_t> {
    struct interval_t {
        lane_id_t lane;
        std::uint64_t begin{};
        std::uint64_t end{};
        object_id_t note;
        note_kind_t kind{note_kind_t::tap};
        bool has_end{};
    };
    std::vector<interval_t> intervals;
    std::size_t note_count{};
    for (const auto& measure : chart.measures) {
        note_count += measure.notes.size();
    }
    intervals.reserve(note_count);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto begin = absolute_row(index, note.position);
            const auto end = note.end ? absolute_row(index, *note.end) : begin;
            if (begin.has_value() && end.has_value()) {
                intervals.push_back(
                    {note.lane, *begin, *end, note.id, note.kind, note.end.has_value()});
            }
        }
    }
    std::ranges::sort(intervals, [](const auto& left, const auto& right) {
        if (left.lane != right.lane) {
            return left.lane.value < right.lane.value;
        }
        if (left.begin != right.begin) {
            return left.begin < right.begin;
        }
        if (left.end != right.end) {
            return left.end < right.end;
        }
        return left.note.value < right.note.value;
    });
    struct paint_row_t {
        lane_id_t lane;
        std::uint64_t row{};
        note_paint_target_t target;
    };
    std::vector<paint_row_t> paint_rows;
    paint_rows.reserve(targets.size());
    for (const auto& target : targets) {
        const auto row = absolute_row(index, target.position);
        if (row.has_value()) {
            paint_rows.push_back({target.lane, *row, target});
        } else {
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "paint target lies outside chart geometry")};
        }
    }
    std::ranges::sort(paint_rows, [](const auto& left, const auto& right) {
        return left.lane != right.lane ? left.lane.value < right.lane.value : left.row < right.row;
    });

    using interval_end_t = std::pair<std::uint64_t, std::uint64_t>;
    std::priority_queue<interval_end_t, std::vector<interval_end_t>, std::greater<>> ending;
    std::map<std::uint64_t, interval_t> active_notes;
    auto interval = intervals.begin();
    std::optional<lane_id_t> active_lane;
    note_paint_plan_t plan;
    plan.additions.reserve(paint_rows.size());
    plan.removals.reserve(paint_rows.size());
    for (const auto& target : paint_rows) {
        const auto lane = target.lane;
        const auto row = target.row;
        if (!active_lane.has_value() || *active_lane != lane) {
            while (interval != intervals.end() && interval->lane.value < lane.value) {
                ++interval;
            }
            ending = {};
            active_notes.clear();
            active_lane = lane;
        }
        while (interval != intervals.end() && interval->lane == lane && interval->begin <= row) {
            ending.emplace(interval->end, interval->note.value);
            active_notes.emplace(interval->note.value, *interval);
            ++interval;
        }
        while (!ending.empty() && ending.top().first < row) {
            active_notes.erase(ending.top().second);
            ending.pop();
        }
        if (active_notes.empty()) {
            plan.additions.push_back(target.target);
        } else if (active_notes.size() == 1) {
            const auto& [id, note] = *active_notes.begin();
            if (note.kind == note_kind_t::tap && !note.has_end && note.begin == row &&
                note.end == row) {
                plan.removals.push_back(object_id_t{id});
            } else {
                return diagnostic_list_t{error(diagnostic_code_t::collision,
                                               "paint gesture overlaps a non-tap note", note.note)};
            }
        } else {
            return diagnostic_list_t{error(diagnostic_code_t::collision,
                                           "paint gesture overlaps multiple same-lane notes",
                                           active_notes.begin()->second.note)};
        }
    }
    return plan;
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

auto paint_cell_hash_t::operator()(paint_cell_key_t cell) const noexcept -> std::size_t {
    const auto lane_hash = std::hash<std::uint64_t>{}(cell.lane.value);
    const auto row_hash = std::hash<std::uint64_t>{}(cell.absolute_row);
    return lane_hash ^ (row_hash + std::size_t{0x9e3779b9} + (lane_hash << 6U) + (lane_hash >> 2U));
}

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

auto editor_position_at_y_pixels(const chart_document_t& chart,
                                 const editor_interaction_view_model_t& view, double y_pixels)
    -> std::optional<local_position_t> {
    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::nullopt;
    }
    return position_at_row_y_pixels(chart, std::get<measure_index_t>(built_index), view, y_pixels);
}

auto build_editor_hit_test_index(const chart_document_t& chart, std::uint64_t document_revision)
    -> std::variant<editor_hit_test_index_t, diagnostic_list_t> {
    const auto built_index = build_measure_index(chart);
    if (std::holds_alternative<diagnostic_list_t>(built_index)) {
        return std::get<diagnostic_list_t>(built_index);
    }
    auto index = editor_hit_test_index_t{
        .chart = chart.id,
        .document_revision = document_revision,
        .measures = std::get<measure_index_t>(built_index),
    };
    index.measure_buckets.resize(index.measures.entries.size());
    const auto add_selection_span = [&](editor_selection_object_kind_t kind, object_id_t id,
                                        local_position_t begin, local_position_t end,
                                        std::optional<lane_id_t> lane =
                                            std::nullopt) -> std::optional<diagnostic_t> {
        const auto begin_row = absolute_row(index.measures, begin);
        const auto end_row = absolute_row(index.measures, end);
        if (!begin_row.has_value() || !end_row.has_value() || *begin_row > *end_row) {
            return error(diagnostic_code_t::row_out_of_range,
                         "selection object position lies outside chart geometry", id);
        }
        const editor_selection_object_t object{kind, id};
        index.selection_spans.push_back({object, *begin_row, *end_row, lane});
        index.available_objects.push_back(object);
        return std::nullopt;
    };
    const auto add_hover_object = [&](editor_selection_object_kind_t kind, object_id_t id,
                                      local_position_t position, editor_inline_editor_kind_t editor,
                                      std::string tooltip,
                                      std::optional<local_position_t> end_position =
                                          std::nullopt) -> std::optional<diagnostic_t> {
        const auto row = absolute_row(index.measures, position);
        const auto end_row = absolute_row(index.measures, end_position.value_or(position));
        if (!row.has_value() || !end_row.has_value() || *row > *end_row) {
            return error(diagnostic_code_t::row_out_of_range,
                         "hover object position lies outside chart geometry", id);
        }
        index.hover_objects_by_id.push_back({
            .object = {kind, id},
            .editor = editor,
            .position = position,
            .absolute_row = *row,
            .end_absolute_row = *end_row,
            .tooltip = std::move(tooltip),
        });
        return std::nullopt;
    };
    std::unordered_map<std::uint64_t, std::size_t> bucket_by_measure;
    bucket_by_measure.reserve(index.measures.entries.size());
    for (std::size_t bucket = 0; bucket < index.measures.entries.size(); ++bucket) {
        bucket_by_measure.emplace(index.measures.entries[bucket].id.value, bucket);
    }

    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto add_entry = [&](local_position_t position,
                                       bool is_tail) -> std::optional<diagnostic_t> {
                const auto row = absolute_row(index.measures, position);
                const auto bucket = bucket_by_measure.find(position.measure.value);
                if (!row.has_value() || bucket == bucket_by_measure.end()) {
                    return error(diagnostic_code_t::row_out_of_range,
                                 "note hit-test position lies outside chart geometry", note.id);
                }
                index.measure_buckets[bucket->second].push_back({
                    .note = note.id,
                    .lane = note.lane,
                    .kind = note.kind,
                    .position = position,
                    .absolute_row = *row,
                    .is_tail = is_tail,
                });
                return std::nullopt;
            };
            if (const auto invalid = add_entry(note.position, false); invalid.has_value()) {
                return diagnostic_list_t{*invalid};
            }
            if (note.end.has_value()) {
                if (const auto invalid = add_entry(*note.end, true); invalid.has_value()) {
                    return diagnostic_list_t{*invalid};
                }
            }
            const auto begin_row = absolute_row(index.measures, note.position);
            const auto end_position = note.end.value_or(note.position);
            const auto end_row = absolute_row(index.measures, end_position);
            if (!begin_row.has_value() || !end_row.has_value()) {
                return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                               "note span lies outside chart geometry", note.id)};
            }
            index.note_spans.push_back({
                .note = note.id,
                .lane = note.lane,
                .kind = note.kind,
                .begin_row = *begin_row,
                .end_row = *end_row,
            });
            if (const auto invalid =
                    add_selection_span(editor_selection_object_kind_t::note, note.id, note.position,
                                       end_position, note.lane);
                invalid.has_value()) {
                return diagnostic_list_t{*invalid};
            }
            auto tooltip = std::string{note_kind_label(note.kind)};
            tooltip += " · lane " + std::to_string(note.lane.value);
            tooltip += " · row " + position_label(note.position);
            if (note.end.has_value())
                tooltip += "–" + position_label(*note.end);
            if (note.label.has_value() && !note.label->empty())
                tooltip += " · " + *note.label;
            if (note.sound.has_value())
                tooltip += " · sample " + std::to_string(note.sound->value);
            if (const auto invalid = add_hover_object(
                    editor_selection_object_kind_t::note, note.id, note.position,
                    editor_inline_editor_kind_t::note_properties, std::move(tooltip), end_position);
                invalid.has_value()) {
                return diagnostic_list_t{*invalid};
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (const auto invalid = add_selection_span(editor_selection_object_kind_t::bgm_event,
                                                        event.id, event.position, event.position);
                invalid.has_value()) {
                return diagnostic_list_t{*invalid};
            }
            auto tooltip = std::string{"BGM event · row "} + position_label(event.position);
            if (event.sound.has_value())
                tooltip += " · sample " + std::to_string(event.sound->value);
            if (const auto invalid = add_hover_object(
                    editor_selection_object_kind_t::bgm_event, event.id, event.position,
                    editor_inline_editor_kind_t::bgm_event_properties, std::move(tooltip));
                invalid.has_value()) {
                return diagnostic_list_t{*invalid};
            }
        }
    }
    const auto add_points = [&](const auto& events, editor_selection_object_kind_t kind,
                                auto position_of, editor_inline_editor_kind_t editor,
                                auto tooltip_of) -> std::optional<diagnostic_t> {
        for (const auto& event : events) {
            const auto position = position_of(event);
            if (const auto invalid = add_selection_span(kind, event.id, position, position);
                invalid.has_value()) {
                return invalid;
            }
            if (const auto invalid =
                    add_hover_object(kind, event.id, position, editor, tooltip_of(event));
                invalid.has_value()) {
                return invalid;
            }
        }
        return std::nullopt;
    };
    const auto add_timing = [&](const auto& events, editor_selection_object_kind_t kind,
                                editor_inline_editor_kind_t editor,
                                auto tooltip_of) -> std::optional<diagnostic_t> {
        return add_points(
            events, kind, [](const auto& event) { return event.position; }, editor, tooltip_of);
    };
    if (const auto invalid = add_timing(chart.bpms, editor_selection_object_kind_t::bpm_event,
                                        editor_inline_editor_kind_t::bpm_event,
                                        [](const auto& event) {
                                            return std::string{"BPM "} +
                                                   rational_label(event.beats_per_minute) +
                                                   " · row " + position_label(event.position);
                                        });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    for (const auto& event : chart.pauses) {
        const auto kind = event.kind == pause_kind_t::stop
                              ? editor_selection_object_kind_t::stop_event
                              : editor_selection_object_kind_t::delay_event;
        const auto editor = event.kind == pause_kind_t::stop
                                ? editor_inline_editor_kind_t::stop_event
                                : editor_inline_editor_kind_t::delay_event;
        const auto name = event.kind == pause_kind_t::stop ? "Stop " : "Delay ";
        const auto unit = [&] {
            switch (event.source_unit) {
            case pause_unit_t::seconds:
                return " seconds";
            case pause_unit_t::milliseconds:
                return " ms";
            case pause_unit_t::bms_stop_ticks:
                return " BMS ticks";
            case pause_unit_t::beats:
                return " beats";
            }
            return "";
        }();
        if (const auto invalid = add_selection_span(kind, event.id, event.position, event.position);
            invalid.has_value())
            return diagnostic_list_t{*invalid};
        if (const auto invalid =
                add_hover_object(kind, event.id, event.position, editor,
                                 std::string{name} + rational_label(event.duration) + unit +
                                     " · row " + position_label(event.position));
            invalid.has_value())
            return diagnostic_list_t{*invalid};
    }
    for (const auto& event : chart.warps) {
        if (const auto invalid = add_selection_span(editor_selection_object_kind_t::warp_event,
                                                    event.id, event.span.begin, event.span.end);
            invalid.has_value())
            return diagnostic_list_t{*invalid};
        if (const auto invalid =
                add_hover_object(editor_selection_object_kind_t::warp_event, event.id,
                                 event.span.begin, editor_inline_editor_kind_t::warp_event,
                                 std::string{"Warp · "} + position_label(event.span.begin) + "–" +
                                     position_label(event.span.end),
                                 event.span.end);
            invalid.has_value())
            return diagnostic_list_t{*invalid};
    }
    if (const auto invalid = add_timing(chart.scrolls, editor_selection_object_kind_t::scroll_event,
                                        editor_inline_editor_kind_t::scroll_event,
                                        [](const auto& event) {
                                            return std::string{"Scroll ×"} +
                                                   rational_label(event.multiplier) + " · row " +
                                                   position_label(event.position);
                                        });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    if (const auto invalid =
            add_timing(chart.speeds, editor_selection_object_kind_t::speed_event,
                       editor_inline_editor_kind_t::speed_event,
                       [](const auto& event) {
                           auto tooltip = std::string{"Speed ×"} + rational_label(event.multiplier);
                           if (event.transition_beats.numerator != 0)
                               tooltip += " · transition " +
                                          std::to_string(event.transition_beats.numerator) + "/" +
                                          std::to_string(event.transition_beats.denominator) +
                                          (event.transition_is_seconds ? " seconds" : " beats");
                           return tooltip + " · row " + position_label(event.position);
                       });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    if (const auto invalid = add_timing(chart.kiai, editor_selection_object_kind_t::kiai_event,
                                        editor_inline_editor_kind_t::kiai_event,
                                        [](const auto& event) {
                                            return std::string{"Kiai "} +
                                                   (event.enabled ? "on" : "off") + " · row " +
                                                   position_label(event.position);
                                        });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    if (const auto invalid = add_points(
            chart.timing_anchors, editor_selection_object_kind_t::timing_anchor,
            [&](const auto& event) {
                return local_position_t{event.position.measure, event.position.row};
            },
            editor_inline_editor_kind_t::timing_anchor,
            [](const auto& event) {
                return std::string{"Timing anchor · frame "} +
                       std::to_string(event.source_sample_frame) +
                       (event.locked ? " · locked" : "");
            });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    if (const auto invalid = add_timing(chart.bga_events, editor_selection_object_kind_t::bga_event,
                                        editor_inline_editor_kind_t::bga_event,
                                        [](const auto& event) {
                                            return std::string{"BGA resource "} +
                                                   std::to_string(event.resource.value) +
                                                   " · row " + position_label(event.position);
                                        });
        invalid.has_value())
        return diagnostic_list_t{*invalid};
    // Audio-time markers have no exact chart row until a timing index projects
    // audio through BPM, stops, delays, warps, and the beat-zero offset. Keep
    // them out of this musical-position selection index rather than guessing.
    for (const auto& bookmark : chart.bookmarks) {
        if (bookmark.anchor == marker_anchor_t::beat && bookmark.position.has_value()) {
            if (const auto invalid =
                    add_selection_span(editor_selection_object_kind_t::bookmark, bookmark.id,
                                       *bookmark.position, *bookmark.position);
                invalid.has_value())
                return diagnostic_list_t{*invalid};
            if (const auto invalid = add_hover_object(
                    editor_selection_object_kind_t::bookmark, bookmark.id, *bookmark.position,
                    editor_inline_editor_kind_t::bookmark,
                    std::string{"Bookmark"} +
                        (bookmark.label.empty() ? std::string{} : " · " + bookmark.label));
                invalid.has_value())
                return diagnostic_list_t{*invalid};
        }
    }
    for (const auto& section : chart.sections) {
        if (section.anchor == marker_anchor_t::beat && section.beat_span.has_value()) {
            if (const auto invalid =
                    add_selection_span(editor_selection_object_kind_t::section, section.id,
                                       section.beat_span->begin, section.beat_span->end);
                invalid.has_value())
                return diagnostic_list_t{*invalid};
            if (const auto invalid = add_hover_object(
                    editor_selection_object_kind_t::section, section.id, section.beat_span->begin,
                    editor_inline_editor_kind_t::section,
                    std::string{"Section"} +
                        (section.label.empty() ? std::string{} : " · " + section.label),
                    section.beat_span->end);
                invalid.has_value())
                return diagnostic_list_t{*invalid};
        }
    }
    for (auto& bucket : index.measure_buckets) {
        std::ranges::sort(bucket, [](const auto& left, const auto& right) {
            if (left.absolute_row != right.absolute_row) {
                return left.absolute_row < right.absolute_row;
            }
            if (left.is_tail != right.is_tail) {
                return left.is_tail < right.is_tail;
            }
            return left.note.value < right.note.value;
        });
    }
    std::ranges::sort(index.note_spans, [](const auto& left, const auto& right) {
        if (left.begin_row != right.begin_row) {
            return left.begin_row < right.begin_row;
        }
        if (left.end_row != right.end_row) {
            return left.end_row < right.end_row;
        }
        return left.note.value < right.note.value;
    });
    auto leaf_base = std::size_t{1};
    while (leaf_base < index.note_spans.size()) {
        if (leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                           "note span index exceeds addressable size")};
        }
        leaf_base *= 2;
    }
    if (leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "note span index exceeds addressable size")};
    }
    index.note_span_tree_leaf_base = leaf_base;
    index.note_span_tree_max_end.assign(leaf_base * 2, 0);
    for (std::size_t span = 0; span < index.note_spans.size(); ++span) {
        index.note_span_tree_max_end[leaf_base + span] = index.note_spans[span].end_row;
    }
    for (auto node = leaf_base; node-- > 1;) {
        index.note_span_tree_max_end[node] = std::max(index.note_span_tree_max_end[node * 2],
                                                      index.note_span_tree_max_end[node * 2 + 1]);
    }
    std::ranges::sort(index.selection_spans, [](const auto& left, const auto& right) {
        if (left.begin_row != right.begin_row)
            return left.begin_row < right.begin_row;
        if (left.end_row != right.end_row)
            return left.end_row < right.end_row;
        return left.object < right.object;
    });
    std::ranges::sort(index.available_objects);
    if (const auto duplicate = std::ranges::adjacent_find(index.available_objects);
        duplicate != index.available_objects.end()) {
        return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                       "selection index contains a duplicate object ID",
                                       duplicate->id)};
    }
    index.available_objects.erase(
        std::unique(index.available_objects.begin(), index.available_objects.end()),
        index.available_objects.end());
    std::ranges::sort(index.hover_objects_by_id, {}, &editor_hover_index_entry_t::object);
    auto selection_leaf_base = std::size_t{1};
    while (selection_leaf_base < index.selection_spans.size()) {
        if (selection_leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                           "selection span index exceeds addressable size")};
        }
        selection_leaf_base *= 2;
    }
    if (selection_leaf_base > std::numeric_limits<std::size_t>::max() / 2) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "selection span index exceeds addressable size")};
    }
    index.selection_span_tree_leaf_base = selection_leaf_base;
    index.selection_span_tree_max_end.assign(selection_leaf_base * 2, 0);
    for (std::size_t span = 0; span < index.selection_spans.size(); ++span) {
        index.selection_span_tree_max_end[selection_leaf_base + span] =
            index.selection_spans[span].end_row;
    }
    for (auto node = selection_leaf_base; node-- > 1;) {
        index.selection_span_tree_max_end[node] =
            std::max(index.selection_span_tree_max_end[node * 2],
                     index.selection_span_tree_max_end[node * 2 + 1]);
    }
    return index;
}

auto hit_test_editor(const chart_document_t& chart, const editor_hit_test_index_t& index,
                     const editor_interaction_view_model_t& view,
                     const editor_hit_test_request_t& request)
    -> std::optional<editor_hit_target_t> {
    if (index.chart != chart.id || view.chart != chart.id ||
        index.document_revision != view.document_revision ||
        index.measure_buckets.size() != index.measures.entries.size() ||
        !std::isfinite(request.x_pixels) || !std::isfinite(request.y_pixels) ||
        !std::isfinite(request.field_left_pixels) || !std::isfinite(request.field_width_pixels) ||
        request.field_width_pixels <= 0.0 || !std::isfinite(request.note_hit_radius_pixels) ||
        request.note_hit_radius_pixels < 0.0 || !std::isfinite(view.viewport_height_pixels) ||
        view.viewport_height_pixels <= 0.0 || chart.divisions_per_standard_measure == 0) {
        return std::nullopt;
    }
    const auto pointer_position =
        position_at_row_y_pixels(chart, index.measures, view, request.y_pixels);
    const auto pointer_row =
        absolute_row_at_y_pixels(chart, index.measures, view, request.y_pixels);
    if (!pointer_position.has_value() || !pointer_row.has_value() ||
        request.x_pixels < request.field_left_pixels ||
        request.x_pixels >= request.field_left_pixels + request.field_width_pixels) {
        return std::nullopt;
    }
    const auto* layout = effective_layout(chart, index.measures, *pointer_position);
    if (layout == nullptr) {
        return std::nullopt;
    }
    std::vector<const lane_definition_t*> lanes;
    lanes.reserve(layout->lanes.size());
    for (const auto& lane : layout->lanes) {
        if (editable_lane(lane)) {
            lanes.push_back(&lane);
        }
    }
    std::ranges::sort(lanes, [](const auto* left, const auto* right) {
        if (left->display_order != right->display_order) {
            return left->display_order < right->display_order;
        }
        return left->id.value < right->id.value;
    });
    if (lanes.empty()) {
        return std::nullopt;
    }
    const auto lane_width = request.field_width_pixels / static_cast<double>(lanes.size());
    const auto lane_offset = (request.x_pixels - request.field_left_pixels) / lane_width;
    if (!std::isfinite(lane_offset) || lane_offset < 0.0 ||
        lane_offset >= static_cast<double>(lanes.size())) {
        return std::nullopt;
    }
    const auto lane = lanes[static_cast<std::size_t>(std::floor(lane_offset))]->id;
    const auto snapped =
        snapped_position(chart, index.measures, *pointer_position, view.active_snap_denominator);
    const auto lane_position = snapped.value_or(*pointer_position);

    auto result = editor_hit_target_t{
        .kind = editor_hit_target_kind_t::lane,
        .lane = lane,
        .position = lane_position,
    };
    auto result_priority = 0;
    auto result_distance = std::numeric_limits<double>::infinity();
    const auto radius_rows = static_cast<long double>(request.note_hit_radius_pixels) *
                             static_cast<long double>(chart.divisions_per_standard_measure) /
                             static_cast<long double>(view.pixels_per_standard_measure);
    const auto query_row = static_cast<long double>(*pointer_row);
    const auto minimum_row = query_row - radius_rows <= 0.0L
                                 ? std::uint64_t{0}
                                 : static_cast<std::uint64_t>(std::floor(query_row - radius_rows));
    const auto maximum_row =
        query_row + radius_rows >= static_cast<long double>(index.measures.total_rows - 1)
            ? index.measures.total_rows - 1
            : static_cast<std::uint64_t>(std::ceil(query_row + radius_rows));
    const auto first_after_min = std::ranges::upper_bound(
        index.measures.entries, minimum_row, {}, &measure_index_entry_t::absolute_begin_row);
    auto first_bucket = first_after_min == index.measures.entries.begin()
                            ? first_after_min
                            : std::prev(first_after_min);
    for (auto measure = first_bucket;
         measure != index.measures.entries.end() && measure->absolute_begin_row <= maximum_row;
         ++measure) {
        const auto bucket_number =
            static_cast<std::size_t>(std::distance(index.measures.entries.begin(), measure));
        const auto& bucket = index.measure_buckets[bucket_number];
        const auto first_entry = std::ranges::lower_bound(bucket, minimum_row, {},
                                                          &editor_hit_test_entry_t::absolute_row);
        for (auto entry = first_entry; entry != bucket.end() && entry->absolute_row <= maximum_row;
             ++entry) {
            if (entry->lane != lane) {
                continue;
            }
            const auto note_y = static_cast<double>(
                static_cast<long double>(entry->absolute_row) *
                    static_cast<long double>(view.pixels_per_standard_measure) /
                    static_cast<long double>(chart.divisions_per_standard_measure) -
                static_cast<long double>(view.viewport_scroll_pixels));
            const auto distance = std::abs(note_y - request.y_pixels);
            if (distance > request.note_hit_radius_pixels) {
                continue;
            }
            const auto kind = entry->is_tail ? editor_hit_target_kind_t::hold_tail
                                             : (entry->kind == note_kind_t::hold ||
                                                        entry->kind == note_kind_t::roll
                                                    ? editor_hit_target_kind_t::hold_head
                                                    : editor_hit_target_kind_t::note);
            const auto priority = kind == editor_hit_target_kind_t::hold_head
                                      ? 3
                                      : (kind == editor_hit_target_kind_t::hold_tail ? 2 : 1);
            if (priority < result_priority ||
                (priority == result_priority && distance > result_distance) ||
                (priority == result_priority && distance == result_distance && result.note &&
                 entry->note.value >= result.note->value)) {
                continue;
            }
            result = {
                .kind = kind,
                .lane = lane,
                .note = entry->note,
                .note_kind = entry->kind,
                .position = entry->position,
            };
            result_priority = priority;
            result_distance = distance;
        }
    }
    if (result_priority == 0 && index.note_span_tree_leaf_base != 0 &&
        index.note_span_tree_max_end.size() == index.note_span_tree_leaf_base * 2) {
        const auto after_candidate_spans = std::ranges::upper_bound(
            index.note_spans, *pointer_row, {}, &editor_note_span_entry_t::begin_row);
        const auto candidate_count = static_cast<std::size_t>(
            std::distance(index.note_spans.begin(), after_candidate_spans));
        const editor_note_span_entry_t* body{};
        const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                               std::size_t last) -> void {
            if (first >= candidate_count || index.note_span_tree_max_end[node] <= *pointer_row) {
                return;
            }
            if (last - first == 1) {
                if (first >= index.note_spans.size()) {
                    return;
                }
                const auto& span = index.note_spans[first];
                if (span.lane == lane && span.begin_row < *pointer_row &&
                    *pointer_row < span.end_row &&
                    (body == nullptr || span.note.value < body->note.value)) {
                    body = &span;
                }
                return;
            }
            const auto middle = first + (last - first) / 2;
            self(self, node * 2, first, middle);
            self(self, node * 2 + 1, middle, last);
        };
        if (candidate_count != 0) {
            visit(visit, 1, 0, index.note_span_tree_leaf_base);
        }
        if (body != nullptr) {
            result = {
                .kind = editor_hit_target_kind_t::note,
                .lane = body->lane,
                .note = body->note,
                .note_kind = body->kind,
                .position = *pointer_position,
            };
        }
    }
    return result;
}

auto hover_editor_object(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         const editor_interaction_view_model_t& view,
                         const editor_object_hover_request_t& request)
    -> std::optional<editor_object_hover_state_t> {
    if (index.chart != chart.id || view.chart != chart.id ||
        index.document_revision != view.document_revision || !std::isfinite(request.x_pixels) ||
        !std::isfinite(request.y_pixels) || !std::isfinite(request.field_left_pixels) ||
        !std::isfinite(request.field_width_pixels) || request.field_width_pixels <= 0.0 ||
        !std::isfinite(request.hit_radius_pixels) || request.hit_radius_pixels < 0.0 ||
        !std::isfinite(request.field_left_pixels + request.field_width_pixels) ||
        request.x_pixels < request.field_left_pixels ||
        request.x_pixels >= request.field_left_pixels + request.field_width_pixels ||
        index.hover_objects_by_id.size() != index.available_objects.size()) {
        return std::nullopt;
    }
    const auto pointer_row =
        absolute_row_at_y_pixels(chart, index.measures, view, request.y_pixels);
    if (!pointer_row.has_value())
        return std::nullopt;

    const editor_hover_index_entry_t* best{};
    auto best_distance = std::numeric_limits<double>::infinity();
    if (request.scope.notes) {
        const auto note_hit =
            hit_test_editor(chart, index, view,
                            {.x_pixels = request.x_pixels,
                             .y_pixels = request.y_pixels,
                             .field_left_pixels = request.field_left_pixels,
                             .field_width_pixels = request.field_width_pixels,
                             .note_hit_radius_pixels = request.hit_radius_pixels});
        if (note_hit.has_value() && note_hit->note.has_value()) {
            const auto object =
                editor_selection_object_t{editor_selection_object_kind_t::note, *note_hit->note};
            const auto found = std::ranges::lower_bound(index.hover_objects_by_id, object, {},
                                                        &editor_hover_index_entry_t::object);
            if (found != index.hover_objects_by_id.end() && found->object == object) {
                best = &*found;
                const auto hit_row = absolute_row(index.measures, note_hit->position);
                const auto y = static_cast<double>(
                    static_cast<long double>(hit_row.value_or(best->absolute_row)) *
                        static_cast<long double>(view.pixels_per_standard_measure) /
                        static_cast<long double>(chart.divisions_per_standard_measure) -
                    static_cast<long double>(view.viewport_scroll_pixels));
                best_distance = std::abs(y - request.y_pixels);
            }
        }
    }

    const auto radius_rows = static_cast<long double>(request.hit_radius_pixels) *
                             static_cast<long double>(chart.divisions_per_standard_measure) /
                             static_cast<long double>(view.pixels_per_standard_measure);
    const auto min_row = radius_rows >= static_cast<long double>(*pointer_row)
                             ? std::uint64_t{0}
                             : static_cast<std::uint64_t>(std::floor(
                                   static_cast<long double>(*pointer_row) - radius_rows));
    const auto max_row =
        radius_rows >= static_cast<long double>(index.measures.total_rows - 1 - *pointer_row)
            ? index.measures.total_rows - 1
            : static_cast<std::uint64_t>(
                  std::ceil(static_cast<long double>(*pointer_row) + radius_rows));
    const auto row_objects =
        editor_objects_in_row_range(chart, index, min_row, max_row, request.scope);
    if (std::holds_alternative<diagnostic_list_t>(row_objects))
        return std::nullopt;
    const auto pointer_chart_row = (static_cast<long double>(request.y_pixels) +
                                    static_cast<long double>(view.viewport_scroll_pixels)) *
                                   static_cast<long double>(chart.divisions_per_standard_measure) /
                                   static_cast<long double>(view.pixels_per_standard_measure);
    const auto pixels_per_row = static_cast<long double>(view.pixels_per_standard_measure) /
                                static_cast<long double>(chart.divisions_per_standard_measure);
    for (const auto& object : std::get<std::vector<editor_selection_object_t>>(row_objects)) {
        // Notes have lane geometry; their candidate was resolved through the
        // ordinary lane-aware hit test above.
        if (object.kind == editor_selection_object_kind_t::note)
            continue;
        const auto candidate = std::ranges::lower_bound(index.hover_objects_by_id, object, {},
                                                        &editor_hover_index_entry_t::object);
        if (candidate == index.hover_objects_by_id.end() || candidate->object != object)
            continue;
        const auto nearest_row =
            std::clamp(pointer_chart_row, static_cast<long double>(candidate->absolute_row),
                       static_cast<long double>(candidate->end_absolute_row));
        const auto distance =
            static_cast<double>(std::abs(nearest_row - pointer_chart_row) * pixels_per_row);
        if (distance > request.hit_radius_pixels ||
            (best != nullptr &&
             (distance > best_distance || (distance == best_distance && object >= best->object)))) {
            continue;
        }
        best = &*candidate;
        best_distance = distance;
    }
    if (best == nullptr)
        return std::nullopt;
    return editor_object_hover_state_t{
        .object = best->object,
        .editor = best->editor,
        .position = best->position,
        .document_revision = index.document_revision,
        .tooltip = best->tooltip,
    };
}

auto register_editor_object_click(editor_double_click_state_t& state,
                                  const editor_hit_test_index_t& index,
                                  const editor_interaction_view_model_t& view,
                                  const std::optional<editor_object_hover_state_t>& hover,
                                  double x_pixels, double y_pixels, std::uint64_t time_milliseconds,
                                  editor_double_click_settings_t settings, bool pointer_captured,
                                  bool focused) -> std::optional<editor_object_edit_request_t> {
    const auto clear = [&] { state.previous_click.reset(); };
    if (!focused || !pointer_captured || !hover.has_value() || view.chart != index.chart ||
        view.document_revision != index.document_revision ||
        hover->document_revision != index.document_revision || !std::isfinite(x_pixels) ||
        !std::isfinite(y_pixels) || !std::isfinite(settings.pointer_slop_pixels) ||
        settings.pointer_slop_pixels < 0.0 || settings.maximum_interval_milliseconds == 0) {
        clear();
        return std::nullopt;
    }
    const auto found = std::ranges::lower_bound(index.hover_objects_by_id, hover->object, {},
                                                &editor_hover_index_entry_t::object);
    if (found == index.hover_objects_by_id.end() || found->object != hover->object ||
        found->editor != hover->editor || found->position != hover->position) {
        clear();
        return std::nullopt;
    }
    if (state.previous_click.has_value()) {
        const auto& previous = *state.previous_click;
        const auto dx = x_pixels - previous.x_pixels;
        const auto dy = y_pixels - previous.y_pixels;
        const auto slop_squared = settings.pointer_slop_pixels * settings.pointer_slop_pixels;
        if (previous.object == hover->object &&
            previous.document_revision == index.document_revision &&
            time_milliseconds >= previous.time_milliseconds &&
            time_milliseconds - previous.time_milliseconds <=
                settings.maximum_interval_milliseconds &&
            dx * dx + dy * dy <= slop_squared) {
            const auto request = editor_object_edit_request_t{
                .chart = index.chart,
                .document_revision = index.document_revision,
                .object = found->object,
                .editor = found->editor,
            };
            clear();
            return request;
        }
    }
    state.previous_click = editor_click_candidate_t{
        .object = hover->object,
        .document_revision = index.document_revision,
        .time_milliseconds = time_milliseconds,
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
    };
    return std::nullopt;
}

auto clear_editor_double_click_state(editor_double_click_state_t& state) -> void {
    state.previous_click.reset();
}

auto editor_notes_in_area_selection(const chart_document_t& chart,
                                    const editor_hit_test_index_t& index,
                                    const editor_interaction_view_model_t& view,
                                    const editor_area_selection_request_t& request)
    -> std::variant<std::vector<object_id_t>, diagnostic_list_t> {
    const auto invalid = [](diagnostic_code_t code, std::string message) {
        return std::variant<std::vector<object_id_t>, diagnostic_list_t>{
            diagnostic_list_t{error(code, std::move(message))}};
    };
    if (index.chart != chart.id || view.chart != chart.id ||
        index.document_revision != view.document_revision) {
        return invalid(diagnostic_code_t::invalid_hold,
                       "area selection hit-test index is stale or belongs to another chart");
    }
    if (index.measure_buckets.size() != index.measures.entries.size() ||
        index.note_span_tree_leaf_base == 0 ||
        (index.note_span_tree_leaf_base < index.note_spans.size()) ||
        index.note_span_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2 ||
        index.note_span_tree_max_end.size() != index.note_span_tree_leaf_base * 2 ||
        !std::isfinite(request.start_x_pixels) || !std::isfinite(request.start_y_pixels) ||
        !std::isfinite(request.end_x_pixels) || !std::isfinite(request.end_y_pixels) ||
        !std::isfinite(request.field_left_pixels) || !std::isfinite(request.field_width_pixels) ||
        request.field_width_pixels <= 0.0 || !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels) ||
        !std::isfinite(view.viewport_height_pixels) || view.viewport_height_pixels <= 0.0 ||
        chart.divisions_per_standard_measure == 0 || index.measures.total_rows == 0 ||
        !std::isfinite(request.field_left_pixels + request.field_width_pixels)) {
        return invalid(diagnostic_code_t::invalid_resolution,
                       "area selection geometry or hit-test index is invalid");
    }

    const auto field_right = request.field_left_pixels + request.field_width_pixels;
    const auto left =
        std::max(std::min(request.start_x_pixels, request.end_x_pixels), request.field_left_pixels);
    const auto right =
        std::min(std::max(request.start_x_pixels, request.end_x_pixels), field_right);
    const auto top = std::max(0.0, std::min(request.start_y_pixels, request.end_y_pixels));
    const auto bottom = std::min(view.viewport_height_pixels,
                                 std::max(request.start_y_pixels, request.end_y_pixels));
    if (left > right || top > bottom) {
        return std::vector<object_id_t>{};
    }

    const auto row_at_y = [&](double y) {
        return (static_cast<long double>(y) + view.viewport_scroll_pixels) *
               static_cast<long double>(chart.divisions_per_standard_measure) /
               static_cast<long double>(view.pixels_per_standard_measure);
    };
    const auto minimum_row =
        std::clamp(row_at_y(top), 0.0L, static_cast<long double>(index.measures.total_rows - 1));
    const auto maximum_row =
        std::clamp(row_at_y(bottom), 0.0L, static_cast<long double>(index.measures.total_rows - 1));
    if (minimum_row > maximum_row) {
        return std::vector<object_id_t>{};
    }

    const auto query_minimum = static_cast<std::uint64_t>(std::floor(minimum_row));
    const auto query_maximum = static_cast<std::uint64_t>(std::ceil(maximum_row));
    const auto after_candidate_spans = std::ranges::upper_bound(
        index.note_spans, query_maximum, {}, &editor_note_span_entry_t::begin_row);
    const auto candidate_count =
        static_cast<std::size_t>(std::distance(index.note_spans.begin(), after_candidate_spans));

    struct layout_segment_t {
        std::uint64_t row{};
        const lane_layout_t* layout{};
    };
    std::vector<layout_segment_t> layouts;
    layouts.reserve(chart.lane_layouts.size());
    for (const auto& segment : chart.lane_layouts) {
        const auto row = absolute_row(index.measures, segment.position);
        if (row.has_value()) {
            layouts.push_back({*row, &segment.layout});
        }
    }
    std::ranges::stable_sort(layouts, {}, &layout_segment_t::row);
    std::vector<layout_segment_t> unique_layouts;
    unique_layouts.reserve(layouts.size());
    for (const auto& segment : layouts) {
        if (!unique_layouts.empty() && unique_layouts.back().row == segment.row) {
            unique_layouts.back() = segment;
        } else {
            unique_layouts.push_back(segment);
        }
    }
    const auto layout_at_row = [&](long double row) -> const lane_layout_t* {
        const auto after =
            std::ranges::upper_bound(unique_layouts, row, {}, &layout_segment_t::row);
        return after == unique_layouts.begin() ? nullptr : std::prev(after)->layout;
    };

    using lane_bounds_t = std::pair<double, double>;
    std::unordered_map<const lane_layout_t*, std::unordered_map<std::uint64_t, lane_bounds_t>>
        lane_bounds_by_layout;
    const auto lane_column_intersects = [&](const lane_layout_t* layout, lane_id_t lane) {
        if (layout == nullptr) {
            return false;
        }
        auto cached = lane_bounds_by_layout.find(layout);
        if (cached == lane_bounds_by_layout.end()) {
            std::vector<const lane_definition_t*> editable_lanes;
            editable_lanes.reserve(layout->lanes.size());
            for (const auto& candidate : layout->lanes) {
                if (editable_lane(candidate)) {
                    editable_lanes.push_back(&candidate);
                }
            }
            std::ranges::sort(editable_lanes, [](const auto* left_lane, const auto* right_lane) {
                if (left_lane->display_order != right_lane->display_order) {
                    return left_lane->display_order < right_lane->display_order;
                }
                return left_lane->id.value < right_lane->id.value;
            });
            std::unordered_map<std::uint64_t, lane_bounds_t> bounds;
            bounds.reserve(editable_lanes.size());
            const auto lane_width =
                request.field_width_pixels / static_cast<double>(editable_lanes.size());
            for (std::size_t lane_index = 0; lane_index < editable_lanes.size(); ++lane_index) {
                const auto lane_left =
                    request.field_left_pixels + static_cast<double>(lane_index) * lane_width;
                bounds.emplace(editable_lanes[lane_index]->id.value,
                               lane_bounds_t{lane_left, lane_left + lane_width});
            }
            cached = lane_bounds_by_layout.emplace(layout, std::move(bounds)).first;
        }
        const auto found = cached->second.find(lane.value);
        if (found == cached->second.end()) {
            return false;
        }
        const auto [lane_left, lane_right] = found->second;
        if (left == right) {
            return left >= lane_left && left < lane_right;
        }
        return left < lane_right && right > lane_left;
    };

    const auto note_point_intersects = [&](lane_id_t lane, std::uint64_t row) {
        const auto y = static_cast<long double>(row) *
                           static_cast<long double>(view.pixels_per_standard_measure) /
                           static_cast<long double>(chart.divisions_per_standard_measure) -
                       static_cast<long double>(view.viewport_scroll_pixels);
        return y >= static_cast<long double>(top) && y <= static_cast<long double>(bottom) &&
               lane_column_intersects(layout_at_row(static_cast<long double>(row)), lane);
    };
    const auto hold_body_intersects = [&](lane_id_t lane, std::uint64_t begin_row,
                                          std::uint64_t end_row) {
        auto body_begin = std::max(minimum_row, static_cast<long double>(begin_row));
        auto body_end = std::min(maximum_row, static_cast<long double>(end_row));
        if (body_begin > body_end || begin_row == end_row || unique_layouts.empty()) {
            return false;
        }
        if (body_begin == body_end) {
            return body_begin > static_cast<long double>(begin_row) &&
                   body_begin < static_cast<long double>(end_row) &&
                   lane_column_intersects(layout_at_row(body_begin), lane);
        }

        auto after =
            std::ranges::upper_bound(unique_layouts, body_begin, {}, &layout_segment_t::row);
        if (after == unique_layouts.begin()) {
            return false;
        }
        auto segment = std::prev(after);
        while (segment != unique_layouts.end()) {
            const auto next = std::next(segment);
            const auto segment_begin = std::max(body_begin, static_cast<long double>(segment->row));
            const auto segment_end = next == unique_layouts.end()
                                         ? body_end
                                         : std::min(body_end, static_cast<long double>(next->row));
            if (segment_begin < segment_end && lane_column_intersects(segment->layout, lane)) {
                return true;
            }
            if (next == unique_layouts.end() || static_cast<long double>(next->row) >= body_end) {
                break;
            }
            segment = next;
        }
        return false;
    };

    std::vector<object_id_t> selected;
    const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                           std::size_t last) -> void {
        if (first >= candidate_count || index.note_span_tree_max_end[node] < query_minimum) {
            return;
        }
        if (last - first == 1) {
            if (first >= index.note_spans.size()) {
                return;
            }
            const auto& span = index.note_spans[first];
            const auto head_intersects = note_point_intersects(span.lane, span.begin_row);
            const auto tail_intersects = note_point_intersects(span.lane, span.end_row);
            const auto body_intersects =
                hold_body_intersects(span.lane, span.begin_row, span.end_row);
            if (head_intersects || tail_intersects || body_intersects) {
                selected.push_back(span.note);
            }
            return;
        }
        const auto middle = first + (last - first) / 2;
        self(self, node * 2, first, middle);
        self(self, node * 2 + 1, middle, last);
    };
    if (candidate_count != 0) {
        visit(visit, 1, 0, index.note_span_tree_leaf_base);
    }
    std::ranges::sort(selected, {}, &object_id_t::value);
    return selected;
}

auto editor_objects_in_row_range(const chart_document_t& chart,
                                 const editor_hit_test_index_t& index, std::uint64_t first_row,
                                 std::uint64_t last_row, editor_selection_scope_t scope)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    if (index.chart != chart.id || index.measures.total_rows == 0 || first_row > last_row ||
        last_row >= index.measures.total_rows || index.selection_span_tree_leaf_base == 0 ||
        index.selection_span_tree_leaf_base < index.selection_spans.size() ||
        index.selection_span_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2 ||
        index.selection_span_tree_max_end.size() != index.selection_span_tree_leaf_base * 2) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "selection index or row interval is invalid")};
    }
    const auto after_candidates = std::ranges::upper_bound(
        index.selection_spans, last_row, {}, &editor_selection_span_entry_t::begin_row);
    const auto candidate_count =
        static_cast<std::size_t>(std::distance(index.selection_spans.begin(), after_candidates));
    std::vector<editor_selection_object_t> selected;
    const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                           std::size_t last) -> void {
        if (first >= candidate_count || index.selection_span_tree_max_end[node] < first_row) {
            return;
        }
        if (last - first == 1) {
            if (first >= index.selection_spans.size())
                return;
            const auto& span = index.selection_spans[first];
            if (span.begin_row > last_row || span.end_row < first_row ||
                !scope_accepts(scope, span.object.kind))
                return;
            if (span.object.kind == editor_selection_object_kind_t::note) {
                const auto position = position_at(index.measures, span.begin_row);
                if (!position || !span.lane ||
                    find_active_lane(chart, index.measures, *position, *span.lane) == nullptr)
                    return;
            }
            selected.push_back(span.object);
            return;
        }
        const auto middle = first + (last - first) / 2;
        self(self, node * 2, first, middle);
        self(self, node * 2 + 1, middle, last);
    };
    if (candidate_count != 0)
        visit(visit, 1, 0, index.selection_span_tree_leaf_base);
    std::ranges::sort(selected);
    return selected;
}

auto editor_objects_in_area_selection(const chart_document_t& chart,
                                      const editor_hit_test_index_t& index,
                                      const editor_interaction_view_model_t& view,
                                      const editor_area_selection_request_t& request)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    std::vector<editor_selection_object_t> selected;
    if (request.scope.notes) {
        auto notes = editor_notes_in_area_selection(chart, index, view, request);
        if (std::holds_alternative<diagnostic_list_t>(notes))
            return std::get<diagnostic_list_t>(std::move(notes));
        for (const auto id : std::get<std::vector<object_id_t>>(notes))
            selected.push_back({editor_selection_object_kind_t::note, id});
    }
    const auto has_non_note_scope = request.scope.bgm || request.scope.timing || request.scope.bga;
    if (has_non_note_scope) {
        const auto maximum_scroll = maximum_viewport_scroll(
            chart, index.measures, view.pixels_per_standard_measure, view.viewport_height_pixels);
        if (index.chart != chart.id || view.chart != chart.id ||
            index.document_revision != view.document_revision ||
            !std::isfinite(request.start_y_pixels) || !std::isfinite(request.end_y_pixels) ||
            !std::isfinite(view.pixels_per_standard_measure) ||
            view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
            view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
            !std::isfinite(view.viewport_scroll_pixels) ||
            !std::isfinite(view.viewport_height_pixels) || view.viewport_height_pixels <= 0.0 ||
            !maximum_scroll || view.viewport_scroll_pixels < 0.0 ||
            view.viewport_scroll_pixels > *maximum_scroll ||
            chart.divisions_per_standard_measure == 0 || index.measures.total_rows == 0 ||
            index.selection_span_tree_leaf_base == 0 ||
            index.selection_span_tree_leaf_base < index.selection_spans.size() ||
            index.selection_span_tree_leaf_base > std::numeric_limits<std::size_t>::max() / 2 ||
            index.selection_span_tree_max_end.size() != index.selection_span_tree_leaf_base * 2) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                           "area selection geometry or index is invalid")};
        }
        const auto top = std::max(0.0, std::min(request.start_y_pixels, request.end_y_pixels));
        const auto bottom = std::min(view.viewport_height_pixels,
                                     std::max(request.start_y_pixels, request.end_y_pixels));
        if (top <= bottom) {
            const auto to_row = [&](double y) {
                return (static_cast<long double>(y) + view.viewport_scroll_pixels) *
                       static_cast<long double>(chart.divisions_per_standard_measure) /
                       static_cast<long double>(view.pixels_per_standard_measure);
            };
            const auto max_row = static_cast<long double>(index.measures.total_rows - 1);
            const auto low = std::clamp(to_row(top), 0.0L, max_row);
            const auto high = std::clamp(to_row(bottom), 0.0L, max_row);
            const editor_selection_scope_t non_note_scope{
                .notes = false,
                .bgm = request.scope.bgm,
                .timing = request.scope.timing,
                .bga = request.scope.bga,
            };
            const auto after_candidates = std::ranges::upper_bound(
                index.selection_spans, high,
                [](long double row, std::uint64_t begin_row) {
                    return row < static_cast<long double>(begin_row);
                },
                &editor_selection_span_entry_t::begin_row);
            const auto candidate_count = static_cast<std::size_t>(
                std::distance(index.selection_spans.begin(), after_candidates));
            const auto visit = [&](auto&& self, std::size_t node, std::size_t first,
                                   std::size_t last) -> void {
                if (first >= candidate_count ||
                    static_cast<long double>(index.selection_span_tree_max_end[node]) < low) {
                    return;
                }
                if (last - first == 1) {
                    if (first >= index.selection_spans.size())
                        return;
                    const auto& span = index.selection_spans[first];
                    if (static_cast<long double>(span.begin_row) > high ||
                        static_cast<long double>(span.end_row) < low ||
                        !scope_accepts(non_note_scope, span.object.kind)) {
                        return;
                    }
                    selected.push_back(span.object);
                    return;
                }
                const auto middle = first + (last - first) / 2;
                self(self, node * 2, first, middle);
                self(self, node * 2 + 1, middle, last);
            };
            if (candidate_count != 0)
                visit(visit, 1, 0, index.selection_span_tree_leaf_base);
        }
    }
    std::ranges::sort(selected);
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    return selected;
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
        state.pending_note_paint_gesture.reset();
        state.pending_editor_area_selection.reset();
        state.pending_space_range_selection.reset();
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
    if (std::holds_alternative<cancel_editor_area_selection_action_t>(action)) {
        state.pending_editor_area_selection.reset();
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
    if (state.pending_space_range_selection.has_value() &&
        state.pending_space_range_selection->document_revision != history.document.revision) {
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "Space range selection is stale after a document edit")});
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
    if (state.pending_note_paint_gesture.has_value() &&
        !std::holds_alternative<update_note_paint_gesture_action_t>(action) &&
        !std::holds_alternative<end_note_paint_gesture_action_t>(action)) {
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "finish or cancel the active note paint gesture first")});
    }
    if (state.pending_editor_area_selection.has_value() &&
        !std::holds_alternative<update_editor_area_selection_action_t>(action) &&
        !std::holds_alternative<commit_editor_area_selection_action_t>(action)) {
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active area selection gesture first")});
    }
    if (state.pending_space_range_selection.has_value() &&
        !std::holds_alternative<space_range_selection_action_t>(action) &&
        !std::holds_alternative<move_edit_cursor_action_t>(action) &&
        !std::holds_alternative<jump_edit_cursor_action_t>(action) &&
        !std::holds_alternative<cycle_edit_snap_action_t>(action) &&
        !std::holds_alternative<adjust_editor_zoom_action_t>(action) &&
        !std::holds_alternative<scroll_editor_view_action_t>(action)) {
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active Space range selection first")});
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
        set_selected_notes(state, plan.notes);
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

    const auto append_paint_target = [&](lane_id_t lane, local_position_t position,
                                         bool pointer_captured) -> editor_interaction_result_t {
        if (!pointer_captured) {
            return dispatch_editor_action(
                history, state,
                cancel_editor_gesture_action_t{
                    editor_gesture_cancel_reason_t::pointer_capture_lost});
        }
        if (!state.pending_note_paint_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "note paint update requires an active gesture")});
        }
        auto& pending = *state.pending_note_paint_gesture;
        if (pending.document_revision != history.document.revision) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "note paint gesture is stale after a document edit")});
        }
        const auto target =
            snapped_position(*chart, index, position, state.active_snap_denominator);
        if (!target.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::row_out_of_range,
                                            "paint target cannot be snapped inside chart")});
        }
        const auto target_row = absolute_row(index, *target);
        const auto previous_row = absolute_row(index, pending.last_position);
        if (!target_row.has_value() || !previous_row.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::row_out_of_range,
                                            "paint gesture lies outside chart geometry")});
        }
        const auto same_lane = lane == pending.last_lane;
        const auto step = chart->divisions_per_standard_measure / state.active_snap_denominator;
        const auto row_distance = *target_row >= *previous_row ? *target_row - *previous_row
                                                               : *previous_row - *target_row;
        const auto path_cells = same_lane ? row_distance / step + 1 : 1;
        if (path_cells > maximum_note_paint_targets) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_resolution,
                                            "paint drag exceeds the per-gesture note target cap")});
        }
        std::vector<note_paint_target_t> additions;
        additions.reserve(static_cast<std::size_t>(path_cells));
        const auto append_cell = [&](std::uint64_t row) -> std::optional<diagnostic_t> {
            const auto cell_position = position_at(index, row);
            if (!cell_position.has_value()) {
                return error(diagnostic_code_t::row_out_of_range,
                             "paint path leaves chart geometry");
            }
            if (find_active_lane(*chart, index, *cell_position, lane) == nullptr) {
                return error(diagnostic_code_t::unknown_lane,
                             "paint lane is unavailable along the drag path");
            }
            const paint_cell_key_t key{lane, row};
            if (pending.visited.contains(key)) {
                return std::nullopt;
            }
            if (pending.targets->size() + additions.size() >= maximum_note_paint_targets) {
                return error(diagnostic_code_t::invalid_resolution,
                             "paint drag exceeds the per-gesture note target cap");
            }
            additions.push_back({lane, *cell_position});
            return std::nullopt;
        };

        if (same_lane) {
            const auto cell_count = row_distance / step + 1;
            for (std::uint64_t cell = 0; cell < cell_count; ++cell) {
                const auto offset = cell * step;
                const auto row =
                    *target_row >= *previous_row ? *previous_row + offset : *previous_row - offset;
                if (const auto invalid = append_cell(row); invalid.has_value()) {
                    return diagnostic_result(history, state, {*invalid});
                }
            }
        } else if (const auto invalid = append_cell(*target_row); invalid.has_value()) {
            return diagnostic_result(history, state, {*invalid});
        }

        if (!additions.empty()) {
            auto expanded = std::make_shared<std::vector<note_paint_target_t>>(*pending.targets);
            expanded->reserve(expanded->size() + additions.size());
            for (const auto& addition : additions) {
                const auto row = absolute_row(index, addition.position);
                expanded->push_back(addition);
                pending.visited.insert({addition.lane, *row});
            }
            pending.targets = std::move(expanded);
        }
        pending.last_lane = lane;
        pending.last_position = *target;
        state.active_lane = lane;
        state.cursor = *target;
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
    };
    const auto begin_paint_gesture = [&](lane_id_t lane, local_position_t position,
                                         bool pointer_captured) -> editor_interaction_result_t {
        if (!pointer_captured) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "pointer capture is required to begin note painting")});
        }
        if (state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value() ||
            state.pending_note_paint_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "another editor gesture is already active")});
        }
        const auto target =
            snapped_position(*chart, index, position, state.active_snap_denominator);
        if (!target.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::row_out_of_range,
                                            "paint start cannot be snapped inside chart")});
        }
        if (find_active_lane(*chart, index, *target, lane) == nullptr) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::unknown_lane,
                                            "paint lane is unavailable at the start position")});
        }
        const auto row = absolute_row(index, *target);
        if (!row.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::row_out_of_range,
                                            "paint start lies outside chart geometry")});
        }
        auto targets = std::make_shared<std::vector<note_paint_target_t>>();
        targets->push_back({lane, *target});
        pending_note_paint_gesture_t pending{
            .document_revision = history.document.revision,
            .targets = std::move(targets),
            .last_lane = lane,
            .last_position = *target,
        };
        pending.visited.reserve(64);
        pending.visited.insert({lane, *row});
        state.pending_note_paint_gesture = std::move(pending);
        state.active_lane = lane;
        state.cursor = *target;
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
    };
    const auto end_paint_gesture = [&](lane_id_t lane, local_position_t position,
                                       bool pointer_captured) -> editor_interaction_result_t {
        if (!pointer_captured) {
            return append_paint_target(lane, position, false);
        }
        const auto updated = append_paint_target(lane, position, pointer_captured);
        if (!updated) {
            return updated;
        }
        if (!state.pending_note_paint_gesture.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "note paint gesture was cancelled before commit")});
        }
        const auto pending = *state.pending_note_paint_gesture;
        if (pending.document_revision != history.document.revision) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "note paint gesture is stale after a document edit")});
        }
        auto planned = plan_note_paint(*chart, index, *pending.targets);
        if (std::holds_alternative<diagnostic_list_t>(planned)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(planned)));
        }
        const auto& paint_plan = std::get<note_paint_plan_t>(planned);
        if (paint_plan.additions.empty() && paint_plan.removals.empty()) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id, "paint gesture contains no note changes")});
        }
        std::optional<object_id_t> first_id;
        if (!paint_plan.additions.empty()) {
            first_id = next_object_id(*chart);
        }
        if (!paint_plan.additions.empty() &&
            (!first_id.has_value() ||
             paint_plan.additions.size() - 1 >
                 std::numeric_limits<std::uint64_t>::max() - first_id->value)) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id, "chart object ID sequence is exhausted")});
        }
        auto proposed = history.document;
        auto* proposed_chart = find_chart(proposed, state.chart);
        std::unordered_map<std::uint64_t, measure_chunk_t*> measures_by_id;
        measures_by_id.reserve(proposed_chart->measures.size());
        for (auto& measure : proposed_chart->measures) {
            measures_by_id.emplace(measure.id.value, &measure);
        }
        std::vector<object_id_t> painted_notes;
        painted_notes.reserve(paint_plan.additions.size());
        std::unordered_set<std::uint64_t> removed_ids;
        removed_ids.reserve(paint_plan.removals.size());
        for (const auto id : paint_plan.removals) {
            removed_ids.insert(id.value);
        }
        for (auto& measure : proposed_chart->measures) {
            std::erase_if(measure.notes,
                          [&](const auto& note) { return removed_ids.contains(note.id.value); });
        }
        auto next_id = first_id ? first_id->value : 0;
        for (const auto& target : paint_plan.additions) {
            const auto measure = measures_by_id.find(target.position.measure.value);
            if (measure == measures_by_id.end()) {
                return diagnostic_result(history, state,
                                         {error(diagnostic_code_t::unknown_measure,
                                                "paint target refers to an unknown measure")});
            }
            const auto id = object_id_t{next_id++};
            measure->second->notes.push_back({
                .id = id,
                .position = target.position,
                .lane = target.lane,
                .kind = note_kind_t::tap,
            });
            painted_notes.push_back(id);
        }
        auto command = commit_document_command(history, std::move(proposed), "Paint notes");
        if (!command) {
            return diagnostic_result(history, state, std::move(command.diagnostics));
        }
        state.pending_note_paint_gesture.reset();
        set_selected_notes(state, std::move(painted_notes));
        state.active_lane = lane;
        state.cursor = pending.last_position;
        const auto next_view = make_view(history, state);
        return {
            .accepted = std::holds_alternative<editor_interaction_view_model_t>(next_view),
            .document_changed = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                               ? std::get<diagnostic_list_t>(next_view)
                               : diagnostic_list_t{},
        };
    };

    const auto combine_area_selection = [&](const std::vector<editor_selection_object_t>& hits,
                                            editor_area_selection_mode_t mode,
                                            const editor_hit_test_index_t& hit_index)
        -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
        if (mode != editor_area_selection_mode_t::replace &&
            mode != editor_area_selection_mode_t::add &&
            mode != editor_area_selection_mode_t::toggle) {
            return diagnostic_list_t{
                error(diagnostic_code_t::invalid_id, "unknown area selection mode")};
        }
        if (mode == editor_area_selection_mode_t::replace) {
            return hits;
        }
        std::set<editor_selection_object_t> combined;
        for (const auto object : state.selected_objects) {
            if (!object.id || !std::ranges::binary_search(hit_index.available_objects, object)) {
                return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                               "current selection contains a missing object",
                                               object.id)};
            }
            if (!combined.insert(object).second) {
                return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                               "current selection contains a duplicate object",
                                               object.id)};
            }
        }
        std::set<editor_selection_object_t> hit_set(hits.begin(), hits.end());
        auto result = state.selected_objects;
        if (mode == editor_area_selection_mode_t::add) {
            for (const auto object : hits) {
                if (combined.insert(object).second) {
                    result.push_back(object);
                }
            }
            return result;
        }
        const auto originally_selected = combined;
        std::erase_if(result, [&](const auto object) {
            if (hit_set.contains(object)) {
                combined.erase(object);
                return true;
            }
            return false;
        });
        for (const auto object : hits) {
            if (!originally_selected.contains(object)) {
                result.push_back(object);
            }
        }
        return result;
    };

    const auto begin_area_selection =
        [&](const begin_editor_area_selection_action_t& request) -> editor_interaction_result_t {
        if (!request.pointer_captured) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "pointer capture is required to begin area selection")});
        }
        if (state.pending_hold.has_value() || state.pending_note_duration_gesture.has_value() ||
            state.pending_note_paint_gesture.has_value() ||
            state.pending_editor_area_selection.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "another editor gesture is already active")});
        }
        if (!std::isfinite(request.x_pixels) || !std::isfinite(request.y_pixels) ||
            !std::isfinite(request.field_left_pixels) ||
            !std::isfinite(request.field_width_pixels) || request.field_width_pixels <= 0.0 ||
            !std::isfinite(request.field_left_pixels + request.field_width_pixels) ||
            request.x_pixels < request.field_left_pixels ||
            request.x_pixels >= request.field_left_pixels + request.field_width_pixels ||
            request.y_pixels < 0.0 || request.y_pixels >= state.viewport_height_pixels) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_resolution,
                                            "area selection must begin inside the chart field")});
        }
        if (request.mode != editor_area_selection_mode_t::replace &&
            request.mode != editor_area_selection_mode_t::add &&
            request.mode != editor_area_selection_mode_t::toggle) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id, "unknown area selection mode")});
        }
        auto built = build_editor_hit_test_index(*chart, history.document.revision);
        if (std::holds_alternative<diagnostic_list_t>(built)) {
            return diagnostic_result(history, state, std::get<diagnostic_list_t>(std::move(built)));
        }
        auto hit_index = std::make_shared<editor_hit_test_index_t>(
            std::move(std::get<editor_hit_test_index_t>(built)));
        const auto& view = std::get<editor_interaction_view_model_t>(initial_view);
        const editor_area_selection_request_t area{
            .start_x_pixels = request.x_pixels,
            .start_y_pixels = request.y_pixels,
            .end_x_pixels = request.x_pixels,
            .end_y_pixels = request.y_pixels,
            .field_left_pixels = request.field_left_pixels,
            .field_width_pixels = request.field_width_pixels,
        };
        auto scoped_area = area;
        scoped_area.scope = request.scope;
        const auto queried =
            editor_objects_in_area_selection(*chart, *hit_index, view, scoped_area);
        if (std::holds_alternative<diagnostic_list_t>(queried)) {
            return diagnostic_result(history, state, std::get<diagnostic_list_t>(queried));
        }
        auto combined = combine_area_selection(
            std::get<std::vector<editor_selection_object_t>>(queried), request.mode, *hit_index);
        if (std::holds_alternative<diagnostic_list_t>(combined)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(combined)));
        }
        state.pending_editor_area_selection = pending_editor_area_selection_t{
            .document_revision = history.document.revision,
            .hit_index = std::move(hit_index),
            .anchor_x_pixels = request.x_pixels,
            .anchor_y_pixels = request.y_pixels,
            .current_x_pixels = request.x_pixels,
            .current_y_pixels = request.y_pixels,
            .field_left_pixels = request.field_left_pixels,
            .field_width_pixels = request.field_width_pixels,
            .mode = request.mode,
            .notes = std::make_shared<const std::vector<object_id_t>>(
                notes_from_objects(std::get<std::vector<editor_selection_object_t>>(combined))),
            .scope = request.scope,
            .objects = std::make_shared<const std::vector<editor_selection_object_t>>(
                std::move(std::get<std::vector<editor_selection_object_t>>(combined))),
        };
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
    };

    const auto update_area_selection = [&](double x_pixels, double y_pixels,
                                           bool pointer_captured) -> editor_interaction_result_t {
        if (!pointer_captured) {
            return dispatch_editor_action(
                history, state,
                cancel_editor_gesture_action_t{
                    editor_gesture_cancel_reason_t::pointer_capture_lost});
        }
        if (!state.pending_editor_area_selection.has_value()) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "area selection update requires an active gesture")});
        }
        const auto& pending = *state.pending_editor_area_selection;
        if (pending.document_revision != history.document.revision ||
            pending.hit_index == nullptr ||
            pending.hit_index->document_revision != history.document.revision) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "area selection gesture is stale after a document edit")});
        }
        if (!std::isfinite(x_pixels) || !std::isfinite(y_pixels)) {
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_resolution,
                                            "area selection pointer coordinates must be finite")});
        }
        const auto& view = std::get<editor_interaction_view_model_t>(initial_view);
        const editor_area_selection_request_t area{
            .start_x_pixels = pending.anchor_x_pixels,
            .start_y_pixels = pending.anchor_y_pixels,
            .end_x_pixels = x_pixels,
            .end_y_pixels = y_pixels,
            .field_left_pixels = pending.field_left_pixels,
            .field_width_pixels = pending.field_width_pixels,
        };
        auto scoped_area = area;
        scoped_area.scope = pending.scope;
        const auto queried =
            editor_objects_in_area_selection(*chart, *pending.hit_index, view, scoped_area);
        if (std::holds_alternative<diagnostic_list_t>(queried)) {
            return diagnostic_result(history, state, std::get<diagnostic_list_t>(queried));
        }
        auto combined =
            combine_area_selection(std::get<std::vector<editor_selection_object_t>>(queried),
                                   pending.mode, *pending.hit_index);
        if (std::holds_alternative<diagnostic_list_t>(combined)) {
            return diagnostic_result(history, state,
                                     std::get<diagnostic_list_t>(std::move(combined)));
        }
        auto& updated = *state.pending_editor_area_selection;
        updated.current_x_pixels = x_pixels;
        updated.current_y_pixels = y_pixels;
        updated.notes = std::make_shared<const std::vector<object_id_t>>(
            notes_from_objects(std::get<std::vector<editor_selection_object_t>>(combined)));
        updated.objects = std::make_shared<const std::vector<editor_selection_object_t>>(
            std::move(std::get<std::vector<editor_selection_object_t>>(combined)));
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
    };

    const auto commit_area_selection = [&](double x_pixels, double y_pixels,
                                           bool pointer_captured) -> editor_interaction_result_t {
        const auto updated = update_area_selection(x_pixels, y_pixels, pointer_captured);
        if (!updated) {
            return updated;
        }
        if (!state.pending_editor_area_selection.has_value()) {
            return updated;
        }
        const auto& pending = *state.pending_editor_area_selection;
        if (pending.document_revision != history.document.revision || pending.objects == nullptr) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "area selection gesture is stale after a document edit")});
        }
        set_selected_objects(state, *pending.objects);
        state.pending_editor_area_selection.reset();
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
            } else if constexpr (std::is_same_v<action_t, jump_edit_cursor_action_t>) {
                if (typed_action.expected_document_revision != history.document.revision) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "cursor jump refers to a stale document revision")});
                }
                if (!absolute_row(index, typed_action.position).has_value()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::row_out_of_range,
                               "cursor jump target lies outside chart geometry")});
                }

                auto proposed_state = state;
                proposed_state.cursor = typed_action.position;
                auto next_view = make_view(history, proposed_state);
                if (std::holds_alternative<diagnostic_list_t>(next_view)) {
                    const auto replacement = transition_lane(
                        *chart, index, state.cursor, typed_action.position, state.active_lane);
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
            } else if constexpr (std::is_same_v<action_t, begin_note_paint_gesture_action_t>) {
                return begin_paint_gesture(typed_action.lane, typed_action.position,
                                           typed_action.pointer_captured);
            } else if constexpr (std::is_same_v<action_t, update_note_paint_gesture_action_t>) {
                return append_paint_target(typed_action.lane, typed_action.position,
                                           typed_action.pointer_captured);
            } else if constexpr (std::is_same_v<action_t, end_note_paint_gesture_action_t>) {
                return end_paint_gesture(typed_action.lane, typed_action.position,
                                         typed_action.pointer_captured);
            } else if constexpr (std::is_same_v<action_t, begin_editor_area_selection_action_t>) {
                return begin_area_selection(typed_action);
            } else if constexpr (std::is_same_v<action_t, update_editor_area_selection_action_t>) {
                return update_area_selection(typed_action.x_pixels, typed_action.y_pixels,
                                             typed_action.pointer_captured);
            } else if constexpr (std::is_same_v<action_t, commit_editor_area_selection_action_t>) {
                return commit_area_selection(typed_action.x_pixels, typed_action.y_pixels,
                                             typed_action.pointer_captured);
            } else if constexpr (std::is_same_v<action_t, cancel_editor_area_selection_action_t>) {
                return {};
            } else if constexpr (std::is_same_v<action_t, space_range_selection_action_t>) {
                if (!state.pending_space_range_selection.has_value()) {
                    if (typed_action.mode != editor_area_selection_mode_t::replace &&
                        typed_action.mode != editor_area_selection_mode_t::add &&
                        typed_action.mode != editor_area_selection_mode_t::toggle) {
                        return diagnostic_result(history, state,
                                                 {error(diagnostic_code_t::invalid_id,
                                                        "unknown Space range selection mode")});
                    }
                    auto built = build_editor_hit_test_index(*chart, history.document.revision);
                    if (std::holds_alternative<diagnostic_list_t>(built)) {
                        return diagnostic_result(history, state,
                                                 std::get<diagnostic_list_t>(std::move(built)));
                    }
                    state.pending_space_range_selection = pending_space_range_selection_t{
                        .document_revision = history.document.revision,
                        .hit_index = std::make_shared<editor_hit_test_index_t>(
                            std::move(std::get<editor_hit_test_index_t>(built))),
                        .anchor = state.cursor,
                        .mode = typed_action.mode,
                        .scope = typed_action.scope,
                    };
                    const auto next_view = make_view(history, state);
                    return {
                        .accepted =
                            std::holds_alternative<editor_interaction_view_model_t>(next_view),
                        .view = std::holds_alternative<editor_interaction_view_model_t>(next_view)
                                    ? std::optional{std::get<editor_interaction_view_model_t>(
                                          next_view)}
                                    : std::nullopt,
                        .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                           ? std::get<diagnostic_list_t>(next_view)
                                           : diagnostic_list_t{},
                    };
                }
                const auto pending = *state.pending_space_range_selection;
                if (pending.hit_index == nullptr ||
                    pending.document_revision != history.document.revision ||
                    pending.hit_index->document_revision != history.document.revision) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "Space range selection is stale after a document edit")});
                }
                const auto& current_view = std::get<editor_interaction_view_model_t>(initial_view);
                if (!current_view.pending_space_range_selection.has_value() ||
                    current_view.pending_space_range_selection->objects == nullptr) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_hold,
                               "Space range preview is unavailable for commit")});
                }
                auto combined =
                    combine_area_selection(*current_view.pending_space_range_selection->objects,
                                           pending.mode, *pending.hit_index);
                if (std::holds_alternative<diagnostic_list_t>(combined)) {
                    return diagnostic_result(history, state,
                                             std::get<diagnostic_list_t>(std::move(combined)));
                }
                set_selected_objects(
                    state, std::move(std::get<std::vector<editor_selection_object_t>>(combined)));
                state.pending_space_range_selection.reset();
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
                set_selected_notes(state, typed_action.notes);
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
                state.selected_objects.clear();
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
                    set_selected_notes(state, targets);
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
                set_selected_notes(state, targets);
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
            } else if constexpr (std::is_same_v<action_t, assign_selected_notes_to_lane_action_t>) {
                if (state.selected_notes.empty()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "lane assignment requires one or more selected notes")});
                }
                struct assignment_candidate_t {
                    const note_t* note{};
                    std::uint64_t begin{};
                    std::uint64_t end{};
                };
                std::vector<assignment_candidate_t> candidates;
                candidates.reserve(state.selected_notes.size());
                std::unordered_set<std::uint64_t> selected_ids;
                selected_ids.reserve(state.selected_notes.size());
                for (const auto id : state.selected_notes) {
                    if (!id || !selected_ids.insert(id.value).second) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::duplicate_id,
                                   "selected lane assignment contains an invalid or duplicate ID",
                                   id)});
                    }
                }
                for (const auto& measure : chart->measures) {
                    for (const auto& note : measure.notes) {
                        if (!selected_ids.contains(note.id.value)) {
                            continue;
                        }
                        const auto begin = absolute_row(index, note.position);
                        const auto end = note.end ? absolute_row(index, *note.end) : begin;
                        if (!begin.has_value() || !end.has_value()) {
                            return diagnostic_result(
                                history, state,
                                {error(diagnostic_code_t::row_out_of_range,
                                       "selected lane assignment lies outside chart geometry",
                                       note.id)});
                        }
                        candidates.push_back({&note, *begin, *end});
                    }
                }
                if (candidates.size() != selected_ids.size()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "selected lane assignment refers to an unavailable note")});
                }
                std::ranges::sort(candidates, [](const auto& left, const auto& right) {
                    return std::tie(left.begin, left.note->lane.value, left.note->id.value) <
                           std::tie(right.begin, right.note->lane.value, right.note->id.value);
                });
                if (typed_action.successive) {
                    candidates.resize(1);
                }

                std::unordered_set<std::uint64_t> assigned_ids;
                assigned_ids.reserve(candidates.size());
                for (const auto& candidate : candidates) {
                    assigned_ids.insert(candidate.note->id.value);
                    if (find_active_lane(*chart, index, candidate.note->position,
                                         typed_action.lane) == nullptr ||
                        (candidate.note->end.has_value() &&
                         find_active_lane(*chart, index, *candidate.note->end, typed_action.lane) ==
                             nullptr)) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::unknown_lane,
                                   "target lane is unavailable at a selected note endpoint",
                                   candidate.note->id)});
                    }
                }
                struct assignment_interval_t {
                    std::uint64_t begin{};
                    std::uint64_t end{};
                    object_id_t note;
                    bool moving{};
                };
                std::vector<assignment_interval_t> intervals;
                std::size_t chart_note_count{};
                for (const auto& measure : chart->measures) {
                    if (measure.notes.size() >
                        std::numeric_limits<std::size_t>::max() - chart_note_count) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::invalid_resolution,
                                   "chart note count overflows lane-assignment capacity")});
                    }
                    chart_note_count += measure.notes.size();
                }
                if (candidates.size() >
                    std::numeric_limits<std::size_t>::max() - chart_note_count) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_resolution,
                               "lane-assignment interval count exceeds capacity")});
                }
                intervals.reserve(chart_note_count + candidates.size());
                for (const auto& candidate : candidates) {
                    intervals.push_back({candidate.begin, candidate.end, candidate.note->id, true});
                }
                for (const auto& measure : chart->measures) {
                    for (const auto& note : measure.notes) {
                        if (note.lane != typed_action.lane ||
                            assigned_ids.contains(note.id.value)) {
                            continue;
                        }
                        const auto begin = absolute_row(index, note.position);
                        const auto end = note.end ? absolute_row(index, *note.end) : begin;
                        if (!begin.has_value() || !end.has_value()) {
                            return diagnostic_result(
                                history, state,
                                {error(diagnostic_code_t::row_out_of_range,
                                       "target-lane note lies outside chart geometry", note.id)});
                        }
                        intervals.push_back({*begin, *end, note.id, false});
                    }
                }
                std::ranges::sort(intervals, [](const auto& left, const auto& right) {
                    return std::tie(left.begin, left.end, left.moving, left.note.value) <
                           std::tie(right.begin, right.end, right.moving, right.note.value);
                });
                std::optional<assignment_interval_t> longest_moving;
                std::optional<assignment_interval_t> longest_existing;
                for (const auto& interval : intervals) {
                    const auto& opposite = interval.moving ? longest_existing : longest_moving;
                    if (opposite.has_value() && interval.begin <= opposite->end) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::collision,
                                   "lane assignment overlaps another target-lane note",
                                   interval.moving ? interval.note : opposite->note)});
                    }
                    if (interval.moving && longest_moving.has_value() &&
                        interval.begin <= longest_moving->end) {
                        return diagnostic_result(
                            history, state,
                            {error(diagnostic_code_t::collision,
                                   "selected notes overlap when assigned to one lane",
                                   interval.note)});
                    }
                    auto& longest = interval.moving ? longest_moving : longest_existing;
                    if (!longest.has_value() || longest->end < interval.end) {
                        longest = interval;
                    }
                }

                std::optional<editor_interaction_result_t::note_audition_t> audition;
                if (typed_action.successive) {
                    audition = editor_interaction_result_t::note_audition_t{
                        .note = candidates.front().note->id,
                        .sound = candidates.front().note->sound,
                    };
                }

                auto proposed = history.document;
                auto* proposed_chart = find_chart(proposed, state.chart);
                bool changed = false;
                std::size_t assigned_count{};
                for (auto& measure : proposed_chart->measures) {
                    for (auto& note : measure.notes) {
                        if (!assigned_ids.contains(note.id.value)) {
                            continue;
                        }
                        changed = changed || note.lane != typed_action.lane;
                        note.lane = typed_action.lane;
                        ++assigned_count;
                    }
                }
                if (assigned_count != candidates.size()) {
                    return diagnostic_result(
                        history, state,
                        {error(diagnostic_code_t::invalid_id,
                               "lane assignment could not resolve every selected note")});
                }
                if (changed) {
                    auto command = commit_document_command(history, std::move(proposed),
                                                           typed_action.successive
                                                               ? "Assign next selected note to lane"
                                                               : "Assign selected notes to lane");
                    if (!command) {
                        return diagnostic_result(history, state, std::move(command.diagnostics));
                    }
                }

                if (typed_action.successive) {
                    const auto assigned = audition->note;
                    std::erase(state.selected_notes, assigned);
                    std::erase_if(state.selected_objects, [&](const auto& object) {
                        return object.kind == editor_selection_object_kind_t::note &&
                               object.id == assigned;
                    });
                }
                state.active_lane = typed_action.lane;
                const auto next_view = make_view(history, state);
                return {
                    .accepted = true,
                    .document_changed = changed,
                    .view =
                        std::holds_alternative<editor_interaction_view_model_t>(next_view)
                            ? std::optional{std::get<editor_interaction_view_model_t>(next_view)}
                            : std::nullopt,
                    .diagnostics = std::holds_alternative<diagnostic_list_t>(next_view)
                                       ? std::get<diagnostic_list_t>(next_view)
                                       : diagnostic_list_t{},
                    .audition = audition,
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
