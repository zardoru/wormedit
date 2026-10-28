#include "interaction_edit_commit.h"
#include "interaction_gesture_runner.h"
#include "chart_object_ids.h"
#include "interaction_internal.h"
#include <wormedit/core/interaction_cursor_pose.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/measure_editor.h>
namespace wormedit::interaction_detail {
auto is_plain_editable_note(const chart_document_t& chart, const note_t& note) -> bool {
    const auto plain_kind = (note.kind == note_kind_t::tap && !note.end.has_value()) ||
                            (note.kind == note_kind_t::hold && note.end.has_value());
    return plain_kind && !note.sound && !note.sound_channel_event && !note.label &&
           is_keyboard_entry_editable(chart, note);
}

auto is_removable_sound_note(const chart_document_t& chart, const note_t& note) -> bool {
    const auto ordinary = (note.kind == note_kind_t::tap && !note.end) ||
                          (note.kind == note_kind_t::hold && note.end);
    return ordinary && note.sound && !note.sound_channel_event && !note.label &&
           is_keyboard_entry_editable(chart, note);
}

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
        if (hold.requires_span && anchor == end) continue;
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

auto next_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    // Temporary: removed by T-642 when interaction callers use the shared allocation query.
    return next_chart_object_id(chart).id;
}

auto resolve_row_placement(const chart_document_t& chart, const measure_index_t& index,
                           const editor_interaction_state_t& state, lane_id_t lane,
                           local_position_t position)
    -> std::variant<local_position_t, diagnostic_list_t> {
    const auto target =
        snap_position(chart, index, position, edit_snap_grid(chart, state));
    if (!target)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "placement target cannot be snapped inside chart geometry")};
    if (!find_active_lane(chart, index, *target, lane))
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "placement lane is unavailable at the snapped row")};
    return *target;
}

auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<std::vector<object_id_t>>) -> std::vector<object_id_t> {
    return removals;
}

auto make_hold_removals(std::vector<object_id_t> removals,
                        std::type_identity<hold_preview_removals_t>) -> hold_preview_removals_t {
    return std::make_shared<const std::vector<object_id_t>>(std::move(removals));
}

auto held_note_kind(note_kind_t armed) -> note_kind_t {
    return armed == note_kind_t::roll || armed == note_kind_t::fake ? armed : note_kind_t::hold;
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

auto stage_hold_note_consumption(document_command_transaction_t& transaction, chart_id_t chart_id,
                                 std::span<const object_id_t> removals) -> diagnostic_list_t {
    const auto* chart = transaction.read_chart(chart_id);
    std::vector<object_id_t> keysounds;
    for (const auto id : removals) {
        const auto* note = chart ? chart->find_note(id) : nullptr;
        if (!note)
            return {error(diagnostic_code_t::invalid_id, "Consumed hold note no longer exists", id)};
        // osu! custom samples are hitsounds too: they disappear with the covered note.
        if (note->sound && !note->osu_hitsample)
            keysounds.push_back(id);
    }
    if (keysounds.empty()) return {};
    auto moved = stage_move_notes_to_bgm(transaction, chart_id, keysounds, std::nullopt);
    return std::move(moved.diagnostics);
}

auto paint_rows(const measure_index_t& index, std::span<const note_paint_target_t> targets)
    -> std::variant<std::vector<paint_row_t>, diagnostic_list_t> {
    std::vector<paint_row_t> rows;
    rows.reserve(targets.size());
    for (const auto& target : targets) {
        const auto row = target.absolute_row(index);
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

auto erase_active_paint_notes(std::map<std::uint64_t, lane_occupant_t>& active,
                              note_paint_plan_t& plan) -> void {
    while (!active.empty()) {
        const auto id = active.begin()->first;
        plan.removals.push_back(object_id_t{id});
        active.erase(active.begin());
    }
}

auto process_draw_paint_row(const paint_row_t& row, note_paint_plan_t& plan,
                            const std::map<std::uint64_t, lane_occupant_t>& active,
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
    if (note.kind == note_kind_t::hold && note.has_end) {
        plan.removals.push_back(note.note);
        return std::nullopt;
    }
    if (note.kind == kind && !note.has_end && note.begin == row.row && note.end == row.row)
        return std::nullopt;
    return error(diagnostic_code_t::collision, "paint gesture overlaps a non-tap note",
                 active.begin()->second.note);
}

auto note_paint_plan_t::plan_hold_consumption(const chart_document_t& chart) -> diagnostic_list_t {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.kind != note_kind_t::hold || !note.end ||
                !std::ranges::binary_search(removals, note.id)) continue;
            if (!is_plain_editable_note(chart, note) && !is_removable_sound_note(chart, note))
                return {error(diagnostic_code_t::collision,
                    "note placement overlaps a non-editable hold", note.id)};
            consumed_holds.push_back(note.id);
        }
    }
    return {};
}

