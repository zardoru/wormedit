#include "interaction_context.h"
#include "interaction_geometry.h"
#include "interaction_internal.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/interaction_gestures.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/snap.h>

#include "interaction_cursor.h"

#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/virtual_canvas.h>

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
using namespace interaction_detail;
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

auto editor_selection_targets(const editor_interaction_state_t& state)
    -> std::vector<editor_selection_object_t> {
    if (!state.selected_objects.empty()) return state.selected_objects;
    std::vector<editor_selection_object_t> objects;
    objects.reserve(state.selected_notes.size());
    for (const auto id : state.selected_notes)
        objects.push_back({editor_selection_object_kind_t::note, id});
    return objects;
}

auto interaction_view_result(const document_history_t& history,
                             const editor_interaction_state_t& state,
                             bool changed) -> editor_interaction_result_t;
auto combine_editor_area_selection(const editor_interaction_state_t& state,
    const std::vector<editor_selection_object_t>& hits, editor_area_selection_mode_t mode,
    const editor_hit_test_index_t& hit_index)
    -> std::variant<std::vector<editor_selection_object_t>, diagnostic_list_t>;

namespace interaction_detail {

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


// Keyboard deletion may remove a sounded note, but never use it as a merge target.
// Preserve source-linked/labelled objects and hidden or locked memberships on this fast path.


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

auto has_span_measures(const chart_document_t& chart, const position_span_t& span) -> bool {
    const auto has = [&](measure_id_t id) {
        return std::ranges::any_of(chart.measures,
                                   [id](const auto& measure) { return measure.id == id; });
    };
    return has(span.begin.measure) && has(span.end.measure);
}

;



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



auto transition_lane(const chart_document_t& chart, const measure_index_t& index,
                     local_position_t from, local_position_t to, lane_id_t current,
                     const editor_scratch_presentation_t& sides)
    -> std::optional<lane_id_t> {
    const auto* source = effective_layout(chart, index, from);
    const auto* target = effective_layout(chart, index, to);
    return source && target ? editor_lane_across_presented_layouts(*source, *target, current, sides)
                            : std::nullopt;
}





auto resolve_canvas_cursor(const chart_document_t& chart, const measure_index_t& index,
                           const editor_interaction_state_t& state)
    -> std::variant<canvas_cursor_t, diagnostic_list_t> {
    auto absolute = absolute_row(index, state.cursor);
    if (!absolute)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "editor cursor lies outside chart geometry")};
    auto rows = index.total_rows;
    if (has_chart_virtual_tail(chart)) {
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

auto is_valid_view_snap(const chart_document_t& chart,
                     const editor_interaction_state_t& state) -> bool {
    return state.active_snap_denominator != 0 &&
        std::ranges::find(state.supported_snap_denominators, state.active_snap_denominator) !=
            state.supported_snap_denominators.end() &&
        chart.divisions_per_standard_measure % state.active_snap_denominator == 0;
}

auto is_valid_viewport(const chart_document_t& chart, const measure_index_t& index,
                    const editor_interaction_state_t& state) -> bool {
    const auto maximum = maximum_viewport_scroll(
        chart, index, state.pixels_per_standard_measure, state.viewport_height_pixels, state.viewport_end_margin_fraction);
    if (!std::isfinite(state.pixels_per_standard_measure) ||
        state.pixels_per_standard_measure < minimum_pixels_per_standard_measure ||
        state.pixels_per_standard_measure > maximum_pixels_per_standard_measure ||
        !maximum || !std::isfinite(state.viewport_scroll_pixels) ||
        !is_editor_viewport_margins_valid(state.viewport_minimum_margin_fraction,
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
                           const editor_interaction_state_t& state, const measure_index_t* retained)
    -> std::variant<view_geometry_t, diagnostic_list_t> {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "editor interaction refers to an unknown chart")};
    // A heap-stable owner keeps the reference valid when the result variant
    // moves. Retained geometry is borrowed only for this synchronous inspection
    // call.
    std::unique_ptr<measure_index_t> owned;
    if (!retained) {
        auto built = build_measure_index(*chart);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
            return *diagnostics;
        owned = std::make_unique<measure_index_t>(std::get<measure_index_t>(std::move(built)));
        retained = owned.get();
    }
    const auto& index = *retained;
    const auto canvas = resolve_canvas_cursor(*chart, index, state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&canvas))
        return *diagnostics;
    const auto cursor = std::get<canvas_cursor_t>(canvas);
    if (state.selected_canvas_measure &&
        (!has_chart_virtual_tail(*chart) ||
         *state.selected_canvas_measure >= virtual_tail_measure_limit(*chart)))
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "selected BMS measure is outside #000-#999")};
    const auto lane_position =
        state.virtual_bms_cursor_row && !index.entries.empty()
            ? local_position_t{index.entries.back().id, index.entries.back().row_capacity - 1}
            : state.cursor;
    const auto* lane = find_active_lane(*chart, index, lane_position, state.active_lane);
    if (!lane)
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "active editor lane is unavailable at the cursor")};
    if (!is_valid_view_snap(*chart, state))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "active editor snap is invalid for this chart")};
    if (!is_valid_viewport(*chart, index, state))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_resolution, "editor viewport geometry is invalid")};
    return view_geometry_t{.chart = chart,
                           .owned_index = std::move(owned),
                           .index = index,
                           .absolute_row = cursor.absolute_row,
                           .canvas_rows = cursor.canvas_rows,
                           .lane_position = lane_position,
                           .lane = lane};
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
    if (!pending.hit_index)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "Space range selection is stale after a document edit")};
    if (!pending.stamp.is_revision_current(history.document.revision) ||
        pending.hit_index->document_revision != history.document.revision ||
        pending.hit_index->chart != geometry.chart->id)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       stale_action_message("Editor gesture"))};
    if (pending.anchor_canvas_row >= geometry.canvas_rows)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "Space range anchor lies outside chart geometry")};
    const auto [first, last] = space_range_bounds(pending, geometry.absolute_row, geometry.canvas_rows);
    auto queried = space_range_objects(*geometry.chart, *pending.hit_index, first, last,
                                      pending.scope);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried)) return *diagnostics;
    auto combined = combine_editor_area_selection(state,
        std::get<std::vector<editor_selection_object_t>>(queried), pending.mode, *pending.hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined)) return *diagnostics;
    auto objects = std::get<std::vector<editor_selection_object_t>>(std::move(combined));
    auto notes = notes_from_objects(objects);
    return std::optional{space_range_selection_preview_t{
        .stamp = pending.stamp, .anchor = pending.anchor,
        .cursor = state.cursor, .anchor_canvas_row = pending.anchor_canvas_row,
        .cursor_canvas_row = geometry.absolute_row, .first_row = first, .last_row = last,
        .mode = pending.mode,
        .notes = std::make_shared<const std::vector<object_id_t>>(std::move(notes)),
        .objects = std::make_shared<const std::vector<editor_selection_object_t>>(
            std::move(objects))}};
}

