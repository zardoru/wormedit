#include "bmson_sound_channel_editor_internal.h"
#include "interaction_edit_commit.h"
#include "interaction_context.h"
#include "interaction_geometry.h"
#include "interaction_gesture_runner.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/interaction_cursor_pose.h>
#include <wormedit/core/interaction_queries.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/measure_editor.h>
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
#include <concepts>
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
auto editor_interaction_result_t::from(const document_history_t& history,
    const editor_interaction_state_t& state, bool changed) -> editor_interaction_result_t {
    return from(make_view(history, state), changed);
}

auto editor_interaction_result_t::from(
    std::variant<editor_interaction_view_model_t, diagnostic_list_t> view,
    bool changed) -> editor_interaction_result_t {
    if (auto* model = std::get_if<editor_interaction_view_model_t>(&view))
        return {.accepted = true, .document_changed = changed, .view = std::move(*model)};
    return {.document_changed = changed,
            .diagnostics = std::get<diagnostic_list_t>(std::move(view))};
}

void editor_interaction_result_t::ensure_view(const document_history_t& history,
                                              const editor_interaction_state_t& state) {
    if (accepted || view)
        return;
    auto inspected = make_view(history, state);
    if (auto* model = std::get_if<editor_interaction_view_model_t>(&inspected))
        view = std::move(*model);
}

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
    case kind_t::bar_line:
        for (std::size_t at = 1; at < chart.measures.size(); ++at) ids.insert(chart.measures[at].id.value);
        break;
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
    auto absolute = index.row_of(state.cursor);
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
        auto built = measure_index_t::from(*chart);
        if (!built)
            return built.error();
        owned = std::make_unique<measure_index_t>(std::move(built).value());
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
    if (!state.gesture<pending_space_range_selection_t>())
        return std::optional<space_range_selection_preview_t>{};
    const auto& pending = *state.gesture<pending_space_range_selection_t>();
    const auto& stamp = *state.gesture_stamp<pending_space_range_selection_t>();
    if (!pending.hit_index)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "Space range selection is stale after a document edit")};
    if (!stamp.is_revision_current(history.document.revision) ||
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
        .stamp = stamp, .anchor = pending.anchor,
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
        .pending_canvas_keyboard_holds =
            std::get<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>>(
                std::move(keyboard_previews)),
        .active_gesture = build_editor_gesture_preview(state.active_gesture, state.viewport_scroll_pixels,
            editor_area_preview_anchor_policy_t::viewport_relative, std::move(space_preview)),
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

auto editor_area_selection_preview_t::from(const pending_editor_area_selection_t& pending,
    document_stamp_t stamp, double viewport_scroll_pixels, editor_area_preview_anchor_policy_t policy)
    -> editor_area_selection_preview_t {
    const auto anchor_y = policy == editor_area_preview_anchor_policy_t::captured_coordinates
        ? pending.anchor_y_pixels
        : interaction_detail::area_selection_anchor_y(pending, viewport_scroll_pixels);
    return {.stamp = stamp,
        .left_pixels = std::min(pending.anchor_x_pixels, pending.current_x_pixels),
        .top_pixels = std::min(anchor_y, pending.current_y_pixels),
        .right_pixels = std::max(pending.anchor_x_pixels, pending.current_x_pixels),
        .bottom_pixels = std::max(anchor_y, pending.current_y_pixels),
        .notes = pending.notes, .objects = pending.objects};
}

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

static auto follow_editor_playhead_impl(document_history_t& history,
                                        editor_interaction_state_t& state,
                                        editor_measure_geometry_t geometry,
                                        local_position_t position, double target_scroll,
                                        double following_anchor_fraction, double cursor_subrow)
    -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart || !geometry.index || !std::isfinite(target_scroll) ||
        !std::isfinite(following_anchor_fraction) || !std::isfinite(cursor_subrow) ||
        following_anchor_fraction < 0.0 || following_anchor_fraction > 1.0 || state.is_pending())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                     "playhead geometry is stale or playback editing is blocked")}};
    if (geometry.chart != state.chart || geometry.document_revision != history.document.revision)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                     stale_action_message("Playhead geometry"))}};
    const auto& index = *geometry.index;
    if (!index.row_of(position))
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

