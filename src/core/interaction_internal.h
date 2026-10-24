#pragma once
#include "lane_occupancy.h"
#include "interaction_context.h"
#include "interaction_geometry.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/interaction_queries.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/snap.h>

#include "interaction_cursor.h"

#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/time_shift.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <queue>
#include <ranges>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {
struct hold_start_t {
    lane_id_t lane;
    local_position_t position;
    std::optional<hold_gesture_source_t> source;
    std::optional<resource_id_t> sound;
    bool is_displacing{};
    std::optional<bmson_source_geometry_t> geometry;
    template<class Action>
    [[nodiscard]] static auto from(const editor_interaction_state_t& state, const Action& action) -> hold_start_t {
        hold_start_t start{.lane = state.active_lane, .position = state.cursor};
        if constexpr (requires { action.position; }) start.position = action.position;
        if constexpr (requires { action.lane; }) start.lane = action.lane;
        if constexpr (requires { action.sound; }) start.sound = action.sound;
        if constexpr (requires { action.source_geometry; }) start.geometry = action.source_geometry;
        if constexpr (requires { action.displace_to_bgm; }) start.is_displacing = action.displace_to_bgm;
        if constexpr (std::same_as<Action, begin_lane_key_hold_gesture_action_t>)
            start.source = hold_gesture_source_t::keyboard_lane;
        else if constexpr (std::same_as<Action, begin_alt_pointer_hold_gesture_action_t>)
            start.source = hold_gesture_source_t::alt_pointer;
        return start;
    }
    [[nodiscard]] auto validate_sound(const song_document_t&) const -> diagnostic_list_t;
    auto operator==(const hold_start_t&) const -> bool = default;
};

auto combine_editor_area_selection(const editor_interaction_state_t& state,
                                   const std::vector<editor_selection_object_t>& hits,
                                   editor_area_selection_mode_t mode,
                                   const editor_hit_test_index_t& hit_index)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;

namespace interaction_detail {
auto bms_branch_source_orders(const chart_document_t& chart) -> std::unordered_set<std::size_t>;

struct hold_row_plan_t {
    std::uint64_t head_row{};
    std::uint64_t tail_row{};
    std::vector<object_id_t> removals;
    bool adds_note{};
};

auto is_plain_editable_note(const note_t& note) -> bool;

auto is_removable_sound_note(const chart_document_t& chart, const note_t& note) -> bool;

enum class hold_overlap_policy_t { merge_plain, remove_sound };

struct hold_row_expansion_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    lane_id_t lane;
    std::uint64_t begin_row;
    std::uint64_t end_row;
    std::unordered_set<std::uint64_t> removals;
    hold_overlap_policy_t overlap_policy{hold_overlap_policy_t::merge_plain};

    auto is_intersecting(std::uint64_t begin, std::uint64_t end) const -> bool {
        return begin <= end_row && end >= begin_row;
    }

    auto expand_once() -> std::variant<bool, diagnostic_list_t> {
        auto expanded = false;
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                if (note.lane != lane)
                    continue;
                const auto note_begin = index.row_of(note.position);
                const auto note_end = note.end ? index.row_of(*note.end) : note_begin;
                if (!note_begin || !note_end || *note_end < *note_begin ||
                    !is_intersecting(*note_begin, *note_end))
                    continue;
                if (!is_plain_editable_note(note) &&
                    !(overlap_policy == hold_overlap_policy_t::remove_sound &&
                      is_removable_sound_note(chart, note)))
                    return diagnostic_list_t{
                        error(diagnostic_code_t::collision,
                              "hold placement overlaps a non-editable same-lane note", note.id)};
                removals.insert(note.id.value);
                if (note.kind == note_kind_t::hold) {
                    const auto old_begin = begin_row;
                    const auto old_end = end_row;
                    begin_row = std::min(begin_row, *note_begin);
                    end_row = std::max(end_row, *note_end);
                    expanded = expanded || begin_row != old_begin || end_row != old_end;
                }
            }
        }
        return expanded;
    }
};

auto plan_hold_rows(const chart_document_t& chart, const measure_index_t& index, lane_id_t lane,
                    std::uint64_t anchor_row, std::uint64_t moving_row,
                    hold_overlap_policy_t overlap_policy = hold_overlap_policy_t::merge_plain)
    -> std::variant<hold_row_plan_t, diagnostic_list_t>;

auto canvas_keyboard_hold_previews(const chart_document_t& chart, const measure_index_t& index,
                                   std::uint64_t canvas_rows, std::uint64_t revision,
                                   const editor_interaction_state_t& state)
    -> std::variant<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>,
                    diagnostic_list_t>;

template <typename Values>
void add_ids(std::unordered_set<std::uint64_t>& ids, const Values& values) {
    for (const auto& value : values)
        ids.insert(value.id.value);
}

auto existing_ids(const chart_document_t& chart, editor_selection_object_kind_t kind)
    -> std::unordered_set<std::uint64_t>;

auto has_span_measures(const chart_document_t& chart, const position_span_t& span) -> bool;

inline auto find_active_lane(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, lane_id_t id) -> const lane_definition_t* {
    const auto* layout = effective_layout(chart, index, position);
    if (layout == nullptr) {
        return nullptr;
    }
    for (const auto& lane : layout->lanes) {
        if (lane.id == id && lane.is_editable()) return &lane;
    }
    return nullptr;
}

inline auto normalized_position_span(const measure_index_t& index, local_position_t first,
                              local_position_t second) -> std::optional<position_span_t> {
    const auto first_row = index.row_of(first);
    const auto second_row = index.row_of(second);
    if (!first_row || !second_row || *first_row == *second_row) {
        return std::nullopt;
    }
    return *first_row < *second_row ? position_span_t{first, second}
                                    : position_span_t{second, first};
}

auto notes_from_objects(const std::vector<editor_selection_object_t>& objects)
    -> std::vector<object_id_t>;

auto set_selected_objects(editor_interaction_state_t& state,
                          std::vector<editor_selection_object_t> objects) -> void;

auto set_selected_notes(editor_interaction_state_t& state, std::vector<object_id_t> notes) -> void;