auto is_valid_retained_canvas_range(const editor_interaction_state_t& state,
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
               const measure_index_t* retained)
    -> std::variant<editor_interaction_view_model_t, diagnostic_list_t> {
    auto resolved = resolve_view_geometry(history, state, retained);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&resolved))
        return *diagnostics;
    auto geometry = std::get<view_geometry_t>(std::move(resolved));
    auto range_preview = make_space_range_preview(history, state, geometry);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&range_preview))
        return *diagnostics;
    auto retained_selection_range =
        state.retained_selection_range
            ? normalized_position_span(geometry.index, state.retained_selection_range->begin,
                                       state.retained_selection_range->end)
            : std::optional<position_span_t>{};
    if (!is_valid_retained_canvas_range(state, geometry.canvas_rows))
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "retained canvas range lies outside chart geometry")};
    auto keyboard_previews = canvas_keyboard_hold_previews(
        *geometry.chart, geometry.index, geometry.canvas_rows, history.document.revision, state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&keyboard_previews))
        return *diagnostics;
    auto space_preview =
        std::get<std::optional<space_range_selection_preview_t>>(std::move(range_preview));
    const auto step =
        geometry.chart->divisions_per_standard_measure / state.active_snap_denominator;
    return editor_interaction_view_model_t{
        .chart = state.chart,
        .active_lane = state.active_lane,
        .scratch_on_right = state.scratch_on_right,
        .active_lane_name = geometry.lane->name,
        .cursor = state.cursor,
        .virtual_bms_cursor_row = state.virtual_bms_cursor_row,
        .cursor_subrow = state.cursor_subrow,
        .active_snap_denominator = state.active_snap_denominator,
        .snap_enabled = state.snap_enabled,
        .y_lock = state.y_lock,
        .bga_lane_visible = state.bga_lane_visible,
        .bga_revealed_lane_mask =
            static_cast<std::uint8_t>(state.bga_revealed_lane_mask & editor_bga_lane_mask_all),
        .supported_snap_denominators = state.supported_snap_denominators,
        .document_revision = history.document.revision,
        .pixels_per_standard_measure = state.pixels_per_standard_measure,
        .viewport_scroll_pixels = state.viewport_scroll_pixels,
        .viewport_height_pixels = state.viewport_height_pixels,
        .viewport_minimum_margin_fraction = state.viewport_minimum_margin_fraction,
        .viewport_end_margin_fraction = state.viewport_end_margin_fraction,
        .fixed_cursor_screen_y_pixels = state.fixed_cursor_screen_y_pixels,
        .scroll_direction = state.scroll_direction,
        .pending_hold = state.pending_hold,
        .additional_keyboard_holds = state.additional_keyboard_holds,
        .pending_canvas_keyboard_holds =
            std::get<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>>(
                std::move(keyboard_previews)),
        .pending_note_duration_gesture = state.pending_note_duration_gesture,
        .pending_note_move_gesture = state.pending_note_move_gesture,
        .pending_note_paint_gesture =
            state.pending_note_paint_gesture
                ? std::optional{note_paint_gesture_preview_t{
                      .stamp = state.pending_note_paint_gesture->stamp,
                      .targets = state.pending_note_paint_gesture->targets}}
                : std::nullopt,
        .pending_editor_area_selection =
            state.pending_editor_area_selection
                ? std::optional{editor_area_selection_preview_t{
                      .stamp = state.pending_editor_area_selection->stamp,
                      .left_pixels =
                          std::min(state.pending_editor_area_selection->anchor_x_pixels,
                                   state.pending_editor_area_selection->current_x_pixels),
                      .top_pixels =
                          std::min(area_selection_anchor_y(*state.pending_editor_area_selection,
                                                           state.viewport_scroll_pixels),
                                   state.pending_editor_area_selection->current_y_pixels),
                      .right_pixels =
                          std::max(state.pending_editor_area_selection->anchor_x_pixels,
                                   state.pending_editor_area_selection->current_x_pixels),
                      .bottom_pixels =
                          std::max(area_selection_anchor_y(*state.pending_editor_area_selection,
                                                           state.viewport_scroll_pixels),
                                   state.pending_editor_area_selection->current_y_pixels),
                      .notes = state.pending_editor_area_selection->notes,
                      .objects = state.pending_editor_area_selection->objects}}
                : std::nullopt,
        .pending_space_range_selection = std::move(space_preview),
        .retained_selection_range = retained_selection_range,
        .retained_canvas_selection_range = state.retained_canvas_selection_range,
        .selected_canvas_measure = state.selected_canvas_measure,
        .selected_notes = state.selected_notes,
        .selected_objects = state.selected_objects,
        .can_move_backward = geometry.absolute_row >= step,
        .can_move_forward = step < geometry.canvas_rows - geometry.absolute_row,
    };
}

// Geometry and lane resolution is independent of pointer hit bands and materialization.
// Pause rows remain valid; the shared verb planner applies the Warp head rule only to adds.


// The kind a note takes once it has a tail: rolls and fakes keep their kind, all else is a hold.









// Gives each moved tap's retained slice event the tap's new position (and lane, when playable).

// Taps moved together may share a row; landing on a row another tap holds is refused.

// A BMSON tap that stands for a stem point drags its retained slice event along: the audio stays
// anchored at the stem's last restart, so moving a continuation only moves where the stem is cut,
// while moving a restart moves the audio. The move is refused when it lands on a row another tap
// of the stem holds, or when the stem's slices could no longer be derived.

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

auto is_playhead_gesture_active(const editor_interaction_state_t& state) -> bool {
    return has_pending_editor_gesture(state);
}

auto proposed_playhead_cursor(const document_history_t& history, const chart_document_t& chart,
                              const measure_index_t& index, const editor_interaction_state_t& state,
                              local_position_t position)
    -> std::variant<editor_interaction_state_t, diagnostic_list_t> {
    auto proposed = state;
    if (proposed.cursor == position && !proposed.virtual_bms_cursor_row)
        return proposed;
    place_cursor_at(proposed, position);
    if (!interaction_detail::keep_cursor_visible(chart, index, proposed))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "playhead cursor cannot keep the viewport valid")};
    auto view = make_view(history, proposed, &index);
    if (std::holds_alternative<diagnostic_list_t>(view)) {
        const auto replacement = transition_lane(chart, index, state.cursor, position,
                                                 state.active_lane, state.scratch_on_right);
        if (replacement) {
            proposed.active_lane = *replacement;
            view = make_view(history, proposed, &index);
        }
    }
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return *diagnostics;
    return proposed;
}

} // namespace interaction_detail

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
                                   geometry.document_revision == history.document.revision
                               ? geometry.index
                               : nullptr;
    return make_view(history, state, retained);
}

namespace {
auto prepare_follow_view_state(const document_history_t& history, const chart_document_t& chart,
                               const measure_index_t& index,
                               const editor_interaction_state_t& state, double fraction)
    -> std::variant<editor_interaction_state_t, diagnostic_list_t> {
    if (!is_editor_viewport_margins_valid(state.viewport_minimum_margin_fraction,
                                       state.viewport_end_margin_fraction))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_resolution, "playhead viewport margin is invalid")};
    auto proposed = state;
    proposed.viewport_minimum_margin_fraction =
        std::max(editor_cursor_edge_margin_fraction, fraction);
    proposed.viewport_end_margin_fraction = std::min(editor_cursor_edge_margin_fraction, fraction);
    if (!is_note_kind_allowed(chart, proposed.armed_note_kind))
        proposed.armed_note_kind = note_kind_t::tap;
    const auto maximum = maximum_viewport_scroll(chart, index, proposed.pixels_per_standard_measure,
                                                 proposed.viewport_height_pixels,
                                                 proposed.viewport_end_margin_fraction);
    if (!maximum || !std::isfinite(proposed.viewport_scroll_pixels))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_resolution, "playhead viewport geometry is invalid")};
    set_viewport_scroll_pixels(proposed, std::clamp(proposed.viewport_scroll_pixels,
                                                    -proposed.viewport_height_pixels *
                                                        proposed.viewport_minimum_margin_fraction,
                                                    *maximum));
    auto view = make_view(history, proposed, &index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view))
        return *diagnostics;
    return proposed;
}
} // namespace