auto plan_note_paint(const chart_document_t& chart, const measure_index_t& index,
                     std::span<const note_paint_target_t> targets, note_kind_t kind,
                     note_paint_intent_t intent)
    -> std::variant<note_paint_plan_t, diagnostic_list_t> {
    const auto occupancy = lane_occupancy_t::from(chart, index);
    const auto& intervals = occupancy.entries();
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
    std::ranges::sort(plan.removals);
    const auto duplicates = std::ranges::unique(plan.removals);
    plan.removals.erase(duplicates.begin(), duplicates.end());
    if (intent != note_paint_intent_t::erase) {
        auto diagnostics = plan.plan_hold_consumption(chart);
        if (!diagnostics.empty()) return diagnostics;
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
} // namespace wormedit::interaction_detail

namespace wormedit {
using namespace interaction_detail;
auto resolve_note_paint_targets(const chart_document_t& chart, std::span<const note_paint_target_t> targets)
    -> attempt_t<std::vector<note_paint_target_t>> {
    const auto index = measure_index_t::from(chart);
    if (!index) return {.diagnostics = index.error()};
    std::vector<note_paint_target_t> resolved;
    resolved.reserve(targets.size());
    for (const auto& target : targets) {
        const auto position = target.canvas_row ? position_at(*index, *target.canvas_row)
            : std::optional{target.position};
        if (!position || !index->row_of(*position)) return {.diagnostics = {error(diagnostic_code_t::row_out_of_range,
            "paint target is outside materialized chart geometry")}};
        resolved.push_back({.lane = target.lane, .position = *position});
    }
    return {.value = std::move(resolved)};
}

auto materialize_note_paint_targets(chart_document_t& chart, std::span<const note_paint_target_t> targets)
    -> attempt_t<std::vector<note_paint_target_t>> {
    std::optional<std::uint64_t> last_row;
    for (const auto& target : targets)
        if (target.canvas_row) last_row = std::max(last_row.value_or(0), *target.canvas_row);
    if (last_row && !extend_chart_to_row(chart, *last_row))
        return {.diagnostics = {error(diagnostic_code_t::invalid_measure, "paint extent exceeds chart limits")}};
    return resolve_note_paint_targets(chart, targets);
}

auto stage_scoped_note_paint(document_command_transaction_t& transaction, chart_id_t chart,
    const note_paint_plan_t& plan, const note_t& defaults) -> attempt_t<std::vector<object_id_t>> {
    auto diagnostics = stage_hold_note_consumption(transaction, chart, plan.consumed_holds);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    const auto* source = transaction.read_chart(chart);
    const auto built = source ? build_virtual_canvas(*source) : std::variant<virtual_canvas_t, diagnostic_list_t>{diagnostic_list_t{}};
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    if (!canvas) return {.diagnostics = {error(diagnostic_code_t::unknown_measure, "paint canvas is unavailable")}};
    auto count = source->measures.size();
    for (const auto& target : plan.additions) {
        if (!target.canvas_row) continue;
        const auto measure = canvas->measure_containing(*target.canvas_row);
        if (!measure) return {.diagnostics = {error(diagnostic_code_t::row_out_of_range, "paint target leaves the virtual canvas")}};
        count = std::max(count, measure->number + 1);
    }
    if (count > source->measures.size()) {
        auto extended = transaction.stage_bms_measure_extent(chart, count);
        if (!extended.value || !extended.ok()) return {.diagnostics = std::move(extended.diagnostics)};
    }
    auto resolved = resolve_note_paint_targets(*transaction.read_chart(chart), plan.additions);
    if (!resolved.value) return {.diagnostics = std::move(resolved.diagnostics)};
    std::vector<bms_native_event_t> additions;
    for (const auto& addition : *resolved.value) {
        auto note = defaults;
        note.position = addition.position;
        note.lane = addition.lane;
        additions.push_back(std::move(note));
    }
    auto removals = plan.removals;
    const auto* remaining = transaction.read_chart(chart);
    std::erase_if(removals, [&](object_id_t id) { return !remaining->find_note(id); });
    return transaction.stage_bms_native_events(chart, removals, additions,
        additions.empty() ? "Remove notes" : "Paint notes");
}

constexpr auto virtual_row_placement_label = "Place note in virtual BMS measure";

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
    return editor_interaction_result_t{
        .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                              std::string(description) + " requires a BMS chart and valid snap")}};
}