auto transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to, lane_id_t current,
                     const editor_scratch_presentation_t& sides) -> std::optional<lane_id_t>;

inline auto minimum_viewport_scroll(double viewport_height) -> double {
    return -viewport_height * editor_cursor_edge_margin_fraction;
}

inline auto absolute_row_at_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels,
                              bool include_virtual_bms_tail = false) -> std::optional<std::uint64_t> {
    auto total_rows = index.total_rows;
    if (include_virtual_bms_tail && has_chart_virtual_tail(chart)) {
        const auto built_canvas = build_virtual_canvas(chart);
        if (const auto* canvas = std::get_if<virtual_canvas_t>(&built_canvas))
            total_rows = canvas->total_rows;
        else
            return std::nullopt;
    }
    if (view.chart != chart.id || !std::isfinite(y_pixels) || y_pixels < 0.0 ||
        y_pixels >= view.viewport_height_pixels ||
        !std::isfinite(view.pixels_per_standard_measure) ||
        view.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        view.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !std::isfinite(view.viewport_scroll_pixels) || chart.divisions_per_standard_measure == 0 ||
        total_rows == 0) {
        return std::nullopt;
    }
    const auto row = (static_cast<long double>(y_pixels) + view.viewport_scroll_pixels) *
                     static_cast<long double>(chart.divisions_per_standard_measure) /
                     static_cast<long double>(view.pixels_per_standard_measure);
    if (!std::isfinite(row) || row < 0.0L || row >= static_cast<long double>(total_rows)) {
        return std::nullopt;
    }
    // Pointer positions are continuous; map to the nearest stored row and make
    // exact half-row ties stable by resolving them toward the earlier row.
    return nearest_row(row, total_rows, row_clamp_policy_t::upper_only);
}

struct view_geometry_t {
    const chart_document_t* chart{};
    std::unique_ptr<measure_index_t> owned_index;
    const measure_index_t& index;
    std::uint64_t absolute_row{};
    std::uint64_t canvas_rows{};
    local_position_t lane_position;
    const lane_definition_t* lane{};
};

struct canvas_cursor_t {
    std::uint64_t absolute_row{};
    std::uint64_t canvas_rows{};
};

auto resolve_canvas_cursor(const chart_document_t& chart, const measure_index_t& index,
                           const editor_interaction_state_t& state)
    -> std::variant<canvas_cursor_t, diagnostic_list_t>;

auto is_valid_view_snap(const chart_document_t& chart, const editor_interaction_state_t& state)
    -> bool;

auto is_valid_viewport(const chart_document_t& chart, const measure_index_t& index,
                    const editor_interaction_state_t& state) -> bool;

auto resolve_view_geometry(const document_history_t& history,
                           const editor_interaction_state_t& state,
                           const measure_index_t* retained = nullptr)
    -> std::variant<view_geometry_t, diagnostic_list_t>;

auto space_range_objects(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         std::uint64_t first, std::uint64_t last, editor_selection_scope_t scope)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;

auto space_range_bounds(const pending_space_range_selection_t& pending, std::uint64_t cursor_row,
                        std::uint64_t canvas_rows) -> canvas_row_span_t;

auto make_space_range_preview(const document_history_t& history,
                              const editor_interaction_state_t& state,
                              const view_geometry_t& geometry)
    -> std::variant<std::optional<space_range_selection_preview_t>, diagnostic_list_t>;

auto is_valid_retained_canvas_range(const editor_interaction_state_t& state, std::uint64_t canvas_rows)
    -> bool;

auto area_selection_anchor_y(const pending_editor_area_selection_t& pending, double viewport_scroll)
    -> double;

auto make_view(const document_history_t& history, const editor_interaction_state_t& state,
               const measure_index_t* retained = nullptr)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t>;

auto next_object_id(const chart_document_t& chart) -> std::optional<object_id_t>;

auto resolve_row_placement(const chart_document_t& chart, const measure_index_t& index,
                           const editor_interaction_state_t& state, lane_id_t lane,
                           local_position_t position)
    -> std::variant<local_position_t, diagnostic_list_t>;

using hold_placement_plan_t = hold_placement_t<std::vector<object_id_t>>;

auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<std::vector<object_id_t>>) -> std::vector<object_id_t>;

auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<hold_preview_removals_t>) -> hold_preview_removals_t;

auto held_note_kind(note_kind_t armed) -> note_kind_t;

template <class Removals = std::vector<object_id_t>>
auto plan_hold_placement(const chart_document_t& chart, const measure_index_t& index,
                         lane_id_t lane, local_position_t anchor, local_position_t moving,
                         note_kind_t armed_kind = note_kind_t::hold, bool committing = false,
                         hold_overlap_policy_t overlap_policy = hold_overlap_policy_t::merge_plain)
    -> std::variant<hold_placement_t<Removals>, diagnostic_list_t> {
    const auto anchor_row = index.row_of(anchor);
    const auto moving_row = index.row_of(moving);
    if (!anchor_row.has_value() || !moving_row.has_value()) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "hold endpoints lie outside chart geometry")};
    }

    auto planned = plan_hold_rows(chart, index, lane, *anchor_row, *moving_row, overlap_policy);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return *diagnostics;
    auto rows = std::get<hold_row_plan_t>(std::move(planned));
    // A real hold has a playable head; a single row is judged by the kind about
    // to be committed (previews pass none, so a gesture may start inside a warp
    // and end on a row outside it).
    const auto single_row = rows.head_row == rows.tail_row;
    const auto head_kind = single_row ? armed_kind : held_note_kind(armed_kind);
    if ((committing || !single_row) && is_head_blocked_by_warp(chart, index, rows.head_row, head_kind))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_hold, "playable note heads cannot sit inside a warp")};
    const auto head = position_at(index, rows.head_row);
    const auto tail = position_at(index, rows.tail_row);
    if (!head.has_value() || !tail.has_value()) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "normalized hold endpoints lie outside chart geometry")};
    }
    hold_placement_t<Removals> result{
        .head = *head,
        .tail = rows.head_row == rows.tail_row ? std::optional<local_position_t>{}
                                               : std::optional{*tail},
        .removals = make_hold_removals(std::move(rows.removals), std::type_identity<Removals>{}),
        .adds_note = rows.adds_note,
    };
    return result;
}