auto follow_editor_playhead(document_history_t& history, editor_interaction_state_t& state,
                            editor_measure_geometry_t geometry, local_position_t position,
                            double target_scroll, double following_anchor_fraction,
                            double cursor_subrow) -> editor_interaction_result_t {
    auto result = follow_editor_playhead_impl(history, state, geometry, position, target_scroll,
                                              following_anchor_fraction, cursor_subrow);
    result.ensure_view(history, state);
    return result;
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
    const auto built_index = measure_index_t::from(*chart);
    if (!built_index.has_value()) {
        result.diagnostics = built_index.error();
        return result;
    }
    const auto& index = built_index.value();
    if (!index.row_of(cursor).has_value()) {
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
    if (const auto* model = std::get_if<editor_interaction_view_model_t>(&view)) {
        result.view = *model;
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
    const auto built_index = measure_index_t::from(*chart);
    if (!built_index) {
        result.diagnostics = built_index.error();
        return result;
    }
    const auto& index = built_index.value();
    std::optional<local_position_t> cursor;
    lane_id_t lane;
    auto first_row = std::numeric_limits<std::uint64_t>::max();
    for (const auto& segment : chart->lane_layouts) {
        const auto row = index.row_of(segment.position);
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


auto cancel_pointer_gesture(editor_edit_context_t& context) -> editor_interaction_result_t {
    return dispatch_editor_action(
        context.history, context.state,
        cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::pointer_capture_lost});
}
auto make_duration_plan(editor_edit_context_t& context, std::vector<object_id_t> targets,
                        object_id_t reference, local_position_t end, bool exact_end)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t> {
    return plan_duration_resize(
        context.scope.chart, context.scope.measures, std::move(targets), reference, end,
        edit_snap_denominator(context.scope.chart, context.state), exact_end);
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

// Delete removes playable stem points from the keys without cutting them out of the audio stem.
// Stage this conversion alongside ordinary deletion, so a later guard cannot leave a partial edit.
auto stage_delete_stem_points(document_command_transaction_t& transaction,
                              const chart_document_t& chart,
                              const editor_interaction_state_t& state,
                              const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t> {
    if (chart.source_family != format_family_t::bmson)
        return std::size_t{0};
    std::vector<object_id_t> stems;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            const auto selected =
                ids.total() != 0 ? ids.notes.contains(note.id.value)
                                 : note.position == state.cursor && note.lane == state.active_lane;
            if (selected && note.sound_channel_event)
                stems.push_back(note.id);
        }
    if (stems.empty())
        return std::size_t{0};
    auto moved = stage_move_notes_to_bgm(transaction, state.chart, stems, std::nullopt);
    if (!moved.value || !moved.ok())
        return std::move(moved.diagnostics);
    return stems.size();
}

// Deleted bar lines merge their measures after the other objects are gone, so every remaining
// object keeps its row.
auto stage_delete_bar_lines(chart_document_t& chart, const editor_delete_ids_t& ids)
    -> std::variant<std::size_t, diagnostic_list_t> {
    if (ids.bar_lines.empty()) return std::size_t{0};
    std::vector<measure_id_t> measures;
    measures.reserve(ids.bar_lines.size());
    for (const auto id : ids.bar_lines) measures.push_back(measure_id_t{id});
    if (auto diagnostics = remove_bar_lines(chart, measures); !diagnostics.empty()) return diagnostics;
    return measures.size();
}

auto erase_editor_selection_objects(chart_document_t& chart,
                                    const editor_interaction_state_t& state,
                                    const editor_delete_ids_t& ids) -> std::size_t {
    std::size_t removed{};
    const auto selected = ids.total() != 0;
    removed += std::erase_if(chart.bookmarks, [&](const auto& bookmark) {
        return ids.bookmarks.contains(bookmark.id.value);
    });
    removed += std::erase_if(chart.sections, [&](const auto& section) {
        return ids.sections.contains(section.id.value);
    });
    removed += std::erase_if(chart.bga_events, [&](const auto& event) {
        return ids.bga_events.contains(event.id.value);
    });
    for (auto& measure : chart.measures) {
        if (selected) {
            removed += std::erase_if(measure.notes, [&](const auto& note) {
                return ids.notes.contains(note.id.value);
            });
            removed += std::erase_if(measure.bgm_events, [&](const auto& event) {
                return ids.bgm_events.contains(event.id.value);
            });
        } else {
            removed += std::erase_if(measure.notes, [&](const auto& note) {
                return note.position == state.cursor && note.lane == state.active_lane;
            });
        }
    }
    return removed;
}

auto stage_editor_delete(editor_edit_context_t& context,
                         document_command_transaction_t& transaction)
    -> attempt_t<editor_edit_effect_t> {
    return transaction.stage(
        context.state.chart, "delete refers to an unknown chart",
        [&](chart_document_t& chart, song_document_t&) -> attempt_t<editor_edit_effect_t> {
            auto collected = collect_delete_ids(context.state);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            auto ids = std::get<editor_delete_ids_t>(std::move(collected));
            if (const auto invalid = validate_delete_note_ids(chart, ids.notes))
                return attempt_t<editor_edit_effect_t>{.diagnostics = {*invalid}};
            auto inspected = inspect_delete_bgm_events(chart, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&inspected))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            const auto linked_event = std::get<std::optional<object_id_t>>(inspected);
            if (linked_event) {
                auto staged =
                    stage_delete_bmson_bgm_marker(transaction, context.state.chart, *linked_event);
                if (!staged.value || !staged.ok())
                    return {.diagnostics = std::move(staged.diagnostics)};
                return {.value =
                            editor_edit_effect_t{.label = std::move(staged.value->command_label),
                                                 .selection = clear_editor_selection_t{}}};
            }

            auto staged = stage_delete_stem_points(transaction, chart, context.state, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&staged))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            auto removed = std::get<std::size_t>(staged);
            const auto selected = ids.total() != 0;
            removed += erase_editor_selection_objects(chart, context.state, ids);
            auto merged = stage_delete_bar_lines(chart, ids);
            if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&merged))
                return attempt_t<editor_edit_effect_t>{.diagnostics = *diagnostics};
            removed += std::get<std::size_t>(merged);
            if (removed == 0)
                return {.value = editor_edit_effect_t{.has_document_changes = false}};
            if (selected && removed != ids.total()) {
                return attempt_t<editor_edit_effect_t>{
                    .diagnostics = {error(diagnostic_code_t::invalid_id,
                                          "selected deletion did not resolve every object")}};
            }
            return {.value = editor_edit_effect_t{
                        .label = selected ? "Delete selected chart objects" : "Delete note",
                        .selection = clear_editor_selection_t{}}};
        });
}

auto delete_editor_selection(editor_edit_context_t& context) -> editor_interaction_result_t {
    return run_editor_edit(context, [&](document_command_transaction_t& transaction) {
        return stage_editor_delete(context, transaction);
    });
}

auto collect_assignment_candidates(const chart_scope_t& scope,
                                   const editor_interaction_state_t& state)
    -> std::variant<std::vector<assignment_candidate_t>, diagnostic_list_t> {
    if (state.selected_notes.empty()) {
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "lane assignment requires one or more selected notes")};
    }
    std::unordered_set<std::uint64_t> selected_ids;
    selected_ids.reserve(state.selected_notes.size());
    for (const auto id : state.selected_notes) {
        if (!id || !selected_ids.insert(id.value).second) {
            return diagnostic_list_t{error(
                diagnostic_code_t::duplicate_id,
                "selected lane assignment contains an invalid or duplicate ID", id)};
        }
    }
    std::vector<assignment_candidate_t> candidates;
    candidates.reserve(selected_ids.size());
    for (const auto& measure : scope.chart.measures) {
        for (const auto& note : measure.notes) {
            if (!selected_ids.contains(note.id.value))
                continue;
            const auto begin = scope.measures.row_of(note.position);
            const auto end = note.end ? scope.measures.row_of(*note.end) : begin;
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

auto build_assignment_intervals(const chart_scope_t& scope,
                                std::span<const assignment_candidate_t> candidates,
                                lane_id_t target_lane)
    -> std::variant<std::vector<assignment_interval_t>, diagnostic_list_t> {
    std::unordered_set<std::uint64_t> assigned_ids;
    assigned_ids.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        assigned_ids.insert(candidate.note->id.value);
        if (find_active_lane(scope.chart, scope.measures, candidate.note->position, target_lane) ==
                nullptr ||
            (candidate.note->end &&
             find_active_lane(scope.chart, scope.measures, *candidate.note->end, target_lane) ==
                 nullptr)) {
            return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                           "target lane is unavailable at a selected note endpoint",
                                           candidate.note->id)};
        }
    }
    auto chart_note_count = std::size_t{};
    for (const auto& measure : scope.chart.measures) {
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
    for (const auto& measure : scope.chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != target_lane || assigned_ids.contains(note.id.value))
                continue;
            const auto begin = scope.measures.row_of(note.position);
            const auto end = note.end ? scope.measures.row_of(*note.end) : begin;
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

auto stage_note_lane_assignment(document_command_transaction_t& transaction, chart_id_t chart_id,
                                std::span<const assignment_candidate_t> candidates, lane_id_t lane,
                                std::string label) -> attempt_t<document_edit_effect_t> {
    return transaction.stage(
        chart_id, "lane assignment chart is unavailable",
        [&](chart_document_t& chart, song_document_t&) -> attempt_t<document_edit_effect_t> {
            note_move_plan_t plan;
            plan.moved.reserve(candidates.size());
            for (const auto& candidate : candidates) {
                auto* note = chart.find_note(candidate.note->id);
                if (!note)
                    return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                                  "lane assignment target disappeared")}};
                plan.moved.push_back({.id = note->id,
                                      .lane = lane,
                                      .position = note->position,
                                      .end_position = note->end});
                note->lane = lane;
            }
            if (const auto failure =
                    sync_linked_sound_events(chart, plan, transaction.source_document()))
                return {.diagnostics = {*failure}};
            return {.value =
                        document_edit_effect_t{.changed = true, .command_label = std::move(label)}};
        });
}