auto extend_virtual_chart(chart_document_t& chart, std::uint64_t measure_count)
    -> std::optional<diagnostic_list_t> {
    const auto required =
        measure_count > chart.measures.size() ? measure_count - chart.measures.size() : 0;
    if (required == 0) return std::nullopt;
    const auto first = id_ceiling_t<measure_id_t>::from(chart).next(static_cast<std::size_t>(required));
    if (!first)
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "virtual measure ID sequence is exhausted")};
    chart.measures.reserve(measure_count);
    for (std::size_t added = 0; added < required; ++added) {
        chart.measures.push_back(
            {.id = measure_id_t{first->value + added}, .definition = {{1, 1}, meter_t{4, 4}}});
    }
    return std::nullopt;
}

auto snap_virtual_row(const editor_edit_context_t& context, const virtual_canvas_t& canvas,
                      std::uint64_t row) -> std::optional<std::uint64_t> {
    if (row >= canvas.total_rows)
        return std::nullopt;
    return snap_canvas_row(context.scope.chart, canvas.authored_index, row,
                           edit_snap_grid(context.scope.chart, context.state),
                           canvas.total_rows);
}

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

auto commit_scoped_virtual_hold(editor_edit_context_t& context,
    const place_virtual_bms_hold_action_t& action, const virtual_hold_rows_t& rows,
    const pending_hold_t* captured) -> editor_interaction_result_t {
    return run_editor_edit(context, [&](document_command_transaction_t& transaction) -> attempt_t<editor_edit_effect_t> {
        const auto extent = transaction.stage_bms_measure_extent(context.state.chart, rows.measure_count);
        if (!extent.value || !extent.ok()) return {.diagnostics = extent.diagnostics};
        const auto* chart = transaction.read_chart(context.state.chart);
        const auto index = measure_index_t::from(*chart);
        if (!index) return {.diagnostics = index.error()};
        const auto anchor = position_at(*index, rows.anchor);
        const auto end = position_at(*index, rows.end);
        if (!anchor || !end) return {.diagnostics = {error(diagnostic_code_t::invalid_hold, "Virtual hold positions are unavailable.")}};
        auto pending = captured ? *captured : pending_hold_t{.lane = action.lane, .sound = action.sound,
            .kind = context.state.armed_note_kind, .layers = context.state.armed_note_layers,
            .hitsample = context.state.armed_hitsample};
        pending.position = *anchor;
        pending.anchor_canvas_row.reset(); pending.preview_canvas_row.reset(); pending.gesture_source.reset();
        if (pending.sound && !is_paint_sample_resource(context, *pending.sound)) return {.diagnostics = {error(
            diagnostic_code_t::invalid_id, "Virtual hold sample is unavailable.")}};
        if (*anchor == *end && (pending.kind == note_kind_t::hold || pending.kind == note_kind_t::roll))
            return {.diagnostics = {error(diagnostic_code_t::invalid_hold, "Move to another row to finish the hold.")}};
        const auto planned = plan_hold_placement(*chart, *index, pending.lane, *anchor, *end, pending.kind, true);
        if (const auto* failure = std::get_if<diagnostic_list_t>(&planned)) return {.diagnostics = *failure};
        auto effect = stage_scoped_hold(transaction, context.state.chart, pending, std::get<hold_placement_plan_t>(planned), *end);
        if (effect.value) { effect.value->label = virtual_row_placement_label; effect.value->consume_primary_hold = true; }
        return effect;
    });
}

auto commit_virtual_hold(editor_edit_context_t& context,
                         const place_virtual_bms_hold_action_t& action,
                         const virtual_hold_rows_t& rows, const pending_hold_t* captured) -> editor_interaction_result_t {
    if (context.history.document.find_bms_program(context.state.chart))
        return commit_scoped_virtual_hold(context, action, rows, captured ? captured : context.state.hold());

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
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                  "virtual hold endpoints are unavailable")}};
    auto staged = context.state;
    staged.virtual_bms_cursor_row.reset();
    if (!captured) captured = context.state.hold();
    auto pending = captured ? *captured : pending_hold_t{.position = *anchor,
        .lane = action.lane, .sound = action.sound, .kind = staged.armed_note_kind,
        .layers = staged.armed_note_layers, .hitsample = staged.armed_hitsample};
    staged.clear();
    pending.position = *anchor;
    pending.anchor_canvas_row.reset();
    pending.preview_canvas_row.reset();
    pending.gesture_source.reset();
    editor_edit_context_t placement{scratch, staged, {*chart, index}};
    auto result = pending.apply(placement, *end, action.source_geometry);
    if (!result) return result;
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(scratch.document));
    return commit_editor_edit(context, transaction,
                              {.label = virtual_row_placement_label,
                               .cursor_pose = capture_cursor_pose(staged),
                               .lane = staged.active_lane,
                               .created_note = result.created_note,
                               .consume_primary_hold = true});
}