void erase_notes(chart_document_t& chart, std::span<const object_id_t> ids);

struct note_paint_plan_t {
    std::vector<note_paint_target_t> additions;
    std::vector<object_id_t> removals;
};

struct paint_row_t {
    lane_id_t lane;
    std::uint64_t row{};
    note_paint_target_t target;
};

auto paint_rows(const measure_index_t& index, std::span<const note_paint_target_t> targets)
    -> std::variant<std::vector<paint_row_t>, diagnostic_list_t>;

auto erase_active_paint_notes(std::map<std::uint64_t, lane_occupant_t>& active,
                              note_paint_plan_t& plan) -> void;

auto process_draw_paint_row(const paint_row_t& row, note_paint_plan_t& plan,
                            const std::map<std::uint64_t, lane_occupant_t>& active,
                            note_kind_t kind) -> std::optional<diagnostic_t>;

struct paint_overlap_sweep_t {
    const std::vector<lane_occupant_t>& intervals;
    std::size_t next_interval{};
    std::optional<lane_id_t> lane;
    std::priority_queue<std::pair<std::uint64_t, std::uint64_t>,
                        std::vector<std::pair<std::uint64_t, std::uint64_t>>, std::greater<>>
        ending;
    std::map<std::uint64_t, lane_occupant_t> active;
    note_paint_intent_t current_intent{note_paint_intent_t::toggle};

    auto process(const paint_row_t& row, note_paint_plan_t& plan, note_kind_t kind)
        -> std::optional<diagnostic_t> {
        if (!lane || *lane != row.lane) {
            while (next_interval < intervals.size() &&
                   intervals[next_interval].lane.value < row.lane.value)
                ++next_interval;
            ending = {};
            active.clear();
            lane = row.lane;
        }
        while (next_interval < intervals.size() && intervals[next_interval].lane == lane_occupancy_key_t::from(row.lane) &&
               intervals[next_interval].begin <= row.row) {
            const auto& interval = intervals[next_interval++];
            ending.emplace(interval.end, interval.note.value);
            active.emplace(interval.note.value, interval);
        }
        while (!ending.empty() && lane_occupant_t::is_expired(ending.top().first, row.row)) {
            active.erase(ending.top().second);
            ending.pop();
        }
        if (current_intent == note_paint_intent_t::erase) {
            erase_active_paint_notes(active, plan);
            return std::nullopt;
        }
        if (current_intent == note_paint_intent_t::draw)
            return process_draw_paint_row(row, plan, active, kind);
        if (active.empty()) {
            plan.additions.push_back(row.target);
            return std::nullopt;
        }
        if (active.size() != 1)
            return error(diagnostic_code_t::collision,
                         "paint gesture overlaps multiple same-lane notes",
                         active.begin()->second.note);
        const auto& [id, note] = *active.begin();
        if (note.kind == kind && !note.has_end && note.begin == row.row && note.end == row.row) {
            plan.removals.push_back(object_id_t{id});
            return std::nullopt;
        }
        return error(diagnostic_code_t::collision, "paint gesture overlaps a non-tap note",
                     note.note);
    }
};

auto plan_note_paint(const chart_document_t& chart, const measure_index_t& index,
                     std::span<const note_paint_target_t> targets, note_kind_t kind,
                     note_paint_intent_t intent)
    -> std::variant<note_paint_plan_t, diagnostic_list_t>;

auto chart_note_ids(const chart_document_t& chart) -> std::unordered_set<std::uint64_t>;




struct resized_note_t {
    object_id_t id;
    lane_id_t lane;
    std::uint64_t begin{};
    std::uint64_t end{};
    std::optional<local_position_t> end_position;
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

struct resize_parameters_t {
    const note_t* reference{};
    local_position_t snapped_end;
    std::uint64_t delta{};
    bool shrinking{};
};

struct duration_resize_context_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    measure_lookup_t measures;
    std::unordered_map<std::uint64_t, const note_t*> target_notes;
    std::vector<lane_occupant_t> collisions;

    auto row_for(local_position_t position) const -> std::optional<std::uint64_t> {
        const auto row = measures.row_of(position);
        return row && *row < index.total_rows ? row : std::nullopt;
    }

    auto collect_existing(const std::unordered_set<std::uint64_t>& target_ids)
        -> std::optional<diagnostic_t> {
        std::size_t count{};
        for (const auto& measure : chart.measures)
            count += measure.notes.size();
        collisions.reserve(count);
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                if (target_ids.contains(note.id.value)) {
                    target_notes.emplace(note.id.value, &note);
                    continue;
                }
                const auto begin = row_for(note.position);
                const auto end = note.end ? row_for(*note.end) : begin;
                if (!begin || !end)
                    return error(diagnostic_code_t::row_out_of_range,
                                 "existing note lies outside chart geometry", note.id);
                collisions.push_back({.lane = lane_occupancy_key_t::from(note.lane), .begin = *begin, .end = *end, .note = note.id});
            }
        }
        return std::nullopt;
    }
};

auto resolve_resize_parameters(const chart_document_t& chart, const measure_index_t& index,
                               std::vector<object_id_t>& targets, object_id_t reference_id,
                               local_position_t reference_end, snap_grid_t snap,
                               bool exact) -> std::variant<resize_parameters_t, diagnostic_list_t>;

auto valid_resize_ids(const std::vector<object_id_t>& targets)
    -> std::variant<std::unordered_set<std::uint64_t>, diagnostic_list_t>;

auto resize_one_note(duration_resize_context_t& context, const resize_parameters_t& parameters,
                     duration_resize_plan_t& plan, object_id_t id) -> std::optional<diagnostic_t>;

auto resize_selected_notes(duration_resize_context_t& context,
                           const resize_parameters_t& parameters, duration_resize_plan_t& plan)
    -> std::optional<diagnostic_t>;