auto assign_selected_notes_to_lane(editor_edit_context_t& context,
                                   const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t {
    auto collected = collect_assignment_candidates(context.scope, context.state);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&collected))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    auto candidates = std::get<std::vector<assignment_candidate_t>>(std::move(collected));
    if (action.successive)
        candidates.resize(1);
    auto built_intervals = build_assignment_intervals(context.scope, candidates, action.lane);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_intervals))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    const auto& intervals = std::get<std::vector<assignment_interval_t>>(built_intervals);
    if (const auto invalid = validate_assignment_intervals(intervals))
        return editor_interaction_result_t{.diagnostics = {*invalid}};

    std::optional<editor_object_audition_request_t> audition;
    if (action.successive) {
        audition = editor_object_audition_request_t{
            .document_revision = context.history.document.revision,
            .chart = context.state.chart,
            .object = {editor_selection_object_kind_t::note, candidates.front().note->id}};
    }
    const auto changed = std::ranges::any_of(
        candidates, [&](const auto& candidate) { return candidate.note->lane != action.lane; });
    const auto label =
        action.successive ? "Assign next selected note to lane" : "Assign selected notes to lane";
    return run_editor_edit(
        context,
        [&](document_command_transaction_t& transaction) -> attempt_t<editor_edit_effect_t> {
            if (changed) {
                auto staged = stage_note_lane_assignment(transaction, context.state.chart,
                                                         candidates, action.lane, label);
                if (!staged.value || !staged.ok())
                    return {.diagnostics = std::move(staged.diagnostics)};
            }
            return {.value = editor_edit_effect_t{.label = label,
                                                  .has_document_changes = changed,
                                                  .lane = action.lane,
                                                  .audition = audition}};
        },
        [&](editor_interaction_state_t& selected) {
            if (action.successive) {
                const auto assigned = audition->object.id;
                std::erase(selected.selected_notes, assigned);
                std::erase_if(selected.selected_objects, [&](const auto& object) {
                    return object.kind == editor_selection_object_kind_t::note &&
                           object.id == assigned;
                });
            }
        });
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

auto select_assigned_lane_objects(editor_interaction_state_t& selected,
                                  std::span<const object_id_t> notes,
                                  std::span<const object_id_t> events, bool successive) -> void {
    std::unordered_set<std::uint64_t> assigned_ids;
    for (const auto id : notes) assigned_ids.insert(id.value);
    for (const auto id : events) assigned_ids.insert(id.value);
    const auto assigned = [&](object_id_t id) { return assigned_ids.contains(id.value); };
    if (successive) {
        std::erase_if(selected.selected_notes, assigned);
        std::erase_if(selected.selected_objects, [&](const auto& object) {
            return (object.kind == editor_selection_object_kind_t::note ||
                    object.kind == editor_selection_object_kind_t::bgm_event) && assigned(object.id);
        });
    } else {
        std::erase_if(selected.selected_objects, [&](const auto& object) {
            return object.kind == editor_selection_object_kind_t::bgm_event && assigned(object.id);
        });
        for (const auto id : events) {
            selected.selected_notes.push_back(id);
            selected.selected_objects.push_back({editor_selection_object_kind_t::note, id});
        }
    }
}

auto assign_selected_objects_to_lane(editor_edit_context_t& context,
                                     const assign_selected_objects_to_lane_action_t& action)
    -> editor_interaction_result_t {
    auto [notes, events] = selected_lane_assignment_ids(context.state);
    const auto first = action.successive
                           ? first_selected_lane_object(context.scope.chart, notes, events)
                           : std::nullopt;
    if (action.successive && first) {
        notes = first->kind == editor_selection_object_kind_t::note ? std::vector{first->id}
                                                                    : std::vector<object_id_t>{};
        events = first->kind == editor_selection_object_kind_t::bgm_event
                     ? std::vector{first->id}
                     : std::vector<object_id_t>{};
    }
    std::optional<editor_object_audition_request_t> audition;
    if (action.successive && first)
        audition = editor_object_audition_request_t{
            .chart = context.state.chart,
            .object = {editor_selection_object_kind_t::note, first->id}};
    return commit_editor_edit(
        context, {.lane = action.lane, .audition = audition},
        [&] {
            return move_selected_objects_to_lane(context.history, context.state, notes, events,
                                                 action.lane);
        },
        [&](editor_interaction_state_t& selected) {
            select_assigned_lane_objects(selected, notes, events, action.successive);
        });
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
    if (!context.state.hold() ||
        context.state.hold()->gesture_source != hold_gesture_source_t::keyboard_lane ||
        (proposed.cursor == context.state.cursor &&
         proposed.virtual_bms_cursor_row == context.state.virtual_bms_cursor_row))
        return std::nullopt;
    const auto before = context.state;
    const auto canvas_hold =
        proposed.virtual_bms_cursor_row || context.state.hold()->anchor_canvas_row ||
        std::ranges::any_of(context.state.additional_keyboard_holds(),
                            [](const auto& hold) { return hold.anchor_canvas_row.has_value(); });
    const auto cursor_row = *proposed.cursor_canvas_row(context.scope.measures);
    context.state = proposed;
    auto updated = canvas_hold ? revise_gesture<hold_gesture_t>(context, cursor_row)
                               : revise_gesture<hold_gesture_t>(context, proposed.cursor);
    if (!updated) {
        context.state = before;
        updated.view.reset();
    }
    return updated;
}