auto follow_editor_playhead(document_history_t& history, editor_interaction_state_t& state,
                            editor_measure_geometry_t geometry, local_position_t position,
                            double target_scroll, double following_anchor_fraction,
                            double cursor_subrow) -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart || !geometry.index || !std::isfinite(target_scroll) ||
        !std::isfinite(following_anchor_fraction) || !std::isfinite(cursor_subrow) ||
        following_anchor_fraction < 0.0 || following_anchor_fraction > 1.0 || is_playhead_gesture_active(state))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                     "playhead geometry is stale or playback editing is blocked")}};
    if (geometry.chart != state.chart || geometry.document_revision != history.document.revision)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                     stale_action_message("Playhead geometry"))}};
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

auto editor_interaction_state_t::from(const document_history_t& history, chart_id_t chart_id,
                                      lane_id_t lane_id, local_position_t cursor,
                                      std::span<const std::uint64_t> ordered_snap_denominators,
                                      std::uint64_t requested_snap_denominator,
                                      double viewport_height_pixels) -> editor_interaction_setup_t {
    editor_interaction_setup_t result;
    const auto* chart = history.document.find_chart(chart_id);
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
    const auto* chart = history.document.find_chart(previous.chart);
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
        const auto editable = std::ranges::find_if(segment.layout.lanes, &lane_definition_t::is_editable);
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
    result = editor_interaction_state_t::from(history, previous.chart, lane, *cursor,
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
    if (is_note_kind_allowed(*chart, previous.armed_note_kind))
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

auto apply_duration_resize_plan(document_history_t&, editor_interaction_state_t&,
                                const duration_resize_plan_t&) -> editor_interaction_result_t;
auto apply_note_move_plan(document_history_t&, editor_interaction_state_t&, const note_move_plan_t&)
    -> editor_interaction_result_t;

auto dispatch_nested_action(editor_edit_context_t& context, const editor_interaction_action_t& action)
    -> editor_interaction_result_t {
    return dispatch_editor_action(context.history, context.state, action);
}
auto cancel_pointer_gesture(editor_edit_context_t& context) -> editor_interaction_result_t {
    return dispatch_nested_action(
        context,
        cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::pointer_capture_lost});
}
auto make_duration_plan(editor_edit_context_t& context, std::vector<object_id_t> targets,
                        object_id_t reference, local_position_t end, bool exact_end)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t> {
    return plan_duration_resize(
        context.scope.chart, context.scope.measures, std::move(targets), reference, end,
        edit_snap_denominator(context.scope.chart, context.state), exact_end);
}
auto make_move_plan(editor_edit_context_t& context, std::vector<object_id_t> targets,
                    object_id_t reference, local_position_t position, lane_id_t lane,
                    bool exact_position, std::optional<object_id_t> bgm_lane,
                    std::optional<std::uint64_t> virtual_row)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    return plan_move_gesture(context.history, context.state, context.scope.chart,
                             context.scope.measures, std::move(targets), reference, position, lane,
                             exact_position, bgm_lane, virtual_row);
}
auto apply_duration_plan(editor_edit_context_t& context, const duration_resize_plan_t& plan)
    -> editor_interaction_result_t {
    return apply_duration_resize_plan(context.history, context.state, plan);
}
auto apply_move_plan(editor_edit_context_t& context, const note_move_plan_t& plan)
    -> editor_interaction_result_t {
    return apply_note_move_plan(context.history, context.state, plan);
}


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

auto collect_delete_ids(const editor_interaction_state_t& state)
    -> std::variant<editor_delete_ids_t, diagnostic_list_t> {
    return collect_delete_object_ids(editor_selection_targets(state));
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

auto complete_editor_delete(editor_edit_context_t& context, bool changed)
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

// Delete removes playable stem points from the keys without cutting them out of the audio stem.
// Stage this conversion alongside ordinary deletion, so a later guard cannot leave a partial edit.
auto stage_delete_stem_points(song_document_t& proposed, const editor_edit_context_t& context,
                              const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t> {
    if (context.scope.chart.source_family != format_family_t::bmson) return std::size_t{0};
    std::vector<object_id_t> stems;
    for (const auto& measure : context.scope.chart.measures)
        for (const auto& note : measure.notes) {
            const auto selected = ids.total() != 0 ? ids.notes.contains(note.id.value)
                : note.position == context.state.cursor && note.lane == context.state.active_lane;
            if (selected && note.sound_channel_event) stems.push_back(note.id);
        }
    if (stems.empty()) return std::size_t{0};
    auto scratch = document_history_t::from(proposed);
    const auto moved = move_notes_to_bgm(scratch, context.state.chart, stems, std::nullopt);
    if (!moved.applied) return moved.diagnostics;
    proposed = std::move(scratch.document);
    return stems.size();
}

auto delete_editor_selection(editor_edit_context_t& context) -> editor_interaction_result_t {
    auto collected = collect_delete_ids(context.state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
        return diagnostic_result(context.history, context.state, *diagnostics);
    auto ids = std::get<editor_delete_ids_t>(std::move(collected));
    if (const auto invalid = validate_delete_note_ids(context.scope.chart, ids.notes))
        return diagnostic_result(context.history, context.state, {*invalid});
    auto inspected = inspect_delete_bgm_events(context.scope.chart, ids);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&inspected))
        return diagnostic_result(context.history, context.state, *diagnostics);
    const auto linked_event = std::get<std::optional<object_id_t>>(inspected);
    if (linked_event) {
        auto command = delete_bmson_bgm_marker(context.history,
                                              context.state.chart, *linked_event);
        if (!command)
            return diagnostic_result(context.history, context.state,
                                     std::move(command.diagnostics));
        return complete_editor_delete(context, true);
    }

    auto transaction = document_command_transaction_t::from(context.history);

    auto& proposed = transaction.copy_document();
    auto staged = stage_delete_stem_points(proposed, context, ids);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&staged))
        return diagnostic_result(context.history, context.state, *diagnostics);
    auto* chart = proposed.find_chart(context.state.chart);
    auto removed = std::get<std::size_t>(staged);
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
    auto command = transaction.commit(selected ? "Delete selected chart objects" : "Delete note");
    if (!command)
        return diagnostic_result(context.history, context.state, std::move(command.diagnostics));
    return complete_editor_delete(context, true);
}