struct duration_collision_sweep_t {
    using queue_t =
        std::priority_queue<active_duration_collision_t, std::vector<active_duration_collision_t>,
                            earliest_duration_collision_t>;
    queue_t resized;
    queue_t existing;
    lane_occupancy_key_t lane{};
    bool has_lane{};

    auto process(const lane_occupant_t& interval) -> std::optional<diagnostic_t> {
        if (!has_lane || interval.lane != lane) {
            resized = queue_t{};
            existing = queue_t{};
            lane = interval.lane;
            has_lane = true;
        }
        while (!resized.empty() && lane_occupant_t::is_expired(resized.top().end, interval.begin))
            resized.pop();
        while (!existing.empty() && lane_occupant_t::is_expired(existing.top().end, interval.begin))
            existing.pop();
        if (interval.proposed)
            return process_resized(interval);
        return process_existing(interval);
    }

    auto process_resized(const lane_occupant_t& interval)
        -> std::optional<diagnostic_t> {
        if (!resized.empty())
            return error(diagnostic_code_t::collision, "resized selected notes overlap on one lane",
                         interval.note);
        if (!existing.empty())
            return error(diagnostic_code_t::collision,
                         "resized note overlaps an unselected same-lane note", existing.top().note);
        resized.push({interval.end, interval.note});
        return std::nullopt;
    }

    auto process_existing(const lane_occupant_t& interval)
        -> std::optional<diagnostic_t> {
        if (!resized.empty())
            return error(diagnostic_code_t::collision,
                         "resized note overlaps an unselected same-lane note", interval.note);
        existing.push({interval.end, interval.note});
        return std::nullopt;
    }
};

auto validate_resize_collisions(std::vector<lane_occupant_t>& collisions)
    -> std::optional<diagnostic_t>;

auto plan_duration_resize(const chart_document_t& chart, const measure_index_t& index,
                          std::vector<object_id_t> targets, object_id_t reference_id,
                          local_position_t reference_end, snap_grid_t snap,
                          bool reference_end_is_exact = false)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t>;

auto duration_previews(const duration_resize_plan_t& plan) -> std::vector<note_duration_preview_t>;

struct moved_note_t {
    object_id_t id;
    lane_id_t lane;
    std::uint64_t begin{};
    std::uint64_t end{};
    local_position_t position;
    std::optional<local_position_t> end_position;
    note_move_conflict_t conflict{note_move_conflict_t::none};
    std::optional<object_id_t> occupant;
    std::optional<std::int64_t> display_column;
    std::size_t display_columns{};
    std::optional<long double> display_begin_row;
    std::optional<long double> display_end_row;
};

struct note_move_plan_t {
    object_id_t reference_note;
    lane_id_t reference_lane;
    local_position_t reference_position;
    std::vector<object_id_t> notes;
    std::vector<moved_note_t> moved;
    std::vector<object_id_t> occupants;
    std::vector<note_move_bgm_preview_t> bgm_moves;
    // Prepared by the ordinary/linked/virtual BGM policy commands, committed once
    // on release.
    std::optional<song_document_t> staged_bgm_document;
};

struct moved_stem_events_t {
    std::vector<std::size_t> channels;
    std::vector<object_id_t> events;

    [[nodiscard]] auto has_object(object_id_t id) const -> bool {
        return std::ranges::find(events, id) != events.end();
    }
};

auto move_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan)
    -> moved_stem_events_t;

auto landed_on_taken_row(const sound_channel_t& channel, const moved_stem_events_t& moved)
    -> const sound_channel_event_t*;

auto sync_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan,
                              const song_document_t& document) -> std::optional<diagnostic_t>;

struct note_move_parameters_t {
    const note_t* reference{};
    local_position_t snapped_position;
    std::uint64_t delta{};
    bool forward{};
    bool preserve_lanes{};
    lane_id_t target_lane;
    std::int64_t lane_delta{};
    editor_scratch_presentation_t scratch_on_right;
};

auto known_move_lane(const chart_document_t& chart, lane_id_t id) -> const lane_definition_t*;

auto move_lane_column(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, const lane_definition_t& lane,
                      const editor_scratch_presentation_t& sides) -> std::int64_t;

auto resolve_note_move_parameters(const chart_document_t& chart, const measure_index_t& index,
                                  std::vector<object_id_t>& targets, object_id_t reference_id,
                                  local_position_t reference_position, lane_id_t requested_lane,
                                  snap_grid_t snap, bool exact, bool keep_rows,
                                  const editor_scratch_presentation_t& sides)
    -> std::variant<note_move_parameters_t, diagnostic_list_t>;

struct move_destination_t {
    lane_id_t lane;
    std::int64_t column{};
    bool available{};
    std::size_t columns{};
};

struct note_move_context_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    const note_move_parameters_t& parameters;
    const std::unordered_map<std::uint64_t, const note_t*>& targets;
    std::vector<lane_occupant_t>& collisions;

    auto display_columns(local_position_t position, local_position_t source) const -> std::size_t {
        const auto count = [](const lane_layout_t* layout) -> std::size_t {
            return layout ? std::ranges::count_if(layout->lanes, &lane_definition_t::is_editable) : 0;
        };
        const auto current = count(effective_layout(chart, index, position));
        return current ? current
                       : std::max<std::size_t>(1, count(effective_layout(chart, index, source)));
    }

    auto is_lane_available_across(std::uint64_t begin, std::uint64_t end, lane_id_t lane) const
        -> bool {
        const auto first = position_at(index, begin);
        const auto last = position_at(index, end);
        if (!first || !last || !find_active_lane(chart, index, *first, lane) ||
            !find_active_lane(chart, index, *last, lane))
            return false;
        for (const auto& layout : chart.lane_layouts) {
            const auto row = index.row_of(layout.position);
            if (row && *row > begin && *row <= end &&
                !find_active_lane(chart, index, layout.position, lane))
                return false;
        }
        return true;
    }

    auto destination_lane(const note_t& note, local_position_t position)
        -> std::variant<move_destination_t, diagnostic_t> {
        const auto* source = find_active_lane(chart, index, note.position, note.lane);
        if (!source)
            return error(diagnostic_code_t::unknown_lane,
                         "selected note move source lane is unavailable", note.id);
        const auto columns = display_columns(position, note.position);
        if (parameters.preserve_lanes) {
            const auto* active = find_active_lane(chart, index, position, note.lane);
            return move_destination_t{
                note.lane,
                editor_presented_column(
                    *effective_layout(chart, index, active ? position : note.position), note.lane,
                    parameters.scratch_on_right),
                active != nullptr, columns};
        }
        const auto column = editor_presented_column(*effective_layout(chart, index, note.position),
                                                    source->id, parameters.scratch_on_right) +
                            parameters.lane_delta;
        const auto* layout = effective_layout(chart, index, position);
        if (!layout)
            return move_destination_t{{}, column, false, columns};
        const auto* found =
            editor_lane_at_presented_column(*layout, column, parameters.scratch_on_right);
        return found ? move_destination_t{found->id, column, true, columns}
                     : move_destination_t{{}, column, false, columns};
    }
};