auto place_virtual_bms_hold(editor_edit_context_t& context,
                            const place_virtual_bms_hold_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual hold"))
        return *invalid;
    auto rows = resolve_virtual_hold_rows(context, action);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&rows))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    return commit_virtual_hold(context, action, std::get<virtual_hold_rows_t>(rows));
}

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

auto validate_virtual_tap_sample(const editor_edit_context_t& context,
                                 std::optional<resource_id_t> sample)
    -> std::optional<editor_interaction_result_t> {
    if (!sample)
        return std::nullopt;
    if (is_paint_sample_resource(context, *sample)) return std::nullopt;
    return editor_interaction_result_t{
        .diagnostics = {error(diagnostic_code_t::invalid_id, "virtual tap sample is unavailable")}};
}

auto place_scoped_virtual_tap(editor_edit_context_t& context,
    const place_virtual_bms_tap_action_t& action, const virtual_tap_target_t& resolved) -> editor_interaction_result_t {
    if (context.state.armed_note_kind == note_kind_t::hold || context.state.armed_note_kind == note_kind_t::roll) {
        auto start = hold_start_t::from(context.state, action);
        start.lane = action.lane; start.sound = action.sample; start.canvas_row = resolved.row;
        auto result = begin_gesture<hold_gesture_t>(context, start);
        if (result.accepted) context.state.active_lane = action.lane;
        return result;
    }
    return run_editor_edit(context, [&](document_command_transaction_t& transaction) -> attempt_t<editor_edit_effect_t> {
        const auto extent = transaction.stage_bms_measure_extent(context.state.chart, resolved.measure + 1);
        if (!extent.value || !extent.ok()) return {.diagnostics = extent.diagnostics};
        const auto* chart = transaction.read_chart(context.state.chart);
        const auto index = measure_index_t::from(*chart);
        if (!index) return {.diagnostics = index.error()};
        auto state = context.state;
        state.active_lane = action.lane;
        const auto position = local_position_t{chart->measures[resolved.measure].id, resolved.row - resolved.measure_begin_row};
        place_cursor_at(state, position);
        editor_edit_context_t placement{context.history, state, {*chart, *index}};
        auto effect = stage_editor_tap(placement, transaction, {.position = position, .sound = action.sample,
            .source_geometry = action.source_geometry});
        if (effect.value) { effect.value->label = virtual_row_placement_label; effect.value->lane = action.lane; effect.value->consume_primary_hold = true; }
        return effect;
    });
}

auto place_virtual_bms_tap(editor_edit_context_t& context,
                           const place_virtual_bms_tap_action_t& action)
    -> editor_interaction_result_t {
    if (auto invalid = validate_virtual_note_target(context, action.lane, "virtual tap"))
        return *invalid;
    if (auto invalid = validate_virtual_tap_sample(context, action.sample))
        return *invalid;
    const auto target = resolve_virtual_tap_target(context, action);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&target))
        return editor_interaction_result_t{.diagnostics = *failure};
    const auto resolved = std::get<virtual_tap_target_t>(target);
    if (context.history.document.find_bms_program(context.state.chart)) return place_scoped_virtual_tap(context, action, resolved);
    auto scratch = document_history_t::from(context.history.document);
    auto* chart = scratch.document.find_chart(context.state.chart);
    if (const auto failure = extend_virtual_chart(*chart, resolved.measure + 1))
        return editor_interaction_result_t{.diagnostics = *failure};
    auto staged = context.state;
    staged.active_lane = action.lane;
    staged.virtual_bms_cursor_row.reset();
    const local_position_t position{chart->measures[resolved.measure].id,
                                    resolved.row - resolved.measure_begin_row};
    auto prepared = stage_editor_tap_placement(
        scratch, staged, place_tap_action_t{position, action.sample, action.source_geometry});
    if (!prepared.value || !prepared.ok())
        return {.diagnostics = std::move(prepared.diagnostics)};
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
    if (prepared.value->document)
        scratch.document = std::move(*prepared.value->document);
    prepared.value->effect.apply_cursor(staged);
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(scratch.document));
    return commit_editor_edit(context, transaction,
                              {.label = virtual_row_placement_label,
                               .cursor_pose = capture_cursor_pose(staged),
                               .lane = staged.active_lane,
                               .created_note = prepared.value->effect.created_note,
                               .consume_primary_hold = true});
}

