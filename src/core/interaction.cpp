#include <wormedit/core/interaction.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/dead_span_rules.h>

#include "interaction_cursor.h"

#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/virtual_canvas.h>

#include <algorithm>
#include <cmath>
#include <functional>
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
auto editor_bga_lane_index(visual_event_kind_t kind) -> std::optional<std::size_t> {
    switch (kind) {
    case visual_event_kind_t::background:
    case visual_event_kind_t::video: return 0;
    case visual_event_kind_t::layer: return 1;
    case visual_event_kind_t::layer2: return 2;
    case visual_event_kind_t::poor: return 3;
    case visual_event_kind_t::storyboard: return std::nullopt;
    }
    return std::nullopt;
}

auto editor_bga_visible_lane_mask(const chart_document_t& chart, std::uint8_t revealed)
    -> std::uint8_t {
    auto mask = static_cast<std::uint8_t>(revealed & editor_bga_lane_mask_all);
    for (const auto& event : chart.bga_events)
        if (const auto lane = editor_bga_lane_index(event.kind))
            mask |= static_cast<std::uint8_t>(1U << *lane);
    return mask;
}

auto interaction_view_result(const document_history_t& history,
                             const editor_interaction_state_t& state,
                             bool changed) -> editor_interaction_result_t;
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

auto bms_branch_source_orders(const chart_document_t& chart)
    -> std::unordered_set<std::size_t> {
    std::unordered_set<std::size_t> orders;
    if (!chart.bms_control_tree)
        return orders;
    const auto visit = [&](const auto& self, const bms_control_node_t& node,
                           bool inside_branch) -> void {
        if (inside_branch)
            orders.insert(node.source_order);
        const auto boundary = node.kind == bms_control_kind_t::random ||
                              node.kind == bms_control_kind_t::switch_block ||
                              node.kind == bms_control_kind_t::if_group ||
                              node.kind == bms_control_kind_t::if_branch ||
                              node.kind == bms_control_kind_t::else_if_branch ||
                              node.kind == bms_control_kind_t::else_branch ||
                              node.kind == bms_control_kind_t::case_branch ||
                              node.kind == bms_control_kind_t::default_branch;
        for (const auto& child : node.children)
            self(self, child, inside_branch || boundary);
    };
    visit(visit, *chart.bms_control_tree, false);
    return orders;
}

auto position_at(const measure_index_t& index, std::uint64_t absolute)
    -> std::optional<local_position_t> {
    return position_at_absolute_row(index, absolute);
}

struct hold_row_plan_t {
    std::uint64_t head_row{};
    std::uint64_t tail_row{};
    std::vector<object_id_t> removals;
    bool adds_note{};
};

auto is_plain_editable_note(const note_t& note) -> bool {
    const auto plain_kind = (note.kind == note_kind_t::tap && !note.end.has_value()) ||
                            (note.kind == note_kind_t::hold && note.end.has_value());
    return plain_kind && !note.sound.has_value() && note.semantic_layers.empty() &&
           !note.label.has_value() && !note.branch.has_value();
}

// Keyboard deletion may remove a sounded note, but never use it as a merge target.
// Preserve source-linked/labelled objects and hidden or locked memberships on this fast path.
auto is_removable_sound_note(const chart_document_t& chart, const note_t& note) -> bool {
    if (!note.sound || note.sound_channel_event || note.label || note.branch) return false;
    return std::ranges::all_of(note.semantic_layers, [&](layer_id_t id) {
        const auto layer = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        return layer != chart.semantic_layers.end() && layer->visible && !layer->locked;
    });
}

enum class hold_overlap_policy_t { merge_plain, remove_sound };

struct hold_row_expansion_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    lane_id_t lane;
    std::uint64_t begin_row;
    std::uint64_t end_row;
    std::unordered_set<std::uint64_t> removals;
    hold_overlap_policy_t overlap_policy{hold_overlap_policy_t::merge_plain};

    auto intersects(std::uint64_t begin, std::uint64_t end) const -> bool {
        return begin <= end_row && end >= begin_row;
    }

    auto expand_once() -> std::variant<bool, diagnostic_list_t> {
        auto expanded = false;
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                if (note.lane != lane) continue;
                const auto note_begin = absolute_row(index, note.position);
                const auto note_end = note.end ? absolute_row(index, *note.end) : note_begin;
                if (!note_begin || !note_end || *note_end < *note_begin ||
                    !intersects(*note_begin, *note_end)) continue;
                if (!is_plain_editable_note(note) &&
                    !(overlap_policy == hold_overlap_policy_t::remove_sound &&
                      is_removable_sound_note(chart, note)))
                    return diagnostic_list_t{error(diagnostic_code_t::collision,
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

auto plan_hold_rows(const chart_document_t& chart, const measure_index_t& index,
                    lane_id_t lane, std::uint64_t anchor_row, std::uint64_t moving_row,
                    hold_overlap_policy_t overlap_policy = hold_overlap_policy_t::merge_plain)
    -> std::variant<hold_row_plan_t, diagnostic_list_t> {
    hold_row_expansion_t expansion{.chart = chart, .index = index, .lane = lane,
        .begin_row = std::min(anchor_row, moving_row),
        .end_row = std::max(anchor_row, moving_row), .removals = {},
        .overlap_policy = overlap_policy};
    while (true) {
        auto result = expansion.expand_once();
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&result)) return *diagnostics;
        if (!std::get<bool>(result)) break;
    }
    hold_row_plan_t result{
        .head_row = expansion.begin_row,
        .tail_row = expansion.end_row,
        .removals = {},
        .adds_note = expansion.begin_row != expansion.end_row || expansion.removals.empty(),
    };
    result.removals.reserve(expansion.removals.size());
    for (const auto id : expansion.removals)
        result.removals.push_back(object_id_t{id});
    std::ranges::sort(result.removals, {}, &object_id_t::value);
    return result;
}

auto canvas_keyboard_hold_previews(const chart_document_t& chart, const measure_index_t& index,
                                   std::uint64_t canvas_rows, std::uint64_t revision,
                                   const editor_interaction_state_t& state)
    -> std::variant<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>,
                    diagnostic_list_t> {
    std::vector<editor_interaction_view_model_t::virtual_hold_preview_t> previews;
    auto pending = state.additional_keyboard_holds;
    if (state.pending_hold) pending.push_back(*state.pending_hold);
    for (const auto& hold : pending) {
        if (!hold.anchor_canvas_row && !hold.preview_canvas_row) continue;
        const auto anchor = hold.anchor_canvas_row
                                ? hold.anchor_canvas_row
                                : absolute_row(index, hold.position);
        const auto end = hold.preview_canvas_row ? hold.preview_canvas_row : anchor;
        if (!chart_has_virtual_tail(chart) ||
            hold.document_revision != revision || !anchor || !end ||
            *anchor >= canvas_rows || *end >= canvas_rows) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           "canvas lane hold is stale or outside BMS geometry")};
        }
        auto planned = plan_hold_rows(chart, index, hold.lane, *anchor, *end);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
            return *diagnostics;
        auto rows = std::get<hold_row_plan_t>(std::move(planned));
        previews.push_back({.anchor_row = *anchor,
                            .end_row = *end,
                            .head_row = rows.head_row,
                            .tail_row = rows.head_row == rows.tail_row
                                            ? std::optional<std::uint64_t>{}
                                            : std::optional{rows.tail_row},
                            .lane = hold.lane,
                            .document_revision = revision,
                            .removals = std::make_shared<const std::vector<object_id_t>>(
                                std::move(rows.removals)),
                            .adds_note = rows.adds_note});
    }
    return previews;
}

template <typename Values>
void add_ids(std::unordered_set<std::uint64_t>& ids, const Values& values) {
    for (const auto& value : values) ids.insert(value.id.value);
}

// Existing IDs of one selectable object family; scanned once per revision change.
auto existing_ids(const chart_document_t& chart, editor_selection_object_kind_t kind)
    -> std::unordered_set<std::uint64_t> {
    using kind_t = editor_selection_object_kind_t;
    std::unordered_set<std::uint64_t> ids;
    switch (kind) {
    case kind_t::note:
        for (const auto& measure : chart.measures) add_ids(ids, measure.notes);
        break;
    case kind_t::bgm_event:
        for (const auto& measure : chart.measures) add_ids(ids, measure.bgm_events);
        break;
    case kind_t::bpm_event: add_ids(ids, chart.bpms); break;
    case kind_t::stop_event:
    case kind_t::delay_event: add_ids(ids, chart.pauses); break;
    case kind_t::warp_event: add_ids(ids, chart.warps); break;
    case kind_t::scroll_event: add_ids(ids, chart.scrolls); break;
    case kind_t::speed_event: add_ids(ids, chart.speeds); break;
    case kind_t::kiai_event: add_ids(ids, chart.kiai); break;
    case kind_t::timing_anchor: add_ids(ids, chart.timing_anchors); break;
    case kind_t::bga_event: add_ids(ids, chart.bga_events); break;
    case kind_t::bookmark: add_ids(ids, chart.bookmarks); break;
    case kind_t::section: add_ids(ids, chart.sections); break;
    }
    return ids;
}

auto span_measures_exist(const chart_document_t& chart, const position_span_t& span) -> bool {
    const auto has = [&](measure_id_t id) {
        return std::ranges::any_of(chart.measures,
                                   [id](const auto& measure) { return measure.id == id; });
    };
    return has(span.begin.measure) && has(span.end.measure);
}

auto find_active_lane(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, lane_id_t id) -> const lane_definition_t*;

auto virtual_lane_key_preview(const chart_document_t& chart, const measure_index_t& index,
                              std::uint64_t canvas_rows, std::uint64_t revision,
                              const editor_interaction_state_t& state,
                              local_position_t lane_position, std::uint64_t cursor_row)
    -> std::variant<std::optional<editor_interaction_view_model_t::virtual_hold_preview_t>,
                    diagnostic_list_t> {
    if (!state.pending_virtual_lane_key)
        return std::optional<editor_interaction_view_model_t::virtual_hold_preview_t>{};
    const auto& pending = *state.pending_virtual_lane_key;
    if (pending.document_revision != revision || !chart_has_virtual_tail(chart) ||
        pending.canvas_row < index.total_rows || pending.canvas_row >= canvas_rows ||
        !find_active_lane(chart, index, lane_position, pending.lane)) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "virtual BMS lane-key preview is stale or invalid")};
    }
    auto planned = plan_hold_rows(chart, index, pending.lane, pending.canvas_row, cursor_row);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return *diagnostics;
    auto rows = std::get<hold_row_plan_t>(std::move(planned));
    return std::optional{editor_interaction_view_model_t::virtual_hold_preview_t{
        .anchor_row = pending.canvas_row,
        .end_row = cursor_row,
        .head_row = rows.head_row,
        .tail_row = rows.head_row == rows.tail_row
                        ? std::optional<std::uint64_t>{}
                        : std::optional{rows.tail_row},
        .lane = pending.lane,
        .document_revision = pending.document_revision,
        .removals = std::make_shared<const std::vector<object_id_t>>(
            std::move(rows.removals)),
        .adds_note = rows.adds_note}};
}

auto normalized_position_span(const measure_index_t& index, local_position_t first,
                              local_position_t second) -> std::optional<position_span_t> {
    const auto first_row = absolute_row(index, first);
    const auto second_row = absolute_row(index, second);
    if (!first_row || !second_row || *first_row == *second_row) {
        return std::nullopt;
    }
    return *first_row < *second_row ? position_span_t{first, second}
                                    : position_span_t{second, first};
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
    state.selected_canvas_measure.reset();
    state.selected_notes = notes_from_objects(objects);
    state.selected_objects = std::move(objects);
}

auto set_selected_notes(editor_interaction_state_t& state, std::vector<object_id_t> notes) -> void {
    state.selected_canvas_measure.reset();
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
                             double scale, double viewport_height,
                             double end_fraction = editor_cursor_edge_margin_fraction) -> std::optional<double> {
    auto total_rows = index.total_rows;
    if (chart_has_virtual_tail(chart)) {
        const auto canvas = build_virtual_canvas(chart);
        if (!std::holds_alternative<virtual_canvas_t>(canvas)) {
            return std::nullopt;
        }
        total_rows = std::get<virtual_canvas_t>(canvas).total_rows;
    }
    const auto content_height = static_cast<double>(total_rows) * scale /
                                static_cast<double>(chart.divisions_per_standard_measure);
    if (!std::isfinite(content_height) || content_height < 0.0 || !std::isfinite(viewport_height) ||
        viewport_height <= 0.0) {
        return std::nullopt;
    }
    if (!editor_viewport_margins_valid(editor_cursor_edge_margin_fraction, end_fraction))
        return std::nullopt;
    return editor_maximum_viewport_scroll(content_height, viewport_height, end_fraction);
}

auto minimum_viewport_scroll(double viewport_height) -> double {
    return -viewport_height * editor_cursor_edge_margin_fraction;
}

auto absolute_row_at_y_pixels(const chart_document_t& chart, const measure_index_t& index,
                              const editor_interaction_view_model_t& view, double y_pixels,
                              bool include_virtual_bms_tail = false)
    -> std::optional<std::uint64_t> {
    auto total_rows = index.total_rows;
    if (include_virtual_bms_tail && chart_has_virtual_tail(chart)) {
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
    const auto nearest = std::floor(row + 0.5L);
    if (nearest >= static_cast<long double>(total_rows)) {
        return total_rows - 1;
    }
    return static_cast<std::uint64_t>(nearest);
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
    -> std::variant<canvas_cursor_t, diagnostic_list_t> {
    auto absolute = absolute_row(index, state.cursor);
    if (!absolute)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "editor cursor lies outside chart geometry")};
    auto rows = index.total_rows;
    if (chart_has_virtual_tail(chart)) {
        const auto canvas = build_virtual_canvas(chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&canvas)) return *diagnostics;
        const auto& built = std::get<virtual_canvas_t>(canvas);
        rows = built.total_rows;
        if (state.virtual_bms_cursor_row) {
            const auto measure = built.measure_containing(*state.virtual_bms_cursor_row);
            if (!measure || measure->authored)
                return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                               "virtual cursor lies outside the BMS tail")};
            absolute = *state.virtual_bms_cursor_row;
        }
    } else if (state.virtual_bms_cursor_row) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "virtual cursor requires a BMS chart")};
    }
    return canvas_cursor_t{.absolute_row = *absolute, .canvas_rows = rows};
}

auto valid_view_snap(const chart_document_t& chart,
                     const editor_interaction_state_t& state) -> bool {
    return state.active_snap_denominator != 0 &&
        std::ranges::find(state.supported_snap_denominators, state.active_snap_denominator) !=
            state.supported_snap_denominators.end() &&
        chart.divisions_per_standard_measure % state.active_snap_denominator == 0;
}

auto valid_viewport(const chart_document_t& chart, const measure_index_t& index,
                    const editor_interaction_state_t& state) -> bool {
    const auto maximum = maximum_viewport_scroll(
        chart, index, state.pixels_per_standard_measure, state.viewport_height_pixels, state.viewport_end_margin_fraction);
    if (!std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        state.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !maximum || !std::isfinite(state.viewport_scroll_pixels) ||
        !editor_viewport_margins_valid(state.viewport_minimum_margin_fraction,
                                       state.viewport_end_margin_fraction) ||
        state.viewport_scroll_pixels < -state.viewport_height_pixels * state.viewport_minimum_margin_fraction ||
        state.viewport_scroll_pixels > *maximum) return false;
    if (state.fixed_cursor_screen_y_pixels &&
        (!std::isfinite(*state.fixed_cursor_screen_y_pixels) ||
         *state.fixed_cursor_screen_y_pixels < 0.0 ||
         *state.fixed_cursor_screen_y_pixels > state.viewport_height_pixels ||
         (state.scroll_direction != editor_field_scroll_direction_t::up &&
          state.scroll_direction != editor_field_scroll_direction_t::down))) return false;
    return true;
}

auto resolve_view_geometry(const document_history_t& history,
                           const editor_interaction_state_t& state,
                           const measure_index_t* retained = nullptr)
    -> std::variant<view_geometry_t, diagnostic_list_t> {
    const auto* chart = find_chart(history.document, state.chart);
    if (!chart)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "editor interaction refers to an unknown chart")};
    // A heap-stable owner keeps the reference valid when the result variant moves.
    // Retained geometry is borrowed only for this synchronous inspection call.
    std::unique_ptr<measure_index_t> owned;
    if (!retained) {
        auto built = build_measure_index(*chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) return *diagnostics;
        owned = std::make_unique<measure_index_t>(std::get<measure_index_t>(std::move(built)));
        retained = owned.get();
    }
    const auto& index = *retained;
    const auto canvas = resolve_canvas_cursor(*chart, index, state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&canvas)) return *diagnostics;
    const auto cursor = std::get<canvas_cursor_t>(canvas);
    if (state.selected_canvas_measure &&
        (!chart_has_virtual_tail(*chart) ||
         *state.selected_canvas_measure >= virtual_tail_measure_limit(*chart)))
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "selected BMS measure is outside #000-#999")};
    const auto lane_position = state.virtual_bms_cursor_row && !index.entries.empty()
        ? local_position_t{index.entries.back().id, index.entries.back().row_capacity - 1}
        : state.cursor;
    const auto* lane = find_active_lane(*chart, index, lane_position, state.active_lane);
    if (!lane)
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "active editor lane is unavailable at the cursor")};
    if (!valid_view_snap(*chart, state))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "active editor snap is invalid for this chart")};
    if (!valid_viewport(*chart, index, state))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "editor viewport geometry is invalid")};
    return view_geometry_t{.chart = chart, .owned_index = std::move(owned), .index = index,
        .absolute_row = cursor.absolute_row, .canvas_rows = cursor.canvas_rows,
        .lane_position = lane_position, .lane = lane};
}

auto space_range_objects(const chart_document_t& chart, const editor_hit_test_index_t& index,
                         std::uint64_t first, std::uint64_t last,
                         editor_selection_scope_t scope)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    if (first == last) return std::vector<editor_selection_object_t>{};
    return editor_objects_in_row_range(chart, index, first, last - 1, scope);
}

auto space_range_bounds(const pending_space_range_selection_t& pending,
                        std::uint64_t cursor_row, std::uint64_t canvas_rows)
    -> canvas_row_span_t {
    const auto first = std::min(pending.anchor_canvas_row, cursor_row);
    const auto minimum_width = std::min(pending.minimum_span_rows, canvas_rows - first);
    return {first, std::max(std::max(pending.anchor_canvas_row, cursor_row),
                            first + minimum_width)};
}

auto make_space_range_preview(const document_history_t& history,
                              const editor_interaction_state_t& state,
                              const view_geometry_t& geometry)
    -> std::variant<std::optional<space_range_selection_preview_t>, diagnostic_list_t> {
    if (!state.pending_space_range_selection)
        return std::optional<space_range_selection_preview_t>{};
    const auto& pending = *state.pending_space_range_selection;
    if (pending.document_revision != history.document.revision || !pending.hit_index ||
        pending.hit_index->document_revision != history.document.revision ||
        pending.hit_index->chart != geometry.chart->id)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "Space range selection is stale after a document edit")};
    if (pending.anchor_canvas_row >= geometry.canvas_rows)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "Space range anchor lies outside chart geometry")};
    const auto [first, last] = space_range_bounds(pending, geometry.absolute_row, geometry.canvas_rows);
    auto queried = space_range_objects(*geometry.chart, *pending.hit_index, first, last,
                                      pending.scope);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried)) return *diagnostics;
    auto objects = std::get<std::vector<editor_selection_object_t>>(std::move(queried));
    auto notes = notes_from_objects(objects);
    return std::optional{space_range_selection_preview_t{
        .document_revision = pending.document_revision, .anchor = pending.anchor,
        .cursor = state.cursor, .anchor_canvas_row = pending.anchor_canvas_row,
        .cursor_canvas_row = geometry.absolute_row, .first_row = first, .last_row = last,
        .mode = pending.mode,
        .notes = std::make_shared<const std::vector<object_id_t>>(std::move(notes)),
        .objects = std::make_shared<const std::vector<editor_selection_object_t>>(
            std::move(objects))}};
}

auto valid_retained_canvas_range(const editor_interaction_state_t& state,
                                 std::uint64_t canvas_rows) -> bool {
    return !state.retained_canvas_selection_range ||
        (state.retained_canvas_selection_range->begin_row < canvas_rows &&
         state.retained_canvas_selection_range->end_row < canvas_rows &&
         state.retained_canvas_selection_range->begin_row <=
             state.retained_canvas_selection_range->end_row);
}

auto area_selection_anchor_y(const pending_editor_area_selection_t& pending,
                              double viewport_scroll) -> double {
    if (!pending.object_geometry || !pending.object_geometry->projection)
        return pending.anchor_y_pixels;
    return pending.anchor_y_pixels + pending.object_geometry->projection->viewport_scroll_pixels -
           viewport_scroll;
}

auto make_view(const document_history_t& history, const editor_interaction_state_t& state,
               const measure_index_t* retained = nullptr)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    auto resolved = resolve_view_geometry(history, state, retained);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&resolved)) return *diagnostics;
    auto geometry = std::get<view_geometry_t>(std::move(resolved));
    auto range_preview = make_space_range_preview(history, state, geometry);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&range_preview)) return *diagnostics;
    auto retained_selection_range = state.retained_selection_range
        ? normalized_position_span(geometry.index, state.retained_selection_range->begin,
                                   state.retained_selection_range->end)
        : std::optional<position_span_t>{};
    if (!valid_retained_canvas_range(state, geometry.canvas_rows))
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "retained canvas range lies outside chart geometry")};
    auto virtual_preview = virtual_lane_key_preview(*geometry.chart, geometry.index,
        geometry.canvas_rows, history.document.revision, state, geometry.lane_position,
        geometry.absolute_row);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&virtual_preview)) return *diagnostics;
    auto keyboard_previews = canvas_keyboard_hold_previews(*geometry.chart, geometry.index,
        geometry.canvas_rows, history.document.revision, state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&keyboard_previews)) return *diagnostics;
    auto space_preview = std::get<std::optional<space_range_selection_preview_t>>(
        std::move(range_preview));
    const auto step = geometry.chart->divisions_per_standard_measure /
                      state.active_snap_denominator;
    return editor_interaction_view_model_t{
        .chart = state.chart, .active_lane = state.active_lane,
        .active_lane_name = geometry.lane->name, .cursor = state.cursor,
        .virtual_bms_cursor_row = state.virtual_bms_cursor_row,
        .cursor_subrow = state.cursor_subrow,
        .active_snap_denominator = state.active_snap_denominator,
        .snap_enabled = state.snap_enabled,
        .y_lock = state.y_lock,
        .bga_lane_visible = state.bga_lane_visible,
        .bga_revealed_lane_mask = static_cast<std::uint8_t>(state.bga_revealed_lane_mask & editor_bga_lane_mask_all),
        .supported_snap_denominators = state.supported_snap_denominators,
        .document_revision = history.document.revision,
        .pixels_per_standard_measure = state.pixels_per_standard_measure,
        .viewport_scroll_pixels = state.viewport_scroll_pixels,
        .viewport_height_pixels = state.viewport_height_pixels,
        .viewport_minimum_margin_fraction = state.viewport_minimum_margin_fraction,
        .viewport_end_margin_fraction = state.viewport_end_margin_fraction,
        .fixed_cursor_screen_y_pixels = state.fixed_cursor_screen_y_pixels,
        .scroll_direction = state.scroll_direction, .pending_hold = state.pending_hold,
        .additional_keyboard_holds = state.additional_keyboard_holds,
        .pending_virtual_hold = std::get<std::optional<editor_interaction_view_model_t::virtual_hold_preview_t>>(
            std::move(virtual_preview)),
        .pending_canvas_keyboard_holds = std::get<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>>(
            std::move(keyboard_previews)),
        .pending_note_duration_gesture = state.pending_note_duration_gesture,
        .pending_note_move_gesture = state.pending_note_move_gesture,
        .pending_note_paint_gesture = state.pending_note_paint_gesture
            ? std::optional{note_paint_gesture_preview_t{
                  .document_revision = state.pending_note_paint_gesture->document_revision,
                  .targets = state.pending_note_paint_gesture->targets}} : std::nullopt,
        .pending_editor_area_selection = state.pending_editor_area_selection
            ? std::optional{editor_area_selection_preview_t{
                  .document_revision = state.pending_editor_area_selection->document_revision,
                  .left_pixels = std::min(state.pending_editor_area_selection->anchor_x_pixels,
                                           state.pending_editor_area_selection->current_x_pixels),
                  .top_pixels = std::min(area_selection_anchor_y(*state.pending_editor_area_selection,
                                                       state.viewport_scroll_pixels),
                                          state.pending_editor_area_selection->current_y_pixels),
                  .right_pixels = std::max(state.pending_editor_area_selection->anchor_x_pixels,
                                            state.pending_editor_area_selection->current_x_pixels),
                  .bottom_pixels = std::max(area_selection_anchor_y(*state.pending_editor_area_selection,
                                                       state.viewport_scroll_pixels),
                                             state.pending_editor_area_selection->current_y_pixels),
                  .notes = state.pending_editor_area_selection->notes,
                  .objects = state.pending_editor_area_selection->objects}} : std::nullopt,
        .pending_space_range_selection = std::move(space_preview),
        .retained_selection_range = retained_selection_range,
        .retained_canvas_selection_range = state.retained_canvas_selection_range,
        .selected_canvas_measure = state.selected_canvas_measure,
        .selected_notes = state.selected_notes, .selected_objects = state.selected_objects,
        .can_move_backward = geometry.absolute_row >= step,
        .can_move_forward = step < geometry.canvas_rows - geometry.absolute_row,
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
    return snap_position(chart, index, target, denominator);
}

struct hold_placement_plan_t {
    local_position_t head;
    std::optional<local_position_t> tail;
    std::vector<object_id_t> removals;
    bool adds_note{};
};

auto hold_placement_preview(const hold_placement_plan_t& plan) -> pending_hold_placement_preview_t {
    return {.head = plan.head,
            .tail = plan.tail,
            .removals = std::make_shared<const std::vector<object_id_t>>(plan.removals),
            .adds_note = plan.adds_note};
}

// The kind a note takes once it has a tail: rolls and fakes keep their kind, all else is a hold.
auto held_note_kind(note_kind_t armed) -> note_kind_t {
    return armed == note_kind_t::roll || armed == note_kind_t::fake ? armed : note_kind_t::hold;
}