auto scroll_editor_view(editor_edit_context_t& context, const scroll_editor_view_action_t& action)
    -> editor_interaction_result_t {
    if (!std::isfinite(action.vertical_pixels) ||
        !std::isfinite(context.state.viewport_scroll_pixels)) {
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "viewport scroll must remain finite")}};
    }
    const auto next_scroll = context.state.viewport_scroll_pixels + action.vertical_pixels;
    if (!std::isfinite(next_scroll)) {
        return editor_interaction_result_t{
            .diagnostics = {
                error(diagnostic_code_t::invalid_resolution, "viewport scroll would overflow")}};
    }
    const auto maximum_scroll = maximum_viewport_scroll(context.scope.chart, context.scope.measures,
                                                        context.state.pixels_per_standard_measure,
                                                        context.state.viewport_height_pixels);
    if (!maximum_scroll)
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "editor viewport geometry is invalid")}};
    auto proposed = context.state;
    set_viewport_scroll_pixels(
        proposed,
        std::clamp(next_scroll, minimum_viewport_scroll(context.state.viewport_height_pixels),
                   *maximum_scroll));
    auto cursor_update = update_fixed_scroll_cursor(context, action, proposed);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&cursor_update))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    if (auto updated = preserve_keyboard_hold_after_scroll(context, proposed))
        return *updated;
    auto result = editor_interaction_result_t::from(context.history, proposed);
    if (result.accepted)
        context.state = std::move(proposed);
    return result;
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
    const auto row = index.row_of(target);
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) {
            if (note.lane != lane) continue;
            const auto begin = index.row_of(note.position);
            const auto end = note.end ? index.row_of(*note.end) : begin;
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
    state.clear();
    state.selected_notes = {occupant.id};
    state.selected_objects.clear();
    auto removed = stage_editor_selection_delete(scratch, state);
    if (!removed.value || !removed.ok())
        return std::move(removed.diagnostics);
    if (removed.value->document)
        scratch.document = std::move(*removed.value->document);
    return {};
}

auto place_keyboard_entry(document_history_t& scratch, editor_interaction_state_t state,
                          lane_id_t lane, local_position_t target, resource_id_t sound,
                          std::optional<object_id_t> stem,
                          std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<object_id_t, diagnostic_list_t> {
    state.clear();
    state.selected_notes.clear();
    state.selected_objects.clear();
    place_cursor_at(state, target);
    state.active_lane = lane;
    if (!stem) {
        auto placed = stage_editor_tap_placement(
            scratch, state, place_tap_action_t{.position = target, .sound = sound});
        if (!placed.value || !placed.ok())
            return std::move(placed.diagnostics);
        if (placed.value->document)
            scratch.document = std::move(*placed.value->document);
        if (placed.value->effect.created_note)
            return *placed.value->effect.created_note;
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

auto keyboard_entry_plan_t::from(editor_edit_context_t& context, const hold_start_t& start)
    -> diagnosed_t<std::optional<keyboard_entry_plan_t>> {
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures,
        context.state, start.lane, start.position);
    const auto* target = std::get_if<local_position_t>(&resolved);
    if (!target) return {};
    const auto occupied = keyboard_entry_occupant(context.scope.chart, context.scope.measures, start.lane, *target);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&occupied)) return {.diagnostics = *failure};
    const auto linked = entry_stem_channel(context.history.document, context.scope.chart, start.sound);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&linked)) return {.diagnostics = *failure};
    const auto* occupant = std::get<const note_t*>(occupied);
    const auto stem = std::get<std::optional<object_id_t>>(linked);
    if (!occupant && !stem) return {};
    return {.value = keyboard_entry_plan_t{.lane = start.lane, .position = *target,
        .occupant = occupant ? std::optional{occupant->id} : std::nullopt, .stem = stem,
        .sound = start.sound, .is_displacing = start.is_displacing, .geometry = start.geometry}};
}

auto keyboard_entry_plan_t::apply(editor_edit_context_t& context) const -> editor_interaction_result_t {
    const auto* original = occupant ? context.scope.chart.find_note(*occupant) : nullptr;
    if (occupant && !original)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Lane-key entry target no longer exists", *occupant)}};
    auto staged = stage_keyboard_entry(context, lane, position, original, sound, is_displacing, stem, geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&staged)) return {.diagnostics = *failure};
    auto prepared = std::get<prepared_keyboard_entry_t>(std::move(staged));
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(prepared.document));
    return commit_editor_edit(context, transaction,
                              {.label = "Enter note from lane key",
                               .cursor = position,
                               .lane = lane,
                               .created_note = prepared.created});
}


// Materialization is private preparation. Authored and virtual requests run the
// same placement verb, then publish the prepared document as one history entry.

auto pending_semantic_layer_paint_gesture_t::revise(editor_edit_context_t& context,
    std::optional<editor_selection_object_t> target) -> diagnostic_list_t {
    if (!target) return {};
    if (target->kind != editor_selection_object_kind_t::note &&
        target->kind != editor_selection_object_kind_t::bgm_event)
        return {error(diagnostic_code_t::invalid_id,
            "semantic-layer paint must target one note or BGM event", target->id)};
    const bool is_bgm = target->kind == editor_selection_object_kind_t::bgm_event;
    const auto id = target->id;
    if (is_bgm ? !context.scope.chart.find_bgm_event(id) : !context.scope.chart.find_note(id))
        return {error(diagnostic_code_t::invalid_id,
            is_bgm ? "semantic-layer paint target does not identify a BGM event"
                   : "semantic-layer paint target does not identify a chart note", id)};
    if (visited.contains(id.value)) return {};
    if (!notes || !bgm_events || notes->size() + bgm_events->size() >= maximum_note_paint_targets)
        return {error(diagnostic_code_t::invalid_resolution,
            is_bgm ? "semantic-layer paint exceeds the per-gesture target cap"
                   : "semantic-layer paint exceeds the per-gesture note target cap")};
    auto& targets = is_bgm ? bgm_events : notes;
    auto expanded = std::make_shared<std::vector<object_id_t>>(*targets);
    expanded->push_back(id);
    targets = std::move(expanded);
    visited.insert(id.value);
    return {};
}

