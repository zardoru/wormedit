#include "interaction_gesture_runner.h"
#include "chart_object_ids.h"
#include "interaction_internal.h"
namespace wormedit::interaction_detail {
auto is_plain_editable_note(const note_t& note) -> bool {
    const auto plain_kind = (note.kind == note_kind_t::tap && !note.end.has_value()) ||
                            (note.kind == note_kind_t::hold && note.end.has_value());
    return plain_kind && !note.sound.has_value() && note.semantic_layers.empty() &&
           !note.label.has_value() && !note.branch.has_value();
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto is_removable_sound_note(const chart_document_t& chart, const note_t& note) -> bool {
    if (!note.sound || note.sound_channel_event || note.label || note.branch)
        return false;
    return std::ranges::all_of(note.semantic_layers, [&](layer_id_t id) {
        const auto layer = std::ranges::find(chart.semantic_layers, id, &semantic_layer_t::id);
        return layer != chart.semantic_layers.end() && layer->visible && !layer->locked;
    });
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_hold_rows(const chart_document_t& chart, const measure_index_t& index, lane_id_t lane,
                    std::uint64_t anchor_row, std::uint64_t moving_row,
                    hold_overlap_policy_t overlap_policy)
    -> std::variant<hold_row_plan_t, diagnostic_list_t> {
    hold_row_expansion_t expansion{.chart = chart,
                                   .index = index,
                                   .lane = lane,
                                   .begin_row = std::min(anchor_row, moving_row),
                                   .end_row = std::max(anchor_row, moving_row),
                                   .removals = {},
                                   .overlap_policy = overlap_policy};
    while (true) {
        auto result = expansion.expand_once();
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&result))
            return *diagnostics;
        if (!std::get<bool>(result))
            break;
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
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto canvas_keyboard_hold_previews(const chart_document_t& chart, const measure_index_t& index,
                                   std::uint64_t canvas_rows, std::uint64_t revision,
                                   const editor_interaction_state_t& state)
    -> std::variant<std::vector<editor_interaction_view_model_t::virtual_hold_preview_t>,
                    diagnostic_list_t> {
    std::vector<editor_interaction_view_model_t::virtual_hold_preview_t> previews;
    const auto additional = state.additional_keyboard_holds();
    auto pending = std::vector<pending_hold_t>{additional.begin(), additional.end()};
    if (state.hold())
        pending.push_back(*state.hold());
    for (const auto& hold : pending) {
        if (!hold.anchor_canvas_row && !hold.preview_canvas_row)
            continue;
        const auto anchor =
            hold.anchor_canvas_row ? hold.anchor_canvas_row : index.row_of(hold.position);
        const auto end = hold.preview_canvas_row ? hold.preview_canvas_row : anchor;
        if (!has_chart_virtual_tail(chart) || !anchor || !end || *anchor >= canvas_rows || *end >= canvas_rows) {
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           "canvas lane hold is stale or outside BMS geometry")};
        }
        if (!state.gesture_stamp<hold_gesture_t>()->is_revision_current(revision))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           stale_action_message("Editor gesture"))};
        auto planned = plan_hold_rows(chart, index, hold.lane, *anchor, *end);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
            return *diagnostics;
        auto rows = std::get<hold_row_plan_t>(std::move(planned));
        previews.push_back(
            {.anchor_row = *anchor,
             .end_row = *end,
             .head_row = rows.head_row,
             .tail_row = rows.head_row == rows.tail_row ? std::optional<std::uint64_t>{}
                                                        : std::optional{rows.tail_row},
             .lane = hold.lane,
             .document_revision = revision,
             .removals = std::make_shared<const std::vector<object_id_t>>(std::move(rows.removals)),
             .adds_note = rows.adds_note});
    }
    return previews;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto next_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    // Temporary: removed by T-642 when interaction callers use the shared allocation query.
    return next_chart_object_id(chart).id;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {

} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto resolve_row_placement(const chart_document_t& chart, const measure_index_t& index,
                           const editor_interaction_state_t& state, lane_id_t lane,
                           local_position_t position)
    -> std::variant<local_position_t, diagnostic_list_t> {
    const auto target =
        snapped_position(chart, index, position, edit_snap_denominator(chart, state));
    if (!target)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "placement target cannot be snapped inside chart geometry")};
    if (!find_active_lane(chart, index, *target, lane))
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "placement lane is unavailable at the snapped row")};
    return *target;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<std::vector<object_id_t>>) -> std::vector<object_id_t> {
    return removals;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<hold_preview_removals_t>) -> hold_preview_removals_t {
    return std::make_shared<const std::vector<object_id_t>>(std::move(removals));
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto held_note_kind(note_kind_t armed) -> note_kind_t {
    return armed == note_kind_t::roll || armed == note_kind_t::fake ? armed : note_kind_t::hold;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
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
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto paint_note_intervals(const chart_document_t& chart, const measure_index_t& index)
    -> std::vector<paint_note_interval_t> {
    std::size_t count{};
    for (const auto& measure : chart.measures)
        count += measure.notes.size();
    std::vector<paint_note_interval_t> intervals;
    intervals.reserve(count);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto begin = index.row_of(note.position);
            const auto end = note.end ? index.row_of(*note.end) : begin;
            if (begin && end)
                intervals.push_back(
                    {note.lane, *begin, *end, note.id, note.kind, note.end.has_value()});
        }
    }
    std::ranges::sort(intervals, [](const auto& left, const auto& right) {
        if (left.lane != right.lane)
            return left.lane.value < right.lane.value;
        if (left.begin != right.begin)
            return left.begin < right.begin;
        if (left.end != right.end)
            return left.end < right.end;
        return left.note.value < right.note.value;
    });
    return intervals;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto paint_rows(const measure_index_t& index, std::span<const note_paint_target_t> targets)
    -> std::variant<std::vector<paint_row_t>, diagnostic_list_t> {
    std::vector<paint_row_t> rows;
    rows.reserve(targets.size());
    for (const auto& target : targets) {
        const auto row = index.row_of(target.position);
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
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto erase_active_paint_notes(std::map<std::uint64_t, paint_note_interval_t>& active,
                              note_paint_plan_t& plan) -> void {
    while (!active.empty()) {
        const auto id = active.begin()->first;
        plan.removals.push_back(object_id_t{id});
        active.erase(active.begin());
    }
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto process_draw_paint_row(const paint_row_t& row, note_paint_plan_t& plan,
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
    return error(diagnostic_code_t::collision, "paint gesture overlaps a non-tap note",
                 active.begin()->second.note);
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_note_paint(const chart_document_t& chart, const measure_index_t& index,
                     std::span<const note_paint_target_t> targets, note_kind_t kind,
                     note_paint_intent_t intent)
    -> std::variant<note_paint_plan_t, diagnostic_list_t> {
    auto intervals = paint_note_intervals(chart, index);
    auto rows_result = paint_rows(index, targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&rows_result))
        return *diagnostics;
    const auto& rows = std::get<std::vector<paint_row_t>>(rows_result);
    note_paint_plan_t plan;
    plan.additions.reserve(rows.size());
    plan.removals.reserve(rows.size());
    paint_overlap_sweep_t sweep{.intervals = intervals,
                                .next_interval = 0,
                                .lane = std::nullopt,
                                .ending = {},
                                .active = {},
                                .current_intent = intent};
    for (const auto& row : rows) {
        if (auto failure = sweep.process(row, plan, kind))
            return diagnostic_list_t{*failure};
    }
    for (const auto& addition : plan.additions) {
        const auto row = index.row_of(addition.position);
        if (row && is_head_blocked_by_warp(chart, index, *row, kind))
            return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                           "playable note heads cannot sit inside a warp")};
    }
    return plan;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
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
} // namespace wormedit::interaction_detail

namespace wormedit {
using namespace interaction_detail;
auto validate_virtual_note_target(const editor_edit_context_t& context, lane_id_t lane,
                                  std::string_view description)
    -> std::optional<editor_interaction_result_t> {
    if (has_chart_virtual_tail(context.scope.chart) && lane &&
        context.state.active_snap_denominator != 0 &&
        context.scope.chart.divisions_per_standard_measure %
                context.state.active_snap_denominator ==
            0) {
        return std::nullopt;
    }
    return diagnostic_result(
        context.history, context.state,
        {error(diagnostic_code_t::invalid_resolution,
               std::string(description) + " requires a BMS chart and valid snap")});
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto extend_virtual_chart(chart_document_t& chart, std::uint64_t measure_count)
    -> std::optional<diagnostic_list_t> {
    const auto required =
        measure_count > chart.measures.size() ? measure_count - chart.measures.size() : 0;
    std::uint64_t last_measure_id{};
    for (const auto& measure : chart.measures)
        last_measure_id = std::max(last_measure_id, measure.id.value);
    if (required > std::numeric_limits<std::uint64_t>::max() - last_measure_id)
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "virtual measure ID sequence is exhausted")};
    chart.measures.reserve(measure_count);
    for (std::size_t added = 0; added < required; ++added) {
        chart.measures.push_back(
            {.id = measure_id_t{++last_measure_id}, .definition = {{1, 1}, meter_t{4, 4}}});
    }
    return std::nullopt;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto snap_virtual_row(const editor_edit_context_t& context, const virtual_canvas_t& canvas,
                      std::uint64_t row) -> std::optional<std::uint64_t> {
    if (row >= canvas.total_rows)
        return std::nullopt;
    return snap_canvas_row(context.scope.chart, canvas.authored_index, row,
                           edit_snap_denominator(context.scope.chart, context.state),
                           canvas.total_rows);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto resolve_virtual_hold_rows(const editor_edit_context_t& context,
                               const place_virtual_bms_hold_action_t& action)
    -> std::variant<virtual_hold_rows_t, diagnostic_list_t> {
    const auto built_canvas = build_virtual_canvas(context.scope.chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas))
        return *diagnostics;
    const auto& canvas = std::get<virtual_canvas_t>(built_canvas);
    const auto anchor = snap_virtual_row(context, canvas, action.anchor_canvas_row);
    const auto end = snap_virtual_row(context, canvas, action.end_canvas_row);
    const auto anchor_measure = anchor ? canvas.measure_containing(*anchor) : std::nullopt;
    const auto end_measure = end ? canvas.measure_containing(*end) : std::nullopt;
    if (!anchor_measure || !end_measure ||
        anchor_measure->number >= virtual_tail_measure_limit(context.scope.chart) ||
        end_measure->number >= virtual_tail_measure_limit(context.scope.chart)) {
        return diagnostic_list_t{
            error(diagnostic_code_t::row_out_of_range, "virtual hold endpoints are unavailable")};
    }
    return virtual_hold_rows_t{*anchor, *end,
                               std::max(anchor_measure->number, end_measure->number) + 1};
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto publish_row_placement(editor_edit_context_t& context, document_history_t& scratch,
                           const editor_interaction_state_t& staged,
                           editor_interaction_result_t result) -> editor_interaction_result_t {
    if (!result)
        return diagnostic_result(context.history, context.state, result.diagnostics);
    auto transaction = document_command_transaction_t::from(context.history);
    transaction.adopt_document(std::move(scratch.document));
    const auto command = transaction.commit("Place note in virtual BMS measure");
    if (!command)
        return diagnostic_result(context.history, context.state, command.diagnostics);
    if (auto* holds = context.state.gesture<hold_gesture_t>()) {
        holds->discard_primary();
        if (holds->has_pending()) refresh_gesture_stamp<hold_gesture_t>(context);
        else context.state.clear();
    }
    restore_cursor_pose(context.state, capture_cursor_pose(staged));
    context.state.active_lane = staged.active_lane;
    auto published = editor_interaction_result_t::from(context.history, context.state, true);
    published.created_note = result.created_note;
    return published;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto commit_virtual_hold(editor_edit_context_t& context,
                         const place_virtual_bms_hold_action_t& action,
                         const virtual_hold_rows_t& rows, const pending_hold_t* captured) -> editor_interaction_result_t {
    auto scratch = document_history_t::from(context.history.document);
    auto* chart = scratch.document.find_chart(context.state.chart);
    if (const auto failure = extend_virtual_chart(*chart, rows.measure_count))
        return editor_interaction_result_t{.diagnostics = *failure};
    const auto built = measure_index_t::from(*chart);
    if (!built)
        return editor_interaction_result_t{.diagnostics = built.error()};
    const auto& index = *built;
    const auto anchor = position_at(index, rows.anchor);
    const auto end = position_at(index, rows.end);
    if (!anchor || !end)
        return diagnostic_result(
            context.history, context.state,
            {error(diagnostic_code_t::row_out_of_range, "virtual hold endpoints are unavailable")});
    auto staged = context.state;
    staged.virtual_bms_cursor_row.reset();
    if (!captured) captured = context.state.hold();
    auto pending = captured ? *captured : pending_hold_t{.position = *anchor,
        .lane = action.lane, .sound = action.sound, .kind = staged.armed_note_kind,
        .layers = staged.armed_note_layers};
    staged.clear();
    pending.position = *anchor;
    pending.anchor_canvas_row.reset();
    pending.preview_canvas_row.reset();
    pending.gesture_source.reset();
    editor_edit_context_t placement{scratch, staged, {*chart, index}};
    auto result = pending.apply(placement, *end, action.source_geometry);
    if (!result) return result;
    return publish_row_placement(context, scratch, staged, std::move(result));
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto place_virtual_bms_hold(editor_edit_context_t& context,
                            const place_virtual_bms_hold_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual hold"))
        return *invalid;
    auto rows = resolve_virtual_hold_rows(context, action);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&rows))
        return diagnostic_result(context.history, context.state, *diagnostics);
    auto result = commit_virtual_hold(context, action, std::get<virtual_hold_rows_t>(rows));
    if (!result) return diagnostic_result(context.history, context.state, std::move(result.diagnostics));
    return result;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto resolve_virtual_tap_target(const editor_edit_context_t& context,
                                const place_virtual_bms_tap_action_t& action)
    -> std::variant<virtual_tap_target_t, diagnostic_list_t> {
    const auto built_canvas = build_virtual_canvas(context.scope.chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built_canvas))
        return *diagnostics;
    const auto& canvas = std::get<virtual_canvas_t>(built_canvas);
    if (action.canvas_row >= canvas.total_rows)
        return diagnostic_list_t{
            error(diagnostic_code_t::row_out_of_range, "virtual tap lies outside #000-#999")};
    const auto snapped = snap_virtual_row(context, canvas, action.canvas_row);
    if (!snapped)
        return diagnostic_list_t{
            error(diagnostic_code_t::row_out_of_range, "virtual snap row is unavailable")};
    const auto row = *snapped;
    const auto destination = canvas.measure_containing(row);
    if (!destination || destination->authored ||
        destination->number >= virtual_tail_measure_limit(context.scope.chart) ||
        context.scope.measures.entries.empty() ||
        find_active_lane(context.scope.chart, context.scope.measures,
                         {context.scope.measures.entries.back().id,
                          context.scope.measures.entries.back().row_capacity - 1},
                         action.lane) == nullptr) {
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "virtual tap cannot use this measure or lane")};
    }
    return virtual_tap_target_t{row, destination->number, destination->begin_row};
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto validate_virtual_tap_sample(const editor_edit_context_t& context,
                                 std::optional<resource_id_t> sample)
    -> std::optional<editor_interaction_result_t> {
    if (!sample)
        return std::nullopt;
    const auto resource =
        std::ranges::find(context.history.document.resources, *sample, &resource_entry_t::id);
    if (resource != context.history.document.resources.end() &&
        std::ranges::find(resource->roles, resource_role_t::sample) != resource->roles.end())
        return std::nullopt;
    return diagnostic_result(
        context.history, context.state,
        {error(diagnostic_code_t::invalid_id, "virtual tap sample is unavailable")});
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto place_virtual_bms_tap(editor_edit_context_t& context,
                           const place_virtual_bms_tap_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual tap"))
        return *invalid;
    if (auto invalid = validate_virtual_tap_sample(context, action.sample))
        return *invalid;
    const auto target = resolve_virtual_tap_target(context, action);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&target))
        return diagnostic_result(context.history, context.state, *failure);
    const auto resolved = std::get<virtual_tap_target_t>(target);
    auto scratch = document_history_t::from(context.history.document);
    auto* chart = scratch.document.find_chart(context.state.chart);
    if (const auto failure = extend_virtual_chart(*chart, resolved.measure + 1))
        return diagnostic_result(context.history, context.state, *failure);
    auto staged = context.state;
    staged.active_lane = action.lane;
    staged.virtual_bms_cursor_row.reset();
    const local_position_t position{chart->measures[resolved.measure].id,
                                    resolved.row - resolved.measure_begin_row};
    const auto result = dispatch_editor_action(
        scratch, staged, place_tap_action_t{position, action.sample, action.source_geometry});
    if (!result)
        return diagnostic_result(context.history, context.state, result.diagnostics);
    if (staged.hold()) {
        auto holds = std::move(*staged.gesture<hold_gesture_t>());
        auto& pending = *holds.primary();
        pending.position = context.state.cursor;
        pending.anchor_canvas_row = resolved.row;
        pending.preview_canvas_row = resolved.row;
        capture_gesture(context, std::move(holds));
        context.state.active_lane = action.lane;
        return editor_interaction_result_t::from(context.history, context.state, false);
    }
    return publish_row_placement(context, scratch, staged, result);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto cycle_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                       const cycle_edit_snap_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id, "unknown snap direction")});
    const auto found =
        std::ranges::find(state.supported_snap_denominators, state.active_snap_denominator);
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
    return editor_interaction_result_t::from(history, state, false);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto set_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                     const set_edit_snap_action_t& action) -> editor_interaction_result_t {
    const auto supported = std::ranges::find(state.supported_snap_denominators, action.denominator);
    if (supported != state.supported_snap_denominators.end() &&
        action.denominator != state.active_snap_denominator) {
        state.active_snap_denominator = action.denominator;
        state.active_snap_was_fallback = false;
    }
    return editor_interaction_result_t::from(history, state, false);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto adjust_editor_zoom(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index,
                        std::uint64_t absolute, const adjust_editor_zoom_action_t& action)
    -> editor_interaction_result_t {
    if (!std::isfinite(action.factor) || action.factor <= 0.0)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "editor zoom factor must be finite and positive")});
    const auto current = state.pixels_per_standard_measure;
    const auto next = std::clamp(current * action.factor, minimum_pixels_per_standard_measure,
                                 maximum_pixels_per_standard_measure);
    auto anchor_y = static_cast<double>(absolute) * current /
                        static_cast<double>(chart.divisions_per_standard_measure) -
                    state.viewport_scroll_pixels;
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
    const auto unclamped =
        absolute_rows * next / static_cast<double>(chart.divisions_per_standard_measure) - anchor_y;
    const auto maximum = maximum_viewport_scroll(chart, index, next, state.viewport_height_pixels);
    const auto scroll =
        maximum
            ? std::clamp(unclamped, minimum_viewport_scroll(state.viewport_height_pixels), *maximum)
            : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(scroll))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_resolution,
                                        "editor zoom would overflow viewport coordinates")});
    auto proposed = state;
    set_cursor_scale(proposed, next);
    set_viewport_scroll_pixels(proposed, scroll);
    if (!synchronize_editor_cursor_to_viewport(chart, index, proposed))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_resolution, "zoom cannot resolve the fixed cursor")});
    state = std::move(proposed);
    return editor_interaction_result_t::from(history, state, false);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto place_tap(editor_edit_context_t& context, const place_tap_action_t& action)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                state.active_lane, action.position);
    const auto* target = std::get_if<local_position_t>(&resolved);
    if (!target)
        return diagnostic_result(context.history, state, std::get<diagnostic_list_t>(resolved));
    const std::array targets{note_paint_target_t{state.active_lane, *target}};
    const auto planned = plan_note_paint(context.scope.chart, context.scope.measures, targets,
                                         state.armed_note_kind, note_paint_intent_t::toggle);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *failure);
    const auto& plan = std::get<note_paint_plan_t>(planned);
    auto prepared =
        prepare_note_paint_document(context.history, state.chart, plan, state.armed_note_kind,
                                    action.sound, state.armed_note_layers, action.source_geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&prepared))
        return diagnostic_result(context.history, state, *failure);
    auto document = std::get<prepared_note_paint_t>(std::move(prepared));
    const auto created = document.painted_notes.empty()
                             ? std::optional<object_id_t>{}
                             : std::optional{document.painted_notes.front()};
    auto transaction = document_command_transaction_t::from(context.history);
    transaction.adopt_document(std::move(document.document));
    const auto command = transaction.commit(created ? "Place note" : "Remove note");
    if (!command)
        return diagnostic_result(context.history, state, command.diagnostics);
    place_cursor_at(state, *target);
    auto result = editor_interaction_result_t::from(context.history, state, true);
    result.created_note = created;
    return result;
}
} // namespace wormedit