struct shifted_move_row_t {
    std::uint64_t row{};
    bool before_chart{};
    bool after_chart{};
};

auto shifted_move_row(std::uint64_t row, std::uint64_t last,
                      const note_move_parameters_t& parameters) -> shifted_move_row_t;

auto display_move_row(std::uint64_t row, const note_move_parameters_t& parameters) -> long double;

auto move_one_note(note_move_context_t& context, object_id_t id, note_move_plan_t& plan)
    -> std::optional<diagnostic_t>;

struct active_move_intervals_t {
    using queue_t =
        std::priority_queue<active_duration_collision_t, std::vector<active_duration_collision_t>,
                            earliest_duration_collision_t>;
    queue_t endings;
    std::set<std::uint64_t> active;
    std::set<std::uint64_t> unmarked;
    std::set<std::uint64_t> unoutlined;

    void expire(std::uint64_t begin) {
        // Holds occupy both endpoints; an equal end remains active.
        while (!endings.empty() && lane_occupant_t::is_expired(endings.top().end, begin)) {
            const auto id = endings.top().note.value;
            active.erase(id);
            unmarked.erase(id);
            unoutlined.erase(id);
            endings.pop();
        }
    }
    void insert(const lane_occupant_t& interval, bool track_outline = false) {
        endings.push({interval.end, interval.note});
        active.insert(interval.note.value);
        unmarked.insert(interval.note.value);
        if (track_outline)
            unoutlined.insert(interval.note.value);
    }
};

struct note_move_collision_sweep_t {
    note_move_plan_t& plan;
    std::unordered_map<std::uint64_t, std::size_t> moved_indices;
    std::set<std::uint64_t> occupants;
    active_move_intervals_t moved;
    active_move_intervals_t existing;
    lane_occupancy_key_t lane{};
    bool has_lane{};

    explicit note_move_collision_sweep_t(note_move_plan_t& proposed) : plan(proposed) {
        for (std::size_t index = 0; index < plan.moved.size(); ++index)
            moved_indices.emplace(plan.moved[index].id.value, index);
    }
    void mark_moved(std::uint64_t id, std::uint64_t occupant) {
        auto& note = plan.moved[moved_indices.at(id)];
        if (note.conflict == note_move_conflict_t::none)
            note.conflict = note_move_conflict_t::occupied;
        if (!note.occupant)
            note.occupant = object_id_t{occupant};
        occupants.insert(occupant);
        moved.unmarked.erase(id);
    }
    void mark_active_moved(std::uint64_t occupant) {
        // Each interval leaves this set on its first conflict, avoiding pair
        // enumeration.
        while (!moved.unmarked.empty())
            mark_moved(*moved.unmarked.begin(), occupant);
    }
    void mark_active_existing() {
        occupants.insert(existing.unmarked.begin(), existing.unmarked.end());
        existing.unmarked.clear();
    }
    void process_moved(const lane_occupant_t& interval) {
        if (!existing.active.empty()) {
            mark_moved(interval.note.value, *existing.active.begin());
            mark_active_existing();
        }
        if (!moved.active.empty()) {
            occupants.insert(moved.unoutlined.begin(), moved.unoutlined.end());
            moved.unoutlined.clear();
            occupants.insert(interval.note.value);
            mark_moved(interval.note.value, *moved.active.begin());
            mark_active_moved(interval.note.value);
        }
        moved.insert(interval, true);
        if (plan.moved[moved_indices.at(interval.note.value)].occupant)
            moved.unmarked.erase(interval.note.value);
        if (occupants.contains(interval.note.value))
            moved.unoutlined.erase(interval.note.value);
    }
    void process_existing(const lane_occupant_t& interval) {
        existing.insert(interval);
        if (!moved.active.empty()) {
            occupants.insert(interval.note.value);
            existing.unmarked.erase(interval.note.value);
            mark_active_moved(interval.note.value);
        }
    }
    void process(const lane_occupant_t& interval) {
        if (!has_lane || interval.lane != lane) {
            moved = {};
            existing = {};
            lane = interval.lane;
            has_lane = true;
        }
        moved.expire(interval.begin);
        existing.expire(interval.begin);
        if (interval.proposed)
            process_moved(interval);
        else
            process_existing(interval);
    }
};

auto merged_move_ranges(const std::vector<lane_occupant_t>& collisions,
                        std::size_t moved_count) -> std::vector<lane_occupant_t>;

auto is_overlapping_move_range(const lane_occupant_t& interval,
                         const std::vector<lane_occupant_t>& ranges) -> bool;

void mark_move_collisions(std::vector<lane_occupant_t>& collisions,
                          note_move_plan_t& plan);

auto plan_note_move(const chart_document_t& chart, const measure_index_t& index,
                    std::vector<object_id_t> targets, object_id_t reference_id,
                    local_position_t reference_position, lane_id_t requested_reference_lane,
                    snap_grid_t snap, bool reference_position_is_exact = false,
                    bool keep_rows = false, const editor_scratch_presentation_t& sides = {})
    -> std::variant<note_move_plan_t, diagnostic_list_t>;