auto pending_semantic_layer_paint_gesture_t::begin(editor_edit_context_t& context,
    const begin_semantic_layer_paint_gesture_action_t& request)
    -> diagnosed_t<std::optional<pending_semantic_layer_paint_gesture_t>> {
    const auto& [layer_id, note_id, pointer_captured, replace_memberships, clear_memberships, bgm_event, mode] = request;
    if (!pointer_captured)
        return {.diagnostics = {error(diagnostic_code_t::invalid_hold,
                   "pointer capture is required to begin semantic-layer painting")}};
    const auto layer = std::ranges::find(context.scope.chart.semantic_layers, layer_id,
                                         &semantic_layer_t::id);
    if (!clear_memberships && (layer == context.scope.chart.semantic_layers.end() || !layer->visible))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint requires a visible chart layer")}};
    if (!clear_memberships && layer->locked)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                        "semantic-layer paint cannot modify a locked layer")}};
    if (static_cast<bool>(note_id) == bgm_event.has_value())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint must target one note or BGM event")}};
    if ((note_id && !context.scope.chart.find_note(note_id)) ||
        (bgm_event && !context.scope.chart.find_bgm_event(*bgm_event)))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "semantic-layer paint target is not in the chart", note_id ? note_id : *bgm_event)}};
    auto notes = std::make_shared<std::vector<object_id_t>>();
    auto bgm_events = std::make_shared<std::vector<object_id_t>>();
    if (note_id) notes->push_back(note_id);
    if (bgm_event) bgm_events->push_back(*bgm_event);
    pending_semantic_layer_paint_gesture_t pending{
        .layer = layer_id,
        .notes = std::move(notes),
        .bgm_events = std::move(bgm_events),
        .replace_memberships = replace_memberships,
        .clear_memberships = clear_memberships,
        .mode = mode,
    };
    pending.visited.reserve(64);
    pending.visited.insert(note_id ? note_id.value : bgm_event->value);
    return {.value = std::move(pending)};
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

auto pending_semantic_layer_paint_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto& pending = *this;
    if (!pending.notes || !pending.bgm_events || (pending.notes->empty() && pending.bgm_events->empty()))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  "semantic-layer paint gesture is malformed or stale")}};
    const auto layer = std::ranges::find(context.scope.chart.semantic_layers, pending.layer,
                                        &semantic_layer_t::id);
    if (!pending.clear_memberships &&
        (layer == context.scope.chart.semantic_layers.end() || !layer->visible || layer->locked))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_id,
                                  "semantic-layer paint requires an unlocked visible layer")}};
    const auto edit = semantic_paint_edit(state.chart, pending);
    return commit_editor_edit(context,
                              {.noop_policy = command_noop_policy_t::accept_without_diagnostics},
                              [&] { return apply_semantic_layer_edit(history, edit); });
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
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    const auto& hit_index = std::get<editor_hit_test_index_t>(built);
    const auto& available = hit_index.available_objects;
    const auto& selected = action.objects;
    const std::set unique(selected.begin(), selected.end());
    if (unique.size() != selected.size())
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::duplicate_id,
                                  "object selection contains a duplicate typed object")}};
    for (const auto& object : selected) {
        if (object.id.value == 0 || !std::ranges::binary_search(available, object))
            return editor_interaction_result_t{
                .diagnostics = {error(diagnostic_code_t::invalid_id,
                                      "object selection contains an unknown typed object",
                                      object.id)}};
    }
    if (action.request_audition &&
        (selected.size() != 1 ||
         (selected.front().kind != editor_selection_object_kind_t::note &&
          selected.front().kind != editor_selection_object_kind_t::bgm_event)))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_id,
                                  "audition selection requires exactly one note or BGM event")}};
    auto combined = combine_editor_area_selection(state, selected, action.mode, hit_index);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&combined))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    set_selected_objects(
        state, std::move(std::get<std::vector<editor_selection_object_t>>(combined)));
    auto result = editor_interaction_result_t::from(history, state);
    result.audition = audition_selected_object(action.request_audition, selected, state,
                                              history.document.revision);
    return result;
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
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::unknown_lane,
                                  "selected editor lane is unavailable at the cursor")}};
    state.active_lane = lane;
    return editor_interaction_result_t::from(history, state, false);
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

static auto arm_note_kind_impl(const document_history_t& history, editor_interaction_state_t& state,
                               note_kind_t kind) -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart || !is_note_kind_allowed(*chart, kind))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_binding,
                                  "Note kind is unavailable for this chart profile.")}};
    if (state.hold() || state.gesture<wormedit::pending_note_paint_gesture_t>())
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  "Finish or cancel note placement before changing kind.")}};
    state.armed_note_kind = kind;
    return editor_interaction_result_t::from(history, state);
}

auto arm_note_kind(const document_history_t& history, editor_interaction_state_t& state,
                   note_kind_t kind) -> editor_interaction_result_t {
    auto result = arm_note_kind_impl(history, state, kind);
    result.ensure_view(history, state);
    return result;
}

namespace {
enum class virtual_cursor_policy_t { materialized, allowed, selection };

struct editor_action_context_t {
    editor_edit_context_t edit;
    std::uint64_t absolute;
};

// Intentionally incomplete: every action declares its capability beside execution.
template <class Action> struct editor_action_handler_t;

template <> struct editor_action_handler_t<move_edit_cursor_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& context, const move_edit_cursor_action_t& typed_action)
        -> editor_interaction_result_t {
        interaction_detail::cursor_navigation_context_t cursor_navigation{context.edit, context.absolute};
        return interaction_detail::move_edit_cursor(cursor_navigation, typed_action);
    }
};

template <> struct editor_action_handler_t<page_edit_cursor_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& context, const page_edit_cursor_action_t& typed_action)
        -> editor_interaction_result_t {
        interaction_detail::cursor_navigation_context_t cursor_navigation{context.edit, context.absolute};
        return interaction_detail::page_edit_cursor(cursor_navigation, typed_action);
    }
};