auto cycle_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                       const cycle_edit_snap_action_t& action) -> editor_interaction_result_t {
    if (action.direction != interaction_direction_t::backward &&
        action.direction != interaction_direction_t::forward)
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_id, "unknown snap direction")}};
    const auto found =
        std::ranges::find(state.supported_snap_denominators, state.active_snap_denominator);
    if (found == state.supported_snap_denominators.end())
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "active editor snap is not in the supported snap list")}};
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

auto set_editor_snap(document_history_t& history, editor_interaction_state_t& state,
                     const set_edit_snap_action_t& action) -> editor_interaction_result_t {
    const auto supported = std::ranges::find(state.supported_snap_denominators, action.denominator);
    if (supported != state.supported_snap_denominators.end() &&
        action.denominator != state.active_snap_denominator) {
        state.active_snap_denominator = action.denominator;
        state.active_snap_was_fallback = false;
    }
    if (action.space) state.active_snap_space = *action.space;
    return editor_interaction_result_t::from(history, state, false);
}

auto adjust_editor_zoom(document_history_t& history, editor_interaction_state_t& state,
                        const chart_document_t& chart, const measure_index_t& index,
                        std::uint64_t absolute, const adjust_editor_zoom_action_t& action)
    -> editor_interaction_result_t {
    if (!std::isfinite(action.factor) || action.factor <= 0.0)
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "editor zoom factor must be finite and positive")}};
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
            return editor_interaction_result_t{
                .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                      "zoom anchor lies outside the editor viewport")}};
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
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "editor zoom would overflow viewport coordinates")}};
    auto proposed = state;
    set_cursor_scale(proposed, next);
    set_viewport_scroll_pixels(proposed, scroll);
    if (!synchronize_editor_cursor_to_viewport(chart, index, proposed))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                  "zoom cannot resolve the fixed cursor")}};
    state = std::move(proposed);
    return editor_interaction_result_t::from(history, state, false);
}

auto stage_editor_tap(editor_edit_context_t& context, document_command_transaction_t& transaction,
                      const place_tap_action_t& action) -> attempt_t<editor_edit_effect_t> {
    auto& state = context.state;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                state.active_lane, action.position);
    const auto* target = std::get_if<local_position_t>(&resolved);
    if (!target)
        return attempt_t<editor_edit_effect_t>{.diagnostics =
                                                   std::get<diagnostic_list_t>(resolved)};
    const std::array targets{note_paint_target_t{.lane = state.active_lane, .position = *target}};
    const auto planned = plan_note_paint(context.scope.chart, context.scope.measures, targets,
                                         state.armed_note_kind, note_paint_intent_t::toggle);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&planned))
        return attempt_t<editor_edit_effect_t>{.diagnostics = *failure};
    const auto& plan = std::get<note_paint_plan_t>(planned);
    if (transaction.has_bms_program(state.chart)) {
        const auto staged = stage_scoped_note_paint(transaction, state.chart, plan,
            note_t{.kind = state.armed_note_kind, .sound = action.sound,
                .semantic_layers = state.armed_note_layers.value_or(std::vector<layer_id_t>{}),
                .osu_hitsample = state.armed_hitsample});
        if (!staged.value || !staged.ok()) return {.diagnostics = staged.diagnostics};
        const auto created = staged.value->empty() ? std::optional<object_id_t>{}
            : std::optional{staged.value->front()};
        return {.value = editor_edit_effect_t{.label = created ? "Place note" : "Remove note",
            .cursor = *target, .created_note = created}};
    }
    auto prepared =
        prepare_note_paint_document(context.history, state.chart, plan, state.armed_note_kind,
                                    action.sound, state.armed_note_layers, action.source_geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&prepared))
        return attempt_t<editor_edit_effect_t>{.diagnostics = *failure};
    auto document = std::get<prepared_note_paint_t>(std::move(prepared));
    document.set_hitsample(state.chart, state.armed_hitsample);
    const auto created = document.painted_notes.empty()
                             ? std::optional<object_id_t>{}
                             : std::optional{document.painted_notes.front()};
    transaction.adopt_document(std::move(document.document));
    return {.value = editor_edit_effect_t{.label = created ? "Place note" : "Remove note",
                                          .cursor = *target,
                                          .created_note = created}};
}

auto place_tap(editor_edit_context_t& context, const place_tap_action_t& action)
    -> editor_interaction_result_t {
    return run_editor_edit(context, [&](document_command_transaction_t& transaction) {
        return stage_editor_tap(context, transaction, action);
    });
}

} // namespace wormedit