auto selected_move_bgm_ids(const editor_interaction_state_t& state, object_id_t reference,
                           std::span<const object_id_t>) -> std::vector<object_id_t>;

auto translated_object_position(const measure_index_t& index, local_position_t position,
                                std::uint64_t origin, std::uint64_t target)
    -> std::optional<local_position_t>;

auto append_selected_bgm_moves(const chart_document_t& chart, const measure_index_t& index,
                               std::span<const object_id_t> ids, local_position_t origin,
                               note_move_plan_t& plan) -> diagnostic_list_t;

auto plan_selected_object_move(const chart_document_t& chart, const measure_index_t& index,
                               const editor_interaction_state_t& state,
                               std::vector<object_id_t> notes, object_id_t reference,
                               local_position_t position, lane_id_t lane, std::uint64_t snap,
                               bool exact) -> std::variant<note_move_plan_t, diagnostic_list_t>;

auto stage_single_bgm_move(document_history_t& scratch, chart_id_t chart, const bgm_event_t& source,
                           local_position_t position, object_id_t lane,
                           std::optional<std::uint64_t> canvas_row, std::uint64_t snap)
    -> document_command_result_t;

auto plan_single_bgm_move(const document_history_t& history,
                          const editor_interaction_state_t& state, const bgm_event_t& source,
                          local_position_t requested, std::optional<object_id_t> lane,
                          std::optional<std::uint64_t> canvas_row, bool exact)
    -> std::variant<note_move_plan_t, diagnostic_list_t>;

auto plan_move_gesture(const editor_edit_context_t& context,
                       std::vector<object_id_t> targets, object_id_t reference,
                       local_position_t position, lane_id_t lane, bool exact_position,
                       std::optional<object_id_t> bgm_lane,
                       std::optional<std::uint64_t> virtual_row)
    -> std::variant<note_move_plan_t, diagnostic_list_t>;

auto relocate_selected_bgm(song_document_t& proposed, chart_id_t chart_id,
                           const note_move_plan_t& plan) -> diagnostic_list_t;

auto move_previews(const note_move_plan_t& plan) -> std::vector<note_move_preview_t>;

auto move_conflict_count(const note_move_plan_t& plan) -> std::size_t;

auto proposed_playhead_cursor(const document_history_t& history, const chart_document_t& chart,
                              const measure_index_t& index, const editor_interaction_state_t& state,
                              local_position_t position)
    -> std::variant<editor_interaction_state_t, diagnostic_list_t>;
} // namespace interaction_detail
using interaction_detail::active_duration_collision_t;
using interaction_detail::active_move_intervals_t;
using interaction_detail::canvas_cursor_t;
using interaction_detail::duration_collision_sweep_t;
using interaction_detail::duration_resize_context_t;
using interaction_detail::duration_resize_plan_t;
using interaction_detail::earliest_duration_collision_t;
using interaction_detail::hold_overlap_policy_t;
using interaction_detail::hold_placement_plan_t;
using interaction_detail::hold_row_expansion_t;
using interaction_detail::hold_row_plan_t;
using interaction_detail::move_destination_t;
using interaction_detail::moved_note_t;
using interaction_detail::moved_stem_events_t;
using interaction_detail::note_move_collision_sweep_t;
using interaction_detail::note_move_context_t;
using interaction_detail::note_move_parameters_t;
using interaction_detail::note_move_plan_t;
using interaction_detail::note_paint_plan_t;
using interaction_detail::paint_overlap_sweep_t;
using interaction_detail::paint_row_t;
using interaction_detail::resize_parameters_t;
using interaction_detail::resized_note_t;
using interaction_detail::shifted_move_row_t;
using interaction_detail::view_geometry_t;
auto cancel_pointer_gesture(editor_edit_context_t& context) -> editor_interaction_result_t;
auto make_duration_plan(editor_edit_context_t& context, std::vector<object_id_t> targets,
                        object_id_t reference, local_position_t end, bool exact_end = false)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t>;


struct editor_delete_ids_t {
    std::unordered_set<std::uint64_t> notes;
    std::unordered_set<std::uint64_t> bgm_events;
    // Chart-level objects that are not on a measure; IDs are unique within each
    // family.
    std::unordered_set<std::uint64_t> bookmarks;
    std::unordered_set<std::uint64_t> sections;
    std::unordered_set<std::uint64_t> bga_events;
    // Measures whose starting bar line is removed by merging them into the measure before.
    std::unordered_set<std::uint64_t> bar_lines;

    [[nodiscard]] auto total() const -> std::size_t {
        return notes.size() + bgm_events.size() + bookmarks.size() + sections.size() +
               bga_events.size() + bar_lines.size();
    }
    [[nodiscard]] auto family(editor_selection_object_kind_t kind)
        -> std::unordered_set<std::uint64_t>* {
        switch (kind) {
        case editor_selection_object_kind_t::note:
            return &notes;
        case editor_selection_object_kind_t::bgm_event:
            return &bgm_events;
        case editor_selection_object_kind_t::bookmark:
            return &bookmarks;
        case editor_selection_object_kind_t::section:
            return &sections;
        case editor_selection_object_kind_t::bga_event:
            return &bga_events;
        case editor_selection_object_kind_t::bar_line:
            return &bar_lines;
        default:
            return nullptr;
        }
    }
};
auto collect_delete_object_ids(const std::vector<editor_selection_object_t>& objects)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t>;
auto collect_delete_ids(const editor_interaction_state_t& state)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t>;
auto validate_delete_note_ids(const chart_document_t& chart,
                              const std::unordered_set<std::uint64_t>& ids)
    -> std::optional<diagnostic_t>;
auto inspect_delete_bgm_events(const chart_document_t& chart, const editor_delete_ids_t& ids)
    -> std::variant<std::optional<object_id_t>, diagnostic_list_t>;