auto plan_hold_placement(const chart_document_t& chart, const measure_index_t& index,
                         lane_id_t lane, local_position_t anchor, local_position_t moving,
                         note_kind_t armed_kind = note_kind_t::hold, bool committing = false,
                         hold_overlap_policy_t overlap_policy = hold_overlap_policy_t::merge_plain)
    -> std::variant<hold_placement_plan_t, diagnostic_list_t> {
    const auto anchor_row = absolute_row(index, anchor);
    const auto moving_row = absolute_row(index, moving);
    if (!anchor_row.has_value() || !moving_row.has_value()) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "hold endpoints lie outside chart geometry")};
    }

    auto planned = plan_hold_rows(chart, index, lane, *anchor_row, *moving_row, overlap_policy);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return *diagnostics;
    auto rows = std::get<hold_row_plan_t>(std::move(planned));
    // A real hold has a playable head; a single row is judged by the kind about to be committed
    // (previews pass none, so a gesture may start inside a warp and end on a row outside it).
    const auto single_row = rows.head_row == rows.tail_row;
    const auto head_kind = single_row ? armed_kind : held_note_kind(armed_kind);
    if ((committing || !single_row) && head_blocked_by_warp(chart, index, rows.head_row, head_kind))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "playable note heads cannot sit inside a warp")};
    const auto head = position_at(index, rows.head_row);
    const auto tail = position_at(index, rows.tail_row);
    if (!head.has_value() || !tail.has_value()) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "normalized hold endpoints lie outside chart geometry")};
    }
    hold_placement_plan_t result{
        .head = *head,
        .tail = rows.head_row == rows.tail_row ? std::optional<local_position_t>{}
                                               : std::optional{*tail},
        .removals = std::move(rows.removals),
        .adds_note = rows.adds_note,
    };
    return result;
}

void erase_notes(chart_document_t& chart, std::span<const object_id_t> ids) {
    if (ids.empty()) {
        return;
    }
    std::unordered_set<std::uint64_t> wanted;
    wanted.reserve(ids.size());
    for (const auto id : ids) {
        wanted.insert(id.value);
    }
    for (auto& measure : chart.measures) {
        std::erase_if(measure.notes,
                      [&](const auto& note) { return wanted.contains(note.id.value); });
    }
}

struct note_paint_plan_t {
    std::vector<note_paint_target_t> additions;
    std::vector<object_id_t> removals;
};

struct paint_note_interval_t {
    lane_id_t lane;
    std::uint64_t begin{};
    std::uint64_t end{};
    object_id_t note;
    note_kind_t kind{note_kind_t::tap};
    bool has_end{};
};

struct paint_row_t {
    lane_id_t lane;
    std::uint64_t row{};
    note_paint_target_t target;
};

auto paint_note_intervals(const chart_document_t& chart, const measure_index_t& index)
    -> std::vector<paint_note_interval_t> {
    std::size_t count{};
    for (const auto& measure : chart.measures) count += measure.notes.size();
    std::vector<paint_note_interval_t> intervals;
    intervals.reserve(count);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto begin = absolute_row(index, note.position);
            const auto end = note.end ? absolute_row(index, *note.end) : begin;
            if (begin && end)
                intervals.push_back({note.lane, *begin, *end, note.id, note.kind,
                                     note.end.has_value()});
        }
    }
    std::ranges::sort(intervals, [](const auto& left, const auto& right) {
        if (left.lane != right.lane) return left.lane.value < right.lane.value;
        if (left.begin != right.begin) return left.begin < right.begin;
        if (left.end != right.end) return left.end < right.end;
        return left.note.value < right.note.value;
    });
    return intervals;
}

auto paint_rows(const measure_index_t& index, std::span<const note_paint_target_t> targets)
    -> std::variant<std::vector<paint_row_t>, diagnostic_list_t> {
    std::vector<paint_row_t> rows;
    rows.reserve(targets.size());
    for (const auto& target : targets) {
        const auto row = absolute_row(index, target.position);
        if (!row)
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "paint target lies outside chart geometry")};
        rows.push_back({target.lane, *row, target});
    }
    std::ranges::sort(rows, [](const auto& left, const auto& right) {
        return left.lane != right.lane ? left.lane.value < right.lane.value : left.row < right.row;
    });
    return rows;
}

auto erase_active_paint_notes(std::map<std::uint64_t, paint_note_interval_t>& active,
                              note_paint_plan_t& plan) -> void {
    while (!active.empty()) {
        const auto id = active.begin()->first;
        plan.removals.push_back(object_id_t{id});
        active.erase(active.begin());
    }
}

auto process_draw_paint_row(const paint_row_t& row,
                            note_paint_plan_t& plan,
                            const std::map<std::uint64_t, paint_note_interval_t>& active,
                            note_kind_t kind) -> std::optional<diagnostic_t> {
    if (active.empty()) {
        plan.additions.push_back(row.target);
        return std::nullopt;
    }
    if (active.size() != 1)
        return error(diagnostic_code_t::collision,
                     "paint gesture overlaps multiple same-lane notes",
                     active.begin()->second.note);
    const auto& note = active.begin()->second;
    if (note.kind == kind && !note.has_end && note.begin == row.row && note.end == row.row)
        return std::nullopt;
    return error(diagnostic_code_t::collision,
                 "paint gesture overlaps a non-tap note", active.begin()->second.note);
}

struct paint_overlap_sweep_t {
    const std::vector<paint_note_interval_t>& intervals;
    std::size_t next_interval{};
    std::optional<lane_id_t> lane;
    std::priority_queue<std::pair<std::uint64_t, std::uint64_t>,
        std::vector<std::pair<std::uint64_t, std::uint64_t>>,
        std::greater<>> ending;
    std::map<std::uint64_t, paint_note_interval_t> active;
    note_paint_intent_t current_intent{note_paint_intent_t::toggle};

    auto process(const paint_row_t& row, note_paint_plan_t& plan, note_kind_t kind)
        -> std::optional<diagnostic_t> {
        if (!lane || *lane != row.lane) {
            while (next_interval < intervals.size() &&
                   intervals[next_interval].lane.value < row.lane.value) ++next_interval;
            ending = {};
            active.clear();
            lane = row.lane;
        }
        while (next_interval < intervals.size() && intervals[next_interval].lane == row.lane &&
               intervals[next_interval].begin <= row.row) {
            const auto& interval = intervals[next_interval++];
            ending.emplace(interval.end, interval.note.value);
            active.emplace(interval.note.value, interval);
        }
        while (!ending.empty() && ending.top().first < row.row) {
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
        if (note.kind == kind && !note.has_end && note.begin == row.row &&
            note.end == row.row) {
            plan.removals.push_back(object_id_t{id});
            return std::nullopt;
        }
        return error(diagnostic_code_t::collision,
                     "paint gesture overlaps a non-tap note", note.note);
    }
};

auto plan_note_paint(const chart_document_t& chart, const measure_index_t& index,
                     std::span<const note_paint_target_t> targets, note_kind_t kind,
                     note_paint_intent_t intent)
    -> std::variant<note_paint_plan_t, diagnostic_list_t> {
    auto intervals = paint_note_intervals(chart, index);
    auto rows_result = paint_rows(index, targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&rows_result)) return *diagnostics;
    const auto& rows = std::get<std::vector<paint_row_t>>(rows_result);
    note_paint_plan_t plan;
    plan.additions.reserve(rows.size());
    plan.removals.reserve(rows.size());
    paint_overlap_sweep_t sweep{.intervals = intervals, .next_interval = 0, .lane = std::nullopt,
                                .ending = {}, .active = {}, .current_intent = intent};
    for (const auto& row : rows) {
        if (auto failure = sweep.process(row, plan, kind)) return diagnostic_list_t{*failure};
    }
    for (const auto& addition : plan.additions) {
        const auto row = absolute_row(index, addition.position);
        if (row && head_blocked_by_warp(chart, index, *row, kind))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold, "playable note heads cannot sit inside a warp")};
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

auto find_bgm_event(const chart_document_t& chart, object_id_t id) -> const bgm_event_t* {
    for (const auto& measure : chart.measures) {
        const auto event = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (event != measure.bgm_events.end()) return &*event;
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

struct resize_parameters_t {
    const note_t* reference{};
    local_position_t snapped_end;
    std::uint64_t delta{};
    bool shrinking{};
};

struct duration_resize_context_t {
    const chart_document_t& chart;
    const measure_index_t& index;
    std::unordered_map<std::uint64_t, const measure_index_entry_t*> measures;
    std::unordered_map<std::uint64_t, const note_t*> target_notes;
    std::vector<duration_collision_interval_t> collisions;

    auto row_for(local_position_t position) const -> std::optional<std::uint64_t> {
        const auto found = measures.find(position.measure.value);
        if (found == measures.end() || position.row >= found->second->row_capacity ||
            position.row > std::numeric_limits<std::uint64_t>::max() -
                               found->second->absolute_begin_row) return std::nullopt;
        const auto row = found->second->absolute_begin_row + position.row;
        return row < index.total_rows ? std::optional{row} : std::nullopt;
    }

    auto collect_existing(const std::unordered_set<std::uint64_t>& target_ids)
        -> std::optional<diagnostic_t> {
        std::size_t count{};
        for (const auto& measure : chart.measures) count += measure.notes.size();
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
                collisions.push_back({note.lane, *begin, *end, note.id, false});
            }
        }
        return std::nullopt;
    }
};

auto resolve_resize_parameters(const chart_document_t& chart, const measure_index_t& index,
                               std::vector<object_id_t>& targets, object_id_t reference_id,
                               local_position_t reference_end, std::uint64_t snap_denominator,
                               bool exact) -> std::variant<resize_parameters_t, diagnostic_list_t> {
    const auto* reference = find_note(chart, reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
            "duration resize requires a tap or hold reference note", reference_id)};
    const auto snapped = exact
        ? (absolute_row(index, reference_end) ? std::optional{reference_end} : std::nullopt)
        : snapped_position(chart, index, reference_end, snap_denominator);
    const auto begin = absolute_row(index, reference->position);
    const auto old_end = reference->end ? absolute_row(index, *reference->end) : begin;
    const auto new_end = snapped ? absolute_row(index, *snapped) : std::nullopt;
    if (!snapped || !begin || !old_end || !new_end || *new_end < *begin)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
            "reference duration cannot end before its note head", reference_id)};
    if (std::ranges::find(targets, reference_id) == targets.end()) targets = {reference_id};
    const auto old_duration = *old_end - *begin;
    const auto new_duration = *new_end - *begin;
    return resize_parameters_t{reference, *snapped,
        new_duration < old_duration ? old_duration - new_duration : new_duration - old_duration,
        new_duration < old_duration};
}

auto valid_resize_ids(const std::vector<object_id_t>& targets)
    -> std::variant<std::unordered_set<std::uint64_t>, diagnostic_list_t> {
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(targets.size());
    for (const auto id : targets) {
        if (id.value == 0 || !ids.insert(id.value).second)
            return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                "duration resize contains an invalid or duplicate note ID", id)};
    }
    return ids;
}

auto resize_one_note(duration_resize_context_t& context,
                     const resize_parameters_t& parameters, duration_resize_plan_t& plan,
                     object_id_t id) -> std::optional<diagnostic_t> {
        const auto found = context.target_notes.find(id.value);
        const auto* note = found == context.target_notes.end() ? nullptr : found->second;
        if (!note || (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold))
            return error(diagnostic_code_t::invalid_hold,
                         "selected duration resize contains a non-tap/hold note", id);
        const auto begin = context.row_for(note->position);
        const auto old_end = note->end ? context.row_for(*note->end) : begin;
        if (!begin || !old_end)
            return error(diagnostic_code_t::row_out_of_range,
                         "selected note duration lies outside chart geometry", id);
        const auto old_duration = *old_end - *begin;
        if (parameters.shrinking && parameters.delta > old_duration)
            return error(diagnostic_code_t::invalid_hold,
                         "duration delta would move a selected tail before its head", id);
        if (!parameters.shrinking &&
            parameters.delta > std::numeric_limits<std::uint64_t>::max() - old_duration)
            return error(diagnostic_code_t::row_out_of_range,
                         "selected note duration would overflow", id);
        const auto duration = parameters.shrinking ? old_duration - parameters.delta
                                                   : old_duration + parameters.delta;
        if (duration > context.index.total_rows - *begin)
            return error(diagnostic_code_t::row_out_of_range,
                         "selected note tail would leave chart geometry", id);
        const auto end = *begin + duration;
        const auto end_position = duration == 0 ? std::optional<local_position_t>{}
                                               : position_at(context.index, end);
        if (duration != 0 && !end_position)
            return error(diagnostic_code_t::row_out_of_range,
                         "selected note tail cannot be represented in the chart", id);
        if (end_position && find_active_lane(context.chart, context.index, *end_position,
                                             note->lane) == nullptr)
            return error(diagnostic_code_t::unknown_lane,
                         "selected note lane is unavailable at its resized tail", id);
        plan.resized.push_back({id, note->lane, *begin, end, end_position});
        context.collisions.push_back({note->lane, *begin, end, id, true});
    return std::nullopt;
}

auto resize_selected_notes(duration_resize_context_t& context,
                           const resize_parameters_t& parameters,
                           duration_resize_plan_t& plan) -> std::optional<diagnostic_t> {
    plan.resized.reserve(plan.notes.size());
    for (const auto id : plan.notes)
        if (auto failure = resize_one_note(context, parameters, plan, id)) return failure;
    return std::nullopt;
}

struct duration_collision_sweep_t {
    using queue_t = std::priority_queue<active_duration_collision_t,
        std::vector<active_duration_collision_t>, earliest_duration_collision_t>;
    queue_t resized;
    queue_t existing;
    lane_id_t lane{};
    bool has_lane{};

    auto process(const duration_collision_interval_t& interval) -> std::optional<diagnostic_t> {
        if (!has_lane || interval.lane != lane) {
            resized = queue_t{};
            existing = queue_t{};
            lane = interval.lane;
            has_lane = true;
        }
        while (!resized.empty() && resized.top().end < interval.begin) resized.pop();
        while (!existing.empty() && existing.top().end < interval.begin) existing.pop();
        if (interval.resizing) return process_resized(interval);
        return process_existing(interval);
    }

    auto process_resized(const duration_collision_interval_t& interval)
        -> std::optional<diagnostic_t> {
        if (!resized.empty())
            return error(diagnostic_code_t::collision,
                         "resized selected notes overlap on one lane", interval.note);
        if (!existing.empty())
            return error(diagnostic_code_t::collision,
                         "resized note overlaps an unselected same-lane note", existing.top().note);
        resized.push({interval.end, interval.note});
        return std::nullopt;
    }

    auto process_existing(const duration_collision_interval_t& interval)
        -> std::optional<diagnostic_t> {
        if (!resized.empty())
            return error(diagnostic_code_t::collision,
                         "resized note overlaps an unselected same-lane note", interval.note);
        existing.push({interval.end, interval.note});
        return std::nullopt;
    }
};

auto validate_resize_collisions(std::vector<duration_collision_interval_t>& collisions)
    -> std::optional<diagnostic_t> {
    std::ranges::sort(collisions, [](const auto& left, const auto& right) {
        if (left.lane != right.lane) return left.lane.value < right.lane.value;
        if (left.begin != right.begin) return left.begin < right.begin;
        if (left.end != right.end) return left.end < right.end;
        return left.resizing < right.resizing;
    });
    duration_collision_sweep_t sweep;
    for (const auto& interval : collisions)
        if (auto failure = sweep.process(interval)) return failure;
    return std::nullopt;
}

auto plan_duration_resize(const chart_document_t& chart, const measure_index_t& index,
                          std::vector<object_id_t> targets, object_id_t reference_id,
                          local_position_t reference_end, std::uint64_t snap_denominator,
                          bool reference_end_is_exact = false)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t> {
    auto parameters_result = resolve_resize_parameters(chart, index, targets, reference_id,
        reference_end, snap_denominator, reference_end_is_exact);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&parameters_result)) return *diagnostics;
    const auto parameters = std::get<resize_parameters_t>(parameters_result);
    auto ids_result = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids_result)) return *diagnostics;
    duration_resize_context_t context{.chart = chart, .index = index, .measures = {},
        .target_notes = {}, .collisions = {}};
    context.measures.reserve(index.entries.size());
    for (const auto& entry : index.entries) context.measures.emplace(entry.id.value, &entry);
    if (auto failure = context.collect_existing(std::get<std::unordered_set<std::uint64_t>>(ids_result)))
        return diagnostic_list_t{*failure};
    duration_resize_plan_t plan{.reference_note = reference_id,
        .reference_lane = parameters.reference->lane, .reference_end = parameters.snapped_end,
        .notes = std::move(targets), .resized = {}};
    if (auto failure = resize_selected_notes(context, parameters, plan))
        return diagnostic_list_t{*failure};
    if (auto failure = validate_resize_collisions(context.collisions))
        return diagnostic_list_t{*failure};
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
    // Prepared by the ordinary/linked/virtual BGM policy commands, committed once on release.
    std::optional<song_document_t> staged_bgm_document;
};

struct note_move_parameters_t {
    const note_t* reference{};
    local_position_t snapped_position;
    std::uint64_t delta{};
    bool forward{};
    bool preserve_lanes{};
    lane_id_t target_lane;
    std::int64_t lane_delta{};
};

auto known_move_lane(const chart_document_t& chart, lane_id_t id) -> const lane_definition_t* {
    for (const auto& layout : chart.lane_layouts) {
        for (const auto& lane : layout.layout.lanes) {
            if (lane.id == id && editable_lane(lane))
                return &lane;
        }
    }
    return nullptr;
}

auto resolve_note_move_parameters(const chart_document_t& chart, const measure_index_t& index,
                                  std::vector<object_id_t>& targets, object_id_t reference_id,
                                  local_position_t reference_position, lane_id_t requested_lane,
                                  std::uint64_t snap_denominator, bool exact, bool keep_rows)
    -> std::variant<note_move_parameters_t, diagnostic_list_t> {
    const auto* reference = find_note(chart, reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
            "note move requires a tap or hold reference note", reference_id)};
    auto snapped = exact
        ? (absolute_row(index, reference_position) ? std::optional{reference_position} : std::nullopt)
        : snapped_position(chart, index, reference_position, snap_denominator);
    // A pointer move that stays in the reference note's own grid cell (a horizontal drag) keeps
    // its exact row instead of resnapping it; Y-lock keeps the row whatever the pointer does.
    if (!exact && snapped) {
        const auto home = snapped_position(chart, index, reference->position, snap_denominator);
        if (keep_rows || (home && *home == *snapped)) snapped = reference->position;
    }
    const auto begin = absolute_row(index, reference->position);
    const auto target = snapped ? absolute_row(index, *snapped) : std::nullopt;
    if (!snapped || !begin || !target)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
            "note move reference cannot be snapped inside chart", reference_id)};
    if (std::ranges::find(targets, reference_id) == targets.end()) targets = {reference_id};
    const auto ids = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids)) {
        auto errors = *diagnostics;
        if (!errors.empty()) errors.front().message =
            "note move contains an invalid or duplicate note ID";
        return errors;
    }
    const auto preserve = !requested_lane;
    const auto* source_lane = find_active_lane(chart, index, reference->position, reference->lane);
    const auto desired_lane = preserve ? reference->lane : requested_lane;
    const auto* destination = find_active_lane(chart, index, *snapped, desired_lane);
    if (!destination)
        destination = known_move_lane(chart, desired_lane);
    if (!source_lane || !destination)
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
            "note move reference lane is unavailable at its target row", reference_id)};
    return note_move_parameters_t{.reference = reference, .snapped_position = *snapped,
        .delta = *target >= *begin ? *target - *begin : *begin - *target,
        .forward = *target >= *begin, .preserve_lanes = preserve, .target_lane = destination->id,
        .lane_delta = preserve ? 0 : static_cast<std::int64_t>(destination->display_order) -
                                      static_cast<std::int64_t>(source_lane->display_order)};
}

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
    std::vector<duration_collision_interval_t>& collisions;

    auto display_columns(local_position_t position, local_position_t source) const -> std::size_t {
        const auto count = [](const lane_layout_t* layout) -> std::size_t {
            return layout ? std::ranges::count_if(layout->lanes, editable_lane) : 0;
        };
        const auto current = count(effective_layout(chart, index, position));
        return current ? current
                       : std::max<std::size_t>(1, count(effective_layout(chart, index, source)));
    }

    auto lane_available_across(std::uint64_t begin, std::uint64_t end, lane_id_t lane) const
        -> bool {
        const auto first = position_at(index, begin);
        const auto last = position_at(index, end);
        if (!first || !last || !find_active_lane(chart, index, *first, lane) ||
            !find_active_lane(chart, index, *last, lane)) return false;
        for (const auto& layout : chart.lane_layouts) {
            const auto row = absolute_row(index, layout.position);
            if (row && *row > begin && *row <= end &&
                !find_active_lane(chart, index, layout.position, lane)) return false;
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
            return move_destination_t{note.lane,
                                      active ? active->display_order : source->display_order,
                                      active != nullptr, columns};
        }
        const auto column =
            static_cast<std::int64_t>(source->display_order) + parameters.lane_delta;
        const auto* layout = effective_layout(chart, index, position);
        if (!layout)
            return move_destination_t{{}, column, false, columns};
        const auto found = std::ranges::find_if(layout->lanes, [&](const auto& candidate) {
            return editable_lane(candidate) &&
                   static_cast<std::int64_t>(candidate.display_order) == column;
        });
        if (found == layout->lanes.end())
            return move_destination_t{{}, column, false, columns};
        return move_destination_t{found->id, column, true, columns};
    }
};

struct shifted_move_row_t {
    std::uint64_t row{};
    bool before_chart{};
    bool after_chart{};
};

auto shifted_move_row(std::uint64_t row, std::uint64_t last,
                      const note_move_parameters_t& parameters) -> shifted_move_row_t {
    if (parameters.forward) {
        if (parameters.delta > last - row)
            return {last, false, true};
        return {row + parameters.delta, false, false};
    }
    if (parameters.delta > row)
        return {0, true, false};
    return {row - parameters.delta, false, false};
}

auto display_move_row(std::uint64_t row, const note_move_parameters_t& parameters) -> long double {
    const auto delta = static_cast<long double>(parameters.delta);
    return static_cast<long double>(row) + (parameters.forward ? delta : -delta);
}

auto move_one_note(note_move_context_t& context, object_id_t id, note_move_plan_t& plan)
    -> std::optional<diagnostic_t> {
    const auto found = context.targets.find(id.value);
    const auto* note = found == context.targets.end() ? nullptr : found->second;
    if (!note || (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold))
        return error(diagnostic_code_t::invalid_hold,
                     "selected note move contains a non-tap/hold note", id);
    const auto begin = absolute_row(context.index, note->position);
    const auto end = note->end ? absolute_row(context.index, *note->end) : begin;
    if (!begin || !end || *end < *begin)
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note move source lies outside chart geometry", id);
    const auto last = context.index.total_rows - 1;
    const auto shifted_begin = shifted_move_row(*begin, last, context.parameters);
    const auto shifted_end = shifted_move_row(*end, last, context.parameters);
    const auto outside = shifted_begin.before_chart || shifted_end.after_chart;
    const auto moved_begin = shifted_begin.row;
    const auto moved_end = shifted_end.row;
    const auto position = position_at(context.index, moved_begin);
    const auto end_position = note->end ? position_at(context.index, moved_end)
                                        : std::optional<local_position_t>{};
    if (!position || (note->end && !end_position))
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note move could not resolve its target row", id);
    auto destination = context.destination_lane(*note, *position);
    if (const auto* failure = std::get_if<diagnostic_t>(&destination))
        return *failure;
    const auto target = std::get<move_destination_t>(destination);
    const auto available =
        target.available && context.lane_available_across(moved_begin, moved_end, target.lane);
    auto conflict = available ? note_move_conflict_t::none : note_move_conflict_t::out_of_lane;
    if (available && head_blocked_by_warp(context.chart, context.index, moved_begin, note->kind))
        conflict = note_move_conflict_t::other;
    if (outside)
        conflict = note_move_conflict_t::out_of_chart;
    plan.moved.push_back(
        {id,
         target.lane,
         moved_begin,
         moved_end,
         *position,
         end_position,
         conflict,
         {},
         target.column,
         target.columns,
         outside ? std::optional{display_move_row(*begin, context.parameters)} : std::nullopt,
         outside && note->end ? std::optional{display_move_row(*end, context.parameters)}
                              : std::nullopt});
    // Partially visible invalid holds still occupy their safely clipped interval.
    if (target.available && !shifted_begin.after_chart && !shifted_end.before_chart)
        context.collisions.push_back({target.lane, moved_begin, moved_end, id, true});
    return std::nullopt;
}

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
        while (!endings.empty() && endings.top().end < begin) {
            const auto id = endings.top().note.value;
            active.erase(id);
            unmarked.erase(id);
            unoutlined.erase(id);
            endings.pop();
        }
    }
    void insert(const duration_collision_interval_t& interval, bool track_outline = false) {
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
    lane_id_t lane{};
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
        // Each interval leaves this set on its first conflict, avoiding pair enumeration.
        while (!moved.unmarked.empty())
            mark_moved(*moved.unmarked.begin(), occupant);
    }
    void mark_active_existing() {
        occupants.insert(existing.unmarked.begin(), existing.unmarked.end());
        existing.unmarked.clear();
    }
    void process_moved(const duration_collision_interval_t& interval) {
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
    void process_existing(const duration_collision_interval_t& interval) {
        existing.insert(interval);
        if (!moved.active.empty()) {
            occupants.insert(interval.note.value);
            existing.unmarked.erase(interval.note.value);
            mark_active_moved(interval.note.value);
        }
    }
    void process(const duration_collision_interval_t& interval) {
        if (!has_lane || interval.lane != lane) {
            moved = {};
            existing = {};
            lane = interval.lane;
            has_lane = true;
        }
        moved.expire(interval.begin);
        existing.expire(interval.begin);
        if (interval.resizing)
            process_moved(interval);
        else
            process_existing(interval);
    }
};

auto merged_move_ranges(const std::vector<duration_collision_interval_t>& collisions,
                        std::size_t moved_count) -> std::vector<duration_collision_interval_t> {
    std::vector<duration_collision_interval_t> ranges;
    ranges.reserve(moved_count);
    for (const auto& interval : collisions)
        if (interval.resizing)
            ranges.push_back(interval);
    std::ranges::sort(ranges, [](const auto& left, const auto& right) {
        return std::tie(left.lane.value, left.begin, left.end) <
               std::tie(right.lane.value, right.begin, right.end);
    });
    std::size_t merged = 0;
    for (const auto& range : ranges) {
        if (merged != 0 && ranges[merged - 1].lane == range.lane &&
            range.begin <= ranges[merged - 1].end) {
            ranges[merged - 1].end = std::max(ranges[merged - 1].end, range.end);
        } else {
            ranges[merged++] = range;
        }
    }
    ranges.resize(merged);
    return ranges;
}