template <> struct editor_action_handler_t<jump_edit_cursor_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& context, const jump_edit_cursor_action_t& typed_action)
        -> editor_interaction_result_t {
        interaction_detail::cursor_navigation_context_t cursor_navigation{context.edit, context.absolute};
        return interaction_detail::jump_edit_cursor(cursor_navigation, typed_action);
    }
};

template <> struct editor_action_handler_t<select_bms_canvas_measure_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& context, const select_bms_canvas_measure_action_t& typed_action)
        -> editor_interaction_result_t {
        interaction_detail::cursor_navigation_context_t cursor_navigation{context.edit, context.absolute};
        return interaction_detail::select_bms_canvas_measure(cursor_navigation, typed_action);
    }
};

template <> struct editor_action_handler_t<cycle_edit_snap_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const cycle_edit_snap_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return cycle_editor_snap(context.history, context.state, typed_action);
    }
};

template <> struct editor_action_handler_t<set_edit_snap_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const set_edit_snap_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return set_editor_snap(context.history, context.state, typed_action);
    }
};

template <> struct editor_action_handler_t<place_virtual_bms_hold_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const place_virtual_bms_hold_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return place_virtual_bms_hold(context, typed_action);
    }
};

template <> struct editor_action_handler_t<place_virtual_bms_tap_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const place_virtual_bms_tap_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return place_virtual_bms_tap(context, typed_action);
    }
};

template <> struct editor_action_handler_t<place_tap_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const place_tap_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (context.state.armed_note_kind == note_kind_t::hold ||
            context.state.armed_note_kind == note_kind_t::roll)
            return begin_gesture<hold_gesture_t>(context, hold_start_t::from(context.state, typed_action));
        return place_tap(context, typed_action);
    }
};

template <> struct editor_action_handler_t<begin_hold_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_hold_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<hold_gesture_t>(context, hold_start_t::from(context.state, typed_action));
    }
};

template <> struct editor_action_handler_t<update_hold_preview_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_hold_preview_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return revise_gesture<hold_gesture_t>(context, typed_action);
    }
};

template <> struct editor_action_handler_t<commit_hold_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const commit_hold_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return apply_gesture<hold_gesture_t>(context, editor_gesture_apply_view_policy_t::captured, typed_action);
    }
};

template <> struct editor_action_handler_t<begin_lane_key_hold_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const begin_lane_key_hold_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (const auto* holds = context.state.gesture<hold_gesture_t>(); holds && holds->is_keyboard())
            return revise_gesture<hold_gesture_t>(context, typed_action);
        return begin_gesture<hold_gesture_t>(context, hold_start_t::from(context.state, typed_action));
    }
};

template <> struct editor_action_handler_t<release_lane_key_hold_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const release_lane_key_hold_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!context.state.hold()) return editor_interaction_result_t::from(context.history, context.state);
        return apply_gesture<hold_gesture_t>(context, editor_gesture_apply_view_policy_t::captured, typed_action);
    }
};

template <> struct editor_action_handler_t<begin_alt_pointer_hold_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_alt_pointer_hold_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!typed_action.alt_or_option_down) {
            return editor_interaction_result_t{
                .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                      "Alt/Option must be held to begin a pointer hold gesture")}};
        }
        return begin_gesture<hold_gesture_t>(context, hold_start_t::from(context.state, typed_action));
    }
};

template <> struct editor_action_handler_t<update_alt_pointer_hold_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_alt_pointer_hold_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!typed_action.pointer_captured) {
            return cancel_pointer_gesture(context);
        }
        if (!typed_action.alt_or_option_down) {
            return dispatch_editor_action(
                context.history, context.state,
                cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::modifier_released});
        }
        return revise_gesture<hold_gesture_t>(context, typed_action);
    }
};

template <> struct editor_action_handler_t<release_alt_pointer_hold_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const release_alt_pointer_hold_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!typed_action.pointer_captured) {
            return cancel_pointer_gesture(context);
        }
        if (!typed_action.alt_or_option_down) {
            return dispatch_editor_action(
                context.history, context.state,
                cancel_editor_gesture_action_t{editor_gesture_cancel_reason_t::modifier_released});
        }
        return apply_gesture<hold_gesture_t>(context, editor_gesture_apply_view_policy_t::captured, typed_action);
    }
};

template <> struct editor_action_handler_t<begin_semantic_layer_paint_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_semantic_layer_paint_gesture_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<pending_semantic_layer_paint_gesture_t>(context, typed_action);
    }
};

template <> struct editor_action_handler_t<update_semantic_layer_paint_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_semantic_layer_paint_gesture_action_t& request)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!request.pointer_captured) return cancel_pointer_gesture(context);
        std::optional<editor_selection_object_t> target;
        if (request.bgm_event)
            target = editor_selection_object_t{editor_selection_object_kind_t::bgm_event, *request.bgm_event};
        else if (request.note || request.operation != proposal_operation_t::apply)
            target = editor_selection_object_t{editor_selection_object_kind_t::note, request.note};
        if (request.operation == proposal_operation_t::apply)
            return apply_gesture<pending_semantic_layer_paint_gesture_t>(context, target);
        return revise_gesture<pending_semantic_layer_paint_gesture_t>(context, target);
    }
};

template <> struct editor_action_handler_t<begin_editor_area_selection_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const begin_editor_area_selection_action_t& request)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<pending_editor_area_selection_t>(context, request);
    }
};

template <> struct editor_action_handler_t<update_editor_area_selection_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const update_editor_area_selection_action_t& request)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!request.pointer_captured) return cancel_pointer_gesture(context);
        if (request.operation == proposal_operation_t::apply)
            return apply_gesture<pending_editor_area_selection_t>(context, request.x_pixels, request.y_pixels);
        return revise_gesture<pending_editor_area_selection_t>(context, request.x_pixels, request.y_pixels);
    }
};

template <> struct editor_action_handler_t<cancel_editor_area_selection_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t&, const cancel_editor_area_selection_action_t&)
        -> editor_interaction_result_t {
        return {};
    }
};

template <> struct editor_action_handler_t<space_range_selection_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const space_range_selection_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (context.state.gesture<pending_space_range_selection_t>())
            return apply_gesture<pending_space_range_selection_t>(context, editor_gesture_apply_view_policy_t::captured);
        return begin_gesture<pending_space_range_selection_t>(context, action);
    }
};