auto collect_assignment_candidates(const editor_edit_context_t& context)
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
    for (const auto& measure : context.scope.chart.measures) {
        for (const auto& note : measure.notes) {
            if (!selected_ids.contains(note.id.value))
                continue;
            const auto begin = absolute_row(context.scope.measures, note.position);
            const auto end = note.end ? absolute_row(context.scope.measures, *note.end) : begin;
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

auto build_assignment_intervals(const editor_edit_context_t& context,
                                std::span<const assignment_candidate_t> candidates,
                                lane_id_t target_lane)
    -> std::variant<std::vector<assignment_interval_t>, diagnostic_list_t> {
    std::unordered_set<std::uint64_t> assigned_ids;
    assigned_ids.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        assigned_ids.insert(candidate.note->id.value);
        if (find_active_lane(context.scope.chart, context.scope.measures, candidate.note->position,
                             target_lane) == nullptr ||
            (candidate.note->end &&
             find_active_lane(context.scope.chart, context.scope.measures, *candidate.note->end,
                              target_lane) == nullptr)) {
            return diagnostic_list_t{error(
                diagnostic_code_t::unknown_lane,
                "target lane is unavailable at a selected note endpoint", candidate.note->id)};
        }
    }
    auto chart_note_count = std::size_t{};
    for (const auto& measure : context.scope.chart.measures) {
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
    for (const auto& measure : context.scope.chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != target_lane || assigned_ids.contains(note.id.value))
                continue;
            const auto begin = absolute_row(context.scope.measures, note.position);
            const auto end = note.end ? absolute_row(context.scope.measures, *note.end) : begin;
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

auto apply_note_lane_assignment_document(document_history_t& history, chart_id_t chart_id,
    std::span<const assignment_candidate_t> candidates, lane_id_t lane, std::string label)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto* chart = proposed.find_chart(chart_id);
    note_move_plan_t plan;
    plan.moved.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        auto* note = (*chart).find_note(candidate.note->id);
        if (!note) return {.revision = history.document.revision,
            .diagnostics = {error(diagnostic_code_t::invalid_id, "lane assignment target disappeared")}};
        plan.moved.push_back({.id = note->id, .lane = lane, .position = note->position,
                              .end_position = note->end});
        note->lane = lane;
    }
    if (const auto failure = sync_linked_sound_events(*chart, plan, history.document))
        return {.revision = history.document.revision, .diagnostics = {*failure}};
    return transaction.commit(std::move(label));
}

auto assign_selected_notes_to_lane(editor_edit_context_t& context,
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
    const auto changed = std::ranges::any_of(candidates, [&](const auto& candidate) {
        return candidate.note->lane != action.lane;
    });
    if (changed) {
        auto command = apply_note_lane_assignment_document(context.history, context.state.chart,
            candidates, action.lane, action.successive ? "Assign next selected note to lane"
                                                     : "Assign selected notes to lane");
        if (!command) return diagnostic_result(context.history, context.state, std::move(command.diagnostics));
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


auto lane_move_candidate_less(const lane_move_candidate_t& left, const lane_move_candidate_t& right) -> bool {
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
        if (!first || lane_move_candidate_less(candidate, *first)) first = candidate;
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
    auto targets = editor_quantize_selection(state);
    return {std::move(targets.notes), std::move(targets.bgm_events)};
}

auto assign_selected_objects_to_lane(editor_edit_context_t& context,
                                     const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t {
    auto [notes, events] = selected_lane_assignment_ids(context.state);
    const auto first = action.successive ? first_selected_lane_object(context.scope.chart, notes, events)
                                         : std::nullopt;
    if (action.successive && first) {
        notes = first->kind == editor_selection_object_kind_t::note
                    ? std::vector{first->id} : std::vector<object_id_t>{};
        events = first->kind == editor_selection_object_kind_t::bgm_event
                    ? std::vector{first->id} : std::vector<object_id_t>{};
    }
    const auto revision = context.history.document.revision;
    const auto command = move_selected_objects_to_lane(
        context.history, context.state, notes, events, action.lane);
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


auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<diagnostic_t>;

auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t>;

auto update_fixed_scroll_cursor(editor_edit_context_t& context,
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
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_resolution, "fixed cursor scroll anchor is invalid")};
    }
    set_cursor_screen_anchor(proposed, anchor_y, action.direction);
    if (!synchronize_editor_cursor_to_viewport(context.scope.chart, context.scope.measures,
                                               proposed))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_resolution,
                                       "fixed cursor scroll cannot resolve chart geometry")};
    return std::monostate{};
}

auto preserve_keyboard_hold_after_scroll(editor_edit_context_t& context,
                                         const editor_interaction_state_t& proposed)
    -> std::optional<editor_interaction_result_t> {
    if (!context.state.pending_hold ||
        context.state.pending_hold->gesture_source != hold_gesture_source_t::keyboard_lane ||
        (proposed.cursor == context.state.cursor &&
         proposed.virtual_bms_cursor_row == context.state.virtual_bms_cursor_row))
        return std::nullopt;
    const auto before = context.state;
    const auto canvas_hold =
        proposed.virtual_bms_cursor_row || context.state.pending_hold->anchor_canvas_row ||
        std::ranges::any_of(context.state.additional_keyboard_holds,
                            [](const auto& hold) { return hold.anchor_canvas_row.has_value(); });
    const auto cursor_row = proposed.virtual_bms_cursor_row.value_or(
        *absolute_row(context.scope.measures, proposed.cursor));
    context.state = proposed;
    const auto updated = canvas_hold
                             ? interaction_detail::update_canvas_keyboard_holds(context, cursor_row)
                             : interaction_detail::update_keyboard_holds(context, proposed.cursor);
    if (!updated)
        context.state = before;
    return updated;
}

auto scroll_editor_view(editor_edit_context_t& context, const scroll_editor_view_action_t& action)
    -> editor_interaction_result_t {
    if (!std::isfinite(action.vertical_pixels) ||
        !std::isfinite(context.state.viewport_scroll_pixels)) {
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::invalid_resolution, "viewport scroll must remain finite")});
    }
    const auto next_scroll = context.state.viewport_scroll_pixels + action.vertical_pixels;
    if (!std::isfinite(next_scroll)) {
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::invalid_resolution, "viewport scroll would overflow")});
    }
    const auto maximum_scroll = maximum_viewport_scroll(context.scope.chart, context.scope.measures,
                                                        context.state.pixels_per_standard_measure,
                                                        context.state.viewport_height_pixels);
    if (!maximum_scroll)
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::invalid_resolution, "editor viewport geometry is invalid")});
    auto proposed = context.state;
    set_viewport_scroll_pixels(
        proposed,
        std::clamp(next_scroll, minimum_viewport_scroll(context.state.viewport_height_pixels),
                   *maximum_scroll));
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

auto is_keyboard_entry_editable(const chart_document_t& chart, const note_t& note) -> bool {
    return !note.branch && std::ranges::all_of(note.semantic_layers, [&](layer_id_t id) {
        const auto layer = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        return layer != chart.semantic_layers.end() && layer->visible && !layer->locked;
    });
}

auto keyboard_entry_occupant(const chart_document_t& chart, const measure_index_t& index,
    lane_id_t lane, local_position_t target) -> std::variant<const note_t*, diagnostic_list_t> {
    const note_t* found{};
    const auto row = absolute_row(index, target);
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            if (note.lane != lane) continue;
            const auto begin = absolute_row(index, note.position);
            const auto end = note.end ? absolute_row(index, *note.end) : begin;
            if (!row || !begin || !end || *row < *begin || *row > *end) continue;
            if (note.position != target || found)
                return diagnostic_list_t{error(diagnostic_code_t::collision,
                    "Lane-key entry overlaps a hold body or multiple note heads", note.id)};
            found = &note;
        }
    if (found && !is_keyboard_entry_editable(chart, *found))
        return diagnostic_list_t{error(diagnostic_code_t::collision,
            "Lane-key entry cannot change a hidden, locked or conditional note", found->id)};
    return found;
}