auto overlaps_move_range(const duration_collision_interval_t& interval,
                         const std::vector<duration_collision_interval_t>& ranges) -> bool {
    const auto key = std::pair{interval.lane.value, interval.begin};
    const auto found = std::ranges::lower_bound(
        ranges, key, {}, [](const auto& range) { return std::pair{range.lane.value, range.end}; });
    return found != ranges.end() && found->lane == interval.lane && found->begin <= interval.end;
}

void mark_move_collisions(std::vector<duration_collision_interval_t>& collisions,
                          note_move_plan_t& plan) {
    const auto ranges = merged_move_ranges(collisions, plan.moved.size());
    // Only intervals intersecting a proposed closed range can be occupants. Keeping all moved
    // intervals preserves selected-selected conflicts, including clipped invalid hold portions.
    std::erase_if(collisions, [&](const auto& interval) {
        return !interval.resizing && !overlaps_move_range(interval, ranges);
    });
    std::ranges::sort(collisions, [](const auto& left, const auto& right) {
        return std::tie(left.lane.value, left.begin, left.end, left.resizing, left.note.value) <
               std::tie(right.lane.value, right.begin, right.end, right.resizing, right.note.value);
    });
    note_move_collision_sweep_t sweep{plan};
    for (const auto& interval : collisions)
        sweep.process(interval);
    for (const auto id : sweep.occupants)
        plan.occupants.push_back(object_id_t{id});
}

auto plan_note_move(const chart_document_t& chart, const measure_index_t& index,
                    std::vector<object_id_t> targets, object_id_t reference_id,
                    local_position_t reference_position, lane_id_t requested_reference_lane,
                    std::uint64_t snap_denominator, bool reference_position_is_exact = false,
                    bool keep_rows = false)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    auto parameters_result = resolve_note_move_parameters(chart, index, targets, reference_id,
        reference_position, requested_reference_lane, snap_denominator,
        reference_position_is_exact, keep_rows);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&parameters_result)) return *diagnostics;
    const auto parameters = std::get<note_move_parameters_t>(parameters_result);
    auto ids_result = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids_result)) {
        auto errors = *diagnostics;
        if (!errors.empty()) errors.front().message =
            "note move contains an invalid or duplicate note ID";
        return errors;
    }
    duration_resize_context_t collected{.chart = chart, .index = index, .measures = {},
        .target_notes = {}, .collisions = {}};
    collected.measures.reserve(index.entries.size());
    for (const auto& entry : index.entries) collected.measures.emplace(entry.id.value, &entry);
    if (auto failure = collected.collect_existing(
            std::get<std::unordered_set<std::uint64_t>>(ids_result))) {
        auto diagnostics = diagnostic_list_t{*failure};
        if (!diagnostics.empty()) diagnostics.front().message =
            "existing note lies outside chart geometry";
        return diagnostics;
    }
    note_move_plan_t plan{.reference_note = reference_id, .reference_lane = parameters.target_lane,
        .reference_position = parameters.snapped_position, .notes = std::move(targets), .moved = {}, .occupants = {}, .bgm_moves = {}};
    note_move_context_t context{.chart = chart, .index = index, .parameters = parameters,
        .targets = collected.target_notes, .collisions = collected.collisions};
    plan.moved.reserve(plan.notes.size());
    for (const auto id : plan.notes)
        if (auto failure = move_one_note(context, id, plan))
            return diagnostic_list_t{*failure};
    mark_move_collisions(collected.collisions, plan);
    return plan;
}

auto selected_move_bgm_ids(const editor_interaction_state_t& state, object_id_t reference,
                           std::span<const object_id_t>) -> std::vector<object_id_t> {
    const auto reference_selected = std::ranges::find(state.selected_notes, reference) != state.selected_notes.end() ||
        std::ranges::any_of(state.selected_objects, [&](const auto& object) { return object.id == reference; });
    if (!reference_selected) return {};
    std::vector<object_id_t> ids;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event) ids.push_back(object.id);
    return ids;
}

auto translated_object_position(const measure_index_t& index, local_position_t position,
                                 std::uint64_t origin, std::uint64_t target)
    -> std::optional<local_position_t> {
    const auto row = absolute_row(index, position);
    if (!row) return std::nullopt;
    const auto delta = target >= origin ? target - origin : origin - target;
    if (target < origin && delta > *row) return std::nullopt;
    if (target >= origin && delta > std::numeric_limits<std::uint64_t>::max() - *row)
        return std::nullopt;
    return position_at_absolute_row(index, target >= origin ? *row + delta : *row - delta);
}

auto append_selected_bgm_moves(const chart_document_t& chart, const measure_index_t& index,
                                std::span<const object_id_t> ids, local_position_t origin,
                                note_move_plan_t& plan) -> diagnostic_list_t {
    const auto from = absolute_row(index, origin);
    const auto to = absolute_row(index, plan.reference_position);
    if (!from || !to) return {error(diagnostic_code_t::row_out_of_range, "Mixed move reference is outside chart geometry")};
    for (const auto id : ids) {
        const auto* event = find_bgm_event(chart, id);
        if (!event) return {error(diagnostic_code_t::invalid_id, "Selected BGM event is unavailable", id)};
        const auto position = translated_object_position(index, event->position, *from, *to);
        const auto end = event->end ? translated_object_position(index, *event->end, *from, *to) : std::nullopt;
        if (!position || (event->end && !end))
            return {error(diagnostic_code_t::row_out_of_range, "Mixed move would place a BGM head or tail outside the chart", id)};
        plan.bgm_moves.push_back({id, event->source_lane, *position, end, event->sound});
    }
    return {};
}

auto plan_selected_object_move(const chart_document_t& chart, const measure_index_t& index,
                               const editor_interaction_state_t& state,
                               std::vector<object_id_t> notes, object_id_t reference,
                               local_position_t position, lane_id_t lane,
                               std::uint64_t snap, bool exact)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto bgm_ids = selected_move_bgm_ids(state, reference, notes);
    const auto* reference_note = find_note(chart, reference);
    const auto* reference_bgm = find_bgm_event(chart, reference);
    if (reference_note) {
        auto planned = plan_note_move(chart, index, std::move(notes), reference, position, lane,
                                      snap, exact, state.y_lock);
        if (auto* plan = std::get_if<note_move_plan_t>(&planned)) {
            auto diagnostics = append_selected_bgm_moves(chart, index, bgm_ids, reference_note->position, *plan);
            if (!diagnostics.empty()) return diagnostics;
        }
        return planned;
    }
    if (!reference_bgm || bgm_ids.empty())
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id, "Move reference is unavailable", reference)};
    const auto target = state.y_lock ? std::optional{reference_bgm->position}
        : resolve_resource_placement_position(chart, position, exact ? 0 : snap);
    if (!target) return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range, "BGM move reference is outside chart geometry")};
    note_move_plan_t plan{.reference_note = reference, .reference_lane = state.active_lane,
                          .reference_position = *target, .notes = {}, .moved = {}, .occupants = {}, .bgm_moves = {}};
    if (!notes.empty()) {
        const auto* first = find_note(chart, notes.front());
        const auto origin = absolute_row(index, reference_bgm->position);
        const auto destination = absolute_row(index, *target);
        const auto shifted = first && origin && destination
            ? translated_object_position(index, first->position, *origin, *destination) : std::nullopt;
        if (!shifted) return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range, "Mixed move note is outside chart geometry")};
        auto planned = plan_note_move(chart, index, std::move(notes), first->id, *shifted, {}, 0, true, state.y_lock);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) return *diagnostics;
        plan = std::get<note_move_plan_t>(std::move(planned));
        plan.reference_note = reference;
        plan.reference_position = *target;
    }
    auto diagnostics = append_selected_bgm_moves(chart, index, bgm_ids, reference_bgm->position, plan);
    if (!diagnostics.empty()) return diagnostics;
    return plan;
}

auto stage_single_bgm_move(document_history_t& scratch, chart_id_t chart,
                           const bgm_event_t& source, local_position_t position,
                           object_id_t lane, std::optional<std::uint64_t> canvas_row, std::uint64_t snap)
    -> document_command_result_t {
    if (source.sound_channel_event) {
        if (canvas_row) return {.revision = scratch.document.revision,
            .diagnostics = {error(diagnostic_code_t::row_out_of_range, "Stem markers cannot move into virtual BMS rows")}};
        return move_bmson_bgm_marker(scratch, scratch.document.revision, chart, source.id, position);
    }
    if (canvas_row)
        return apply_virtual_bms_bgm_event_move(scratch, scratch.document.revision,
            {.chart = chart, .event = source.id, .canvas_row = *canvas_row, .source_lane = lane, .snap_denominator = snap});
    return apply_bgm_event_move(scratch, scratch.document.revision,
        {.chart = chart, .event = source.id, .position = position, .source_lane = lane});
}

auto plan_single_bgm_move(const document_history_t& history, const editor_interaction_state_t& state,
                          const bgm_event_t& source, local_position_t requested,
                          std::optional<object_id_t> lane, std::optional<std::uint64_t> canvas_row,
                          bool exact) -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto& chart = *find_chart(history.document, state.chart);
    const auto target = state.y_lock ? std::optional{source.position}
        : canvas_row ? std::optional{source.position}
        : resolve_resource_placement_position(chart, requested, exact ? 0 : edit_snap_denominator(chart, state));
    if (!target) return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range, "BGM move cannot snap to this chart row")};
    auto scratch = make_document_history(history.document);
    const auto moved = stage_single_bgm_move(scratch, state.chart, source, *target,
        lane.value_or(source.source_lane), state.y_lock ? std::nullopt : canvas_row,
        exact ? 0 : edit_snap_denominator(chart, state));
    if (!moved.diagnostics.empty()) return moved.diagnostics;
    const auto& destination = *find_chart(scratch.document, state.chart);
    const auto* event = find_bgm_event(destination, source.id);
    const auto built = build_measure_index(destination);
    if (const auto* errors = std::get_if<diagnostic_list_t>(&built)) return *errors;
    const auto& index = std::get<measure_index_t>(built);
    note_move_bgm_preview_t preview{event->id, event->source_lane, event->position, event->end, event->sound};
    preview.canvas_begin_row = absolute_row(index, event->position);
    preview.canvas_end_row = event->end ? absolute_row(index, *event->end) : preview.canvas_begin_row;
    note_move_plan_t plan{.reference_note = source.id, .reference_lane = state.active_lane,
        .reference_position = event->position, .notes = {}, .moved = {}, .occupants = {}, .bgm_moves = {preview}};
    if (moved.applied) plan.staged_bgm_document = std::move(scratch.document);
    return plan;
}

auto plan_move_gesture(const document_history_t& history, const editor_interaction_state_t& state,
                   const chart_document_t& chart, const measure_index_t& index,
                   std::vector<object_id_t> targets, object_id_t reference, local_position_t position,
                   lane_id_t lane, bool exact_position, std::optional<object_id_t> bgm_lane,
                   std::optional<std::uint64_t> virtual_row)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto* bgm = find_bgm_event(chart, reference);
    if (bgm && state.selected_notes.empty() && state.selected_objects.size() == 1 &&
        state.selected_objects.front() == editor_selection_object_t{editor_selection_object_kind_t::bgm_event, reference})
        return plan_single_bgm_move(history, state, *bgm, position, bgm_lane, virtual_row, exact_position);
    if (virtual_row) return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
        "Mixed moves require authored chart rows")};
    return plan_selected_object_move(chart, index, state, std::move(targets), reference, position, lane,
                                     edit_snap_denominator(chart, state), exact_position);
}

auto relocate_selected_bgm(song_document_t& proposed, chart_id_t chart_id,
                            const note_move_plan_t& plan) -> diagnostic_list_t {
    if (plan.bgm_moves.empty()) return {};
    auto scratch = make_document_history(proposed);
    std::vector<bgm_event_t> ordinary;
    for (const auto& preview : plan.bgm_moves) {
        auto* chart = find_chart(scratch.document, chart_id);
        const auto* source = find_bgm_event(*chart, preview.note);
        if (!source) return {error(diagnostic_code_t::invalid_id, "Selected BGM event is unavailable", preview.note)};
        if (source->sound_channel_event) {
            const auto moved = move_bmson_bgm_marker(scratch, scratch.document.revision, chart_id,
                                                     preview.note, preview.position);
            if (!moved.diagnostics.empty()) return moved.diagnostics;
        } else {
            auto moved = *source;
            if (moved.position != preview.position || moved.end != preview.end) {
                moved.structural_slot = 0;
                moved.provenance = {};
            }
            moved.position = preview.position;
            moved.end = preview.end;
            ordinary.push_back(std::move(moved));
        }
    }
    auto* chart = find_chart(scratch.document, chart_id);
    for (auto& measure : chart->measures)
        std::erase_if(measure.bgm_events, [&](const auto& event) {
            return std::ranges::any_of(ordinary, [&](const auto& moved) { return moved.id == event.id; });
        });
    for (auto& moved : ordinary) {
        const auto destination = std::ranges::find(chart->measures, moved.position.measure, &measure_chunk_t::id);
        if (destination == chart->measures.end())
            return {error(diagnostic_code_t::invalid_measure, "Mixed move BGM destination is unavailable", moved.id)};
        destination->bgm_events.push_back(std::move(moved));
    }
    // Validate the complete destination set, so selected objects vacate their old rows together.
    // The ordinary single-object validator allocates lanes on collision; a group move keeps lanes.
    for (const auto& preview : plan.bgm_moves) {
        const auto* source = find_bgm_event(*find_chart(scratch.document, chart_id), preview.note);
        if (source->sound_channel_event) continue;
        const auto validated = apply_bgm_event_move(scratch, scratch.document.revision,
            {.chart = chart_id, .event = preview.note, .position = preview.position, .source_lane = preview.lane});
        if (!validated.diagnostics.empty()) return validated.diagnostics;
        if (validated.applied)
            return {error(diagnostic_code_t::collision, "Mixed move would collide in a BGM source lane", preview.note)};
    }
    proposed = std::move(scratch.document);
    return {};
}

auto move_previews(const note_move_plan_t& plan) -> std::vector<note_move_preview_t> {
    std::vector<note_move_preview_t> result;
    result.reserve(plan.moved.size());
    for (const auto& note : plan.moved) {
        result.push_back({note.id, note.lane, note.position, note.end_position, note.conflict,
                          note.occupant, note.display_column, note.display_columns,
                          note.display_begin_row, note.display_end_row});
    }
    return result;
}

auto move_conflict_count(const note_move_plan_t& plan) -> std::size_t {
    return std::ranges::count_if(
        plan.moved, [](const auto& note) { return note.conflict != note_move_conflict_t::none; });
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

auto playhead_gesture_active(const editor_interaction_state_t& state) -> bool {
    return state.pending_hold || !state.additional_keyboard_holds.empty() ||
           state.pending_virtual_lane_key || state.pending_note_duration_gesture ||
           state.pending_note_move_gesture || state.pending_note_paint_gesture ||
           state.pending_semantic_layer_paint_gesture || state.pending_editor_area_selection ||
           state.pending_space_range_selection;
}

auto proposed_playhead_cursor(const document_history_t& history,
                             const chart_document_t& chart, const measure_index_t& index,
                             const editor_interaction_state_t& state, local_position_t position)
    -> std::variant<editor_interaction_state_t, diagnostic_list_t> {
    auto proposed = state;
    if (proposed.cursor == position && !proposed.virtual_bms_cursor_row) return proposed;
    place_cursor_at(proposed, position);
    if (!interaction_detail::keep_cursor_visible(chart, index, proposed))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                      "playhead cursor cannot keep the viewport valid")};
    auto view = make_view(history, proposed, &index);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = transition_lane(chart, index, state.cursor, position,
                                                 state.active_lane);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = make_view(history, proposed, &index);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view)) return *diagnostics;
    return proposed;
}

} // namespace

auto inspect_editor_interaction(const document_history_t& history,
                                const editor_interaction_state_t& state)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    return make_view(history, state);
}

auto inspect_editor_interaction(const document_history_t& history,
                                const editor_interaction_state_t& state,
                                editor_measure_geometry_t geometry)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    const auto* retained = geometry.index && geometry.chart == state.chart &&
        geometry.document_revision == history.document.revision ? geometry.index : nullptr;
    return make_view(history, state, retained);
}

namespace {
auto prepare_follow_view_state(const document_history_t& history, const chart_document_t& chart,
    const measure_index_t& index, const editor_interaction_state_t& state, double fraction)
    -> std::variant<editor_interaction_state_t, diagnostic_list_t> {
    if (!editor_viewport_margins_valid(state.viewport_minimum_margin_fraction,
                                       state.viewport_end_margin_fraction))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution, "playhead viewport margin is invalid")};
    auto proposed = state;
    proposed.viewport_minimum_margin_fraction = std::max(editor_cursor_edge_margin_fraction, fraction);
    proposed.viewport_end_margin_fraction = std::min(editor_cursor_edge_margin_fraction, fraction);
    if (!note_kind_allowed(chart, proposed.armed_note_kind)) proposed.armed_note_kind = note_kind_t::tap;
    const auto maximum = maximum_viewport_scroll(chart, index,
        proposed.pixels_per_standard_measure, proposed.viewport_height_pixels, proposed.viewport_end_margin_fraction);
    if (!maximum || !std::isfinite(proposed.viewport_scroll_pixels))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution, "playhead viewport geometry is invalid")};
    set_viewport_scroll_pixels(proposed, std::clamp(proposed.viewport_scroll_pixels,
        -proposed.viewport_height_pixels * proposed.viewport_minimum_margin_fraction, *maximum));
    auto view = make_view(history, proposed, &index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view)) return *diagnostics;
    return proposed;
}
} // namespace

auto follow_editor_playhead(document_history_t& history, editor_interaction_state_t& state,
                            editor_measure_geometry_t geometry, local_position_t position,
                            double target_scroll, double following_anchor_fraction,
                            double cursor_subrow) -> editor_interaction_result_t {
    const auto* chart = find_chart(history.document, state.chart);
    if (!chart || !geometry.index || geometry.chart != state.chart ||
        geometry.document_revision != history.document.revision ||
        !std::isfinite(target_scroll) || !std::isfinite(following_anchor_fraction) ||
        !std::isfinite(cursor_subrow) ||
        following_anchor_fraction < 0.0 || following_anchor_fraction > 1.0 || playhead_gesture_active(state))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                     "playhead geometry is stale or playback editing is blocked")}};
    const auto& index = *geometry.index;
    if (!absolute_row(index, position))
        return {.diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                     "playhead cursor is outside chart geometry")}};
    auto initial = prepare_follow_view_state(history, *chart, index, state, following_anchor_fraction);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&initial))
        return {.diagnostics = *diagnostics};
    auto candidate = proposed_playhead_cursor(history, *chart, index,
        std::get<editor_interaction_state_t>(initial), position);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&candidate))
        return {.diagnostics = *diagnostics};
    auto proposed = std::get<editor_interaction_state_t>(std::move(candidate));
    const auto maximum = maximum_viewport_scroll(*chart, index,
        proposed.pixels_per_standard_measure, proposed.viewport_height_pixels, proposed.viewport_end_margin_fraction);
    if (!maximum)
        return {.diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                     "playhead viewport geometry is invalid")}};
    if (std::abs(target_scroll - proposed.viewport_scroll_pixels) >= 0.001)
        set_viewport_scroll_pixels(proposed, std::clamp(target_scroll,
            -proposed.viewport_height_pixels * proposed.viewport_minimum_margin_fraction, *maximum));
    set_cursor_subrow(proposed, cursor_subrow);
    auto view = make_view(history, proposed, &index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return {.diagnostics = *diagnostics};
    state = std::move(proposed);
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(std::move(view))};
}

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

auto recover_editor_interaction(const document_history_t& history,
                                const editor_interaction_state_t& previous)
    -> editor_interaction_setup_t {
    editor_interaction_setup_t result;
    const auto* chart = find_chart(history.document, previous.chart);
    if (chart == nullptr) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "editor interaction refers to an unknown chart"));
        return result;
    }
    const auto built_index = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_index)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& index = std::get<measure_index_t>(built_index);
    std::optional<local_position_t> cursor;
    lane_id_t lane;
    auto first_row = std::numeric_limits<std::uint64_t>::max();
    for (const auto& segment : chart->lane_layouts) {
        const auto row = absolute_row(index, segment.position);
        if (!row || *row >= first_row)
            continue;
        const auto editable = std::ranges::find_if(segment.layout.lanes, editable_lane);
        if (editable == segment.layout.lanes.end())
            continue;
        first_row = *row;
        cursor = segment.position;
        lane = editable->id;
    }
    if (!cursor) {
        result.diagnostics.push_back(error(diagnostic_code_t::unknown_lane,
                                           "chart has no editable lane for a recovered cursor"));
        return result;
    }
    result = make_editor_interaction(history, previous.chart, lane, *cursor,
                                     previous.supported_snap_denominators,
                                     previous.active_snap_denominator,
                                     previous.viewport_height_pixels);
    if (!result)
        return result;
    if (std::isfinite(previous.pixels_per_standard_measure) &&
        previous.pixels_per_standard_measure >= minimum_pixels_per_standard_measure &&
        previous.pixels_per_standard_measure <= maximum_pixels_per_standard_measure) {
        set_cursor_scale(*result.state, previous.pixels_per_standard_measure);
    }
    const auto maximum_scroll = maximum_viewport_scroll(
        *chart, index, result.state->pixels_per_standard_measure,
        result.state->viewport_height_pixels);
    if (maximum_scroll && std::isfinite(previous.viewport_scroll_pixels)) {
        set_viewport_scroll_pixels(*result.state, std::clamp(previous.viewport_scroll_pixels,
                       minimum_viewport_scroll(result.state->viewport_height_pixels), *maximum_scroll));
    }
    if (previous.fixed_cursor_screen_y_pixels &&
        std::isfinite(*previous.fixed_cursor_screen_y_pixels) &&
        *previous.fixed_cursor_screen_y_pixels >= 0.0 &&
        *previous.fixed_cursor_screen_y_pixels <= result.state->viewport_height_pixels) {
        set_cursor_screen_anchor(*result.state, previous.fixed_cursor_screen_y_pixels, previous.scroll_direction);
    }
    if (note_kind_allowed(*chart, previous.armed_note_kind))
        result.state->armed_note_kind = previous.armed_note_kind;
    const auto view = make_view(history, *result.state);
    if (const auto* recovered = std::get_if<editor_interaction_view_model_t>(&view)) {
        result.view = *recovered;
    } else {
        result.state.reset();
        result.view.reset();
        result.diagnostics = std::get<diagnostic_list_t>(view);
    }
    return result;
}

struct editor_delete_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
};

struct editor_delete_ids_t {
    std::unordered_set<std::uint64_t> notes;
    std::unordered_set<std::uint64_t> bgm_events;
    // Chart-level objects that are not on a measure; IDs are unique within each family.
    std::unordered_set<std::uint64_t> bookmarks;
    std::unordered_set<std::uint64_t> sections;
    std::unordered_set<std::uint64_t> bga_events;

    [[nodiscard]] auto total() const -> std::size_t {
        return notes.size() + bgm_events.size() + bookmarks.size() + sections.size() +
               bga_events.size();
    }
    [[nodiscard]] auto family(editor_selection_object_kind_t kind)
        -> std::unordered_set<std::uint64_t>* {
        switch (kind) {
        case editor_selection_object_kind_t::note: return &notes;
        case editor_selection_object_kind_t::bgm_event: return &bgm_events;
        case editor_selection_object_kind_t::bookmark: return &bookmarks;
        case editor_selection_object_kind_t::section: return &sections;
        case editor_selection_object_kind_t::bga_event: return &bga_events;
        default: return nullptr;
        }
    }
};

auto collect_delete_object_ids(const std::vector<editor_selection_object_t>& objects)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    editor_delete_ids_t ids;
    for (const auto object : objects) {
        auto* family = ids.family(object.kind);
        if (family == nullptr) {
            return diagnostic_list_t{error(diagnostic_code_t::unsupported_feature,
                                           "Delete does not support this selected object type",
                                           object.id)};
        }
        if (!object.id || !family->insert(object.id.value).second) {
            return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                           "Delete selection contains a duplicate or empty identity",
                                           object.id)};
        }
    }
    return ids;
}

auto collect_delete_note_ids(const std::vector<object_id_t>& notes)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    editor_delete_ids_t ids;
    for (const auto id : notes) {
        if (!id || !ids.notes.insert(id.value).second) {
            return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                           "selected note list contains a duplicate or empty identity",
                                           id)};
        }
    }
    return ids;
}

auto collect_delete_ids(const editor_interaction_state_t& state)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    return state.selected_objects.empty() ? collect_delete_note_ids(state.selected_notes)
                                          : collect_delete_object_ids(state.selected_objects);
}

auto validate_delete_note_ids(const chart_document_t& chart,
                              const std::unordered_set<std::uint64_t>& ids)
    -> std::optional<diagnostic_t> {
    const auto available = chart_note_ids(chart);
    for (const auto id : ids) {
        if (!available.contains(id))
            return error(diagnostic_code_t::invalid_id, "selected note no longer exists",
                         object_id_t{id});
    }
    return std::nullopt;
}

auto inspect_delete_bgm_events(const chart_document_t& chart, const editor_delete_ids_t& ids)
    -> std::variant<std::optional<object_id_t>, diagnostic_list_t> {
    std::optional<object_id_t> linked_bmson_event;
    std::size_t found_bgm{};
    const auto branch_source_orders = ids.bgm_events.empty()
                                          ? std::unordered_set<std::size_t>{}
                                          : bms_branch_source_orders(chart);
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (!ids.bgm_events.contains(event.id.value))
                continue;
            ++found_bgm;
            if (event.sound_channel_event) {
                if (ids.total() != 1) {
                    return diagnostic_list_t{error(
                        diagnostic_code_t::unsupported_feature,
                        "delete linked BMSON markers one at a time", event.id)};
                }
                linked_bmson_event = event.id;
            } else if (event.provenance.family == format_family_t::bms &&
                       !event.provenance.source_spelling.empty() &&
                       branch_source_orders.contains(event.provenance.source_order)) {
                return diagnostic_list_t{error(
                    diagnostic_code_t::unsupported_feature,
                    "branch-owned BMS BGM events need a source-aware delete", event.id)};
            }
        }
    }
    if (found_bgm != ids.bgm_events.size()) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "selected BGM event no longer exists")};
    }
    return linked_bmson_event;
}