template <> struct editor_action_handler_t<cancel_editor_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t&, const cancel_editor_gesture_action_t&)
        -> editor_interaction_result_t {
        return {};
    }
};

template <> struct editor_action_handler_t<select_editor_notes_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const select_editor_notes_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return select_editor_notes(context, typed_action);
    }
};

template <> struct editor_action_handler_t<select_editor_objects_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const select_editor_objects_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return select_editor_objects(context, typed_action);
    }
};

template <> struct editor_action_handler_t<delete_editor_selection_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::selection;

    static auto perform(editor_action_context_t& dispatch, const delete_editor_selection_action_t&)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return delete_editor_selection(context);
    }
};

template <> struct editor_action_handler_t<select_editor_lane_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const select_editor_lane_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return select_editor_lane(context.history, context.state, context.scope.chart,
                                  context.scope.measures, typed_action.lane);
    }
};

template <> struct editor_action_handler_t<assign_selected_objects_to_lane_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const assign_selected_objects_to_lane_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return assign_selected_objects_to_lane(context, typed_action);
    }
};

template <> struct editor_action_handler_t<adjust_editor_zoom_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& context, const adjust_editor_zoom_action_t& typed_action)
        -> editor_interaction_result_t {
        return adjust_editor_zoom(context.edit.history, context.edit.state, context.edit.scope.chart,
                                  context.edit.scope.measures, context.absolute, typed_action);
    }
};

template <> struct editor_action_handler_t<scroll_editor_view_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::allowed;

    static auto perform(editor_action_context_t& dispatch, const scroll_editor_view_action_t& typed_action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return scroll_editor_view(context, typed_action);
    }
};

template <> struct editor_action_handler_t<begin_note_duration_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_note_duration_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<pending_note_duration_gesture_t>(context, action.reference_note);
    }
};

template <> struct editor_action_handler_t<update_note_duration_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_note_duration_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (action.operation == proposal_operation_t::apply) {
            return apply_gesture<pending_note_duration_gesture_t>(context, action.reference_end);
        }
        return revise_gesture<pending_note_duration_gesture_t>(context, action.reference_end);
    }
};

template <> struct editor_action_handler_t<begin_note_move_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_note_move_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<pending_note_move_gesture_t>(context, action.reference_note, action.displace_to_bgm);
    }
};

template <> struct editor_action_handler_t<update_note_move_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_note_move_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (action.operation == proposal_operation_t::apply) {
            return apply_gesture<pending_note_move_gesture_t>(context, action.reference_position, action.reference_lane,
                                             action.to_bgm, action.preferred_bgm_lane,
                                             action.virtual_bgm_row);
        }
        return revise_gesture<pending_note_move_gesture_t>(context, action.reference_position, action.reference_lane,
                                        action.to_bgm, action.preferred_bgm_lane,
                                        action.virtual_bgm_row);
    }
};

template <> struct editor_action_handler_t<begin_bar_line_move_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch,
                        const begin_bar_line_move_gesture_action_t& action)
        -> editor_interaction_result_t {
        return begin_gesture<pending_bar_line_move_gesture_t>(dispatch.edit,
                                                               action.reference_bar_line);
    }
};

template <> struct editor_action_handler_t<update_bar_line_move_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch,
                        const update_bar_line_move_gesture_action_t& action)
        -> editor_interaction_result_t {
        if (action.operation == proposal_operation_t::apply)
            return apply_gesture<pending_bar_line_move_gesture_t>(dispatch.edit,
                                                                  action.reference_position);
        return revise_gesture<pending_bar_line_move_gesture_t>(dispatch.edit,
                                                               action.reference_position);
    }
};

template <> struct editor_action_handler_t<begin_note_paint_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const begin_note_paint_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        return begin_gesture<pending_note_paint_gesture_t>(context, action);
    }
};

template <> struct editor_action_handler_t<update_note_paint_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const update_note_paint_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!action.pointer_captured) return cancel_pointer_gesture(context);
        return revise_gesture<pending_note_paint_gesture_t>(context, action.lane, action.position, std::nullopt);
    }
};

template <> struct editor_action_handler_t<end_note_paint_gesture_action_t> {
    static constexpr auto virtual_cursor = virtual_cursor_policy_t::materialized;

    static auto perform(editor_action_context_t& dispatch, const end_note_paint_gesture_action_t& action)
        -> editor_interaction_result_t {
        auto& context = dispatch.edit;
        if (!action.pointer_captured) return cancel_pointer_gesture(context);
        return apply_gesture<pending_note_paint_gesture_t>(context, action.lane, action.position,
                                                           action.source_geometry);
    }
};

template <class Action>
concept editor_action_handler = requires(editor_action_context_t& context, const Action& action) {
    { editor_action_handler_t<Action>::virtual_cursor } -> std::convertible_to<virtual_cursor_policy_t>;
    { editor_action_handler_t<Action>::perform(context, action) } -> std::same_as<editor_interaction_result_t>;
};

template <class... Actions>
consteval auto has_editor_action_handlers(std::type_identity<std::variant<Actions...>>) -> bool {
    return (editor_action_handler<Actions> && ...);
}
static_assert(has_editor_action_handlers(std::type_identity<editor_interaction_action_t>{}));
static_assert(!editor_action_handler<std::monostate>);

struct editor_action_dispatch_t {
    document_history_t& history;
    editor_interaction_state_t& state;

    template <editor_action_handler Action, class Perform>
    auto with_context(const Action&, Perform perform) const
        -> std::invoke_result_t<Perform&, editor_action_context_t&> {
        using result_type = std::invoke_result_t<Perform&, editor_action_context_t&>;
        constexpr auto policy = editor_action_handler_t<Action>::virtual_cursor;
        const auto can_use_virtual_cursor = policy == virtual_cursor_policy_t::allowed ||
            (policy == virtual_cursor_policy_t::selection &&
             (!state.selected_objects.empty() || !state.selected_notes.empty()));
        if (state.virtual_bms_cursor_row && !can_use_virtual_cursor)
            return result_type{
                .diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                      "this edit requires a materialized cursor measure")}};
        const auto* chart = history.document.find_chart(state.chart);
        if (chart && !is_note_kind_allowed(*chart, state.armed_note_kind))
            state.armed_note_kind = note_kind_t::tap;
        const auto initial_view = make_view(history, state);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&initial_view))
            return result_type{.diagnostics = *diagnostics};
        const auto built_index = measure_index_t::from(*chart);
        const auto& index = built_index.value();
        editor_action_context_t context{
            {history, state, {*chart, index},
             &std::get<editor_interaction_view_model_t>(initial_view), history.document.revision},
            *state.cursor_canvas_row(index)};
        return perform(context);
    }
    template <editor_action_handler Action>
    auto operator()(const Action& action) const -> editor_interaction_result_t {
        return with_context(action, [&](editor_action_context_t& context) {
            return editor_action_handler_t<Action>::perform(context, action);
        });
    }
};
} // namespace