auto stage_keyboard_occupant(document_history_t& scratch, editor_interaction_state_t state,
                             const note_t& occupant, bool to_bgm) -> diagnostic_list_t {
    if (to_bgm) {
        const std::array ids{occupant.id};
        const auto moved =
            move_notes_to_bgm(scratch, state.chart, ids, std::nullopt);
        return moved.diagnostics;
    }
    state.pending_hold.reset();
    state.additional_keyboard_holds.clear();
    state.selected_notes = {occupant.id};
    state.selected_objects.clear();
    const auto removed = dispatch_editor_action(scratch, state, delete_editor_selection_action_t{});
    return removed.diagnostics;
}

auto place_keyboard_entry(document_history_t& scratch, editor_interaction_state_t state,
                          lane_id_t lane, local_position_t target, resource_id_t sound,
                          std::optional<object_id_t> stem,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<object_id_t, diagnostic_list_t> {
    state.pending_hold.reset();
    state.additional_keyboard_holds.clear();
    state.selected_notes.clear();
    state.selected_objects.clear();
    place_cursor_at(state, target);
    state.active_lane = lane;
    if (!stem) {
        const auto placed =
            dispatch_editor_action(scratch, state, place_tap_action_t{target, sound});
        if (!placed)
            return placed.diagnostics;
        if (placed.created_note)
            return *placed.created_note;
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "Choose a non-hold note kind to replace an occupied head")};
    }
    const auto inserted = insert_bmson_playable_stem_point(
        scratch, state.chart, *stem, target, lane, geometry);
    if (!inserted)
        return inserted.diagnostics;
    const auto* chart = scratch.document.find_chart(state.chart);
    for (const auto& measure : chart->measures)
        for (const auto& note : measure.notes)
            if (note.position == target && note.lane == lane)
                return note.id;
    return diagnostic_list_t{
        error(diagnostic_code_t::invalid_id, "The linked stem note was not published")};
}

auto stage_keyboard_entry(const editor_edit_context_t& context, lane_id_t lane,
                          local_position_t target, const note_t* occupant,
                          std::optional<resource_id_t> sound, bool displace,
                          std::optional<object_id_t> stem,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_keyboard_entry_t, diagnostic_list_t> {
    auto scratch = document_history_t::from(context.history.document);
    const bool same = occupant && sound && occupant->sound == sound;
    if (occupant) {
        const auto failure =
            stage_keyboard_occupant(scratch, context.state, *occupant, same || (sound && displace));
        if (!failure.empty())
            return failure;
    }
    std::optional<object_id_t> created;
    if (sound && !same) {
        const auto placed =
            place_keyboard_entry(scratch, context.state, lane, target, *sound, stem, geometry);
        if (const auto* failure = std::get_if<diagnostic_list_t>(&placed))
            return *failure;
        created = std::get<object_id_t>(placed);
    }
    return prepared_keyboard_entry_t{std::move(scratch.document), created};
}

auto apply_keyboard_entry(editor_edit_context_t& context, lane_id_t lane, local_position_t position,
                          std::optional<resource_id_t> sound, bool displace,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::optional<editor_interaction_result_t> {
    auto& state = context.state;
    const auto resolved_row =
        resolve_row_placement(context.scope.chart, context.scope.measures, state, lane, position);
    const auto* target = std::get_if<local_position_t>(&resolved_row);
    if (!target)
        return std::nullopt;
    const auto occupied =
        keyboard_entry_occupant(context.scope.chart, context.scope.measures, lane, *target);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&occupied))
        return diagnostic_result(context.history, state, *failure);
    const auto resolved = entry_stem_channel(context.history.document, context.scope.chart, sound);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&resolved))
        return diagnostic_result(context.history, state, *failure);
    const auto* occupant = std::get<const note_t*>(occupied);
    const auto stem = std::get<std::optional<object_id_t>>(resolved);
    if (!occupant && !stem)
        return std::nullopt;
    auto staged =
        stage_keyboard_entry(context, lane, *target, occupant, sound, displace, stem, geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&staged))
        return diagnostic_result(context.history, state, *failure);
    auto prepared = std::get<prepared_keyboard_entry_t>(std::move(staged));
    auto transaction = document_command_transaction_t::from(context.history);
    transaction.adopt_document(std::move(prepared.document));
    const auto command = transaction.commit("Enter note from lane key");
    if (!command)
        return diagnostic_result(context.history, state, command.diagnostics);
    place_cursor_at(state, *target);
    state.active_lane = lane;
    auto result = interaction_view_result(context.history, state, true);
    result.created_note = prepared.created;
    return result;
}

auto space_selection_view_result(
    const std::variant<editor_interaction_view_model_t, diagnostic_list_t>& view)
    -> editor_interaction_result_t;

auto interaction_detail::update_keyboard_holds(editor_edit_context_t& context,
                                               local_position_t position)
    -> editor_interaction_result_t {
    const auto before = context.state;
    place_cursor_at(context.state, context.state.cursor);
    if (!context.state.pending_hold)
        return interaction_view_result(context.history, context.state, false);
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

auto interaction_detail::update_canvas_keyboard_holds(editor_edit_context_t& context,
                                                      std::uint64_t row)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto before = state;
    if (!state.pending_hold || !has_chart_virtual_tail(context.scope.chart))
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "canvas hold movement requires a BMS lane key")});
    state.pending_hold->preview_canvas_row = row;
    for (auto& hold : state.additional_keyboard_holds)
        hold.preview_canvas_row = row;
    const auto authored = position_at(context.scope.measures, row);
    place_cursor_on_canvas_row(state, authored, row);
    if (!interaction_detail::keep_cursor_visible(context.scope.chart, context.scope.measures,
                                                 state)) {
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
    return {.accepted = true, .view = std::get<editor_interaction_view_model_t>(std::move(view))};
}

// Materialization is private preparation. Authored and virtual requests run the
// same placement verb, then publish the prepared document as one history entry.

auto begin_semantic_paint(editor_edit_context_t& context, layer_id_t layer_id,
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
    const auto layer = std::ranges::find(context.scope.chart.semantic_layers, layer_id,
                                         &semantic_layer_t::id);
    if (!clear_memberships && (layer == context.scope.chart.semantic_layers.end() || !layer->visible))
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
    if ((note_id && !context.scope.chart.find_note(note_id)) ||
        (bgm_event && !context.scope.chart.find_bgm_event(*bgm_event)))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint target is not in the chart", note_id ? note_id : *bgm_event)});
    auto notes = std::make_shared<std::vector<object_id_t>>();
    auto bgm_events = std::make_shared<std::vector<object_id_t>>();
    if (note_id) notes->push_back(note_id);
    if (bgm_event) bgm_events->push_back(*bgm_event);
    pending_semantic_layer_paint_gesture_t pending{
        .layer = layer_id,
        .stamp = document_stamp_t::from(history.document.revision),
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

auto semantic_paint_edit(chart_id_t chart, const pending_semantic_layer_paint_gesture_t& pending)
    -> semantic_layer_edit_action_t {
    if (pending.clear_memberships)
        return clear_semantic_layer_membership_action_t{chart, *pending.notes, *pending.bgm_events};
    if (pending.replace_memberships)
        return replace_notes_semantic_layer_action_t{chart, pending.layer, *pending.notes, true,
                                                     *pending.bgm_events};
    return semantic_layer_membership_edit_t{.chart = chart,
                                            .layer = pending.layer,
                                            .notes = *pending.notes,
                                            .bgm_events = *pending.bgm_events,
                                            .operation =
                                                pending.mode == semantic_layer_paint_mode_t::remove
                                                    ? membership_operation_t::remove
                                                    : membership_operation_t::assign};
}

auto end_semantic_paint(editor_edit_context_t& context, object_id_t note_id,
                        std::optional<object_id_t> bgm_event,
                        bool pointer_captured) -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!pointer_captured)
        return cancel_pointer_gesture(context);
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
    if (!pending.notes || !pending.bgm_events || (pending.notes->empty() && pending.bgm_events->empty()))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "semantic-layer paint gesture is malformed or stale")});
    if (!pending.stamp.is_revision_current(history.document.revision))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        stale_action_message("Editor gesture"))});
    const auto layer = std::ranges::find(context.scope.chart.semantic_layers, pending.layer,
                                        &semantic_layer_t::id);
    if (!pending.clear_memberships &&
        (layer == context.scope.chart.semantic_layers.end() || !layer->visible || layer->locked))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint requires an unlocked visible layer")});
    const auto edit = semantic_paint_edit(state.chart, pending);
    const auto applied = apply_semantic_layer_edit(history, edit);
    if (!applied && !applied.diagnostics.empty())
        return diagnostic_result(history, state, applied.diagnostics);
    state.pending_semantic_layer_paint_gesture.reset();
    auto result = semantic_paint_view(history, state);
    result.document_changed = applied.applied;
    return result;
}