auto complete_editor_delete(editor_delete_context_t context, bool changed)
    -> editor_interaction_result_t {
    if (changed) {
        context.state.selected_notes.clear();
        context.state.selected_objects.clear();
    }
    const auto view = make_view(context.history, context.state);
    return {
        .accepted = true,
        .document_changed = changed,
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto delete_editor_selection(editor_delete_context_t context) -> editor_interaction_result_t {
    auto collected = collect_delete_ids(context.state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
        return diagnostic_result(context.history, context.state, *diagnostics);
    auto ids = std::get<editor_delete_ids_t>(std::move(collected));
    if (const auto invalid = validate_delete_note_ids(context.chart, ids.notes))
        return diagnostic_result(context.history, context.state, {*invalid});
    auto inspected = inspect_delete_bgm_events(context.chart, ids);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&inspected))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto linked_event = std::get<std::optional<object_id_t>>(inspected);
    if (linked_event) {
        auto command = delete_bmson_bgm_marker(context.history,
                                              context.history.document.revision,
                                              context.state.chart, *linked_event);
        if (!command)
            return diagnostic_result(context.history, context.state,
                                     std::move(command.diagnostics));
        return complete_editor_delete(context, true);
    }

    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    std::size_t removed{};
    const auto selected = ids.total() != 0;
    removed += std::erase_if(chart->bookmarks, [&](const auto& bookmark) {
        return ids.bookmarks.contains(bookmark.id.value);
    });
    removed += std::erase_if(chart->sections, [&](const auto& section) {
        return ids.sections.contains(section.id.value);
    });
    removed += std::erase_if(chart->bga_events, [&](const auto& event) {
        return ids.bga_events.contains(event.id.value);
    });
    for (auto& measure : chart->measures) {
        if (selected) {
            removed += std::erase_if(measure.notes, [&](const auto& note) {
                return ids.notes.contains(note.id.value);
            });
            removed += std::erase_if(measure.bgm_events, [&](const auto& event) {
                return ids.bgm_events.contains(event.id.value);
            });
        } else {
            removed += std::erase_if(measure.notes, [&](const auto& note) {
                return note.position == context.state.cursor &&
                       note.lane == context.state.active_lane;
            });
        }
    }
    if (removed == 0)
        return complete_editor_delete(context, false);
    if (selected && removed != ids.total()) {
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "selected deletion did not resolve every object")});
    }
    auto command = commit_document_command(
        context.history, std::move(proposed),
        selected ? "Delete selected chart objects" : "Delete note");
    if (!command)
        return diagnostic_result(context.history, context.state, std::move(command.diagnostics));
    return complete_editor_delete(context, true);
}

struct lane_assignment_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
};

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

auto collect_assignment_candidates(const lane_assignment_context_t& context)
    -> std::variant<std::vector<assignment_candidate_t>, diagnostic_list_t> {
    if (context.state.selected_notes.empty()) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "lane assignment requires one or more selected notes")};
    }
    std::unordered_set<std::uint64_t> selected_ids;
    selected_ids.reserve(context.state.selected_notes.size());
    for (const auto id : context.state.selected_notes) {
        if (!id || !selected_ids.insert(id.value).second) {
            return diagnostic_list_t{error(
                diagnostic_code_t::duplicate_id,
                "selected lane assignment contains an invalid or duplicate ID", id)};
        }
    }
    std::vector<assignment_candidate_t> candidates;
    candidates.reserve(selected_ids.size());
    for (const auto& measure : context.chart.measures) {
        for (const auto& note : measure.notes) {
            if (!selected_ids.contains(note.id.value))
                continue;
            const auto begin = absolute_row(context.index, note.position);
            const auto end = note.end ? absolute_row(context.index, *note.end) : begin;
            if (!begin || !end) {
                return diagnostic_list_t{error(
                    diagnostic_code_t::row_out_of_range,
                    "selected lane assignment lies outside chart geometry", note.id)};
            }
            candidates.push_back({&note, *begin, *end});
        }
    }
    if (candidates.size() != selected_ids.size()) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "selected lane assignment refers to an unavailable note")};
    }
    std::ranges::sort(candidates, [](const auto& left, const auto& right) {
        return std::tie(left.begin, left.note->lane.value, left.note->id.value) <
               std::tie(right.begin, right.note->lane.value, right.note->id.value);
    });
    return candidates;
}

auto build_assignment_intervals(const lane_assignment_context_t& context,
                                std::span<const assignment_candidate_t> candidates,
                                lane_id_t target_lane)
    -> std::variant<std::vector<assignment_interval_t>, diagnostic_list_t> {
    std::unordered_set<std::uint64_t> assigned_ids;
    assigned_ids.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        assigned_ids.insert(candidate.note->id.value);
        if (find_active_lane(context.chart, context.index, candidate.note->position,
                             target_lane) == nullptr ||
            (candidate.note->end &&
             find_active_lane(context.chart, context.index, *candidate.note->end,
                              target_lane) == nullptr)) {
            return diagnostic_list_t{error(
                diagnostic_code_t::unknown_lane,
                "target lane is unavailable at a selected note endpoint", candidate.note->id)};
        }
    }
    auto chart_note_count = std::size_t{};
    for (const auto& measure : context.chart.measures) {
        if (measure.notes.size() > std::numeric_limits<std::size_t>::max() - chart_note_count) {
            return diagnostic_list_t{error(
                diagnostic_code_t::invalid_resolution,
                "chart note count overflows lane-assignment capacity")};
        }
        chart_note_count += measure.notes.size();
    }
    if (candidates.size() > std::numeric_limits<std::size_t>::max() - chart_note_count) {
        return diagnostic_list_t{error(
            diagnostic_code_t::invalid_resolution,
            "lane-assignment interval count exceeds capacity")};
    }
    std::vector<assignment_interval_t> intervals;
    intervals.reserve(chart_note_count + candidates.size());
    for (const auto& candidate : candidates)
        intervals.push_back({candidate.begin, candidate.end, candidate.note->id, true});
    for (const auto& measure : context.chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != target_lane || assigned_ids.contains(note.id.value))
                continue;
            const auto begin = absolute_row(context.index, note.position);
            const auto end = note.end ? absolute_row(context.index, *note.end) : begin;
            if (!begin || !end) {
                return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                               "target-lane note lies outside chart geometry",
                                               note.id)};
            }
            intervals.push_back({*begin, *end, note.id, false});
        }
    }
    std::ranges::sort(intervals, [](const auto& left, const auto& right) {
        return std::tie(left.begin, left.end, left.moving, left.note.value) <
               std::tie(right.begin, right.end, right.moving, right.note.value);
    });
    return intervals;
}

auto validate_assignment_intervals(std::span<const assignment_interval_t> intervals)
    -> std::optional<diagnostic_t> {
    std::optional<assignment_interval_t> longest_moving;
    std::optional<assignment_interval_t> longest_existing;
    for (const auto& interval : intervals) {
        const auto& opposite = interval.moving ? longest_existing : longest_moving;
        if (opposite && interval.begin <= opposite->end) {
            return error(diagnostic_code_t::collision,
                         "lane assignment overlaps another target-lane note",
                         interval.moving ? interval.note : opposite->note);
        }
        if (interval.moving && longest_moving && interval.begin <= longest_moving->end) {
            return error(diagnostic_code_t::collision,
                         "selected notes overlap when assigned to one lane", interval.note);
        }
        auto& longest = interval.moving ? longest_moving : longest_existing;
        if (!longest || longest->end < interval.end)
            longest = interval;
    }
    return std::nullopt;
}

auto assign_selected_notes_to_lane(lane_assignment_context_t context,
                                   const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t {
    auto collected = collect_assignment_candidates(context);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
        return diagnostic_result(context.history, context.state, *diagnostics);
    auto candidates = std::get<std::vector<assignment_candidate_t>>(std::move(collected));
    if (action.successive)
        candidates.resize(1);
    auto built_intervals = build_assignment_intervals(context, candidates, action.lane);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_intervals))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto& intervals = std::get<std::vector<assignment_interval_t>>(built_intervals);
    if (const auto invalid = validate_assignment_intervals(intervals))
        return diagnostic_result(context.history, context.state, {*invalid});

    std::unordered_set<std::uint64_t> assigned_ids;
    for (const auto& candidate : candidates)
        assigned_ids.insert(candidate.note->id.value);
    std::optional<editor_object_audition_request_t> audition;
    if (action.successive) {
        audition = editor_object_audition_request_t{
            .document_revision = context.history.document.revision,
            .chart = context.state.chart,
            .object = {editor_selection_object_kind_t::note, candidates.front().note->id}};
    }
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    bool changed = false;
    std::size_t assigned_count{};
    for (auto& measure : chart->measures) {
        for (auto& note : measure.notes) {
            if (!assigned_ids.contains(note.id.value))
                continue;
            changed = changed || note.lane != action.lane;
            note.lane = action.lane;
            ++assigned_count;
        }
    }
    if (assigned_count != candidates.size()) {
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "lane assignment could not resolve every selected note")});
    }
    if (changed) {
        auto command = commit_document_command(
            context.history, std::move(proposed),
            action.successive ? "Assign next selected note to lane"
                              : "Assign selected notes to lane");
        if (!command)
            return diagnostic_result(context.history, context.state,
                                     std::move(command.diagnostics));
    }
    if (audition)
        audition->document_revision = context.history.document.revision;
    if (action.successive) {
        const auto assigned = audition->object.id;
        std::erase(context.state.selected_notes, assigned);
        std::erase_if(context.state.selected_objects, [&](const auto& object) {
            return object.kind == editor_selection_object_kind_t::note &&
                   object.id == assigned;
        });
    }
    context.state.active_lane = action.lane;
    const auto view = make_view(context.history, context.state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .document_changed = changed,
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
        .audition = audition,
    };
}

struct lane_move_candidate_t {
    std::size_t measure{};
    std::uint64_t row{};
    editor_selection_object_kind_t kind{};
    object_id_t id;
    lane_id_t lane;
};

auto precedes(const lane_move_candidate_t& left, const lane_move_candidate_t& right) -> bool {
    return std::tie(left.measure, left.row, left.kind, left.lane.value, left.id.value) <
           std::tie(right.measure, right.row, right.kind, right.lane.value, right.id.value);
}

auto first_selected_lane_object(const chart_document_t& chart,
                                std::span<const object_id_t> notes,
                                std::span<const object_id_t> bgm_events)
    -> std::optional<lane_move_candidate_t> {
    std::unordered_set<std::uint64_t> note_ids, event_ids;
    for (const auto id : notes) note_ids.insert(id.value);
    for (const auto id : bgm_events) event_ids.insert(id.value);
    std::optional<lane_move_candidate_t> first;
    const auto consider = [&](lane_move_candidate_t candidate) {
        if (!first || precedes(candidate, *first)) first = candidate;
    };
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto& measure = chart.measures[index];
        for (const auto& note : measure.notes)
            if (note_ids.contains(note.id.value))
                consider({index, note.position.row, editor_selection_object_kind_t::note,
                          note.id, note.lane});
        for (const auto& event : measure.bgm_events)
            if (event_ids.contains(event.id.value))
                consider({index, event.position.row,
                          editor_selection_object_kind_t::bgm_event, event.id,
                          lane_id_t{event.source_lane.value}});
    }
    return first;
}

auto selected_lane_assignment_ids(const editor_interaction_state_t& state)
    -> std::pair<std::vector<object_id_t>, std::vector<object_id_t>> {
    auto notes = state.selected_notes;
    std::unordered_set<std::uint64_t> note_ids;
    for (const auto id : notes) note_ids.insert(id.value);
    std::vector<object_id_t> events;
    for (const auto& object : state.selected_objects) {
        if (object.kind == editor_selection_object_kind_t::note &&
            note_ids.insert(object.id.value).second) notes.push_back(object.id);
        if (object.kind == editor_selection_object_kind_t::bgm_event) events.push_back(object.id);
    }
    return {std::move(notes), std::move(events)};
}

auto assign_selected_objects_to_lane(lane_assignment_context_t context,
                                     const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t {
    auto [notes, events] = selected_lane_assignment_ids(context.state);
    const auto first = action.successive ? first_selected_lane_object(context.chart, notes, events)
                                         : std::nullopt;
    if (action.successive && first) {
        notes = first->kind == editor_selection_object_kind_t::note
                    ? std::vector{first->id} : std::vector<object_id_t>{};
        events = first->kind == editor_selection_object_kind_t::bgm_event
                    ? std::vector{first->id} : std::vector<object_id_t>{};
    }
    const auto revision = context.history.document.revision;
    const auto command = move_selected_objects_to_lane(
        context.history, revision, context.state, notes, events, action.lane);
    if (!command.applied)
        return diagnostic_result(context.history, context.state, command.diagnostics);
    std::unordered_set<std::uint64_t> assigned_ids;
    for (const auto id : notes) assigned_ids.insert(id.value);
    for (const auto id : events) assigned_ids.insert(id.value);
    const auto assigned = [&](object_id_t id) { return assigned_ids.contains(id.value); };
    if (action.successive) {
        std::erase_if(context.state.selected_notes, assigned);
        std::erase_if(context.state.selected_objects, [&](const auto& object) {
            return (object.kind == editor_selection_object_kind_t::note ||
                    object.kind == editor_selection_object_kind_t::bgm_event) && assigned(object.id);
        });
    } else {
        std::erase_if(context.state.selected_objects, [&](const auto& object) {
            return object.kind == editor_selection_object_kind_t::bgm_event && assigned(object.id);
        });
        for (const auto id : events) {
            context.state.selected_notes.push_back(id);
            context.state.selected_objects.push_back({editor_selection_object_kind_t::note, id});
        }
    }
    context.state.active_lane = action.lane;
    auto result = interaction_view_result(context.history, context.state,
                                          revision != context.history.document.revision);
    if (action.successive && first)
        result.audition = editor_object_audition_request_t{
            .document_revision = context.history.document.revision, .chart = context.state.chart,
            .object = {editor_selection_object_kind_t::note, first->id}};
    return result;
}

struct note_gesture_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    std::function<std::variant<duration_resize_plan_t, diagnostic_list_t>(
        std::vector<object_id_t>, object_id_t, local_position_t, bool)> make_duration_plan;
    std::function<editor_interaction_result_t(const duration_resize_plan_t&)> apply_duration_plan;
    std::function<std::variant<note_move_plan_t, diagnostic_list_t>(
        std::vector<object_id_t>, object_id_t, local_position_t, lane_id_t, bool, std::optional<object_id_t>, std::optional<std::uint64_t>)> make_move_plan;
    std::function<editor_interaction_result_t(const note_move_plan_t&)> apply_move_plan;
};

auto begin_note_duration_gesture(note_gesture_context_t& context, object_id_t reference_id)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture || state.pending_note_move_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    const auto* reference = find_note(context.chart, reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id,
                   "duration gesture requires a tap or hold reference note", reference_id)});
    auto targets = state.selected_notes;
    if (std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    const auto initial_end = reference->end.value_or(reference->position);
    auto planned = context.make_duration_plan(targets, reference_id, initial_end, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& plan = std::get<duration_resize_plan_t>(planned);
    state.pending_note_duration_gesture = pending_note_duration_gesture_t{
        .reference_note = reference_id,
        .document_revision = context.history.document.revision,
        .notes = plan.notes,
        .preview_end = plan.reference_end,
        .preview = duration_previews(plan),
    };
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto update_note_duration_gesture(note_gesture_context_t& context,
                                  local_position_t requested_end) -> editor_interaction_result_t {
    auto& state = context.state;
    if (!state.pending_note_duration_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "duration gesture update requires an active gesture")});
    const auto pending = *state.pending_note_duration_gesture;
    if (pending.document_revision != context.history.document.revision)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "duration gesture is stale after a document edit")});
    const auto* reference = find_note(context.chart, pending.reference_note);
    if (!reference)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id,
                   "duration gesture reference note no longer exists", pending.reference_note)});
    auto snapped_end = snapped_position(context.chart, context.index, requested_end,
                                        edit_snap_denominator(context.chart, state));
    if (!snapped_end)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "duration gesture end cannot be snapped inside chart")});
    const auto head = absolute_row(context.index, reference->position);
    const auto target = absolute_row(context.index, *snapped_end);
    auto exact_head = false;
    if (head && target && *target < *head) {
        snapped_end = reference->position;
        exact_head = true;
    }
    auto planned = context.make_duration_plan(pending.notes, pending.reference_note, *snapped_end,
                                              exact_head);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& plan = std::get<duration_resize_plan_t>(planned);
    state.pending_note_duration_gesture->preview_end = plan.reference_end;
    state.pending_note_duration_gesture->preview = duration_previews(plan);
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto release_note_duration_gesture(note_gesture_context_t& context,
                                   local_position_t requested_end) -> editor_interaction_result_t {
    const auto updated = update_note_duration_gesture(context, requested_end);
    if (!updated)
        return updated;
    const auto pending = *context.state.pending_note_duration_gesture;
    auto planned = context.make_duration_plan(pending.notes, pending.reference_note,
                                              pending.preview_end, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, context.state, *diagnostics);
    return context.apply_duration_plan(std::get<duration_resize_plan_t>(planned));
}

auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<diagnostic_t>;

auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t>;

void prepare_rejected_bgm_preview(const chart_document_t& chart, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    auto lane = pending.preferred_bgm_lane.value_or(chart.bgm_source_lanes.empty()
        ? object_id_t{std::numeric_limits<std::uint64_t>::max()} : chart.bgm_source_lanes.front().id);
    if (chart.bgm_source_lanes.empty()) pending.new_bgm_lanes.push_back({lane, 0});
    if (pending.to_bgm) {
        for (const auto& moved : plan.moved) {
            const auto* note = find_note(chart, moved.id);
            pending.bgm_preview.push_back({moved.id, lane, moved.position, moved.end_position, note->sound});
        }
        return;
    }
    for (const auto id : plan.occupants) {
        const auto* note = find_note(chart, id);
        if (note) pending.bgm_preview.push_back({id, lane, note->position, note->end, note->sound});
    }
}

void prepare_selected_bgm_preview(note_gesture_context_t& context, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    if (plan.staged_bgm_document) {
        const auto* destination = find_chart(*plan.staged_bgm_document, context.state.chart);
        for (const auto& lane : destination->bgm_source_lanes)
            if (std::ranges::find(context.chart.bgm_source_lanes, lane.id, &bgm_source_lane_t::id) == context.chart.bgm_source_lanes.end())
                pending.new_bgm_lanes.push_back({lane.id, lane.source_order});
        return;
    }
    if (plan.bgm_moves.empty() || (plan.notes.empty() && plan.bgm_moves.size() == 1)) return;
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    const auto note_failure = relocate_moved_notes(*chart, plan, context.history, context.state);
    const auto failures = note_failure ? diagnostic_list_t{*note_failure}
                                       : relocate_selected_bgm(proposed, context.state.chart, plan);
    for (const auto& failure : failures) pending.bgm_diagnostics.push_back(failure.message);
    if (!failures.empty()) pending.conflicting_notes = std::max(std::size_t{1}, pending.conflicting_notes);
}

auto prepare_bgm_move_preview(note_gesture_context_t& context, const note_move_plan_t& plan,
                               pending_note_move_gesture_t& pending) -> void {
    pending.bgm_preview = plan.bgm_moves;
    pending.new_bgm_lanes.clear();
    pending.bgm_diagnostics.clear();
    if (!pending.to_bgm && (!pending.displace_to_bgm || plan.occupants.empty())) {
        prepare_selected_bgm_preview(context, plan, pending);
        return;
    }
    auto staged = stage_bgm_note_move(context.history, context.state, plan, pending);
    if (const auto* failures = std::get_if<diagnostic_list_t>(&staged)) {
        for (const auto& failure : *failures) pending.bgm_diagnostics.push_back(failure.message);
        pending.conflicting_notes = std::max(std::size_t{1}, pending.conflicting_notes);
        prepare_rejected_bgm_preview(context.chart, plan, pending);
        return;
    }
    const auto& chart = *find_chart(std::get<song_document_t>(staged), context.state.chart);
    const auto& ids = pending.to_bgm ? pending.notes : plan.occupants;
    for (const auto& lane : chart.bgm_source_lanes)
        if (std::ranges::find(context.chart.bgm_source_lanes, lane.id, &bgm_source_lane_t::id) ==
            context.chart.bgm_source_lanes.end()) pending.new_bgm_lanes.push_back({lane.id, lane.source_order});
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (std::ranges::find(ids, event.id) == ids.end()) continue;
            pending.bgm_preview.push_back({event.id, event.source_lane, event.position,
                                           event.end, event.sound});
        }
    }
    pending.conflicting_notes = 0;
    pending.occupants.clear();
    for (auto& preview : pending.preview) {
        if (preview.conflict != note_move_conflict_t::occupied) continue;
        preview.conflict = note_move_conflict_t::none;
        preview.occupant.reset();
    }
}

auto begin_note_move_gesture(note_gesture_context_t& context, object_id_t reference_id,
                              bool displace_to_bgm)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture || state.pending_note_move_gesture ||
        state.pending_note_paint_gesture || state.pending_semantic_layer_paint_gesture ||
        state.pending_editor_area_selection || state.pending_space_range_selection)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    const auto* reference = find_note(context.chart, reference_id);
    const auto* bgm_reference = find_bgm_event(context.chart, reference_id);
    if ((!reference && !bgm_reference) ||
        (reference && reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_id, "Move requires a tap, hold or BGM reference", reference_id)});
    auto targets = state.selected_notes;
    if (!bgm_reference && std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    auto planned = context.make_move_plan(targets, reference_id, reference ? reference->position : bgm_reference->position, lane_id_t{},
                                          true, std::nullopt, std::nullopt);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& plan = std::get<note_move_plan_t>(planned);
    state.pending_note_move_gesture = pending_note_move_gesture_t{
        .reference_note = reference_id,
        .document_revision = context.history.document.revision,
        .notes = plan.notes,
        .preview_position = plan.reference_position,
        .preview = move_previews(plan),
        .occupants = plan.occupants,
        .conflicting_notes = move_conflict_count(plan),
        .displace_to_bgm = displace_to_bgm,
    };
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto update_note_move_gesture(note_gesture_context_t& context,
                              local_position_t requested_position,
                              lane_id_t requested_lane, bool to_bgm,
                              std::optional<object_id_t> preferred_bgm_lane,
                              std::optional<std::uint64_t> virtual_bgm_row) -> editor_interaction_result_t {
    auto& state = context.state;
    if (!state.pending_note_move_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note move update requires an active gesture")});
    const auto pending = *state.pending_note_move_gesture;
    if (pending.document_revision != context.history.document.revision) {
        state.pending_note_move_gesture.reset();
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note move gesture is stale after a document edit")});
    }
    auto planned = context.make_move_plan(pending.notes, pending.reference_note, requested_position,
                                          to_bgm ? lane_id_t{} : requested_lane, false, preferred_bgm_lane, virtual_bgm_row);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) {
        state.pending_note_move_gesture->bgm_preview.clear();
        state.pending_note_move_gesture->new_bgm_lanes.clear();
        state.pending_note_move_gesture->conflicting_notes = 1;
        state.pending_note_move_gesture->bgm_diagnostics = {diagnostics->empty() ? "Move target unavailable" : diagnostics->front().message};
        return diagnostic_result(context.history, state, *diagnostics);
    }
    const auto& plan = std::get<note_move_plan_t>(planned);
    state.pending_note_move_gesture->virtual_bgm_row = virtual_bgm_row;
    state.pending_note_move_gesture->preview_position = plan.reference_position;
    state.pending_note_move_gesture->preview = move_previews(plan);
    state.pending_note_move_gesture->occupants = plan.occupants;
    state.pending_note_move_gesture->conflicting_notes = move_conflict_count(plan);
    state.pending_note_move_gesture->to_bgm = to_bgm;
    state.pending_note_move_gesture->preferred_bgm_lane = preferred_bgm_lane;
    prepare_bgm_move_preview(context, plan, *state.pending_note_move_gesture);
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto release_note_move_gesture(note_gesture_context_t& context,
                               local_position_t requested_position,
                               lane_id_t requested_lane, bool to_bgm,
                               std::optional<object_id_t> preferred_bgm_lane,
                              std::optional<std::uint64_t> virtual_bgm_row) -> editor_interaction_result_t {
    // A release that cannot commit ends the gesture: leaving it pending would keep drawing the
    // last valid preview as a ghost note.
    const auto reject = [&](diagnostic_list_t diagnostics) {
        context.state.pending_note_move_gesture.reset();
        return diagnostic_result(context.history, context.state, std::move(diagnostics));
    };
    auto updated = update_note_move_gesture(context, requested_position, requested_lane,
                                             to_bgm, preferred_bgm_lane, virtual_bgm_row);
    if (!updated)
        return context.state.pending_note_move_gesture ? reject(std::move(updated.diagnostics))
                                                       : updated;
    const auto pending = *context.state.pending_note_move_gesture;
    auto planned = context.make_move_plan(pending.notes, pending.reference_note,
                                          pending.preview_position, to_bgm ? lane_id_t{} : requested_lane, !virtual_bgm_row.has_value(), preferred_bgm_lane, virtual_bgm_row);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return reject(std::move(*diagnostics));
    const auto& plan = std::get<note_move_plan_t>(planned);
    if (pending.to_bgm || (pending.displace_to_bgm && !plan.occupants.empty())) {
        auto staged = stage_bgm_note_move(context.history, context.state, plan, pending);
        if (auto* failures = std::get_if<diagnostic_list_t>(&staged)) return reject(*failures);
        auto committed = commit_document_command(context.history,
            std::move(std::get<song_document_t>(staged)),
            pending.to_bgm ? "Move selected notes to BGM" : "Move notes and displace to BGM");
        if (!committed) return reject(std::move(committed.diagnostics));
        context.state.pending_note_move_gesture.reset();
        set_selected_notes(context.state, pending.to_bgm ? std::vector<object_id_t>{} : plan.notes);
        for (const auto& moved : plan.bgm_moves)
            context.state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, moved.note});
        if (pending.to_bgm) {
            for (const auto id : plan.notes)
                context.state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, id});
        }
        place_cursor_at(context.state, plan.reference_position);
        auto result = interaction_view_result(context.history, context.state, true);
        result.accepted = true;
        return result;
    }
    const auto conflicts = move_conflict_count(plan);
    if (conflicts != 0) {
        auto warning =
            error(diagnostic_code_t::collision, std::to_string(conflicts) + " notes would overlap");
        warning.severity = diagnostic_severity_t::warning;
        return reject({std::move(warning)});
    }
    return context.apply_move_plan(plan);
}