auto stage_delete_stem_points(song_document_t& proposed, const editor_edit_context_t& context,
                              const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t>;
struct editor_edit_effect_t;
struct prepared_editor_edit_t;
auto stage_editor_selection_delete(document_history_t& history, editor_interaction_state_t& state)
    -> attempt_t<prepared_editor_edit_t>;
auto stage_editor_tap_placement(document_history_t& history, editor_interaction_state_t& state,
                                const place_tap_action_t& action) -> attempt_t<prepared_editor_edit_t>;
auto stage_editor_tap(editor_edit_context_t& context, document_command_transaction_t& transaction,
                       const place_tap_action_t& action) -> attempt_t<editor_edit_effect_t>;
auto delete_editor_selection(editor_edit_context_t& context) -> editor_interaction_result_t;
struct assignment_candidate_t {
    const note_t* note{};
    std::uint64_t begin{};
    std::uint64_t end{};
};
struct assignment_interval_t {
    std::uint64_t begin{};
    std::uint64_t end{};
    object_id_t note;
    bool moving{};
};
auto collect_assignment_candidates(const editor_edit_context_t& context)
    -> std::variant<std::vector<assignment_candidate_t>, diagnostic_list_t>;
auto build_assignment_intervals(const editor_edit_context_t& context,
                                std::span<const assignment_candidate_t> candidates,
                                lane_id_t target_lane)
    -> std::variant<std::vector<assignment_interval_t>, diagnostic_list_t>;
auto validate_assignment_intervals(std::span<const assignment_interval_t> intervals)
    -> std::optional<diagnostic_t>;
auto apply_note_lane_assignment_document(document_history_t& history, chart_id_t chart_id,
                                         std::span<const assignment_candidate_t> candidates,
                                         lane_id_t lane, std::string label)
    -> document_command_result_t;
auto assign_selected_notes_to_lane(editor_edit_context_t& context,
                                   const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t;
struct lane_move_candidate_t {
    std::size_t measure{};
    std::uint64_t row{};
    editor_selection_object_kind_t kind{};
    object_id_t id;
    lane_id_t lane;
};
auto lane_move_candidate_less(const lane_move_candidate_t& left, const lane_move_candidate_t& right) -> bool;
auto first_selected_lane_object(const chart_document_t& chart, std::span<const object_id_t> notes,
                                std::span<const object_id_t> bgm_events)
    -> std::optional<lane_move_candidate_t>;
auto selected_lane_assignment_ids(const editor_interaction_state_t& state)
    -> std::pair<std::vector<object_id_t>, std::vector<object_id_t>>;
auto assign_selected_objects_to_lane(editor_edit_context_t& context,
                                     const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t;
auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<diagnostic_t>;
auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t>;
void prepare_rejected_bgm_preview(const chart_document_t& chart, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending);
void prepare_selected_bgm_preview(editor_edit_context_t& context, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending);
auto prepare_bgm_move_preview(editor_edit_context_t& context, const note_move_plan_t& plan,
                              pending_note_move_gesture_t& pending) -> void;
auto add_paint_path_cell(const editor_edit_context_t& context, lane_id_t lane, std::uint64_t row,
                         const pending_note_paint_gesture_t& pending,
                         std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t>;
auto add_measure_grid_paint_path(editor_edit_context_t& context, lane_id_t lane,
                                 std::uint64_t first, std::uint64_t last,
                                 pending_note_paint_gesture_t& pending,
                                 std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t>;
auto is_paint_sample_resource(const song_document_t& document, resource_id_t id) -> bool;
auto entry_stem_channel(const song_document_t& document, const chart_document_t& chart,
                        std::optional<resource_id_t> sound)
    -> std::variant<std::optional<object_id_t>, diagnostic_list_t>;
struct prepared_note_paint_t {
    song_document_t document;
    std::vector<object_id_t> painted_notes;
    void set_hitsample(chart_id_t chart, const std::optional<osu_hitsample_t>& sample);
};
auto prepare_stem_note_paint(const document_history_t& history, chart_id_t chart_id,
                             const note_paint_plan_t& plan, object_id_t channel,
                             const std::optional<std::vector<layer_id_t>>& armed_layers,
                             std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t>;
auto stage_scoped_note_paint(document_command_transaction_t& transaction, chart_id_t chart,
    const note_paint_plan_t& plan, const note_t& defaults) -> attempt_t<std::vector<object_id_t>>;
auto prepare_note_paint_document(const document_history_t& history, chart_id_t chart_id,
                                 const note_paint_plan_t& plan, note_kind_t kind,
                                 std::optional<resource_id_t> sound,
                                 const std::optional<std::vector<layer_id_t>>& armed_layers,
                                 std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t>;
auto update_fixed_scroll_cursor(editor_edit_context_t& context,
                                const scroll_editor_view_action_t& action,
                                editor_interaction_state_t& proposed)
    -> std::variant<std::monostate, diagnostic_list_t>;
auto preserve_keyboard_hold_after_scroll(editor_edit_context_t& context,
                                         const editor_interaction_state_t& proposed)
    -> std::optional<editor_interaction_result_t>;
auto scroll_editor_view(editor_edit_context_t& context, const scroll_editor_view_action_t& action)
    -> editor_interaction_result_t;
auto is_keyboard_entry_editable(const chart_document_t& chart, const note_t& note) -> bool;
auto keyboard_entry_occupant(const chart_document_t& chart, const measure_index_t& index,
                             lane_id_t lane, local_position_t target)
    -> std::variant<const note_t*, diagnostic_list_t>;
struct prepared_keyboard_entry_t {
    song_document_t document;
    std::optional<object_id_t> created;
};
auto stage_keyboard_occupant(document_history_t& scratch, editor_interaction_state_t state,
                             const note_t& occupant, bool to_bgm) -> diagnostic_list_t;
auto place_keyboard_entry(document_history_t& scratch, editor_interaction_state_t state,
                          lane_id_t lane, local_position_t target, resource_id_t sound,
                          std::optional<object_id_t> stem,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<object_id_t, diagnostic_list_t>;
auto stage_keyboard_entry(const editor_edit_context_t& context, lane_id_t lane,
                          local_position_t target, const note_t* occupant,
                          std::optional<resource_id_t> sound, bool displace,
                          std::optional<object_id_t> stem,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_keyboard_entry_t, diagnostic_list_t>;
auto prepare_stem_hold_document(const editor_edit_context_t& context, const pending_hold_t& pending,
                                const hold_placement_plan_t& placement, object_id_t channel,
                                std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t>;
auto commit_stem_hold(editor_edit_context_t& context, const pending_hold_t& pending,
                      const hold_placement_plan_t& placement, local_position_t target,
                      std::optional<bmson_source_geometry_t> geometry)
    -> std::optional<editor_interaction_result_t>;
auto combine_editor_area_selection(const editor_interaction_state_t& state,
                                   const std::vector<editor_selection_object_t>& hits,
                                   editor_area_selection_mode_t mode,
                                   const editor_hit_test_index_t& hit_index)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;
auto validate_virtual_note_target(const editor_edit_context_t& context, lane_id_t lane,
                                  std::string_view description)
    -> std::optional<editor_interaction_result_t>;
auto extend_virtual_chart(chart_document_t& chart, std::uint64_t measure_count)
    -> std::optional<diagnostic_list_t>;
auto snap_virtual_row(const editor_edit_context_t& context, const virtual_canvas_t& canvas,
                      std::uint64_t row) -> std::optional<std::uint64_t>;
struct virtual_hold_rows_t {
    std::uint64_t anchor{};
    std::uint64_t end{};
    std::uint64_t measure_count{};
};
auto resolve_virtual_hold_rows(const editor_edit_context_t& context,
                               const place_virtual_bms_hold_action_t& action)
    -> std::variant<virtual_hold_rows_t, diagnostic_list_t>;

auto commit_virtual_hold(editor_edit_context_t& context,
                         const place_virtual_bms_hold_action_t& action,
                         const virtual_hold_rows_t& rows, const pending_hold_t* captured = nullptr) -> editor_interaction_result_t;
auto place_virtual_bms_hold(editor_edit_context_t& context,
                            const place_virtual_bms_hold_action_t& action)
    -> editor_interaction_result_t;
struct virtual_tap_target_t {
    std::uint64_t row{};
    std::uint64_t measure{};
    std::uint64_t measure_begin_row{};
};
auto resolve_virtual_tap_target(const editor_edit_context_t& context,
                                const place_virtual_bms_tap_action_t& action)
    -> std::variant<virtual_tap_target_t, diagnostic_list_t>;
auto validate_virtual_tap_sample(const editor_edit_context_t& context,
                                 std::optional<resource_id_t> sample)
    -> std::optional<editor_interaction_result_t>;
auto place_virtual_bms_tap(editor_edit_context_t& context,
                           const place_virtual_bms_tap_action_t& action)
    -> editor_interaction_result_t;
auto cycle_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                       const cycle_edit_snap_action_t& action) -> editor_interaction_result_t;
auto set_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                     const set_edit_snap_action_t& action) -> editor_interaction_result_t;
auto adjust_editor_zoom(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index,
                        std::uint64_t absolute, const adjust_editor_zoom_action_t& action)
    -> editor_interaction_result_t;
auto place_tap(editor_edit_context_t& context, const place_tap_action_t& action)
    -> editor_interaction_result_t;
auto semantic_paint_edit(chart_id_t chart, const pending_semantic_layer_paint_gesture_t& pending)
    -> semantic_layer_edit_action_t;
auto audition_selected_object(bool requested, const std::vector<editor_selection_object_t>& hits,
                              const editor_interaction_state_t& state, std::uint64_t revision)
    -> std::optional<editor_object_audition_request_t>;
auto select_editor_objects(editor_edit_context_t& context,
                           const select_editor_objects_action_t& action)
    -> editor_interaction_result_t;
auto is_valid_area_selection_capture(const begin_editor_area_selection_action_t& request,
                                  double viewport_height) -> bool;
auto snapped_area_selection_row(const editor_edit_context_t& context,
                                const editor_interaction_view_model_t& view, double y)
    -> std::optional<std::uint64_t>;
auto retain_area_selection_range(editor_edit_context_t& context,
                                 const pending_editor_area_selection_t& pending) -> void;
auto handle_editor_cancellation(document_history_t& history, editor_interaction_state_t& state,
                                const editor_interaction_action_t& action)
    -> std::optional<editor_interaction_result_t>;
template <class... Actions> auto is_gesture_action(const editor_interaction_action_t& action) -> bool {
    return (std::holds_alternative<Actions>(action) || ...);
}
auto is_keyboard_hold_action_allowed(const editor_interaction_action_t& action) -> bool;
auto is_hold_gesture_action_allowed(const editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> bool;
auto is_gesture_action_allowed(editor_gesture_kind_t kind, const editor_interaction_state_t& state,
                            const editor_interaction_action_t& action) -> bool;
auto gesture_finish_message(editor_gesture_kind_t kind) -> const char*;
auto validate_active_gesture_action(const editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action)
    -> std::optional<editor_interaction_result_t>;
auto apply_duration_resize_plan(editor_edit_context_t& context, const duration_resize_plan_t& plan) -> editor_interaction_result_t;
auto has_note_move_changes(const chart_document_t& chart, const note_move_plan_t& plan) -> bool;
auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<diagnostic_t>;
auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t>;
// `bar_line_shift`, when set, moves the selection's bar lines by the drag's rows in the same commit.
auto apply_note_move_plan(editor_edit_context_t& context, const note_move_plan_t& plan,
                          const shift_selection_action_t* bar_line_shift = nullptr)
    -> editor_interaction_result_t;
// Keeps the moved bar lines selected after a commit replaced the selection.
void reselect_bar_lines(editor_interaction_state_t& state, const shift_selection_action_t* bar_line_shift);
auto select_editor_notes(editor_edit_context_t& context, const select_editor_notes_action_t& action)
    -> editor_interaction_result_t;
auto select_editor_lane(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index, lane_id_t lane)
    -> editor_interaction_result_t;

// Field/view preparation and the action visitor run after the gesture boundary.
auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
    const editor_interaction_action_t& action) -> editor_interaction_result_t;
} // namespace wormedit