auto audition_selected_object(bool requested,
    const std::vector<editor_selection_object_t>& hits, const editor_interaction_state_t& state,
    std::uint64_t revision) -> std::optional<editor_object_audition_request_t> {
    if (!requested || hits.size() != 1 ||
        std::ranges::find(state.selected_objects, hits.front()) == state.selected_objects.end())
        return std::nullopt;
    return editor_object_audition_request_t{revision, state.chart, hits.front()};
}

auto select_editor_objects(editor_edit_context_t& context,
                           const select_editor_objects_action_t& action)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto built = build_editor_hit_test_index(context.scope.chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    const auto& hit_index = std::get<editor_hit_test_index_t>(built);
    const auto& available = hit_index.available_objects;
    const auto& selected = action.objects;
    const std::set unique(selected.begin(), selected.end());
    if (unique.size() != selected.size())
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
        .audition = audition_selected_object(action.request_audition, selected, state,
                                              history.document.revision),
    };
}


auto is_virtual_cursor_action_allowed(const editor_interaction_state_t& state,
                                   const editor_interaction_action_t& action) -> bool {
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


auto select_editor_notes(editor_edit_context_t& context,
                         const select_editor_notes_action_t& action)
    -> editor_interaction_result_t {
    std::vector<editor_selection_object_t> objects;
    objects.reserve(action.notes.size());
    for (const auto id : action.notes)
        objects.push_back({editor_selection_object_kind_t::note, id});
    return select_editor_objects(context, {std::move(objects)});
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
                                    const editor_interaction_action_t& action)
    -> std::optional<editor_interaction_result_t> {
    if (!state.virtual_bms_cursor_row || is_virtual_cursor_action_allowed(state, action))
        return std::nullopt;
    return diagnostic_result(history, state,
                             {error(diagnostic_code_t::row_out_of_range,
                                    "this edit requires a materialized cursor measure")});
}

auto is_note_kind_allowed(const chart_document_t& chart, note_kind_t kind) -> bool {
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
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart || !is_note_kind_allowed(*chart, kind))
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_binding, "Note kind is unavailable for this chart profile.")});
    if (state.pending_hold || state.pending_note_paint_gesture)
        return diagnostic_result(history, state,
            {error(diagnostic_code_t::invalid_hold, "Finish or cancel note placement before changing kind.")});
    state.armed_note_kind = kind;
    return space_selection_view_result(make_view(history, state));
}

namespace {
auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> editor_interaction_result_t;
} // namespace

template <class Action> auto is_committing_action(const editor_interaction_action_t& action) -> bool {
    const auto* update = std::get_if<Action>(&action);
    return update && update->phase == editor_gesture_phase_t::commit;
}

auto is_terminal_editor_gesture_action(const editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action) -> bool {
    return std::holds_alternative<commit_hold_action_t>(action) ||
           std::holds_alternative<place_virtual_bms_hold_action_t>(action) ||
           std::holds_alternative<release_alt_pointer_hold_gesture_action_t>(action) ||
           std::holds_alternative<release_lane_key_hold_gesture_action_t>(action) ||
           is_committing_action<update_note_duration_gesture_action_t>(action) ||
           is_committing_action<update_note_move_gesture_action_t>(action) ||
           std::holds_alternative<end_note_paint_gesture_action_t>(action) ||
           std::holds_alternative<update_semantic_layer_paint_gesture_action_t>(action) ||
           std::holds_alternative<update_editor_area_selection_action_t>(action) ||
           (state.pending_space_range_selection &&
            std::holds_alternative<space_range_selection_action_t>(action));
}

auto dispatch_editor_action(document_history_t& history, editor_interaction_state_t& state,
                            const editor_interaction_action_t& action) -> editor_interaction_result_t {
    if (auto canceled = handle_editor_cancellation(history, state, action))
        return *canceled;
    if (is_editor_gestures_stale(state, history.document.revision)) {
        clear_editor_gestures(state);
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        stale_action_message("Editor gesture"))});
    }
    auto result = dispatch_editor_action_impl(history, state, action);
    if (!result && is_terminal_editor_gesture_action(state, action)) {
        clear_editor_gestures(state);
        return diagnostic_result(history, state, std::move(result.diagnostics));
    }
    return result;
}