auto dispatch_note_gesture(note_gesture_context_t& context, const editor_ui_action_t& action)
    -> editor_interaction_result_t {
    return std::visit(
        [&](const auto& typed_action) -> editor_interaction_result_t {
            using action_t = std::decay_t<decltype(typed_action)>;
            if constexpr (std::is_same_v<action_t, begin_note_duration_gesture_action_t>)
                return begin_note_duration_gesture(context, typed_action.reference_note);
            else if constexpr (std::is_same_v<action_t, update_note_duration_gesture_action_t>)
                return update_note_duration_gesture(context, typed_action.reference_end);
            else if constexpr (std::is_same_v<action_t, release_note_duration_gesture_action_t>)
                return release_note_duration_gesture(context, typed_action.reference_end);
            else if constexpr (std::is_same_v<action_t, begin_note_move_gesture_action_t>)
                return begin_note_move_gesture(context, typed_action.reference_note,
                                               typed_action.displace_to_bgm);
            else if constexpr (std::is_same_v<action_t, update_note_move_gesture_action_t>)
                return update_note_move_gesture(context, typed_action.reference_position,
                                                typed_action.reference_lane, typed_action.to_bgm,
                                                typed_action.preferred_bgm_lane, typed_action.virtual_bgm_row);
            else if constexpr (std::is_same_v<action_t, release_note_move_gesture_action_t>)
                return release_note_move_gesture(context, typed_action.reference_position,
                                                typed_action.reference_lane, typed_action.to_bgm,
                                                typed_action.preferred_bgm_lane, typed_action.virtual_bgm_row);
            else
                return diagnostic_result(context.history, context.state,
                                         {error(diagnostic_code_t::invalid_id,
                                                "action is outside the note gesture family")});
        },
        action);
}

struct note_paint_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    std::function<editor_interaction_result_t()> cancel_gesture;
};

auto add_paint_path_cell(const note_paint_context_t& context, lane_id_t lane,
                         std::uint64_t row, const pending_note_paint_gesture_t& pending,
                         std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t> {
    const auto position = position_at(context.index, row);
    if (!position)
        return error(diagnostic_code_t::row_out_of_range, "paint path leaves chart geometry");
    if (find_active_lane(context.chart, context.index, *position, lane) == nullptr)
        return error(diagnostic_code_t::unknown_lane, "paint lane is unavailable along the drag path");
    if (pending.visited.contains({lane, row}))
        return std::nullopt;
    if (pending.targets->size() + additions.size() >= maximum_note_paint_targets)
        return error(diagnostic_code_t::invalid_resolution,
                     "paint drag exceeds the per-gesture note target cap");
    additions.push_back({lane, *position});
    return std::nullopt;
}

void cancel_stale_explicit_paint(editor_interaction_state_t& state) {
    if (state.pending_note_paint_gesture->intent != note_paint_intent_t::toggle)
        state.pending_note_paint_gesture.reset();
}

auto append_note_paint_target(note_paint_context_t& context, lane_id_t lane,
                              local_position_t position, bool pointer_captured)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (!pointer_captured)
        return context.cancel_gesture();
    if (!state.pending_note_paint_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note paint update requires an active gesture")});
    auto& pending = *state.pending_note_paint_gesture;
    if (pending.document_revision != context.history.document.revision) {
        cancel_stale_explicit_paint(state);
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note paint gesture is stale after a document edit")});
    }
    const auto target = snapped_position(context.chart, context.index, position,
                                         edit_snap_denominator(context.chart, state));
    if (!target)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint target cannot be snapped inside chart")});
    const auto target_row = absolute_row(context.index, *target);
    const auto previous_row = absolute_row(context.index, pending.last_position);
    if (!target_row || !previous_row)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint gesture lies outside chart geometry")});
    const auto same_lane = lane == pending.last_lane;
    const auto step = context.chart.divisions_per_standard_measure /
                      edit_snap_denominator(context.chart, state);
    const auto row_distance = *target_row >= *previous_row ? *target_row - *previous_row
                                                           : *previous_row - *target_row;
    const auto path_cells = same_lane ? row_distance / step + 1 : 1;
    if (path_cells > maximum_note_paint_targets)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "paint drag exceeds the per-gesture note target cap")});
    std::vector<note_paint_target_t> additions;
    additions.reserve(static_cast<std::size_t>(path_cells));
    if (same_lane) {
        const auto cell_count = row_distance / step + 1;
        for (std::uint64_t cell = 0; cell < cell_count; ++cell) {
            const auto offset = cell * step;
            const auto row = *target_row >= *previous_row ? *previous_row + offset
                                                           : *previous_row - offset;
            if (const auto invalid = add_paint_path_cell(context, lane, row, pending, additions))
                return diagnostic_result(context.history, state, {*invalid});
        }
    } else if (const auto invalid =
                   add_paint_path_cell(context, lane, *target_row, pending, additions)) {
        return diagnostic_result(context.history, state, {*invalid});
    }
    if (!additions.empty()) {
        auto expanded = std::make_shared<std::vector<note_paint_target_t>>(*pending.targets);
        expanded->reserve(expanded->size() + additions.size());
        for (const auto& addition : additions) {
            const auto row = absolute_row(context.index, addition.position);
            expanded->push_back(addition);
            pending.visited.insert({addition.lane, *row});
        }
        pending.targets = std::move(expanded);
    }
    pending.last_lane = lane;
    pending.last_position = *target;
    state.active_lane = lane;
    place_cursor_at(state, *target);
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto is_paint_sample_resource(const song_document_t& document, resource_id_t id) -> bool {
    const auto found = std::ranges::find(document.resources, id, &resource_entry_t::id);
    return found != document.resources.end() &&
           std::ranges::find(found->roles, resource_role_t::sample) != found->roles.end();
}

auto begin_note_paint_gesture(note_paint_context_t& context, lane_id_t lane,
                              local_position_t position, bool pointer_captured,
                              std::optional<resource_id_t> sound,
                              note_paint_intent_t intent)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (intent != note_paint_intent_t::toggle && intent != note_paint_intent_t::draw &&
        intent != note_paint_intent_t::erase)
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_hold, "note paint intent is invalid")});
    if (intent != note_paint_intent_t::erase &&
        (state.armed_note_kind == note_kind_t::hold || state.armed_note_kind == note_kind_t::roll))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_hold, "Use the hold gesture to place Hold or Roll notes; painting needs an explicit end.")});
    if (!pointer_captured)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "pointer capture is required to begin note painting")});
    if (intent != note_paint_intent_t::erase && sound &&
        !is_paint_sample_resource(context.history.document, *sound))
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")});
    if (state.pending_hold || state.pending_note_duration_gesture || state.pending_note_move_gesture ||
        state.pending_note_paint_gesture || state.pending_semantic_layer_paint_gesture ||
        state.pending_editor_area_selection || state.pending_space_range_selection)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    const auto target = snapped_position(context.chart, context.index, position,
                                         edit_snap_denominator(context.chart, state));
    if (!target)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint start cannot be snapped inside chart")});
    if (find_active_lane(context.chart, context.index, *target, lane) == nullptr)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::unknown_lane,
                                        "paint lane is unavailable at the start position")});
    const auto row = absolute_row(context.index, *target);
    if (!row)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint start lies outside chart geometry")});
    auto targets = std::make_shared<std::vector<note_paint_target_t>>();
    targets->push_back({lane, *target});
    pending_note_paint_gesture_t pending{
        .document_revision = context.history.document.revision,
        .targets = std::move(targets),
        .last_lane = lane,
        .last_position = *target,
        .sound = sound,
        .intent = intent,
    };
    pending.visited.reserve(64);
    pending.visited.insert({lane, *row});
    state.pending_note_paint_gesture = std::move(pending);
    state.active_lane = lane;
    place_cursor_at(state, *target);
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

struct prepared_note_paint_t {
    song_document_t document;
    std::vector<object_id_t> painted_notes;
};

auto prepare_note_paint_document(const document_history_t& history, chart_id_t chart_id,
                                const note_paint_plan_t& plan, note_kind_t kind,
                                std::optional<resource_id_t> sound,
                                const std::optional<std::vector<layer_id_t>>& armed_layers)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    const auto first_id = plan.additions.empty()
                              ? std::optional<object_id_t>{}
                              : next_object_id(*find_chart(history.document, chart_id));
    if (!plan.additions.empty() &&
        (!first_id || plan.additions.size() - 1 >
                          std::numeric_limits<std::uint64_t>::max() - first_id->value))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "chart object ID sequence is exhausted")};
    auto proposed = history.document;
    auto* chart = find_chart(proposed, chart_id);
    std::unordered_set<std::uint64_t> removed_ids;
    removed_ids.reserve(plan.removals.size());
    for (const auto id : plan.removals)
        removed_ids.insert(id.value);
    for (auto& measure : chart->measures)
        std::erase_if(measure.notes, [&](const auto& note) { return removed_ids.contains(note.id.value); });
    prepared_note_paint_t prepared{.document = std::move(proposed), .painted_notes = {}};
    auto* prepared_chart = find_chart(prepared.document, chart_id);
    std::unordered_map<std::uint64_t, measure_chunk_t*> prepared_measures;
    prepared_measures.reserve(prepared_chart->measures.size());
    for (auto& measure : prepared_chart->measures)
        prepared_measures.emplace(measure.id.value, &measure);
    auto next_id = first_id ? first_id->value : 0;
    if (!plan.additions.empty() && sound && !is_paint_sample_resource(history.document, *sound))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "paint sound is not an existing sample resource")};
    const auto layers = resolve_placement_layers(*prepared_chart, armed_layers, sound);
    prepared.painted_notes.reserve(plan.additions.size());
    for (const auto& target : plan.additions) {
        const auto found = prepared_measures.find(target.position.measure.value);
        if (found == prepared_measures.end())
            return diagnostic_list_t{error(diagnostic_code_t::unknown_measure,
                                           "paint target refers to an unknown measure")};
        const auto id = object_id_t{next_id++};
        found->second->notes.push_back({.id = id,
                                        .position = target.position,
                                        .lane = target.lane,
                                        .kind = kind,
                                        .end = std::nullopt,
                                        .sound = sound,
                                        .sound_channel_event = std::nullopt,
                                        .semantic_layers = layers});
        prepared.painted_notes.push_back(id);
    }
    return prepared;
}

auto end_note_paint_gesture(note_paint_context_t& context, lane_id_t lane,
                            local_position_t position, bool pointer_captured)
    -> editor_interaction_result_t {
    if (!pointer_captured)
        return append_note_paint_target(context, lane, position, false);
    const auto updated = append_note_paint_target(context, lane, position, true);
    if (!updated)
        return updated;
    auto& state = context.state;
    if (!state.pending_note_paint_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note paint gesture was cancelled before commit")});
    const auto pending = *state.pending_note_paint_gesture;
    if (pending.document_revision != context.history.document.revision) {
        if (pending.intent != note_paint_intent_t::toggle)
            state.pending_note_paint_gesture.reset();
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note paint gesture is stale after a document edit")});
    }
    auto planned = plan_note_paint(context.chart, context.index, *pending.targets,
                                   state.armed_note_kind, pending.intent);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& plan = std::get<note_paint_plan_t>(planned);
    if (plan.additions.empty() && plan.removals.empty() &&
        pending.intent == note_paint_intent_t::toggle)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint gesture contains no note changes")});
    if (plan.additions.empty() && plan.removals.empty()) {
        state.pending_note_paint_gesture.reset();
        const auto view = make_view(context.history, state);
        return {
            .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
        };
    }
    auto prepared = prepare_note_paint_document(context.history, state.chart, plan, state.armed_note_kind,
                                           pending.sound, state.armed_note_layers);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&prepared)) {
        if (pending.intent == note_paint_intent_t::draw && pending.sound &&
            !is_paint_sample_resource(context.history.document, *pending.sound))
            state.pending_note_paint_gesture.reset();
        return diagnostic_result(context.history, state, *diagnostics);
    }
    auto result = std::get<prepared_note_paint_t>(std::move(prepared));
    auto command = commit_document_command(context.history, std::move(result.document), "Paint notes");
    if (!command)
        return diagnostic_result(context.history, state, std::move(command.diagnostics));
    state.pending_note_paint_gesture.reset();
    set_selected_notes(state, std::move(result.painted_notes));
    state.active_lane = lane;
    place_cursor_at(state, pending.last_position);
    const auto view = make_view(context.history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .document_changed = true,
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto dispatch_note_paint_gesture(note_paint_context_t& context, const editor_ui_action_t& action)
    -> editor_interaction_result_t {
    return std::visit(
        [&](const auto& typed_action) -> editor_interaction_result_t {
            using action_t = std::decay_t<decltype(typed_action)>;
            if constexpr (std::is_same_v<action_t, begin_note_paint_gesture_action_t>)
                return begin_note_paint_gesture(context, typed_action.lane, typed_action.position,
                                                typed_action.pointer_captured, typed_action.sound,
                                                typed_action.intent);
            else if constexpr (std::is_same_v<action_t, update_note_paint_gesture_action_t>)
                return append_note_paint_target(context, typed_action.lane, typed_action.position,
                                                typed_action.pointer_captured);
            else if constexpr (std::is_same_v<action_t, end_note_paint_gesture_action_t>)
                return end_note_paint_gesture(context, typed_action.lane, typed_action.position,
                                              typed_action.pointer_captured);
            else
                return diagnostic_result(context.history, context.state,
                                         {error(diagnostic_code_t::invalid_id,
                                                "action is outside the note paint family")});
        },
        action);
}

struct editor_scroll_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    std::function<editor_interaction_result_t(local_position_t)> update_keyboard_holds;
    std::function<editor_interaction_result_t(std::uint64_t)> update_canvas_keyboard_holds;
};

auto update_fixed_scroll_cursor(editor_scroll_context_t& context,
                                const scroll_editor_view_action_t& action,
                                editor_interaction_state_t& proposed)
    -> std::variant<std::monostate, diagnostic_list_t> {
    if (!action.fixed_cursor_screen_y_pixels)
        return std::monostate{};
    const auto anchor_y = *action.fixed_cursor_screen_y_pixels;
    if (!std::isfinite(anchor_y) || anchor_y < 0.0 ||
        anchor_y > context.state.viewport_height_pixels ||
        (action.direction != editor_field_scroll_direction_t::up &&
         action.direction != editor_field_scroll_direction_t::down)) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "fixed cursor scroll anchor is invalid")};
    }
    set_cursor_screen_anchor(proposed, anchor_y, action.direction);
    if (!synchronize_editor_cursor_to_viewport(context.chart, context.index, proposed))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "fixed cursor scroll cannot resolve chart geometry")};
    return std::monostate{};
}

auto preserve_keyboard_hold_after_scroll(editor_scroll_context_t& context,
                                         const editor_interaction_state_t& proposed)
    -> std::optional<editor_interaction_result_t> {
    if (!context.state.pending_hold ||
        context.state.pending_hold->gesture_source != hold_gesture_source_t::keyboard_lane ||
        (proposed.cursor == context.state.cursor &&
         proposed.virtual_bms_cursor_row == context.state.virtual_bms_cursor_row))
        return std::nullopt;
    const auto before = context.state;
    const auto canvas_hold = proposed.virtual_bms_cursor_row ||
                             context.state.pending_hold->anchor_canvas_row ||
                             std::ranges::any_of(
                                 context.state.additional_keyboard_holds,
                                 [](const auto& hold) { return hold.anchor_canvas_row.has_value(); });
    const auto cursor_row = proposed.virtual_bms_cursor_row.value_or(
        *absolute_row(context.index, proposed.cursor));
    context.state = proposed;
    const auto updated = canvas_hold ? context.update_canvas_keyboard_holds(cursor_row)
                                     : context.update_keyboard_holds(proposed.cursor);
    if (!updated)
        context.state = before;
    return updated;
}

auto scroll_editor_view(editor_scroll_context_t context,
                        const scroll_editor_view_action_t& action) -> editor_interaction_result_t {
    if (!std::isfinite(action.vertical_pixels) ||
        !std::isfinite(context.state.viewport_scroll_pixels)) {
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "viewport scroll must remain finite")});
    }
    const auto next_scroll = context.state.viewport_scroll_pixels + action.vertical_pixels;
    if (!std::isfinite(next_scroll)) {
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "viewport scroll would overflow")});
    }
    const auto maximum_scroll = maximum_viewport_scroll(
        context.chart, context.index, context.state.pixels_per_standard_measure,
        context.state.viewport_height_pixels);
    if (!maximum_scroll)
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "editor viewport geometry is invalid")});
    auto proposed = context.state;
    set_viewport_scroll_pixels(proposed, std::clamp(
        next_scroll, minimum_viewport_scroll(context.state.viewport_height_pixels), *maximum_scroll));
    auto cursor_update = update_fixed_scroll_cursor(context, action, proposed);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&cursor_update))
        return diagnostic_result(context.history, context.state, *diagnostics);
    if (auto updated = preserve_keyboard_hold_after_scroll(context, proposed))
        return *updated;
    const auto view = make_view(context.history, proposed);
    if (std::holds_alternative<editor_interaction_view_model_t>(view))
        context.state = std::move(proposed);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

struct hold_gesture_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    std::function<bool(editor_interaction_state_t&)> keep_cursor_visible;
    std::function<editor_interaction_result_t(const editor_ui_action_t&)> dispatch_action;
};

auto remove_note_for_keyboard_hold(hold_gesture_context_t& context, lane_id_t lane,
                                   local_position_t position)
    -> std::optional<editor_interaction_result_t> {
    auto& state = context.state;
    const auto snapped = snapped_position(context.chart, context.index, position,
                                          edit_snap_denominator(context.chart, state));
    if (!snapped || !find_active_lane(context.chart, context.index, *snapped, lane))
        return std::nullopt;
    const auto plan = plan_hold_placement(context.chart, context.index, lane, *snapped, *snapped,
        note_kind_t::hold, false, hold_overlap_policy_t::remove_sound);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& placement = std::get<hold_placement_plan_t>(plan);
    if (placement.removals.empty())
        return std::nullopt;
    if (placement.removals.size() != 1)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::collision,
                   "lane-key removal is ambiguous across multiple notes")});
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, state.chart);
    erase_notes(*chart, placement.removals);
    auto command = commit_document_command(context.history, std::move(proposed), "Remove note");
    if (!command)
        return diagnostic_result(context.history, state, std::move(command.diagnostics));
    place_cursor_at(state, *snapped);
    state.active_lane = lane;
    const auto view = make_view(context.history, state);
    return editor_interaction_result_t{
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .document_changed = true,
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto begin_hold_gesture(hold_gesture_context_t& context, lane_id_t lane,
                        local_position_t position, hold_gesture_source_t source,
                        std::optional<resource_id_t> sound = std::nullopt)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture || state.pending_note_move_gesture)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    if (source == hold_gesture_source_t::keyboard_lane)
        if (auto removed = remove_note_for_keyboard_hold(context, lane, position))
            return std::move(*removed);
    if (sound && !is_paint_sample_resource(context.history.document, *sound))
        return diagnostic_result(context.history, state,
            {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
    const auto before = state;
    state.active_lane = lane;
    auto result = context.dispatch_action(begin_hold_action_t{position});
    if (!result) {
        state = before;
        const auto view = make_view(context.history, state);
        result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                          ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                          : std::nullopt;
        return result;
    }
    if (state.pending_hold) {
        state.pending_hold->gesture_source = source;
        state.pending_hold->sound = sound;
    }
    const auto view = make_view(context.history, state);
    result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                      ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                      : std::nullopt;
    result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                             ? std::get<diagnostic_list_t>(view)
                             : diagnostic_list_t{};
    return result;
}

auto update_hold_gesture(hold_gesture_context_t& context, hold_gesture_source_t source,
                         local_position_t position) -> editor_interaction_result_t {
    auto& state = context.state;
    const auto before = state;
    if (!state.pending_hold || state.pending_hold->gesture_source != source)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "hold gesture update does not match the active input")});
    const auto pending = *state.pending_hold;
    if (pending.document_revision != context.history.document.revision)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "pending hold gesture is stale after a document edit")});
    const auto target = snapped_position(context.chart, context.index, position,
                                         edit_snap_denominator(context.chart, state));
    state.pending_hold->gesture_source.reset();
    auto result = context.dispatch_action(update_hold_preview_action_t{target.value_or(position)});
    if (state.pending_hold)
        state.pending_hold->gesture_source = source;
    if (!result)
        return result;
    state.pending_hold->preview_canvas_row.reset();
    if (source == hold_gesture_source_t::keyboard_lane && !context.keep_cursor_visible(state)) {
        state = before;
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "hold movement cannot keep the cursor visible")});
    }
    const auto view = make_view(context.history, state);
    result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                      ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                      : std::nullopt;
    result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                             ? std::get<diagnostic_list_t>(view)
                             : diagnostic_list_t{};
    return result;
}

auto release_hold_gesture(hold_gesture_context_t& context, hold_gesture_source_t source,
                          local_position_t position) -> editor_interaction_result_t {
    auto& state = context.state;
    if (!state.pending_hold || state.pending_hold->gesture_source != source)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "hold gesture release does not match the active input")});
    const auto target = snapped_position(context.chart, context.index, position,
                                         edit_snap_denominator(context.chart, state));
    state.pending_hold->gesture_source.reset();
    auto result = context.dispatch_action(commit_hold_action_t{target.value_or(position)});
    if (state.pending_hold)
        state.pending_hold->gesture_source = source;
    if (result) {
        const auto view = make_view(context.history, state);
        result.view = std::holds_alternative<editor_interaction_view_model_t>(view)
                          ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                          : std::nullopt;
        result.diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                                 ? std::get<diagnostic_list_t>(view)
                                 : diagnostic_list_t{};
    }
    return result;
}

auto space_selection_view_result(
    const std::variant<editor_interaction_view_model_t, diagnostic_list_t>& view)
    -> editor_interaction_result_t;

auto begin_hold_action(hold_gesture_context_t& context, local_position_t position,
                       std::optional<resource_id_t> sound = std::nullopt)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto snapped = snapped_position(context.chart, context.index, position,
                                          edit_snap_denominator(context.chart, state));
    if (!snapped)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "hold start cannot be snapped inside chart geometry")});
    if (!find_active_lane(context.chart, context.index, *snapped, state.active_lane))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::unknown_lane,
                   "active editor lane is unavailable at the hold start")});
    if (sound && !is_paint_sample_resource(history.document, *sound))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
    const auto plan = plan_hold_placement(context.chart, context.index, state.active_lane,
                                          *snapped, *snapped);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    state.pending_hold = pending_hold_t{
        .position = *snapped,
        .lane = state.active_lane,
        .document_revision = history.document.revision,
        .placement_preview =
            hold_placement_preview(std::get<hold_placement_plan_t>(plan)),
        .sound = sound,
    };
    place_cursor_at(state, *snapped);
    return space_selection_view_result(make_view(history, state));
}

auto validate_pending_hold_start(const hold_gesture_context_t& context,
                                 const pending_hold_t& pending)
    -> std::optional<editor_interaction_result_t> {
    if (pending.document_revision == context.history.document.revision &&
        snapped_position(context.chart, context.index, pending.position,
                         edit_snap_denominator(context.chart, context.state)) == pending.position)
        return std::nullopt;
    return diagnostic_result(
        context.history, context.state,
        {error(diagnostic_code_t::invalid_hold,
               "pending hold start is stale after a document or snap change")});
}

auto update_hold_preview(hold_gesture_context_t& context, local_position_t position)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "hold preview requires a pending hold start")});
    const auto pending = *state.pending_hold;
    if (auto stale = validate_pending_hold_start(context, pending))
        return *stale;
    const auto snapped = snapped_position(context.chart, context.index, position,
                                          edit_snap_denominator(context.chart, state));
    if (!snapped)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "hold preview cannot be snapped inside chart geometry")});
    if (!find_active_lane(context.chart, context.index, *snapped, pending.lane))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::unknown_lane,
                   "hold lane is unavailable at the previewed endpoint")});
    const auto plan = plan_hold_placement(context.chart, context.index, pending.lane,
                                          pending.position, *snapped, state.armed_note_kind);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    state.pending_hold->preview_end = *snapped == pending.position
        ? std::optional<local_position_t>{}
        : std::optional{*snapped};
    state.pending_hold->placement_preview =
        hold_placement_preview(std::get<hold_placement_plan_t>(plan));
    place_cursor_at(state, *snapped);
    state.active_lane = pending.lane;
    return space_selection_view_result(make_view(history, state));
}

auto commit_hold(hold_gesture_context_t& context, local_position_t position)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "hold completion requires a pending hold start")});
    const auto pending = *state.pending_hold;
    if (auto stale = validate_pending_hold_start(context, pending))
        return *stale;
    const auto snapped = snapped_position(context.chart, context.index, position,
                                          edit_snap_denominator(context.chart, state));
    if (!snapped)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "hold end cannot be snapped inside chart geometry")});
    if (!find_active_lane(context.chart, context.index, *snapped, pending.lane))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::unknown_lane,
                   "hold lane is unavailable at the snapped end")});
    if (*snapped == pending.position &&
        (state.armed_note_kind == note_kind_t::hold || state.armed_note_kind == note_kind_t::roll))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_hold, "Move to a different row to finish the Hold or Roll gesture.")});
    if (pending.sound && !is_paint_sample_resource(history.document, *pending.sound))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
    const auto plan = plan_hold_placement(context.chart, context.index, pending.lane,
                                          pending.position, *snapped, state.armed_note_kind, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    auto proposed = history.document;
    auto* chart = find_chart(proposed, state.chart);
    const auto& placement = std::get<hold_placement_plan_t>(plan);
    std::optional<object_id_t> id;
    if (placement.adds_note) {
        id = next_object_id(*chart);
        if (!id)
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "chart object ID sequence is exhausted")});
    }
    erase_notes(*chart, placement.removals);
    if (placement.adds_note) {
        const auto measure = std::ranges::find(chart->measures, placement.head.measure,
                                               &measure_chunk_t::id);
        if (measure == chart->measures.end())
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::unknown_measure,
                       "normalized hold head refers to an unknown measure")});
        measure->notes.push_back({.id = *id, .position = placement.head,
                                  .lane = pending.lane,
                                  .kind = placement.tail
                                      ? held_note_kind(state.armed_note_kind)
                                      : state.armed_note_kind,
                                  .end = placement.tail,
                                  .sound = pending.sound,
                                  .semantic_layers = resolve_placement_layers(
                                      *chart, state.armed_note_layers, pending.sound)});
    }
    const auto label = placement.adds_note
        ? (placement.tail ? "Place merged hold note" : "Place tap note")
        : "Remove note";
    auto command = commit_document_command(history, std::move(proposed), label);
    if (!command)
        return diagnostic_result(history, state, std::move(command.diagnostics));
    state.pending_hold.reset();
    place_cursor_at(state, *snapped);
    state.active_lane = pending.lane;
    auto result = space_selection_view_result(make_view(history, state));
    result.accepted = true;
    result.document_changed = true;
    result.created_note = id;
    return result;
}