template <class Action, class Stage>
auto run_staged_editor_action(document_history_t& history, editor_interaction_state_t& state,
                              const Action& action, Stage stage)
    -> attempt_t<prepared_editor_edit_t> {
    if (auto stale = validate_gesture_capture(history, state))
        return {.diagnostics = std::move(stale->diagnostics)};
    if (auto rejected = validate_active_gesture_action(state, action))
        return {.diagnostics = std::move(rejected->diagnostics)};
    return editor_action_dispatch_t{history, state}.with_context(
        action, [&](editor_action_context_t& context) { return stage(context.edit); });
}

auto stage_editor_selection_delete(document_history_t& history, editor_interaction_state_t& state)
    -> attempt_t<prepared_editor_edit_t> {
    return run_staged_editor_action(
        history, state, delete_editor_selection_action_t{}, [&](editor_edit_context_t& context) {
            return prepare_editor_edit(context, [&](document_command_transaction_t& transaction) {
                return stage_editor_delete(context, transaction);
            });
        });
}

auto stage_editor_tap_placement(document_history_t& history, editor_interaction_state_t& state,
                                const place_tap_action_t& action)
    -> attempt_t<prepared_editor_edit_t> {
    return run_staged_editor_action(
        history, state, action,
        [&](editor_edit_context_t& context) -> attempt_t<prepared_editor_edit_t> {
            if (state.armed_note_kind == note_kind_t::hold ||
                state.armed_note_kind == note_kind_t::roll) {
                auto begun =
                    begin_gesture<hold_gesture_t>(context, hold_start_t::from(state, action));
                if (!begun)
                    return {.diagnostics = std::move(begun.diagnostics)};
                return {.value = prepared_editor_edit_t{.effect = {.has_document_changes = false}}};
            }
            return prepare_editor_edit(context, [&](document_command_transaction_t& transaction) {
                return stage_editor_tap(context, transaction, action);
            });
        });
}

auto dispatch_editor_action_impl(document_history_t& history, editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> editor_interaction_result_t {
    return std::visit(editor_action_dispatch_t{history, state}, action);
}

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

static auto select_editor_scope_impl(document_history_t& history, editor_interaction_state_t& state,
                                     editor_selection_scope_t scope, bool invert,
                                     std::optional<local_position_t> from)
    -> editor_interaction_result_t {
    const auto* chart = history.document.find_chart(state.chart);
    if (!chart)
        return editor_interaction_result_t{
            .diagnostics = {
                error(diagnostic_code_t::invalid_id, "Selection chart is unavailable")}};
    const auto built = build_editor_hit_test_index(*chart, history.document.revision);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    const auto& index = std::get<editor_hit_test_index_t>(built);
    if (index.measures.total_rows == 0)
        return editor_interaction_result_t{
            .diagnostics = {
                error(diagnostic_code_t::row_out_of_range, "Selection chart has no rows")}};
    const auto first_row = from ? index.measures.row_of(*from) : std::optional<std::uint64_t>{0};
    if (!first_row)
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                  "Selection start is outside the chart")}};
    const auto queried = editor_objects_in_row_range(*chart, index, *first_row, index.measures.total_rows - 1, scope);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&queried))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    return dispatch_editor_action(history, state, select_editor_objects_action_t{
        scoped_selection(state, std::get<std::vector<editor_selection_object_t>>(queried), scope, invert)});
}

auto select_editor_scope(document_history_t& history, editor_interaction_state_t& state,
                         editor_selection_scope_t scope, bool invert,
                         std::optional<local_position_t> from) -> editor_interaction_result_t {
    auto result = select_editor_scope_impl(history, state, scope, invert, from);
    result.ensure_view(history, state);
    return result;
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

auto editor_shift_selection(const editor_interaction_state_t& state, std::int64_t rows)
    -> shift_selection_action_t {
    shift_selection_action_t action{.chart = state.chart, .notes = editor_quantize_selection(state).notes,
                                    .bgm_events = {}, .rows = rows, .bar_lines = {},
                                    .bar_line_time = state.bar_line_time};
    for (const auto& object : editor_selection_targets(state))
        if (object.kind == editor_selection_object_kind_t::bar_line)
            action.bar_lines.push_back(measure_id_t{object.id.value});
    return action;
}

} // namespace wormedit

namespace wormedit {

auto stage_assign_note_lanes(document_command_transaction_t& transaction,
                             const editor_interaction_state_t& state, lane_id_t lane)
    -> attempt_t<document_edit_effect_t> {
    return transaction.stage(
        state.chart, "lane assignment chart is unavailable",
        [&](chart_document_t& chart, song_document_t&) -> attempt_t<document_edit_effect_t> {
            auto index = measure_index_t::from(chart);
            if (!index)
                return {.diagnostics = index.error()};
            const chart_scope_t scope{.chart = chart, .measures = *index};
            auto collected = collect_assignment_candidates(scope, state);
            if (const auto* failure = std::get_if<diagnostic_list_t>(&collected))
                return {.diagnostics = *failure};
            const auto& candidates = std::get<std::vector<assignment_candidate_t>>(collected);
            auto intervals = build_assignment_intervals(scope, candidates, lane);
            if (const auto* failure = std::get_if<diagnostic_list_t>(&intervals))
                return {.diagnostics = *failure};
            if (const auto failure = validate_assignment_intervals(
                    std::get<std::vector<assignment_interval_t>>(intervals)))
                return {.diagnostics = {*failure}};
            if (std::ranges::all_of(candidates, [lane](const auto& candidate) {
                    return candidate.note->lane == lane;
                }))
                return {.value = document_edit_effect_t{}};
            return stage_note_lane_assignment(transaction, state.chart, candidates, lane,
                                              "Assign selected notes to lane");
        });
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