namespace {
auto append_semantic_paint_target(editor_edit_context_t& context, object_id_t note,
                                  std::optional<object_id_t> bgm_event, bool captured)
    -> editor_interaction_result_t {
    return bgm_event ? append_semantic_paint_bgm_event(context, *bgm_event, captured)
                     : append_semantic_paint_note(context, note, captured);
}

auto handle(editor_edit_context_t& context, const move_edit_cursor_action_t& typed_action,
            const std::uint64_t& absolute) -> editor_interaction_result_t {
    interaction_detail::cursor_navigation_context_t cursor_navigation{context, absolute};
    return interaction_detail::move_edit_cursor(cursor_navigation, typed_action);
}

auto handle(editor_edit_context_t& context, const page_edit_cursor_action_t& typed_action,
            const std::uint64_t& absolute) -> editor_interaction_result_t {
    interaction_detail::cursor_navigation_context_t cursor_navigation{context, absolute};
    return interaction_detail::page_edit_cursor(cursor_navigation, typed_action);
}

auto handle(editor_edit_context_t& context, const jump_edit_cursor_action_t& typed_action,
            const std::uint64_t& absolute) -> editor_interaction_result_t {
    interaction_detail::cursor_navigation_context_t cursor_navigation{context, absolute};
    return interaction_detail::jump_edit_cursor(cursor_navigation, typed_action);
}

auto handle(editor_edit_context_t& context, const select_bms_canvas_measure_action_t& typed_action,
            const std::uint64_t& absolute) -> editor_interaction_result_t {
    interaction_detail::cursor_navigation_context_t cursor_navigation{context, absolute};
    return interaction_detail::select_bms_canvas_measure(cursor_navigation, typed_action);
}

auto handle(editor_edit_context_t& context, const cycle_edit_snap_action_t& typed_action)
    -> editor_interaction_result_t {
    return cycle_editor_snap(context.history, context.state, typed_action);
}

auto handle(editor_edit_context_t& context, const set_edit_snap_action_t& typed_action)
    -> editor_interaction_result_t {
    return set_editor_snap(context.history, context.state, typed_action);
}

auto handle(editor_edit_context_t& context, const place_virtual_bms_hold_action_t& typed_action)
    -> editor_interaction_result_t {
    return place_virtual_bms_hold(context, typed_action);
}

auto handle(editor_edit_context_t& context, const place_virtual_bms_tap_action_t& typed_action)
    -> editor_interaction_result_t {
    return place_virtual_bms_tap(context, typed_action);
}

auto handle(editor_edit_context_t& context, const place_tap_action_t& typed_action)
    -> editor_interaction_result_t {
    if (context.state.armed_note_kind == note_kind_t::hold ||
        context.state.armed_note_kind == note_kind_t::roll)
        return begin_hold_action(context, typed_action.position, typed_action.sound,
                                 typed_action.source_geometry);
    return place_tap(context, typed_action);
}

auto handle(editor_edit_context_t& context, const begin_hold_action_t& typed_action)
    -> editor_interaction_result_t {
    return begin_hold_action(context, typed_action.position);
}

auto handle(editor_edit_context_t& context, const update_hold_preview_action_t& typed_action)
    -> editor_interaction_result_t {
    return update_hold_preview(context, typed_action.position);
}

auto handle(editor_edit_context_t& context, const commit_hold_action_t& typed_action)
    -> editor_interaction_result_t {
    return commit_hold(context, typed_action.position, typed_action.source_geometry);
}

auto handle(editor_edit_context_t& context,
            const begin_lane_key_hold_gesture_action_t& typed_action)
    -> editor_interaction_result_t {
    return begin_lane_key_hold_gesture(context, typed_action.lane, typed_action.sound,
                                       typed_action.displace_to_bgm, typed_action.source_geometry);
}

auto handle(editor_edit_context_t& context,
            const release_lane_key_hold_gesture_action_t& typed_action)
    -> editor_interaction_result_t {
    return release_lane_key_hold_gesture(context, typed_action.lane);
}

auto handle(editor_edit_context_t& context,
            const begin_alt_pointer_hold_gesture_action_t& typed_action)
    -> editor_interaction_result_t {

    if (!typed_action.alt_or_option_down) {
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::invalid_hold,
                   "Alt/Option must be held to begin a pointer hold gesture")});
    }
    return begin_hold_gesture(context, typed_action.lane, typed_action.position,
                              hold_gesture_source_t::alt_pointer, typed_action.sound, false,
                              typed_action.source_geometry);
}

auto handle(editor_edit_context_t& context,
            const update_alt_pointer_hold_gesture_action_t& typed_action)
    -> editor_interaction_result_t {

    if (!typed_action.pointer_captured) {
        return cancel_pointer_gesture(context);
    }
    if (!typed_action.alt_or_option_down) {
        return dispatch_editor_action(
            context.history, context.state,
            cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::modifier_released});
    }
    return update_hold_gesture(context, hold_gesture_source_t::alt_pointer, typed_action.position);
}

auto handle(editor_edit_context_t& context,
            const release_alt_pointer_hold_gesture_action_t& typed_action)
    -> editor_interaction_result_t {

    if (!typed_action.pointer_captured) {
        return cancel_pointer_gesture(context);
    }
    if (!typed_action.alt_or_option_down) {
        return dispatch_editor_action(
            context.history, context.state,
            cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::modifier_released});
    }
    return release_hold_gesture(context, hold_gesture_source_t::alt_pointer, typed_action.position,
                                typed_action.source_geometry);
}

auto handle(editor_edit_context_t& context,
            const begin_semantic_layer_paint_gesture_action_t& typed_action)
    -> editor_interaction_result_t {

    return begin_semantic_paint(context, typed_action.layer, typed_action.note,
                                typed_action.bgm_event, typed_action.pointer_captured,
                                typed_action.replace_memberships, typed_action.clear_memberships,
                                typed_action.mode);
}

auto handle(editor_edit_context_t& context,
            const update_semantic_layer_paint_gesture_action_t& typed_action)
    -> editor_interaction_result_t {
    if (typed_action.phase == editor_gesture_phase_t::commit) {
        return end_semantic_paint(context, typed_action.note, typed_action.bgm_event,
                                  typed_action.pointer_captured);
    }

    return append_semantic_paint_target(context, typed_action.note, typed_action.bgm_event,
                                        typed_action.pointer_captured);
}


auto handle(editor_edit_context_t& context,
            const begin_editor_area_selection_action_t& typed_action,
            const editor_initial_view_t& initial_view) -> editor_interaction_result_t {
    area_selection_context_t area_selection{context, initial_view};

    return begin_area_selection_gesture(area_selection, typed_action);
}

auto handle(editor_edit_context_t& context,
            const update_editor_area_selection_action_t& typed_action,
            const editor_initial_view_t& initial_view) -> editor_interaction_result_t {
    if (typed_action.phase == editor_gesture_phase_t::commit) {
        area_selection_context_t area_selection{context, initial_view};

        return commit_area_selection_gesture(area_selection, typed_action.x_pixels,
                                             typed_action.y_pixels, typed_action.pointer_captured);
    }
    area_selection_context_t area_selection{context, initial_view};

    return update_area_selection_gesture(area_selection, typed_action.x_pixels,
                                         typed_action.y_pixels, typed_action.pointer_captured);
}


auto handle(editor_edit_context_t&, const cancel_editor_area_selection_action_t&)
    -> editor_interaction_result_t {

    return {};
}

auto handle(editor_edit_context_t& context, const space_range_selection_action_t& typed_action,
            const editor_initial_view_t& initial_view, const std::uint64_t& absolute)
    -> editor_interaction_result_t {
    space_range_selection_context_t space_selection{context, initial_view, absolute};
    return update_space_range_selection(space_selection, typed_action);
}

auto handle(editor_edit_context_t&, const cancel_editor_gesture_action_t&)
    -> editor_interaction_result_t {

    return {};
}

auto handle(editor_edit_context_t& context, const select_editor_notes_action_t& typed_action)
    -> editor_interaction_result_t {
    return select_editor_notes(context, typed_action);
}

auto handle(editor_edit_context_t& context, const select_editor_objects_action_t& typed_action)
    -> editor_interaction_result_t {
    return select_editor_objects(context, typed_action);
}

auto handle(editor_edit_context_t& context, const delete_editor_selection_action_t&)
    -> editor_interaction_result_t {
    return delete_editor_selection(context);
}

auto handle(editor_edit_context_t& context, const select_editor_lane_action_t& typed_action)
    -> editor_interaction_result_t {
    return select_editor_lane(context.history, context.state, context.scope.chart,
                              context.scope.measures, typed_action.lane);
}

auto handle(editor_edit_context_t& context,
            const assign_selected_objects_to_lane_action_t& typed_action)
    -> editor_interaction_result_t {
    return assign_selected_objects_to_lane(context, typed_action);
}

auto handle(editor_edit_context_t& context, const adjust_editor_zoom_action_t& typed_action,
            const std::uint64_t& absolute) -> editor_interaction_result_t {
    return adjust_editor_zoom(context.history, context.state, context.scope.chart,
                              context.scope.measures, absolute, typed_action);
}

auto handle(editor_edit_context_t& context, const scroll_editor_view_action_t& typed_action)
    -> editor_interaction_result_t {
    return scroll_editor_view(context, typed_action);
}

auto handle(editor_edit_context_t& context, const begin_note_duration_gesture_action_t& action)
    -> editor_interaction_result_t {
    return begin_note_duration_gesture(context, action.reference_note);
}