auto update_keyboard_holds(hold_gesture_context_t& context, local_position_t position)
    -> editor_interaction_result_t {
    const auto before = context.state;
    place_cursor_at(context.state, context.state.cursor);
    for (auto& pending : context.state.additional_keyboard_holds) {
        std::swap(*context.state.pending_hold, pending);
        auto result = update_hold_gesture(context, hold_gesture_source_t::keyboard_lane, position);
        std::swap(*context.state.pending_hold, pending);
        if (!result) {
            context.state = before;
            return result;
        }
    }
    return update_hold_gesture(context, hold_gesture_source_t::keyboard_lane, position);
}

auto update_canvas_keyboard_holds(hold_gesture_context_t& context, std::uint64_t row)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto before = state;
    if (!state.pending_hold || !chart_has_virtual_tail(context.chart))
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "canvas hold movement requires a BMS lane key")});
    state.pending_hold->preview_canvas_row = row;
    for (auto& hold : state.additional_keyboard_holds)
        hold.preview_canvas_row = row;
    const auto authored = position_at(context.index, row);
    place_cursor_on_canvas_row(state, authored, row);
    if (!context.keep_cursor_visible(state)) {
        state = before;
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "canvas hold movement cannot keep the cursor visible")});
    }
    auto view = make_view(context.history, state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view)) {
        state = before;
        return diagnostic_result(context.history, state, *diagnostics);
    }
    return {.accepted = true,
            .view = std::get<editor_interaction_view_model_t>(std::move(view))};
}

auto refresh_keyboard_hold_revisions(hold_gesture_context_t& context) -> void {
    auto& state = context.state;
    if (state.pending_hold)
        state.pending_hold->document_revision = context.history.document.revision;
    for (auto& hold : state.additional_keyboard_holds)
        hold.document_revision = context.history.document.revision;
}

auto begin_lane_key_hold_gesture(hold_gesture_context_t& context, lane_id_t lane,
                                  std::optional<resource_id_t> sound)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto before = state;
    if (state.pending_hold) {
        if (state.pending_hold->lane == lane ||
            std::ranges::any_of(state.additional_keyboard_holds,
                                [lane](const auto& hold) { return hold.lane == lane; }))
            return diagnostic_result(context.history, state,
                                     {error(diagnostic_code_t::invalid_hold,
                                            "lane key is already held")});
        state.additional_keyboard_holds.push_back(*state.pending_hold);
        state.pending_hold.reset();
    }
    if (state.virtual_bms_cursor_row) {
        if (sound && !is_paint_sample_resource(context.history.document, *sound)) {
            state = before;
            return diagnostic_result(context.history, state,
                {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
        }
        state.pending_hold = pending_hold_t{
            .position = state.cursor,
            .lane = lane,
            .document_revision = context.history.document.revision,
            .anchor_canvas_row = *state.virtual_bms_cursor_row,
            .preview_canvas_row = *state.virtual_bms_cursor_row,
            .gesture_source = hold_gesture_source_t::keyboard_lane,
            .sound = sound,
        };
        auto view = make_view(context.history, state);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view)) {
            state = before;
            return diagnostic_result(context.history, state, *diagnostics);
        }
        return {.accepted = true,
                .view = std::get<editor_interaction_view_model_t>(std::move(view))};
    }
    auto result = begin_hold_gesture(context, lane, state.cursor,
                                     hold_gesture_source_t::keyboard_lane, sound);
    if (!result) {
        state = before;
    } else if (result.document_changed) {
        if (!state.additional_keyboard_holds.empty()) {
            state.pending_hold = state.additional_keyboard_holds.back();
            state.additional_keyboard_holds.pop_back();
        }
        refresh_keyboard_hold_revisions(context);
        const auto view = make_view(context.history, state);
        if (const auto* current = std::get_if<editor_interaction_view_model_t>(&view))
            result.view = *current;
    }
    return result;
}

auto release_lane_key_hold_gesture(hold_gesture_context_t& context, lane_id_t action_lane)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return space_selection_view_result(make_view(history, state));
    const auto before = state;
    const auto lane = action_lane.value == 0 ? state.pending_hold->lane : action_lane;
    const auto found = std::ranges::find(state.additional_keyboard_holds, lane,
                                         &pending_hold_t::lane);
    const bool primary = state.pending_hold->lane == lane;
    if (!primary && found == state.additional_keyboard_holds.end())
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "released lane key has no pending hold")});
    if (!primary)
        std::swap(*state.pending_hold, *found);
    const auto position = state.pending_hold->preview_end.value_or(state.pending_hold->position);
    editor_interaction_result_t result;
    if (state.pending_hold->anchor_canvas_row || state.pending_hold->preview_canvas_row) {
        const auto anchor = state.pending_hold->anchor_canvas_row.value_or(
            *absolute_row(context.index, state.pending_hold->position));
        const auto end = state.pending_hold->preview_canvas_row.value_or(anchor);
        const auto hold_lane = state.pending_hold->lane;
        const auto sound = state.pending_hold->sound;
        state.pending_hold.reset();
        result = context.dispatch_action(
            place_virtual_bms_hold_action_t{anchor, end, hold_lane, sound});
    } else {
        result = release_hold_gesture(context, hold_gesture_source_t::keyboard_lane, position);
    }
    if (!result) {
        state = before;
        return result;
    }
    if (!primary) {
        state.pending_hold = *found;
        state.additional_keyboard_holds.erase(found);
    } else if (!state.additional_keyboard_holds.empty()) {
        state.pending_hold = state.additional_keyboard_holds.back();
        state.additional_keyboard_holds.pop_back();
    }
    if (result.document_changed)
        refresh_keyboard_hold_revisions(context);
    const auto view = make_view(history, state);
    if (const auto* current = std::get_if<editor_interaction_view_model_t>(&view))
        result.view = *current;
    return result;
}

auto combine_editor_area_selection(
    const editor_interaction_state_t& state,
    const std::vector<editor_selection_object_t>& hits,
    editor_area_selection_mode_t mode, const editor_hit_test_index_t& hit_index)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t> {
    if (mode != editor_area_selection_mode_t::replace &&
        mode != editor_area_selection_mode_t::add &&
        mode != editor_area_selection_mode_t::toggle)
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "unknown area selection mode")};
    if (mode == editor_area_selection_mode_t::replace)
        return hits;
    std::set<editor_selection_object_t> combined;
    for (const auto object : state.selected_objects) {
        if (!object.id || !std::ranges::binary_search(hit_index.available_objects, object))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                           "current selection contains a missing object",
                                           object.id)};
        if (!combined.insert(object).second)
            return diagnostic_list_t{error(diagnostic_code_t::duplicate_id,
                                           "current selection contains a duplicate object",
                                           object.id)};
    }
    std::set<editor_selection_object_t> hit_set(hits.begin(), hits.end());
    auto result = state.selected_objects;
    if (mode == editor_area_selection_mode_t::add) {
        for (const auto object : hits)
            if (combined.insert(object).second)
                result.push_back(object);
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
    for (const auto object : hits)
        if (!originally_selected.contains(object))
            result.push_back(object);
    return result;
}

struct space_range_selection_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    const std::variant<editor_interaction_view_model_t, diagnostic_list_t>& initial_view;
    std::uint64_t absolute;
};

auto space_selection_view_result(const std::variant<editor_interaction_view_model_t,
                                                    diagnostic_list_t>& view)
    -> editor_interaction_result_t {
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto begin_space_range_selection(space_range_selection_context_t& context,
                                 const space_range_selection_action_t& action)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (action.mode != editor_area_selection_mode_t::replace &&
        action.mode != editor_area_selection_mode_t::add &&
        action.mode != editor_area_selection_mode_t::toggle) {
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "unknown Space range selection mode")});
    }
    auto built = build_editor_hit_test_index(context.chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    state.pending_space_range_selection = pending_space_range_selection_t{
        .document_revision = history.document.revision,
        .hit_index = std::make_shared<editor_hit_test_index_t>(
            std::move(std::get<editor_hit_test_index_t>(built))),
        .anchor = state.cursor,
        .anchor_canvas_row = context.absolute,
        .mode = action.mode,
        .scope = action.scope,
        .minimum_span_rows = action.minimum_snap_width
            ? context.chart.divisions_per_standard_measure / state.active_snap_denominator : 0,
    };
    return space_selection_view_result(make_view(history, state));
}

auto commit_space_range_selection(space_range_selection_context_t& context)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto pending = *state.pending_space_range_selection;
    if (pending.hit_index == nullptr ||
        pending.document_revision != history.document.revision ||
        pending.hit_index->document_revision != history.document.revision) {
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "Space range selection is stale after a document edit")});
    }
    const auto& current_view = std::get<editor_interaction_view_model_t>(context.initial_view);
    if (!current_view.pending_space_range_selection.has_value() ||
        current_view.pending_space_range_selection->objects == nullptr) {
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "Space range preview is unavailable for commit")});
    }
    auto combined = combine_editor_area_selection(
        state, *current_view.pending_space_range_selection->objects, pending.mode,
        *pending.hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined))
        return diagnostic_result(history, state, *diagnostics);
    set_selected_objects(
        state, std::move(std::get<std::vector<editor_selection_object_t>>(combined)));
    const auto first_row = current_view.pending_space_range_selection->first_row;
    const auto last_row = current_view.pending_space_range_selection->last_row;
    state.retained_canvas_selection_range = first_row < last_row
        ? std::optional{canvas_row_span_t{first_row, last_row}}
        : std::nullopt;
    const auto anchor_position = position_at(context.index, pending.anchor_canvas_row);
    const auto cursor_position = position_at(context.index, context.absolute);
    state.retained_selection_range = anchor_position && cursor_position
        ? normalized_position_span(context.index, *anchor_position, *cursor_position)
        : std::nullopt;
    state.pending_space_range_selection.reset();
    return space_selection_view_result(make_view(history, state));
}

auto update_space_range_selection(space_range_selection_context_t& context,
                                  const space_range_selection_action_t& action)
    -> editor_interaction_result_t {
    return context.state.pending_space_range_selection
        ? commit_space_range_selection(context)
        : begin_space_range_selection(context, action);
}

struct virtual_note_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
};

auto validate_virtual_note_target(const virtual_note_context_t& context, lane_id_t lane,
                                  std::string_view description)
    -> std::optional<editor_interaction_result_t> {
    if (chart_has_virtual_tail(context.chart) && lane &&
        context.state.active_snap_denominator != 0 &&
        context.chart.divisions_per_standard_measure %
                context.state.active_snap_denominator ==
            0) {
        return std::nullopt;
    }
    return diagnostic_result(
        context.history, context.state,
        {error(diagnostic_code_t::invalid_resolution,
               std::string(description) + " requires a BMS chart and valid snap")});
}

auto extend_virtual_chart(chart_document_t& chart, std::uint64_t measure_count)
    -> std::optional<diagnostic_list_t> {
    const auto required = measure_count > chart.measures.size()
        ? measure_count - chart.measures.size()
        : 0;
    std::uint64_t last_measure_id{};
    for (const auto& measure : chart.measures)
        last_measure_id = std::max(last_measure_id, measure.id.value);
    if (required > std::numeric_limits<std::uint64_t>::max() - last_measure_id)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "virtual measure ID sequence is exhausted")};
    chart.measures.reserve(measure_count);
    for (std::size_t added = 0; added < required; ++added) {
        chart.measures.push_back({.id = measure_id_t{++last_measure_id},
                                  .definition = {{1, 1}, meter_t{4, 4}}});
    }
    return std::nullopt;
}

auto snap_virtual_row(std::uint64_t row, std::uint64_t total_rows, std::uint64_t step)
    -> std::optional<std::uint64_t> {
    if (row >= total_rows)
        return std::nullopt;
    const auto remainder = row % step;
    auto snapped = row - remainder;
    if (remainder > step - remainder) {
        if (step > std::numeric_limits<std::uint64_t>::max() - snapped)
            return std::nullopt;
        snapped += step;
    }
    return snapped < total_rows ? std::optional{snapped} : std::nullopt;
}

struct virtual_hold_rows_t {
    std::uint64_t anchor{};
    std::uint64_t end{};
    std::uint64_t measure_count{};
};

auto resolve_virtual_hold_rows(const virtual_note_context_t& context,
                               const place_virtual_bms_hold_action_t& action)
    -> std::variant<virtual_hold_rows_t, diagnostic_list_t> {
    const auto built_canvas = build_virtual_canvas(context.chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas))
        return *diagnostics;
    const auto& canvas = std::get<virtual_canvas_t>(built_canvas);
    const auto step = context.chart.divisions_per_standard_measure /
                      context.state.active_snap_denominator;
    const auto anchor = snap_virtual_row(action.anchor_canvas_row, canvas.total_rows, step);
    const auto end = snap_virtual_row(action.end_canvas_row, canvas.total_rows, step);
    const auto anchor_measure = anchor ? canvas.measure_containing(*anchor) : std::nullopt;
    const auto end_measure = end ? canvas.measure_containing(*end) : std::nullopt;
    if (!anchor_measure || !end_measure ||
        anchor_measure->number >= virtual_tail_measure_limit(context.chart) ||
        end_measure->number >= virtual_tail_measure_limit(context.chart)) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "virtual hold endpoints are unavailable")};
    }
    return virtual_hold_rows_t{*anchor, *end,
                               std::max(anchor_measure->number, end_measure->number) + 1};
}

auto commit_virtual_hold(virtual_note_context_t& context,
                         const place_virtual_bms_hold_action_t& action,
                         const virtual_hold_rows_t& rows)
    -> editor_interaction_result_t {
    if (rows.anchor == rows.end &&
        (context.state.armed_note_kind == note_kind_t::hold || context.state.armed_note_kind == note_kind_t::roll))
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, "Move to a different row to finish the Hold or Roll gesture.")});
    if (action.sound && !is_paint_sample_resource(context.history.document, *action.sound))
        return diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    if (const auto diagnostics = extend_virtual_chart(*chart, rows.measure_count))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto built_index = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_index))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto& index = std::get<measure_index_t>(built_index);
    const auto anchor = position_at(index, rows.anchor);
    const auto end = position_at(index, rows.end);
    if (!anchor || !end || !find_active_lane(*chart, index, *anchor, action.lane) ||
        !find_active_lane(*chart, index, *end, action.lane)) {
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::unknown_lane,
                                        "virtual hold lane is unavailable at an endpoint")});
    }
    const auto planned = plan_hold_placement(*chart, index, action.lane, *anchor, *end,
                                         context.state.armed_note_kind, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto& placement = std::get<hold_placement_plan_t>(planned);
    std::optional<object_id_t> id;
    if (placement.adds_note) {
        id = next_object_id(*chart);
        if (!id)
            return diagnostic_result(context.history, context.state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "virtual hold object ID sequence is exhausted")});
    }
    erase_notes(*chart, placement.removals);
    if (placement.adds_note) {
        const auto target = std::ranges::find(chart->measures, placement.head.measure,
                                              &measure_chunk_t::id);
        target->notes.push_back({.id = *id, .position = placement.head, .lane = action.lane,
                                 .kind = placement.tail
                                     ? held_note_kind(context.state.armed_note_kind)
                                     : context.state.armed_note_kind,
                                 .end = placement.tail,
                                 .sound = action.sound,
                                 .semantic_layers = resolve_placement_layers(
                                     *chart, context.state.armed_note_layers, action.sound)});
    }
    auto command = commit_document_command(
        context.history, std::move(proposed),
        placement.tail ? "Place hold in virtual BMS measure"
                       : "Place tap in virtual BMS measure");
    if (!command)
        return diagnostic_result(context.history, context.state, std::move(command.diagnostics));
    place_cursor_at(context.state, *end);
    place_cursor_at(context.state, context.state.cursor);
    context.state.active_lane = action.lane;
    auto view = make_view(context.history, context.state);
    return {.accepted = true,
            .document_changed = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
            .created_note = id};
}

auto place_virtual_bms_hold(virtual_note_context_t& context,
                            const place_virtual_bms_hold_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual hold"))
        return *invalid;
    auto rows = resolve_virtual_hold_rows(context, action);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&rows))
        return diagnostic_result(context.history, context.state, *diagnostics);
    return commit_virtual_hold(context, action, std::get<virtual_hold_rows_t>(rows));
}

struct virtual_tap_target_t {
    std::uint64_t row{};
    std::uint64_t measure{};
    std::uint64_t measure_begin_row{};
};

auto resolve_virtual_tap_target(const virtual_note_context_t& context,
                                const place_virtual_bms_tap_action_t& action)
    -> std::variant<virtual_tap_target_t, diagnostic_list_t> {
    const auto built_canvas = build_virtual_canvas(context.chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas))
        return *diagnostics;
    const auto& canvas = std::get<virtual_canvas_t>(built_canvas);
    if (action.canvas_row >= canvas.total_rows)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "virtual tap lies outside #000-#999")};
    const auto step = context.chart.divisions_per_standard_measure /
                      context.state.active_snap_denominator;
    const auto remainder = action.canvas_row % step;
    auto row = action.canvas_row - remainder;
    if (remainder > step - remainder) {
        if (step - remainder >
            std::numeric_limits<std::uint64_t>::max() - action.canvas_row)
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "virtual snap row overflows")};
        row += step;
    }
    const auto destination = canvas.measure_containing(row);
    if (!destination || destination->authored ||
        destination->number >= virtual_tail_measure_limit(context.chart) || context.index.entries.empty() ||
        find_active_lane(context.chart, context.index,
                         {context.index.entries.back().id,
                          context.index.entries.back().row_capacity - 1},
                         action.lane) == nullptr) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "virtual tap cannot use this measure or lane")};
    }
    return virtual_tap_target_t{row, destination->number, destination->begin_row};
}

auto validate_virtual_tap_sample(const virtual_note_context_t& context,
                                 std::optional<resource_id_t> sample)
    -> std::optional<editor_interaction_result_t> {
    if (!sample)
        return std::nullopt;
    const auto resource = std::ranges::find(context.history.document.resources, *sample,
                                            &resource_entry_t::id);
    if (resource != context.history.document.resources.end() &&
        std::ranges::find(resource->roles, resource_role_t::sample) != resource->roles.end())
        return std::nullopt;
    return diagnostic_result(context.history, context.state,
                             {error(diagnostic_code_t::invalid_id,
                                    "virtual tap sample is unavailable")});
}

auto place_virtual_bms_tap(virtual_note_context_t& context,
                           const place_virtual_bms_tap_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual tap"))
        return *invalid;
    if (auto invalid = validate_virtual_tap_sample(context, action.sample))
        return *invalid;
    auto target = resolve_virtual_tap_target(context, action);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&target))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto resolved = std::get<virtual_tap_target_t>(target);
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    if (const auto diagnostics = extend_virtual_chart(*chart, resolved.measure + 1))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto id = next_object_id(*chart);
    if (!id)
        return diagnostic_result(context.history, context.state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "virtual tap object ID sequence is exhausted")});
    const local_position_t position{chart->measures[resolved.measure].id,
                                    resolved.row - resolved.measure_begin_row};
    chart->measures[resolved.measure].notes.push_back(
        {.id = *id, .position = position, .lane = action.lane, .kind = context.state.armed_note_kind,
         .sound = action.sample,
         .semantic_layers = resolve_placement_layers(*chart, context.state.armed_note_layers,
                                                     action.sample)});
    auto command = commit_document_command(context.history, std::move(proposed),
                                           "Place tap in virtual BMS measure");
    if (!command)
        return diagnostic_result(context.history, context.state, std::move(command.diagnostics));
    context.state.active_lane = action.lane;
    place_cursor_at(context.state, position);
    place_cursor_at(context.state, context.state.cursor);
    auto view = make_view(context.history, context.state);
    return {.accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
            .document_changed = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
            .created_note = *id};
}

auto cycle_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                       const cycle_edit_snap_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id, "unknown snap direction")});
    const auto found = std::ranges::find(state.supported_snap_denominators,
                                         state.active_snap_denominator);
    if (found == state.supported_snap_denominators.end())
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "active editor snap is not in the supported snap list")});
    if (!state.supported_snap_denominators.empty()) {
        const auto size = static_cast<std::ptrdiff_t>(state.supported_snap_denominators.size());
        const auto position = std::distance(state.supported_snap_denominators.begin(), found);
        const auto step = action.direction == interaction_direction_t::forward ? 1 : -1;
        const auto next = (position + step + size) % size;
        const auto denominator = state.supported_snap_denominators[static_cast<std::size_t>(next)];
        if (denominator != state.active_snap_denominator) {
            state.active_snap_denominator = denominator;
            state.active_snap_was_fallback = false;
        }
    }
    return interaction_view_result(history, state, false);
}

auto set_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                     const set_edit_snap_action_t& action) -> editor_interaction_result_t {
    const auto supported = std::ranges::find(state.supported_snap_denominators, action.denominator);
    if (supported != state.supported_snap_denominators.end() &&
        action.denominator != state.active_snap_denominator) {
        state.active_snap_denominator = action.denominator;
        state.active_snap_was_fallback = false;
    }
    return interaction_view_result(history, state, false);
}

auto adjust_editor_zoom(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index,
                        std::uint64_t absolute, const adjust_editor_zoom_action_t& action)
    -> editor_interaction_result_t {
    if (!std::isfinite(action.factor) || action.factor <= 0.0)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "editor zoom factor must be finite and positive")});
    const auto current = state.pixels_per_standard_measure;
    const auto next = std::clamp(current * action.factor,
        minimum_pixels_per_standard_measure, maximum_pixels_per_standard_measure);
    auto anchor_y = static_cast<double>(absolute) * current /
        static_cast<double>(chart.divisions_per_standard_measure) - state.viewport_scroll_pixels;
    if (state.fixed_cursor_screen_y_pixels)
        anchor_y = state.scroll_direction == editor_field_scroll_direction_t::down
            ? state.viewport_height_pixels - *state.fixed_cursor_screen_y_pixels
            : *state.fixed_cursor_screen_y_pixels;
    if (action.anchor_screen_y_pixels) {
        anchor_y = *action.anchor_screen_y_pixels;
        if (!std::isfinite(anchor_y) || anchor_y < 0.0 || anchor_y > state.viewport_height_pixels)
            return diagnostic_result(history, state,
                {error(diagnostic_code_t::invalid_resolution,
                       "zoom anchor lies outside the editor viewport")});
    }
    const auto absolute_rows = (state.viewport_scroll_pixels + anchor_y) *
        static_cast<double>(chart.divisions_per_standard_measure) / current;
    const auto unclamped = absolute_rows * next /
        static_cast<double>(chart.divisions_per_standard_measure) - anchor_y;
    const auto maximum = maximum_viewport_scroll(chart, index, next, state.viewport_height_pixels);
    const auto scroll = maximum ? std::clamp(unclamped,
        minimum_viewport_scroll(state.viewport_height_pixels), *maximum)
        : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(scroll))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "editor zoom would overflow viewport coordinates")});
    auto proposed = state;
    set_cursor_scale(proposed, next);
    set_viewport_scroll_pixels(proposed, scroll);
    if (!synchronize_editor_cursor_to_viewport(chart, index, proposed))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_resolution, "zoom cannot resolve the fixed cursor")});
    state = std::move(proposed);
    return interaction_view_result(history, state, false);
}

struct tap_placement_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
};

auto find_tap_hold_overlap(const chart_document_t& chart, const measure_index_t& index,
                           lane_id_t lane, std::uint64_t target_absolute,
                           std::optional<object_id_t> ignored_note)
    -> std::optional<object_id_t> {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != lane || !note.end || note.id == ignored_note)
                continue;
            const auto begin = absolute_row(index, note.position);
            const auto end = absolute_row(index, *note.end);
            if (begin && end && target_absolute >= *begin && target_absolute <= *end)
                return note.id;
        }
    }
    return std::nullopt;
}

auto place_tap(tap_placement_context_t& context, const place_tap_action_t& action)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto snapped = snapped_position(context.chart, context.index, action.position,
                                          edit_snap_denominator(context.chart, state));
    if (!snapped)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::row_out_of_range,
                   "note placement target cannot be snapped inside chart geometry")});
    if (!find_active_lane(context.chart, context.index, *snapped, state.active_lane))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::unknown_lane,
                   "active editor lane is unavailable at snapped position")});
    auto proposed = history.document;
    auto* chart = find_chart(proposed, state.chart);
    const auto measure = std::ranges::find(chart->measures, snapped->measure,
                                           &measure_chunk_t::id);
    if (measure == chart->measures.end())
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::unknown_measure,
                   "snapped note position refers to an unknown measure")});
    const auto existing = std::ranges::find_if(measure->notes, [&](const auto& note) {
        return note.position == *snapped && note.lane == state.active_lane;
    });
    const auto target_absolute = *absolute_row(context.index, *snapped);
    if (existing == measure->notes.end() &&
        head_blocked_by_warp(*chart, context.index, target_absolute, state.armed_note_kind))
        return diagnostic_result(history, state, {error(diagnostic_code_t::invalid_hold, "playable note heads cannot sit inside a warp")});
    const auto existing_id = existing == measure->notes.end()
        ? std::nullopt
        : std::optional{existing->id};
    if (const auto overlap = find_tap_hold_overlap(*chart, context.index, state.active_lane,
                                                   target_absolute, existing_id))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::collision,
                   "tap placement overlaps a same-lane hold", *overlap)});
    if (action.sound && !is_paint_sample_resource(history.document, *action.sound))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")});
    std::string label;
    std::optional<object_id_t> created;
    if (existing != measure->notes.end()) {
        if (existing->kind != state.armed_note_kind || existing->end)
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::collision,
                       "tap placement overlaps a non-tap note", existing->id)});
        measure->notes.erase(existing);
        label = "Remove tap note";
    } else {
        const auto id = next_object_id(*chart);
        if (!id)
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "chart object ID sequence is exhausted")});
        measure->notes.push_back({.id = *id, .position = *snapped, .lane = state.active_lane,
                                  .kind = state.armed_note_kind,
                                  .sound = action.sound,
                                  .semantic_layers = resolve_placement_layers(
                                      *chart, state.armed_note_layers, action.sound)});
        created = *id;
        label = "Place note";
    }
    auto command = commit_document_command(history, std::move(proposed), std::move(label));
    if (!command)
        return diagnostic_result(history, state, std::move(command.diagnostics));
    place_cursor_at(state, *snapped);
    const auto view = make_view(history, state);
    return {.accepted = true,
            .document_changed = true,
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{},
            .created_note = created};
}

struct semantic_paint_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    std::function<editor_interaction_result_t()> cancel_gesture;
};