auto handle(editor_edit_context_t& context, const update_note_duration_gesture_action_t& action)
    -> editor_interaction_result_t {
    if (action.phase == editor_gesture_phase_t::commit) {
        return release_note_duration_gesture(context, action.reference_end);
    }
    return update_note_duration_gesture(context, action.reference_end);
}

auto handle(editor_edit_context_t& context, const begin_note_move_gesture_action_t& action)
    -> editor_interaction_result_t {
    return begin_note_move_gesture(context, action.reference_note, action.displace_to_bgm);
}

auto handle(editor_edit_context_t& context, const update_note_move_gesture_action_t& action)
    -> editor_interaction_result_t {
    if (action.phase == editor_gesture_phase_t::commit) {
        return release_note_move_gesture(context, action.reference_position, action.reference_lane,
                                         action.to_bgm, action.preferred_bgm_lane,
                                         action.virtual_bgm_row);
    }
    return update_note_move_gesture(context, action.reference_position, action.reference_lane,
                                    action.to_bgm, action.preferred_bgm_lane,
                                    action.virtual_bgm_row);
}

auto handle(editor_edit_context_t& context, const begin_note_paint_gesture_action_t& action)
    -> editor_interaction_result_t {
    return begin_note_paint_gesture(context, action.lane, action.position, action.pointer_captured,
                                    action.sound, action.intent, action.source_geometry);
}

auto handle(editor_edit_context_t& context, const update_note_paint_gesture_action_t& action)
    -> editor_interaction_result_t {
    return append_note_paint_target(context, action.lane, action.position, action.pointer_captured);
}

auto handle(editor_edit_context_t& context, const end_note_paint_gesture_action_t& action)
    -> editor_interaction_result_t {
    return end_note_paint_gesture(context, action.lane, action.position, action.pointer_captured,
                                  action.source_geometry);
}

struct editor_action_visitor_t {
    editor_edit_context_t& context;
    const editor_initial_view_t& initial_view;
    const std::uint64_t& absolute;
    template <class Action>
    auto operator()(const Action& action) const -> editor_interaction_result_t {
        return handle(context, action);
    }
    auto operator()(const move_edit_cursor_action_t& action) const -> editor_interaction_result_t {
        return handle(context, action, absolute);
    }
    auto operator()(const page_edit_cursor_action_t& action) const -> editor_interaction_result_t {
        return handle(context, action, absolute);
    }
    auto operator()(const jump_edit_cursor_action_t& action) const -> editor_interaction_result_t {
        return handle(context, action, absolute);
    }
    auto operator()(const select_bms_canvas_measure_action_t& action) const
        -> editor_interaction_result_t {
        return handle(context, action, absolute);
    }
    auto operator()(const begin_editor_area_selection_action_t& action) const
        -> editor_interaction_result_t {
        return handle(context, action, initial_view);
    }
    auto operator()(const update_editor_area_selection_action_t& action) const
        -> editor_interaction_result_t {
        return handle(context, action, initial_view);
    }
    auto operator()(const adjust_editor_zoom_action_t& action) const
        -> editor_interaction_result_t {
        return handle(context, action, absolute);
    }
    auto operator()(const space_range_selection_action_t& action) const
        -> editor_interaction_result_t {
        return handle(context, action, initial_view, absolute);
    }
};

auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> editor_interaction_result_t {
    if (auto rejected = validate_active_gesture_action(history, state, action))
        return *rejected;
    if (auto rejected = validate_virtual_cursor_action(history, state, action))
        return *rejected;
    const auto* chart = history.document.find_chart(state.chart);
    if (chart && !is_note_kind_allowed(*chart, state.armed_note_kind))
        state.armed_note_kind = note_kind_t::tap;
    const auto initial_view = make_view(history, state);
    if (std::holds_alternative<diagnostic_list_t>(initial_view)) {
        return diagnostic_result(history, state, std::get<diagnostic_list_t>(initial_view));
    }
    const auto built_index = build_measure_index(*chart);
    const auto& index = std::get<measure_index_t>(built_index);
    const auto absolute = state.virtual_bms_cursor_row.value_or(*absolute_row(index, state.cursor));
    editor_edit_context_t context{history, state, {*chart, index}};
    return std::visit(editor_action_visitor_t{context, initial_view, absolute}, action);
}
} // namespace

namespace {
auto scoped_selection(const editor_interaction_state_t& state,
                      const std::vector<editor_selection_object_t>& available,
                      editor_selection_scope_t scope, bool invert)
    -> std::vector<editor_selection_object_t> {
    const auto current = editor_selection_targets(state);
    const std::set selected(current.begin(), current.end());
    std::vector<editor_selection_object_t> result;
    for (const auto object : current)
        if (!is_editor_selection_scope_accepted(scope, object.kind))
            result.push_back(object);
    for (const auto object : available)
        if (!invert || !selected.contains(object))
            result.push_back(object);
    return result;
}
} // namespace

auto select_editor_scope(document_history_t& history, editor_interaction_state_t& state,
    editor_selection_scope_t scope, bool invert) -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart) return diagnostic_result(history, state,
        {error(diagnostic_code_t::invalid_id, "Selection chart is unavailable")});
    const auto built = build_editor_hit_test_index(*chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    const auto& index = std::get<editor_hit_test_index_t>(built);
    if (index.measures.total_rows == 0) return diagnostic_result(history, state,
        {error(diagnostic_code_t::row_out_of_range, "Selection chart has no rows")});
    const auto queried = editor_objects_in_row_range(*chart, index, 0, index.measures.total_rows - 1, scope);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried))
        return diagnostic_result(history, state, *diagnostics);
    return dispatch_editor_action(history, state, select_editor_objects_action_t{
        scoped_selection(state, std::get<std::vector<editor_selection_object_t>>(queried), scope, invert)});
}

auto editor_quantize_selection(const editor_interaction_state_t& state) -> quantize_selection_t {
    quantize_selection_t result;
    for (const auto& object : editor_selection_targets(state)) {
        if (object.kind == editor_selection_object_kind_t::note) result.notes.push_back(object.id);
        if (object.kind == editor_selection_object_kind_t::bgm_event) result.bgm_events.push_back(object.id);
    }
    const auto normalize = [](auto& ids) {
        std::ranges::sort(ids, {}, &object_id_t::value);
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    };
    normalize(result.notes);
    normalize(result.bgm_events);
    return result;
}

} // namespace wormedit

namespace wormedit {

auto assign_note_lanes(document_history_t& history, editor_interaction_state_t& state,
                       lane_id_t lane) -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return diagnostic_result(history, state, {error(diagnostic_code_t::invalid_id,
                                                        "lane assignment chart is unavailable")});
    auto built = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return diagnostic_result(history, state, *diagnostics);
    editor_edit_context_t context{history, state, {*chart, std::get<measure_index_t>(built)}};
    return assign_selected_notes_to_lane(context, {lane, false});
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
    if (state.retained_selection_range && !has_span_measures(chart, *state.retained_selection_range)) {
        state.retained_selection_range.reset();
        changed = true;
    }
    return changed;
}

} // namespace wormedit

namespace wormedit {
auto has_pending_editor_gesture(const editor_interaction_state_t& state) -> bool {
    bool pending{};
    visit_editor_gestures(state, [&](auto, const auto& field) {
        pending |= is_editor_gesture_pending(field);
    });
    return pending;
}

} // namespace wormedit