auto semantic_paint_view(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    const auto view = make_view(history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto append_semantic_paint_note(semantic_paint_context_t& context, object_id_t note_id,
                                bool pointer_captured) -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured)
        return context.cancel_gesture();
    if (!state.pending_semantic_layer_paint_gesture)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "semantic-layer paint update requires an active gesture")});
    auto& pending = *state.pending_semantic_layer_paint_gesture;
    if (pending.document_revision != history.document.revision)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "semantic-layer paint gesture is stale after a document edit")});
    if (!find_note(context.chart, note_id))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint target does not identify a chart note", note_id)});
    if (pending.visited.contains(note_id.value))
        return semantic_paint_view(history, state);
    if (!pending.notes || !pending.bgm_events ||
        pending.notes->size() + pending.bgm_events->size() >= maximum_note_paint_targets)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "semantic-layer paint exceeds the per-gesture note target cap")});
    auto expanded = std::make_shared<std::vector<object_id_t>>(*pending.notes);
    expanded->push_back(note_id);
    pending.notes = std::move(expanded);
    pending.visited.insert(note_id.value);
    return semantic_paint_view(history, state);
}

auto append_semantic_paint_bgm_event(semantic_paint_context_t& context, object_id_t event_id,
                                     bool pointer_captured) -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured) return context.cancel_gesture();
    if (!state.pending_semantic_layer_paint_gesture)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "semantic-layer paint update requires an active gesture")});
    auto& pending = *state.pending_semantic_layer_paint_gesture;
    if (pending.document_revision != history.document.revision)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "semantic-layer paint gesture is stale after a document edit")});
    if (!find_bgm_event(context.chart, event_id))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint target does not identify a BGM event", event_id)});
    if (pending.visited.contains(event_id.value)) return semantic_paint_view(history, state);
    if (!pending.bgm_events || !pending.notes ||
        pending.bgm_events->size() + pending.notes->size() >= maximum_note_paint_targets)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_resolution,
                   "semantic-layer paint exceeds the per-gesture target cap")});
    auto expanded = std::make_shared<std::vector<object_id_t>>(*pending.bgm_events);
    expanded->push_back(event_id);
    pending.bgm_events = std::move(expanded);
    pending.visited.insert(event_id.value);
    return semantic_paint_view(history, state);
}

auto begin_semantic_paint(semantic_paint_context_t& context, layer_id_t layer_id,
                          object_id_t note_id, std::optional<object_id_t> bgm_event,
                          bool pointer_captured,
                          bool replace_memberships, bool clear_memberships,
                          semantic_layer_paint_mode_t mode)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "pointer capture is required to begin semantic-layer painting")});
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture || state.pending_note_paint_gesture ||
        state.pending_semantic_layer_paint_gesture || state.pending_editor_area_selection ||
        state.pending_space_range_selection)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    const auto layer = std::ranges::find(context.chart.semantic_layers, layer_id,
                                         &semantic_layer_t::id);
    if (!clear_memberships && (layer == context.chart.semantic_layers.end() || !layer->visible))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint requires a visible chart layer")});
    if (!clear_memberships && layer->locked)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "semantic-layer paint cannot modify a locked layer")});
    if (static_cast<bool>(note_id) == bgm_event.has_value())
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint must target one note or BGM event")});
    if ((note_id && !find_note(context.chart, note_id)) ||
        (bgm_event && !find_bgm_event(context.chart, *bgm_event)))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint target is not in the chart", note_id ? note_id : *bgm_event)});
    auto notes = std::make_shared<std::vector<object_id_t>>();
    auto bgm_events = std::make_shared<std::vector<object_id_t>>();
    if (note_id) notes->push_back(note_id);
    if (bgm_event) bgm_events->push_back(*bgm_event);
    pending_semantic_layer_paint_gesture_t pending{
        .layer = layer_id,
        .document_revision = history.document.revision,
        .notes = std::move(notes),
        .bgm_events = std::move(bgm_events),
        .replace_memberships = replace_memberships,
        .clear_memberships = clear_memberships,
        .mode = mode,
    };
    pending.visited.reserve(64);
    pending.visited.insert(note_id ? note_id.value : bgm_event->value);
    state.pending_semantic_layer_paint_gesture = std::move(pending);
    return semantic_paint_view(history, state);
}

auto end_semantic_paint(semantic_paint_context_t& context, object_id_t note_id,
                        std::optional<object_id_t> bgm_event,
                        bool pointer_captured) -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured)
        return context.cancel_gesture();
    if (bgm_event) {
        const auto updated = append_semantic_paint_bgm_event(context, *bgm_event, true);
        if (!updated) return updated;
    } else if (note_id) {
        const auto updated = append_semantic_paint_note(context, note_id, true);
        if (!updated)
            return updated;
    }
    if (!state.pending_semantic_layer_paint_gesture)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "semantic-layer paint gesture was cancelled before commit")});
    const auto pending = *state.pending_semantic_layer_paint_gesture;
    if (pending.document_revision != history.document.revision || !pending.notes ||
        !pending.bgm_events || (pending.notes->empty() && pending.bgm_events->empty()))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "semantic-layer paint gesture is malformed or stale")});
    const auto layer = std::ranges::find(context.chart.semantic_layers, pending.layer,
                                        &semantic_layer_t::id);
    if (!pending.clear_memberships &&
        (layer == context.chart.semantic_layers.end() || !layer->visible || layer->locked))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint requires an unlocked visible layer")});
    semantic_layer_edit_action_t edit = assign_notes_to_semantic_layer_action_t{
        state.chart, pending.layer, *pending.notes, *pending.bgm_events};
    if (pending.mode == semantic_layer_paint_mode_t::remove)
        edit = remove_notes_from_semantic_layer_action_t{
            state.chart, pending.layer, *pending.notes, *pending.bgm_events};
    if (pending.clear_memberships)
        edit = clear_semantic_layer_membership_action_t{
            state.chart, *pending.notes, *pending.bgm_events};
    else if (pending.replace_memberships)
        edit = replace_notes_semantic_layer_action_t{
            state.chart, pending.layer, *pending.notes, true, *pending.bgm_events};
    const auto applied = apply_semantic_layer_edit(history, pending.document_revision, edit);
    if (!applied && !applied.diagnostics.empty())
        return diagnostic_result(history, state, applied.diagnostics);
    state.pending_semantic_layer_paint_gesture.reset();
    auto result = semantic_paint_view(history, state);
    result.document_changed = applied.applied;
    return result;
}

struct editor_object_selection_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
};

auto select_editor_objects(editor_object_selection_context_t& context,
                           const select_editor_objects_action_t& action)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto built = build_editor_hit_test_index(context.chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    const auto& hit_index = std::get<editor_hit_test_index_t>(built);
    const auto& available = hit_index.available_objects;
    auto selected = action.objects;
    std::ranges::sort(selected);
    if (std::ranges::adjacent_find(selected) != selected.end())
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::duplicate_id,
                   "object selection contains a duplicate typed object")});
    for (const auto& object : selected) {
        if (object.id.value == 0 || !std::ranges::binary_search(available, object))
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id,
                       "object selection contains an unknown typed object", object.id)});
    }
    if (action.request_audition &&
        (selected.size() != 1 ||
         (selected.front().kind != editor_selection_object_kind_t::note &&
          selected.front().kind != editor_selection_object_kind_t::bgm_event)))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_id,
                   "audition selection requires exactly one note or BGM event")});
    auto combined = combine_editor_area_selection(state, selected, action.mode, hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined))
        return diagnostic_result(history, state, *diagnostics);
    set_selected_objects(
        state, std::move(std::get<std::vector<editor_selection_object_t>>(combined)));
    const auto view = make_view(history, state);
    return {
        .accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
        .audition = action.request_audition
            ? std::optional{editor_object_audition_request_t{
                  history.document.revision, state.chart, selected.front()}}
            : std::nullopt,
    };
}

struct area_selection_context_t {
    document_history_t& history;
    editor_interaction_state_t& state;
    const chart_document_t& chart;
    const measure_index_t& index;
    const std::variant<editor_interaction_view_model_t, diagnostic_list_t>& initial_view;
    std::function<editor_interaction_result_t()> cancel_gesture;
};

auto valid_area_selection_capture(const begin_editor_area_selection_action_t& request,
                                   double viewport_height) -> bool {
    return std::isfinite(request.x_pixels) && std::isfinite(request.y_pixels) &&
        std::isfinite(request.field_left_pixels) &&
        std::isfinite(request.field_width_pixels) && request.field_width_pixels > 0.0 &&
        std::isfinite(request.field_left_pixels + request.field_width_pixels) &&
        (request.canvas_pointer_capture ||
         (request.x_pixels >= request.field_left_pixels &&
          request.x_pixels < request.field_left_pixels + request.field_width_pixels)) &&
        request.y_pixels >= 0.0 && request.y_pixels < viewport_height;
}

auto begin_area_selection_gesture(area_selection_context_t& context,
                                  const begin_editor_area_selection_action_t& request)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!request.pointer_captured)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "pointer capture is required to begin area selection")});
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture || state.pending_note_paint_gesture ||
        state.pending_semantic_layer_paint_gesture || state.pending_editor_area_selection)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "another editor gesture is already active")});
    if (!valid_area_selection_capture(request, state.viewport_height_pixels))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "area selection must begin inside the chart field")});
    if (request.mode != editor_area_selection_mode_t::replace &&
        request.mode != editor_area_selection_mode_t::add &&
        request.mode != editor_area_selection_mode_t::toggle)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "unknown area selection mode")});
    auto built = build_editor_hit_test_index(context.chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    auto hit_index = std::make_shared<editor_hit_test_index_t>(
        std::move(std::get<editor_hit_test_index_t>(built)));
    const auto& view = std::get<editor_interaction_view_model_t>(context.initial_view);
    editor_area_selection_request_t area{
        .start_x_pixels = request.x_pixels,
        .start_y_pixels = request.y_pixels,
        .end_x_pixels = request.x_pixels,
        .end_y_pixels = request.y_pixels,
        .field_left_pixels = request.field_left_pixels,
        .field_width_pixels = request.field_width_pixels,
    };
    area.scope = request.scope;
    area.object_geometry = request.object_geometry;
    auto queried = editor_objects_in_area_selection(context.chart, *hit_index, view, area);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried))
        return diagnostic_result(history, state, *diagnostics);
    auto combined = combine_editor_area_selection(
        state, std::get<std::vector<editor_selection_object_t>>(queried), request.mode,
        *hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined))
        return diagnostic_result(history, state, *diagnostics);
    auto objects = std::get<std::vector<editor_selection_object_t>>(std::move(combined));
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
        .notes = std::make_shared<const std::vector<object_id_t>>(notes_from_objects(objects)),
        .scope = request.scope,
        .objects = std::make_shared<const std::vector<editor_selection_object_t>>(
            std::move(objects)),
        .object_geometry = request.object_geometry,
    };
    return space_selection_view_result(make_view(history, state));
}

auto update_area_selection_gesture(area_selection_context_t& context, double x_pixels,
                                   double y_pixels, bool pointer_captured)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured)
        return context.cancel_gesture();
    if (!state.pending_editor_area_selection)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "area selection update requires an active gesture")});
    const auto& pending = *state.pending_editor_area_selection;
    if (pending.document_revision != history.document.revision || !pending.hit_index ||
        pending.hit_index->document_revision != history.document.revision)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "area selection gesture is stale after a document edit")});
    if (!std::isfinite(x_pixels) || !std::isfinite(y_pixels))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "area selection pointer coordinates must be finite")});
    const auto& view = std::get<editor_interaction_view_model_t>(context.initial_view);
    editor_area_selection_request_t area{
        .start_x_pixels = pending.anchor_x_pixels,
        .start_y_pixels = area_selection_anchor_y(pending, view.viewport_scroll_pixels),
        .end_x_pixels = x_pixels,
        .end_y_pixels = y_pixels,
        .field_left_pixels = pending.field_left_pixels,
        .field_width_pixels = pending.field_width_pixels,
    };
    area.scope = pending.scope;
    area.object_geometry = pending.object_geometry;
    auto queried = editor_objects_in_area_selection(
        context.chart, *pending.hit_index, view, area);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried))
        return diagnostic_result(history, state, *diagnostics);
    auto combined = combine_editor_area_selection(
        state, std::get<std::vector<editor_selection_object_t>>(queried), pending.mode,
        *pending.hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined))
        return diagnostic_result(history, state, *diagnostics);
    auto objects = std::get<std::vector<editor_selection_object_t>>(std::move(combined));
    auto& updated = *state.pending_editor_area_selection;
    updated.current_x_pixels = x_pixels;
    updated.current_y_pixels = y_pixels;
    updated.notes = std::make_shared<const std::vector<object_id_t>>(notes_from_objects(objects));
    updated.objects = std::make_shared<const std::vector<editor_selection_object_t>>(
        std::move(objects));
    return space_selection_view_result(make_view(history, state));
}

auto snapped_area_selection_row(const area_selection_context_t& context,
                                 const editor_interaction_view_model_t& view, double y)
    -> std::optional<std::uint64_t> {
    const auto row = absolute_row_at_y_pixels(context.chart, context.index, view,
        std::clamp(y, 0.0, std::nextafter(view.viewport_height_pixels, 0.0)), true);
    if (!row) return std::nullopt;
    const auto denominator = edit_snap_denominator(context.chart, context.state);
    const auto snapped = snap_absolute_row(context.chart, *row, denominator);
    if (!snapped) return std::nullopt;
    auto total_rows = context.index.total_rows;
    if (chart_has_virtual_tail(context.chart)) {
        const auto canvas = build_virtual_canvas(context.chart);
        const auto* geometry = std::get_if<virtual_canvas_t>(&canvas);
        if (!geometry) return std::nullopt;
        total_rows = geometry->total_rows;
    }
    const auto step = context.chart.divisions_per_standard_measure / denominator;
    return std::min(*snapped, (total_rows - 1) / step * step);
}

auto retain_area_selection_range(area_selection_context_t& context,
                                 const pending_editor_area_selection_t& pending)
    -> void {
    const auto& view = std::get<editor_interaction_view_model_t>(context.initial_view);
    const auto anchor_row = snapped_area_selection_row(context, view,
        area_selection_anchor_y(pending, view.viewport_scroll_pixels));
    const auto tail_row = snapped_area_selection_row(context, view, pending.current_y_pixels);
    const auto anchor = anchor_row ? position_at(context.index, *anchor_row) : std::nullopt;
    const auto tail = tail_row ? position_at(context.index, *tail_row) : std::nullopt;
    context.state.retained_selection_range =
        anchor && tail ? normalized_position_span(context.index, *anchor, *tail) : std::nullopt;
    context.state.retained_canvas_selection_range = anchor_row && tail_row && *anchor_row != *tail_row
        ? std::optional{canvas_row_span_t{std::min(*anchor_row, *tail_row),
                                          std::max(*anchor_row, *tail_row)}}
        : std::nullopt;
}

auto commit_area_selection_gesture(area_selection_context_t& context, double x_pixels,
                                   double y_pixels, bool pointer_captured)
    -> editor_interaction_result_t {
    auto updated = update_area_selection_gesture(context, x_pixels, y_pixels,
                                                 pointer_captured);
    if (!updated || !context.state.pending_editor_area_selection)
        return updated;
    const auto& pending = *context.state.pending_editor_area_selection;
    if (pending.document_revision != context.history.document.revision || !pending.objects)
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::invalid_hold,
                   "area selection gesture is stale after a document edit")});
    set_selected_objects(context.state, *pending.objects);
    retain_area_selection_range(context, pending);
    context.state.pending_editor_area_selection.reset();
    return space_selection_view_result(make_view(context.history, context.state));
}

auto recovered_editor_view(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    const auto view = make_view(history, state);
    return {
        .accepted = true,
        .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                    ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                    : std::nullopt,
        .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                           ? std::get<diagnostic_list_t>(view)
                           : diagnostic_list_t{},
    };
}

auto handle_editor_cancellation(document_history_t& history, editor_interaction_state_t& state,
                                const editor_ui_action_t& action)
    -> std::optional<editor_interaction_result_t> {
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
        state.additional_keyboard_holds.clear();
        state.pending_note_duration_gesture.reset();
        state.pending_note_move_gesture.reset();
        state.pending_note_paint_gesture.reset();
        state.pending_semantic_layer_paint_gesture.reset();
        state.pending_editor_area_selection.reset();
        state.pending_space_range_selection.reset();
        state.pending_virtual_lane_key.reset();
        return recovered_editor_view(history, state);
    }
    if (std::holds_alternative<cancel_hold_action_t>(action)) {
        state.pending_hold.reset();
        state.additional_keyboard_holds.clear();
        state.pending_virtual_lane_key.reset();
        return recovered_editor_view(history, state);
    }
    if (std::holds_alternative<cancel_editor_area_selection_action_t>(action)) {
        state.pending_editor_area_selection.reset();
        return recovered_editor_view(history, state);
    }
    return std::nullopt;
}

auto validate_active_gesture_action(const document_history_t& history,
                                    const editor_interaction_state_t& state,
                                    const editor_ui_action_t& action)
    -> std::optional<editor_interaction_result_t> {
    if (state.pending_space_range_selection &&
        state.pending_space_range_selection->document_revision != history.document.revision)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "Space range selection is stale after a document edit")});
    if (state.pending_virtual_lane_key &&
        !std::holds_alternative<move_edit_cursor_action_t>(action) &&
        !std::holds_alternative<page_edit_cursor_action_t>(action))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "release or cancel the virtual BMS lane key first")});
    if (state.pending_hold && state.pending_hold->gesture_source) {
        const auto source = *state.pending_hold->gesture_source;
        const auto keyboard_action = source == hold_gesture_source_t::keyboard_lane &&
            (std::holds_alternative<move_edit_cursor_action_t>(action) ||
             std::holds_alternative<page_edit_cursor_action_t>(action) ||
             std::holds_alternative<scroll_editor_view_action_t>(action) ||
             std::holds_alternative<begin_lane_key_hold_gesture_action_t>(action) ||
             std::holds_alternative<release_lane_key_hold_gesture_action_t>(action));
        const auto pointer_action = source == hold_gesture_source_t::alt_pointer &&
            (std::holds_alternative<update_alt_pointer_hold_gesture_action_t>(action) ||
             std::holds_alternative<release_alt_pointer_hold_gesture_action_t>(action));
        if (!keyboard_action && !pointer_action)
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "finish or cancel the active hold input gesture first")});
    }
    if (state.pending_note_duration_gesture &&
        !std::holds_alternative<update_note_duration_gesture_action_t>(action) &&
        !std::holds_alternative<release_note_duration_gesture_action_t>(action))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active note duration gesture first")});
    if (state.pending_note_move_gesture &&
        !std::holds_alternative<update_note_move_gesture_action_t>(action) &&
        !std::holds_alternative<release_note_move_gesture_action_t>(action))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "finish or cancel the active note move gesture first")});
    if (state.pending_note_paint_gesture &&
        !std::holds_alternative<update_note_paint_gesture_action_t>(action) &&
        !std::holds_alternative<end_note_paint_gesture_action_t>(action))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "finish or cancel the active note paint gesture first")});
    if (state.pending_semantic_layer_paint_gesture &&
        !std::holds_alternative<update_semantic_layer_paint_gesture_action_t>(action) &&
        !std::holds_alternative<end_semantic_layer_paint_gesture_action_t>(action))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active semantic-layer paint gesture first")});
    if (state.pending_editor_area_selection &&
        !std::holds_alternative<update_editor_area_selection_action_t>(action) &&
        !std::holds_alternative<commit_editor_area_selection_action_t>(action))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active area selection gesture first")});
    if (state.pending_space_range_selection &&
        !std::holds_alternative<space_range_selection_action_t>(action) &&
        !std::holds_alternative<move_edit_cursor_action_t>(action) &&
        !std::holds_alternative<page_edit_cursor_action_t>(action) &&
        !std::holds_alternative<jump_edit_cursor_action_t>(action) &&
        !std::holds_alternative<cycle_edit_snap_action_t>(action) &&
        !std::holds_alternative<set_edit_snap_action_t>(action) &&
        !std::holds_alternative<adjust_editor_zoom_action_t>(action) &&
        !std::holds_alternative<scroll_editor_view_action_t>(action))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "finish or cancel the active Space range selection first")});
    return std::nullopt;
}

auto virtual_cursor_action_allowed(const editor_interaction_state_t& state,
                                   const editor_ui_action_t& action) -> bool {
    return std::holds_alternative<move_edit_cursor_action_t>(action) ||
        std::holds_alternative<page_edit_cursor_action_t>(action) ||
        std::holds_alternative<jump_edit_cursor_action_t>(action) ||
        std::holds_alternative<select_bms_canvas_measure_action_t>(action) ||
        std::holds_alternative<cycle_edit_snap_action_t>(action) ||
        std::holds_alternative<set_edit_snap_action_t>(action) ||
        std::holds_alternative<place_virtual_bms_tap_action_t>(action) ||
        std::holds_alternative<place_virtual_bms_hold_action_t>(action) ||
        std::holds_alternative<begin_lane_key_hold_gesture_action_t>(action) ||
        std::holds_alternative<release_lane_key_hold_gesture_action_t>(action) ||
        std::holds_alternative<begin_editor_area_selection_action_t>(action) ||
        std::holds_alternative<update_editor_area_selection_action_t>(action) ||
        std::holds_alternative<commit_editor_area_selection_action_t>(action) ||
        std::holds_alternative<space_range_selection_action_t>(action) ||
        std::holds_alternative<select_editor_notes_action_t>(action) ||
        std::holds_alternative<select_editor_objects_action_t>(action) ||
        (std::holds_alternative<delete_editor_selection_action_t>(action) &&
         (!state.selected_objects.empty() || !state.selected_notes.empty())) ||
        std::holds_alternative<select_editor_lane_action_t>(action) ||
        std::holds_alternative<adjust_editor_zoom_action_t>(action) ||
        std::holds_alternative<scroll_editor_view_action_t>(action);
}

auto interaction_view_result(const document_history_t& history,
                             const editor_interaction_state_t& state,
                             bool changed) -> editor_interaction_result_t {
    const auto view = make_view(history, state);
    return {.accepted = std::holds_alternative<editor_interaction_view_model_t>(view),
            .document_changed = changed,
            .view = std::holds_alternative<editor_interaction_view_model_t>(view)
                        ? std::optional{std::get<editor_interaction_view_model_t>(view)}
                        : std::nullopt,
            .diagnostics = std::holds_alternative<diagnostic_list_t>(view)
                               ? std::get<diagnostic_list_t>(view)
                               : diagnostic_list_t{}};
}

auto apply_duration_resize_plan(document_history_t& history,
                                editor_interaction_state_t& state,
                                const duration_resize_plan_t& plan)
    -> editor_interaction_result_t {
    auto proposed = history.document;
    auto* chart = find_chart(proposed, state.chart);
    auto changed = false;
    for (const auto& candidate : plan.resized) {
        auto* note = find_note(*chart, candidate.id);
        const auto kind = candidate.end_position ? note_kind_t::hold : note_kind_t::tap;
        changed = changed || note->kind != kind || note->end != candidate.end_position;
        note->kind = kind;
        note->end = candidate.end_position;
    }
    if (changed) {
        auto command = commit_document_command(history, std::move(proposed),
            plan.notes.size() == 1 ? "Resize note duration" : "Resize selected note durations");
        if (!command) return diagnostic_result(history, state, std::move(command.diagnostics));
    }
    state.pending_note_duration_gesture.reset();
    set_selected_notes(state, plan.notes);
    state.active_lane = plan.reference_lane;
    place_cursor_at(state, plan.reference_end);
    auto result = interaction_view_result(history, state, changed);
    result.accepted = true;
    return result;
}

auto note_move_changes(const chart_document_t& chart, const note_move_plan_t& plan) -> bool {
    for (const auto& candidate : plan.moved) {
        const auto* note = find_note(chart, candidate.id);
        if (!note || note->position != candidate.position || note->lane != candidate.lane ||
            note->end != candidate.end_position) return true;
    }
    return false;
}

struct moved_stem_events_t {
    std::vector<std::size_t> channels;
    std::vector<object_id_t> events;

    [[nodiscard]] auto contains(object_id_t id) const -> bool {
        return std::ranges::find(events, id) != events.end();
    }
};

// Gives each moved tap's retained slice event the tap's new position (and lane, when playable).
auto move_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan) -> moved_stem_events_t {
    moved_stem_events_t moved;
    for (const auto& candidate : plan.moved) {
        const auto* note = find_note(chart, candidate.id);
        if (!note || !note->sound_channel_event) continue;
        for (std::size_t index = 0; index < chart.sound_channels.size(); ++index) {
            auto& events = chart.sound_channels[index].events;
            const auto event = std::ranges::find(events, *note->sound_channel_event, &sound_channel_event_t::id);
            if (event == events.end()) continue;
            event->position = candidate.position;
            event->end = candidate.end_position;
            if (event->playable) event->lane = candidate.lane;
            moved.channels.push_back(index);
            moved.events.push_back(event->id);
        }
    }
    std::ranges::sort(moved.channels);
    moved.channels.erase(std::unique(moved.channels.begin(), moved.channels.end()), moved.channels.end());
    return moved;
}

// Taps moved together may share a row; landing on a row another tap holds is refused.
auto landed_on_taken_row(const sound_channel_t& channel, const moved_stem_events_t& moved) -> const sound_channel_event_t* {
    for (const auto& event : channel.events) {
        if (!moved.contains(event.id)) continue;
        const auto taken = std::ranges::any_of(channel.events, [&](const auto& other) {
            return !moved.contains(other.id) && other.position == event.position && other.playable == event.playable;
        });
        if (taken) return &event;
    }
    return nullptr;
}

// A BMSON tap that stands for a stem point drags its retained slice event along: the audio stays
// anchored at the stem's last restart, so moving a continuation only moves where the stem is cut,
// while moving a restart moves the audio. The move is refused when it lands on a row another tap
// of the stem holds, or when the stem's slices could no longer be derived.
auto sync_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan,
                              const song_document_t& document) -> std::optional<diagnostic_t> {
    const auto moved = move_linked_sound_events(chart, plan);
    for (const auto index : moved.channels) {
        const auto& channel = chart.sound_channels[index];
        if (const auto* taken = landed_on_taken_row(channel, moved))
            return error(diagnostic_code_t::collision, "another point of this stem is already on that row", taken->id);
        const auto asset = std::ranges::find(document.audio_assets, channel.source_clip.source, &audio_asset_t::id);
        if (asset == document.audio_assets.end() || asset->stream.sample_rate == 0) continue;
        if (!derive_sound_channel_slices(chart, channel, asset->stream.sample_rate, asset->stream.frame_count))
            return error(diagnostic_code_t::invalid_resolution,
                         "moving this point would leave the stem with unusable slices", channel.id);
    }
    return std::nullopt;
}

auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history,
                          editor_interaction_state_t& state) -> std::optional<diagnostic_t> {
    for (const auto& candidate : plan.moved) {
        auto source = chart.measures.end();
        auto found = std::vector<note_t>::iterator{};
        for (auto measure = chart.measures.begin(); measure != chart.measures.end(); ++measure) {
            const auto item = std::ranges::find(measure->notes, candidate.id, &note_t::id);
            if (item == measure->notes.end()) continue;
            source = measure;
            found = item;
            break;
        }
        auto destination = std::ranges::find(chart.measures, candidate.position.measure,
                                             &measure_chunk_t::id);
        if (source == chart.measures.end() || destination == chart.measures.end())
            return error(diagnostic_code_t::invalid_id,
                         "note move could not relocate a planned note", candidate.id);
        auto moved = std::move(*found);
        source->notes.erase(found);
        moved.position = candidate.position;
        moved.lane = candidate.lane;
        moved.end = candidate.end_position;
        destination->notes.push_back(std::move(moved));
    }
    (void)state;
    return sync_linked_sound_events(chart, plan, history.document);
}

auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t> {
    for (const auto& moved : plan.moved) {
        if (moved.conflict == note_move_conflict_t::none ||
            (pending.to_bgm && moved.conflict == note_move_conflict_t::out_of_lane)) continue;
        if (moved.conflict != note_move_conflict_t::occupied)
            return diagnostic_list_t{error(diagnostic_code_t::collision, "Note move lies outside chart geometry")};
        if (!pending.to_bgm && (!pending.displace_to_bgm || !moved.occupant ||
            std::ranges::find(plan.notes, *moved.occupant) != plan.notes.end()))
            return diagnostic_list_t{error(diagnostic_code_t::collision, "Selected notes would overlap")};
    }
    auto scratch = make_document_history(history.document);
    const auto* source = find_chart(history.document, state.chart);
    const note_to_bgm_policy_t policy{.allow_new_lanes = source->source_family == format_family_t::bms ||
        source->source_family == format_family_t::bmson || source->source_family == format_family_t::native_project};
    if (!pending.to_bgm) {
        auto converted = move_notes_to_bgm(scratch, scratch.document.revision, state.chart, plan.occupants, std::nullopt, policy);
        if (!converted) return converted.diagnostics;
    }
    auto* chart = find_chart(scratch.document, state.chart);
    if (auto failure = relocate_moved_notes(*chart, plan, scratch, state))
        return diagnostic_list_t{*failure};
    if (pending.to_bgm) {
        auto converted = move_notes_to_bgm(scratch, scratch.document.revision, state.chart,
                                          plan.notes, pending.preferred_bgm_lane, policy);
        if (!converted) return converted.diagnostics;
    }
    auto failures = relocate_selected_bgm(scratch.document, state.chart, plan);
    if (!failures.empty()) return failures;
    failures = validate_document(scratch.document);
    if (!failures.empty()) return failures;
    return std::move(scratch.document);
}

auto apply_note_move_plan(document_history_t& history, editor_interaction_state_t& state,
                          const note_move_plan_t& plan) -> editor_interaction_result_t {
    auto proposed = plan.staged_bgm_document.value_or(history.document);
    auto* chart = find_chart(proposed, state.chart);
    const auto changed = plan.staged_bgm_document.has_value() || note_move_changes(*chart, plan) || std::ranges::any_of(plan.bgm_moves, [&](const auto& move) {
        const auto* source = find_bgm_event(*chart, move.note);
        return !source || source->position != move.position || source->end != move.end || source->source_lane != move.lane;
    });
    if (changed) {
        if (auto failure = relocate_moved_notes(*chart, plan, history, state))
            return diagnostic_result(history, state, {*failure});
        auto failures = plan.staged_bgm_document ? diagnostic_list_t{} : relocate_selected_bgm(proposed, state.chart, plan);
        if (!failures.empty()) {
            state.pending_note_move_gesture.reset();
            return diagnostic_result(history, state, std::move(failures));
        }
        auto command = commit_document_command(history, std::move(proposed),
            plan.bgm_moves.empty() ? (plan.notes.size() == 1 ? "Move note" : "Move selected notes")
                                   : "Move selected chart objects");
        if (!command) return diagnostic_result(history, state, std::move(command.diagnostics));
    }
    state.pending_note_move_gesture.reset();
    set_selected_notes(state, plan.notes);
    for (const auto& moved : plan.bgm_moves)
        state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, moved.note});
    state.active_lane = plan.reference_lane;
    place_cursor_at(state, plan.reference_position);
    auto result = interaction_view_result(history, state, changed);
    result.accepted = true;
    return result;
}

auto select_editor_notes(document_history_t& history, editor_interaction_state_t& state,
                         const chart_document_t& chart,
                         const select_editor_notes_action_t& action)
    -> editor_interaction_result_t {
    const auto available = chart_note_ids(chart);
    std::unordered_set<std::uint64_t> selected;
    selected.reserve(action.notes.size());
    for (const auto id : action.notes) {
        if (id.value == 0 || !available.contains(id.value))
            return diagnostic_result(history, state,
                {error(diagnostic_code_t::invalid_id,
                       "note selection contains an unknown note ID", id)});
        if (!selected.insert(id.value).second)
            return diagnostic_result(history, state,
                {error(diagnostic_code_t::duplicate_id,
                       "note selection contains a duplicate note ID", id)});
    }
    set_selected_notes(state, action.notes);
    return interaction_view_result(history, state, false);
}

auto select_editor_lane(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index,
                        lane_id_t lane) -> editor_interaction_result_t {
    if (find_active_lane(chart, index, state.cursor, lane) == nullptr)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::unknown_lane,
                   "selected editor lane is unavailable at the cursor")});
    state.active_lane = lane;
    return interaction_view_result(history, state, false);
}

auto validate_virtual_cursor_action(const document_history_t& history,
                                    const editor_interaction_state_t& state,
                                    const editor_ui_action_t& action)
    -> std::optional<editor_interaction_result_t> {
    if (!state.virtual_bms_cursor_row || virtual_cursor_action_allowed(state, action))
        return std::nullopt;
    return diagnostic_result(history, state,
                             {error(diagnostic_code_t::row_out_of_range,
                                    "this edit requires a materialized cursor measure")});
}

auto note_kind_allowed(const chart_document_t& chart, note_kind_t kind) -> bool {
    if (kind == note_kind_t::tap || kind == note_kind_t::hold || kind == note_kind_t::mine)
        return true;
    switch (chart.source_family) {
    case format_family_t::bms:
    case format_family_t::bmson:
        return kind == note_kind_t::invisible;
    case format_family_t::stepmania:
        return kind == note_kind_t::roll || kind == note_kind_t::lift || kind == note_kind_t::fake;
    case format_family_t::native_project:
        return kind == note_kind_t::roll || kind == note_kind_t::invisible ||
               kind == note_kind_t::lift || kind == note_kind_t::fake;
    default:
        return false;
    }
}

auto arm_note_kind(const document_history_t& history, editor_interaction_state_t& state,
                    note_kind_t kind) -> editor_interaction_result_t {
    const auto* chart = find_chart(history.document, state.chart);
    if (!chart || !note_kind_allowed(*chart, kind))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_binding, "Note kind is unavailable for this chart profile.")});
    if (state.pending_hold || state.pending_virtual_lane_key || state.pending_note_paint_gesture)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_hold, "Finish or cancel note placement before changing kind.")});
    state.armed_note_kind = kind;
    return space_selection_view_result(make_view(history, state));
}

namespace {
auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
                                 const editor_ui_action_t& action) -> editor_interaction_result_t;
} // namespace

auto dispatch_editor_action(document_history_t& history, editor_interaction_state_t& state,
                            const editor_ui_action_t& action) -> editor_interaction_result_t {
    return dispatch_editor_action_impl(history, state, action);
}

namespace {
auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
                                 const editor_ui_action_t& action) -> editor_interaction_result_t {
    if (auto canceled = handle_editor_cancellation(history, state, action))
        return *canceled;
    if (auto rejected = validate_active_gesture_action(history, state, action))
        return *rejected;
    if (auto rejected = validate_virtual_cursor_action(history, state, action))
        return *rejected;
    const auto* kind_chart = find_chart(history.document, state.chart);
    if (kind_chart && !note_kind_allowed(*kind_chart, state.armed_note_kind))
        state.armed_note_kind = note_kind_t::tap;
    const auto initial_view = make_view(history, state);
    if (std::holds_alternative<diagnostic_list_t>(initial_view)) {
        return diagnostic_result(history, state, std::get<diagnostic_list_t>(initial_view));
    }
    const auto* chart = find_chart(history.document, state.chart);
    const auto built_index = build_measure_index(*chart);
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = state.virtual_bms_cursor_row.value_or(*absolute_row(index, state.cursor));
    const auto keep_cursor_visible = [&](editor_interaction_state_t& candidate) {
        return interaction_detail::keep_cursor_visible(*chart, index, candidate);
    };

    const auto make_duration_plan = [&](std::vector<object_id_t> targets, object_id_t reference,
                                        local_position_t end, bool exact_end = false) {
        return plan_duration_resize(*chart, index, std::move(targets), reference, end,
                                    edit_snap_denominator(*chart, state), exact_end);
    };
    const auto make_move_plan = [&](std::vector<object_id_t> targets, object_id_t reference,
                                    local_position_t position, lane_id_t lane,
                                    bool exact_position, std::optional<object_id_t> bgm_lane,
                                    std::optional<std::uint64_t> virtual_row) -> std::variant<note_move_plan_t, diagnostic_list_t> {
        return plan_move_gesture(history, state, *chart, index, std::move(targets), reference,
                                 position, lane, exact_position, bgm_lane, virtual_row);
    };
    const auto apply_duration_plan = [&](const duration_resize_plan_t& plan) {
        return apply_duration_resize_plan(history, state, plan);
    };
    const auto apply_move_plan = [&](const note_move_plan_t& plan) {
        return apply_note_move_plan(history, state, plan);
    };

    hold_gesture_context_t hold_gestures{
        history, state, *chart, index, keep_cursor_visible,
        [&](const editor_ui_action_t& nested_action) {
            return dispatch_editor_action(history, state, nested_action);
        }};
    interaction_detail::cursor_navigation_context_t cursor_navigation{
        history,
        state,
        *chart,
        index,
        absolute,
        keep_cursor_visible,
        [&](std::uint64_t row) { return update_canvas_keyboard_holds(hold_gestures, row); },
        [&](local_position_t position) { return update_keyboard_holds(hold_gestures, position); }};
    note_gesture_context_t note_gestures{history, state, *chart, index, make_duration_plan,
                                         apply_duration_plan, make_move_plan, apply_move_plan};
    const auto cancel_paint_gesture = [&] {
        return dispatch_editor_action(
            history, state,
            cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::pointer_capture_lost});
    };
    note_paint_context_t note_paint{history, state, *chart, index, cancel_paint_gesture};

    semantic_paint_context_t semantic_paint{
        history, state, *chart, [&] {
            return dispatch_editor_action(
                history, state,
                cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::pointer_capture_lost});
        }};
    const auto append_semantic_layer_paint_target =
        [&](object_id_t note, std::optional<object_id_t> bgm_event, bool pointer_captured) {
            return bgm_event
                       ? append_semantic_paint_bgm_event(semantic_paint, *bgm_event, pointer_captured)
                       : append_semantic_paint_note(semantic_paint, note, pointer_captured);
        };
    const auto begin_semantic_layer_paint_gesture =
        [&](layer_id_t layer, object_id_t note, std::optional<object_id_t> bgm_event,
            bool pointer_captured, bool replace, bool clear, semantic_layer_paint_mode_t mode) {
            return begin_semantic_paint(semantic_paint, layer, note, bgm_event,
                                        pointer_captured, replace, clear, mode);
        };
    const auto end_semantic_layer_paint_gesture =
        [&](object_id_t note, std::optional<object_id_t> bgm_event, bool pointer_captured) {
            return end_semantic_paint(semantic_paint, note, bgm_event, pointer_captured);
        };

    area_selection_context_t area_selection{
        history, state, *chart, index, initial_view,
        [&] {
            return dispatch_editor_action(
                history, state,
                cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::pointer_capture_lost});
        }};
    const auto begin_area_selection = [&](const begin_editor_area_selection_action_t& request) {
        return begin_area_selection_gesture(area_selection, request);
    };
    const auto update_area_selection = [&](double x, double y, bool captured) {
        return update_area_selection_gesture(area_selection, x, y, captured);
    };
    const auto commit_area_selection = [&](double x, double y, bool captured) {
        return commit_area_selection_gesture(area_selection, x, y, captured);
    };

    const auto handle_move_edit_cursor = [&](const move_edit_cursor_action_t& typed_action) -> editor_interaction_result_t {
        return interaction_detail::move_edit_cursor(cursor_navigation, typed_action);
    };

    const auto handle_page_edit_cursor = [&](const page_edit_cursor_action_t& typed_action) -> editor_interaction_result_t {
        return interaction_detail::page_edit_cursor(cursor_navigation, typed_action);
    };

    const auto handle_jump_edit_cursor = [&](const jump_edit_cursor_action_t& typed_action) {
        return interaction_detail::jump_edit_cursor(cursor_navigation, typed_action);
    };

    const auto handle_select_bms_canvas_measure =
        [&](const select_bms_canvas_measure_action_t& typed_action) {
            return interaction_detail::select_bms_canvas_measure(cursor_navigation, typed_action);
        };

    const auto handle_cycle_edit_snap = [&](const cycle_edit_snap_action_t& typed_action) {
        return cycle_editor_snap(history, state, typed_action);
    };
    const auto handle_set_edit_snap = [&](const set_edit_snap_action_t& typed_action) {
        return set_editor_snap(history, state, typed_action);
    };

    virtual_note_context_t virtual_notes{history, state, *chart, index};
    const auto handle_place_virtual_bms_hold = [&](const place_virtual_bms_hold_action_t& typed_action) -> editor_interaction_result_t {
        return place_virtual_bms_hold(virtual_notes, typed_action);
    };

    const auto handle_place_virtual_bms_tap = [&](const place_virtual_bms_tap_action_t& typed_action) -> editor_interaction_result_t {
        return place_virtual_bms_tap(virtual_notes, typed_action);
    };

    tap_placement_context_t tap_placement{history, state, *chart, index};
    const auto handle_place_tap = [&](const place_tap_action_t& typed_action) -> editor_interaction_result_t {
        if (state.armed_note_kind == note_kind_t::hold || state.armed_note_kind == note_kind_t::roll)
            return begin_hold_action(hold_gestures, typed_action.position, typed_action.sound);
        return place_tap(tap_placement, typed_action);
    };

    const auto handle_begin_hold = [&](const begin_hold_action_t& typed_action) {
        return begin_hold_action(hold_gestures, typed_action.position);
    };
    const auto handle_update_hold_preview = [&](const update_hold_preview_action_t& typed_action) {
        return update_hold_preview(hold_gestures, typed_action.position);
    };
    const auto handle_commit_hold = [&](const commit_hold_action_t& typed_action) {
        return commit_hold(hold_gestures, typed_action.position);
    };

    const auto handle_cancel_hold = [&](const cancel_hold_action_t& typed_action) -> editor_interaction_result_t {

        return {};

    };

    const auto handle_begin_lane_key_hold_gesture = [&](const begin_lane_key_hold_gesture_action_t& typed_action) {
        return begin_lane_key_hold_gesture(hold_gestures, typed_action.lane, typed_action.sound);
    };

    const auto handle_release_lane_key_hold_gesture = [&](const release_lane_key_hold_gesture_action_t& typed_action) {
        return release_lane_key_hold_gesture(hold_gestures, typed_action.lane);
    };

    const auto handle_begin_alt_pointer_hold_gesture = [&](const begin_alt_pointer_hold_gesture_action_t& typed_action) -> editor_interaction_result_t {

        if (!typed_action.alt_or_option_down) {
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_hold,
                       "Alt/Option must be held to begin a pointer hold gesture")});
        }
        return begin_hold_gesture(hold_gestures, typed_action.lane, typed_action.position,
                                  hold_gesture_source_t::alt_pointer, typed_action.sound);

    };

    const auto handle_update_alt_pointer_hold_gesture = [&](const update_alt_pointer_hold_gesture_action_t& typed_action) -> editor_interaction_result_t {

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
        return update_hold_gesture(hold_gestures, hold_gesture_source_t::alt_pointer,
                                   typed_action.position);

    };

    const auto handle_release_alt_pointer_hold_gesture = [&](const release_alt_pointer_hold_gesture_action_t& typed_action) -> editor_interaction_result_t {

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
        return release_hold_gesture(hold_gestures, hold_gesture_source_t::alt_pointer,
                                    typed_action.position);

    };










    const auto handle_begin_semantic_layer_paint_gesture = [&](const begin_semantic_layer_paint_gesture_action_t& typed_action) -> editor_interaction_result_t {

        return begin_semantic_layer_paint_gesture(typed_action.layer, typed_action.note,
                                                  typed_action.bgm_event,
                                                  typed_action.pointer_captured, typed_action.replace_memberships,
                                                  typed_action.clear_memberships, typed_action.mode);

    };

    const auto handle_update_semantic_layer_paint_gesture = [&](const update_semantic_layer_paint_gesture_action_t& typed_action) -> editor_interaction_result_t {

        return append_semantic_layer_paint_target(typed_action.note, typed_action.bgm_event,
                                                  typed_action.pointer_captured);

    };

    const auto handle_end_semantic_layer_paint_gesture = [&](const end_semantic_layer_paint_gesture_action_t& typed_action) -> editor_interaction_result_t {

        return end_semantic_layer_paint_gesture(typed_action.note, typed_action.bgm_event,
                                               typed_action.pointer_captured);

    };

    const auto handle_begin_editor_area_selection = [&](const begin_editor_area_selection_action_t& typed_action) -> editor_interaction_result_t {

        return begin_area_selection(typed_action);

    };

    const auto handle_update_editor_area_selection = [&](const update_editor_area_selection_action_t& typed_action) -> editor_interaction_result_t {

        return update_area_selection(typed_action.x_pixels, typed_action.y_pixels,
                                     typed_action.pointer_captured);

    };

    const auto handle_commit_editor_area_selection = [&](const commit_editor_area_selection_action_t& typed_action) -> editor_interaction_result_t {

        return commit_area_selection(typed_action.x_pixels, typed_action.y_pixels,
                                     typed_action.pointer_captured);

    };

    const auto handle_cancel_editor_area_selection = [&](const cancel_editor_area_selection_action_t& typed_action) -> editor_interaction_result_t {

        return {};

    };

    space_range_selection_context_t space_selection{
        history, state, *chart, index, initial_view, absolute};
    const auto handle_space_range_selection = [&](const space_range_selection_action_t& typed_action) -> editor_interaction_result_t {
        return update_space_range_selection(space_selection, typed_action);
    };

    const auto handle_cancel_editor_gesture = [&](const cancel_editor_gesture_action_t& typed_action) -> editor_interaction_result_t {

        return {};

    };

    const auto handle_select_editor_notes = [&](const select_editor_notes_action_t& typed_action) {
        return select_editor_notes(history, state, *chart, typed_action);
    };

    editor_object_selection_context_t object_selection{history, state, *chart};
    const auto handle_select_editor_objects = [&](const select_editor_objects_action_t& typed_action) -> editor_interaction_result_t {
        return select_editor_objects(object_selection, typed_action);
    };

    const auto handle_delete_editor_selection =
        [&](const delete_editor_selection_action_t&) -> editor_interaction_result_t {
            return delete_editor_selection({history, state, *chart});
        };

    const auto handle_resize_note_durations =
        [&](const resize_note_durations_action_t& typed_action) -> editor_interaction_result_t {
            auto planned = make_duration_plan(state.selected_notes, typed_action.reference_note,
                                              typed_action.reference_end);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
                return diagnostic_result(history, state, *diagnostics);
            return apply_duration_plan(std::get<duration_resize_plan_t>(std::move(planned)));
        };

    const auto handle_select_editor_lane = [&](const select_editor_lane_action_t& typed_action) {
        return select_editor_lane(history, state, *chart, index, typed_action.lane);
    };

    const auto handle_assign_selected_notes_to_lane =
        [&](const assign_selected_objects_to_lane_action_t& typed_action)
            -> editor_interaction_result_t {
            return assign_selected_objects_to_lane({history, state, *chart, index}, typed_action);
        };

    const auto handle_adjust_editor_zoom = [&](const adjust_editor_zoom_action_t& typed_action) {
        return adjust_editor_zoom(history, state, *chart, index, absolute, typed_action);
    };

    const auto handle_scroll_editor_view =
        [&](const scroll_editor_view_action_t& typed_action) -> editor_interaction_result_t {
            return scroll_editor_view(
                {history, state, *chart, index,
                 [&](local_position_t position) { return update_keyboard_holds(hold_gestures, position); },
                 [&](std::uint64_t row) { return update_canvas_keyboard_holds(hold_gestures, row); }},
                typed_action);
        };

    switch (action.index()) {

    case 0: return handle_move_edit_cursor(std::get<move_edit_cursor_action_t>(action));

    case 1: return handle_page_edit_cursor(std::get<page_edit_cursor_action_t>(action));

    case 2: return handle_jump_edit_cursor(std::get<jump_edit_cursor_action_t>(action));

    case 3: return handle_select_bms_canvas_measure(std::get<select_bms_canvas_measure_action_t>(action));

    case 4: return handle_cycle_edit_snap(std::get<cycle_edit_snap_action_t>(action));

    case 7: return handle_place_virtual_bms_hold(std::get<place_virtual_bms_hold_action_t>(action));

    case 6: return handle_place_virtual_bms_tap(std::get<place_virtual_bms_tap_action_t>(action));

    case 5: return handle_place_tap(std::get<place_tap_action_t>(action));

    case 8: return handle_begin_hold(std::get<begin_hold_action_t>(action));

    case 9: return handle_update_hold_preview(std::get<update_hold_preview_action_t>(action));

    case 10: return handle_commit_hold(std::get<commit_hold_action_t>(action));

    case 11: return handle_cancel_hold(std::get<cancel_hold_action_t>(action));

    case 12: return handle_begin_lane_key_hold_gesture(std::get<begin_lane_key_hold_gesture_action_t>(action));

    case 13: return handle_release_lane_key_hold_gesture(std::get<release_lane_key_hold_gesture_action_t>(action));

    case 14: return handle_begin_alt_pointer_hold_gesture(std::get<begin_alt_pointer_hold_gesture_action_t>(action));

    case 15: return handle_update_alt_pointer_hold_gesture(std::get<update_alt_pointer_hold_gesture_action_t>(action));

    case 16: return handle_release_alt_pointer_hold_gesture(std::get<release_alt_pointer_hold_gesture_action_t>(action));

    case 17: return dispatch_note_gesture(note_gestures, action);

    case 18: return dispatch_note_gesture(note_gestures, action);

    case 19: return dispatch_note_gesture(note_gestures, action);

    case 20: return dispatch_note_gesture(note_gestures, action);

    case 21: return dispatch_note_gesture(note_gestures, action);

    case 22: return dispatch_note_gesture(note_gestures, action);

    case 23: return dispatch_note_paint_gesture(note_paint, action);

    case 24: return dispatch_note_paint_gesture(note_paint, action);

    case 25: return dispatch_note_paint_gesture(note_paint, action);

    case 26: return handle_begin_semantic_layer_paint_gesture(std::get<begin_semantic_layer_paint_gesture_action_t>(action));

    case 27: return handle_update_semantic_layer_paint_gesture(std::get<update_semantic_layer_paint_gesture_action_t>(action));

    case 28: return handle_end_semantic_layer_paint_gesture(std::get<end_semantic_layer_paint_gesture_action_t>(action));

    case 29: return handle_begin_editor_area_selection(std::get<begin_editor_area_selection_action_t>(action));

    case 30: return handle_update_editor_area_selection(std::get<update_editor_area_selection_action_t>(action));

    case 31: return handle_commit_editor_area_selection(std::get<commit_editor_area_selection_action_t>(action));

    case 32: return handle_cancel_editor_area_selection(std::get<cancel_editor_area_selection_action_t>(action));

    case 33: return handle_space_range_selection(std::get<space_range_selection_action_t>(action));

    case 34: return handle_cancel_editor_gesture(std::get<cancel_editor_gesture_action_t>(action));

    case 35: return handle_select_editor_notes(std::get<select_editor_notes_action_t>(action));

    case 36: return handle_select_editor_objects(std::get<select_editor_objects_action_t>(action));

    case 37: return handle_delete_editor_selection(std::get<delete_editor_selection_action_t>(action));

    case 38: return handle_resize_note_durations(std::get<resize_note_durations_action_t>(action));

    case 39: return handle_select_editor_lane(std::get<select_editor_lane_action_t>(action));

    case 40: return handle_assign_selected_notes_to_lane(std::get<assign_selected_objects_to_lane_action_t>(action));

    case 41: return handle_adjust_editor_zoom(std::get<adjust_editor_zoom_action_t>(action));

    case 42: return handle_scroll_editor_view(std::get<scroll_editor_view_action_t>(action));

    case 43: return handle_set_edit_snap(std::get<set_edit_snap_action_t>(action));

    default:
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id, "unknown editor action")});
    }
}
} // namespace

} // namespace wormedit

namespace wormedit {

auto assign_note_lanes(document_history_t& history, editor_interaction_state_t& state,
                       lane_id_t lane) -> editor_interaction_result_t {
    const auto* chart = find_chart(history.document, state.chart);
    if (!chart)
        return diagnostic_result(history, state, {error(diagnostic_code_t::invalid_id,
                                                        "lane assignment chart is unavailable")});
    auto built = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    return assign_selected_notes_to_lane({history, state, *chart, std::get<measure_index_t>(built)},
                                         {lane, false});
}

auto prune_stale_selection(const chart_document_t& chart, std::uint64_t document_revision,
                           editor_interaction_state_t& state) -> bool {
    if (state.selection_checked_revision == document_revision) return false;
    state.selection_checked_revision = document_revision;
    const auto before = state.selected_notes.size() + state.selected_objects.size();
    std::map<editor_selection_object_kind_t, std::unordered_set<std::uint64_t>> existing;
    const auto exists = [&](editor_selection_object_kind_t kind, object_id_t id) {
        auto found = existing.find(kind);
        if (found == existing.end()) found = existing.emplace(kind, existing_ids(chart, kind)).first;
        return found->second.contains(id.value);
    };
    std::erase_if(state.selected_notes, [&](object_id_t id) {
        return !exists(editor_selection_object_kind_t::note, id);
    });
    std::erase_if(state.selected_objects, [&](const editor_selection_object_t& object) {
        return !exists(object.kind, object.id);
    });
    bool changed = before != state.selected_notes.size() + state.selected_objects.size();
    if (state.retained_selection_range && !span_measures_exist(chart, *state.retained_selection_range)) {
        state.retained_selection_range.reset();
        changed = true;
    }
    return changed;
}

} // namespace wormedit
