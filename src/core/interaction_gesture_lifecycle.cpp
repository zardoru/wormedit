#include "interaction_internal.h"
namespace wormedit {
using namespace interaction_detail;
auto begin_note_duration_gesture(editor_edit_context_t& context, object_id_t reference_id)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    const auto* reference = find_note(context.scope.chart, reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id,
                   "duration gesture requires a tap or hold reference note", reference_id)});
    auto targets = editor_quantize_selection(state).notes;
    if (std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    const auto initial_end = reference->end.value_or(reference->position);
    auto planned = make_duration_plan(context, targets, reference_id, initial_end, true);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto update_note_duration_gesture(editor_edit_context_t& context, local_position_t requested_end)
    -> editor_interaction_result_t {
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
    const auto* reference = find_note(context.scope.chart, pending.reference_note);
    if (!reference)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id,
                   "duration gesture reference note no longer exists", pending.reference_note)});
    auto snapped_end = snapped_position(context.scope.chart, context.scope.measures, requested_end,
                                        edit_snap_denominator(context.scope.chart, state));
    if (!snapped_end)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "duration gesture end cannot be snapped inside chart")});
    const auto head = absolute_row(context.scope.measures, reference->position);
    const auto target = absolute_row(context.scope.measures, *snapped_end);
    auto exact_head = false;
    if (head && target && *target < *head) {
        snapped_end = reference->position;
        exact_head = true;
    }
    auto planned = make_duration_plan(context, pending.notes, pending.reference_note, *snapped_end,
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto release_note_duration_gesture(editor_edit_context_t& context, local_position_t requested_end)
    -> editor_interaction_result_t {
    const auto updated = update_note_duration_gesture(context, requested_end);
    if (!updated)
        return updated;
    const auto pending = *context.state.pending_note_duration_gesture;
    auto planned = make_duration_plan(context, pending.notes, pending.reference_note,
                                      pending.preview_end, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, context.state, *diagnostics);
    return apply_duration_plan(context, std::get<duration_resize_plan_t>(planned));
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
void prepare_rejected_bgm_preview(const chart_document_t& chart, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    auto lane = pending.preferred_bgm_lane.value_or(
        chart.bgm_source_lanes.empty() ? object_id_t{std::numeric_limits<std::uint64_t>::max()}
                                       : chart.bgm_source_lanes.front().id);
    if (chart.bgm_source_lanes.empty())
        pending.new_bgm_lanes.push_back({lane, 0});
    if (pending.to_bgm) {
        for (const auto& moved : plan.moved) {
            const auto* note = find_note(chart, moved.id);
            pending.bgm_preview.push_back(
                {moved.id, lane, moved.position, moved.end_position, note->sound});
        }
        return;
    }
    for (const auto id : plan.occupants) {
        const auto* note = find_note(chart, id);
        if (note)
            pending.bgm_preview.push_back({id, lane, note->position, note->end, note->sound});
    }
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
void prepare_selected_bgm_preview(editor_edit_context_t& context, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    if (plan.staged_bgm_document) {
        const auto* destination = find_chart(*plan.staged_bgm_document, context.state.chart);
        for (const auto& lane : destination->bgm_source_lanes)
            if (std::ranges::find(context.scope.chart.bgm_source_lanes, lane.id,
                                  &bgm_source_lane_t::id) ==
                context.scope.chart.bgm_source_lanes.end())
                pending.new_bgm_lanes.push_back({lane.id, lane.source_order});
        return;
    }
    if (plan.bgm_moves.empty() || (plan.notes.empty() && plan.bgm_moves.size() == 1))
        return;
    auto proposed = context.history.document;
    auto* chart = find_chart(proposed, context.state.chart);
    const auto note_failure = relocate_moved_notes(*chart, plan, context.history, context.state);
    const auto failures = note_failure ? diagnostic_list_t{*note_failure}
                                       : relocate_selected_bgm(proposed, context.state.chart, plan);
    for (const auto& failure : failures)
        pending.bgm_diagnostics.push_back(failure.message);
    if (!failures.empty())
        pending.conflicting_notes = std::max(std::size_t{1}, pending.conflicting_notes);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto prepare_bgm_move_preview(editor_edit_context_t& context, const note_move_plan_t& plan,
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
        for (const auto& failure : *failures)
            pending.bgm_diagnostics.push_back(failure.message);
        pending.conflicting_notes = std::max(std::size_t{1}, pending.conflicting_notes);
        prepare_rejected_bgm_preview(context.scope.chart, plan, pending);
        return;
    }
    const auto& chart = *find_chart(std::get<song_document_t>(staged), context.state.chart);
    const auto& ids = pending.to_bgm ? pending.notes : plan.occupants;
    for (const auto& lane : chart.bgm_source_lanes)
        if (std::ranges::find(context.scope.chart.bgm_source_lanes, lane.id,
                              &bgm_source_lane_t::id) == context.scope.chart.bgm_source_lanes.end())
            pending.new_bgm_lanes.push_back({lane.id, lane.source_order});
    for (const auto& measure : chart.measures) {
        for (const auto& event : measure.bgm_events) {
            if (std::ranges::find(ids, event.id) == ids.end())
                continue;
            pending.bgm_preview.push_back(
                {event.id, event.source_lane, event.position, event.end, event.sound});
        }
    }
    pending.conflicting_notes = 0;
    pending.occupants.clear();
    for (auto& preview : pending.preview) {
        if (preview.conflict != note_move_conflict_t::occupied)
            continue;
        preview.conflict = note_move_conflict_t::none;
        preview.occupant.reset();
    }
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto begin_note_move_gesture(editor_edit_context_t& context, object_id_t reference_id,
                             bool displace_to_bgm) -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture || state.pending_note_paint_gesture ||
        state.pending_semantic_layer_paint_gesture || state.pending_editor_area_selection ||
        state.pending_space_range_selection)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    const auto* reference = find_note(context.scope.chart, reference_id);
    const auto* bgm_reference = find_bgm_event(context.scope.chart, reference_id);
    if ((!reference && !bgm_reference) ||
        (reference && reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id, "Move requires a tap, hold or BGM reference",
                   reference_id)});
    auto targets = editor_quantize_selection(state).notes;
    if (!bgm_reference && std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    auto planned = make_move_plan(context, targets, reference_id,
                                  reference ? reference->position : bgm_reference->position,
                                  lane_id_t{}, true, std::nullopt, std::nullopt);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto update_note_move_gesture(editor_edit_context_t& context, local_position_t requested_position,
                              lane_id_t requested_lane, bool to_bgm,
                              std::optional<object_id_t> preferred_bgm_lane,
                              std::optional<std::uint64_t> virtual_bgm_row)
    -> editor_interaction_result_t {
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
    auto planned = make_move_plan(context, pending.notes, pending.reference_note,
                                  requested_position, to_bgm ? lane_id_t{} : requested_lane, false,
                                  preferred_bgm_lane, virtual_bgm_row);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) {
        state.pending_note_move_gesture->bgm_preview.clear();
        state.pending_note_move_gesture->new_bgm_lanes.clear();
        state.pending_note_move_gesture->conflicting_notes = 1;
        state.pending_note_move_gesture->bgm_diagnostics = {
            diagnostics->empty() ? "Move target unavailable" : diagnostics->front().message};
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto release_note_move_gesture(editor_edit_context_t& context, local_position_t requested_position,
                               lane_id_t requested_lane, bool to_bgm,
                               std::optional<object_id_t> preferred_bgm_lane,
                               std::optional<std::uint64_t> virtual_bgm_row)
    -> editor_interaction_result_t {
    // A release that cannot commit ends the gesture: leaving it pending would
    // keep drawing the last valid preview as a ghost note.
    const auto reject = [&](diagnostic_list_t diagnostics) {
        context.state.pending_note_move_gesture.reset();
        return diagnostic_result(context.history, context.state, std::move(diagnostics));
    };
    auto updated = update_note_move_gesture(context, requested_position, requested_lane, to_bgm,
                                            preferred_bgm_lane, virtual_bgm_row);
    if (!updated)
        return context.state.pending_note_move_gesture ? reject(std::move(updated.diagnostics))
                                                       : updated;
    const auto pending = *context.state.pending_note_move_gesture;
    auto planned =
        make_move_plan(context, pending.notes, pending.reference_note, pending.preview_position,
                       to_bgm ? lane_id_t{} : requested_lane, !virtual_bgm_row.has_value(),
                       preferred_bgm_lane, virtual_bgm_row);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return reject(std::move(*diagnostics));
    const auto& plan = std::get<note_move_plan_t>(planned);
    if (pending.to_bgm || (pending.displace_to_bgm && !plan.occupants.empty())) {
        auto staged = stage_bgm_note_move(context.history, context.state, plan, pending);
        if (auto* failures = std::get_if<diagnostic_list_t>(&staged))
            return reject(*failures);
        auto transaction = document_command_transaction_t::from(context.history);
        transaction.adopt_document(std::move(std::get<song_document_t>(staged)));
        auto committed = transaction.commit(pending.to_bgm ? "Move selected notes to BGM" : "Move notes and displace to BGM");
        if (!committed)
            return reject(std::move(committed.diagnostics));
        context.state.pending_note_move_gesture.reset();
        set_selected_notes(context.state, pending.to_bgm ? std::vector<object_id_t>{} : plan.notes);
        for (const auto& moved : plan.bgm_moves)
            context.state.selected_objects.push_back(
                {editor_selection_object_kind_t::bgm_event, moved.note});
        if (pending.to_bgm) {
            for (const auto id : plan.notes)
                context.state.selected_objects.push_back(
                    {editor_selection_object_kind_t::bgm_event, id});
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
    return apply_move_plan(context, plan);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto add_paint_path_cell(const editor_edit_context_t& context, lane_id_t lane, std::uint64_t row,
                         const pending_note_paint_gesture_t& pending,
                         std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t> {
    const auto position = position_at(context.scope.measures, row);
    if (!position)
        return error(diagnostic_code_t::row_out_of_range, "paint path leaves chart geometry");
    if (find_active_lane(context.scope.chart, context.scope.measures, *position, lane) == nullptr)
        return error(diagnostic_code_t::unknown_lane,
                     "paint lane is unavailable along the drag path");
    if (pending.visited.contains({lane, row}))
        return std::nullopt;
    if (pending.targets->size() + additions.size() >= maximum_note_paint_targets)
        return error(diagnostic_code_t::invalid_resolution,
                     "paint drag exceeds the per-gesture note target cap");
    additions.push_back({lane, *position});
    return std::nullopt;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
void cancel_stale_explicit_paint(editor_interaction_state_t& state) {
    if (state.pending_note_paint_gesture->intent != note_paint_intent_t::toggle)
        state.pending_note_paint_gesture.reset();
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto add_measure_grid_paint_path(editor_edit_context_t& context, lane_id_t lane,
                                 std::uint64_t first, std::uint64_t last,
                                 pending_note_paint_gesture_t& pending,
                                 std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t> {
    auto row = first;
    const bool forward = last >= first;
    const auto denominator = edit_snap_denominator(context.scope.chart, context.state);
    while (true) {
        if (const auto invalid = add_paint_path_cell(context, lane, row, pending, additions))
            return invalid;
        if (row == last)
            return std::nullopt;
        const auto next =
            adjacent_snap_canvas_row(context.scope.chart, context.scope.measures, row, denominator,
                                     context.scope.measures.total_rows, forward);
        if (!next || (forward ? *next > last : *next < last))
            return error(diagnostic_code_t::row_out_of_range,
                         "paint path cannot reach the target grid position");
        row = *next;
    }
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto append_note_paint_target(editor_edit_context_t& context, lane_id_t lane,
                              local_position_t position, bool pointer_captured)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (!pointer_captured)
        return cancel_pointer_gesture(context);
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
    const auto target = snapped_position(context.scope.chart, context.scope.measures, position,
                                         edit_snap_denominator(context.scope.chart, state));
    if (!target)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint target cannot be snapped inside chart")});
    const auto target_row = absolute_row(context.scope.measures, *target);
    const auto previous_row = absolute_row(context.scope.measures, pending.last_position);
    if (!target_row || !previous_row)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::row_out_of_range,
                                        "paint gesture lies outside chart geometry")});
    const auto same_lane = lane == pending.last_lane;
    std::vector<note_paint_target_t> additions;
    if (same_lane) {
        if (const auto invalid = add_measure_grid_paint_path(context, lane, *previous_row,
                                                             *target_row, pending, additions))
            return diagnostic_result(context.history, state, {*invalid});
    } else if (const auto invalid =
                   add_paint_path_cell(context, lane, *target_row, pending, additions)) {
        return diagnostic_result(context.history, state, {*invalid});
    }
    if (!additions.empty()) {
        auto expanded = std::make_shared<std::vector<note_paint_target_t>>(*pending.targets);
        expanded->reserve(expanded->size() + additions.size());
        for (const auto& addition : additions) {
            const auto row = absolute_row(context.scope.measures, addition.position);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto is_paint_sample_resource(const song_document_t& document, resource_id_t id) -> bool {
    const auto found = std::ranges::find(document.resources, id, &resource_entry_t::id);
    return found != document.resources.end() &&
           std::ranges::find(found->roles, resource_role_t::sample) != found->roles.end();
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto begin_note_paint_gesture(editor_edit_context_t& context, lane_id_t lane,
                              local_position_t position, bool pointer_captured,
                              std::optional<resource_id_t> sound, note_paint_intent_t intent,
                              std::optional<bmson_source_geometry_t> geometry)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (intent != note_paint_intent_t::toggle && intent != note_paint_intent_t::draw &&
        intent != note_paint_intent_t::erase)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_hold, "note paint intent is invalid")});
    if (intent != note_paint_intent_t::erase &&
        (state.armed_note_kind == note_kind_t::hold || state.armed_note_kind == note_kind_t::roll))
        return diagnostic_result(
            context.history, state,
            {error(
                diagnostic_code_t::invalid_hold,
                "Use the hold gesture to place Hold or Roll notes; painting needs an explicit end.")});
    if (!pointer_captured)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "pointer capture is required to begin note painting")});
    if (intent != note_paint_intent_t::erase && sound &&
        !is_paint_sample_resource(context.history.document, *sound))
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")});
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture || state.pending_note_paint_gesture ||
        state.pending_semantic_layer_paint_gesture || state.pending_editor_area_selection ||
        state.pending_space_range_selection)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    const auto resolved =
        resolve_row_placement(context.scope.chart, context.scope.measures, state, lane, position);
    const auto* target = std::get_if<local_position_t>(&resolved);
    if (!target)
        return diagnostic_result(context.history, state, std::get<diagnostic_list_t>(resolved));
    const auto row = absolute_row(context.scope.measures, *target);
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
        .kind = state.armed_note_kind,
        .layers = state.armed_note_layers,
        .source_geometry = geometry,
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto entry_stem_channel(const song_document_t& document, const chart_document_t& chart,
                        std::optional<resource_id_t> sound)
    -> std::variant<std::optional<object_id_t>, diagnostic_list_t> {
    if (!sound)
        return std::optional<object_id_t>{};
    if (!is_paint_sample_resource(document, *sound))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")};
    if (chart.source_family != format_family_t::bmson)
        return std::optional<object_id_t>{};
    const auto bindings = bmson_channel_resource_bindings(document, chart.id);
    const auto binding =
        std::ranges::find(bindings, *sound, &bmson_channel_resource_binding_t::resource);
    if (binding != bindings.end() && binding->ambiguous)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "The armed BMSON sound has ambiguous source channels")};
    if (binding != bindings.end() && binding->channel &&
        std::ranges::any_of(chart.bgm_source_lanes, [&](const auto& lane) {
            return lane.stem_channel == binding->channel;
        }))
        return binding->channel;
    const auto resource = std::ranges::find(document.resources, *sound, &resource_entry_t::id);
    if (resource->sound_kind == sound_kind_t::stem)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "The armed BMSON stem has no linked source channel")};
    return std::optional<object_id_t>{};
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto prepare_stem_note_paint(const document_history_t& history, chart_id_t chart_id,
                             const note_paint_plan_t& plan, object_id_t channel,
                             const std::optional<std::vector<layer_id_t>>& armed_layers,
                             std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    auto scratch = make_document_history(history.document);
    if (!plan.removals.empty()) {
        editor_interaction_state_t selection;
        selection.chart = chart_id;
        selection.selected_notes = plan.removals;
        const auto removed =
            dispatch_editor_action(scratch, selection, delete_editor_selection_action_t{});
        if (!removed)
            return removed.diagnostics;
    }
    std::vector<object_id_t> notes;
    for (const auto& target : plan.additions) {
        const auto placed =
            insert_bmson_playable_stem_point(scratch, scratch.document.revision, chart_id, channel,
                                             target.position, target.lane, geometry);
        if (!placed && !placed.diagnostics.empty())
            return placed.diagnostics;
        auto* chart = find_chart(scratch.document, chart_id);
        for (auto& measure : chart->measures)
            for (auto& note : measure.notes) {
                if (note.position != target.position || note.lane != target.lane)
                    continue;
                if (!find_note(*find_chart(history.document, chart_id), note.id))
                    note.semantic_layers =
                        resolve_placement_layers(*chart, armed_layers, note.sound);
                notes.push_back(note.id);
            }
    }
    // A stroke may visit two lanes at one pulse; retain only its final
    // assignment.
    const auto* chart = find_chart(scratch.document, chart_id);
    std::erase_if(notes, [&](const auto id) { return !find_note(*chart, id); });
    std::ranges::sort(notes);
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return prepared_note_paint_t{std::move(scratch.document), std::move(notes)};
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto prepare_note_paint_document(const document_history_t& history, chart_id_t chart_id,
                                 const note_paint_plan_t& plan, note_kind_t kind,
                                 std::optional<resource_id_t> sound,
                                 const std::optional<std::vector<layer_id_t>>& armed_layers,
                                 std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    const auto stem =
        entry_stem_channel(history.document, *find_chart(history.document, chart_id), sound);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&stem))
        return *failure;
    if (const auto channel = std::get<std::optional<object_id_t>>(stem))
        return prepare_stem_note_paint(history, chart_id, plan, *channel, armed_layers, geometry);
    const auto first_id = plan.additions.empty()
                              ? std::optional<object_id_t>{}
                              : next_object_id(*find_chart(history.document, chart_id));
    if (!plan.additions.empty() &&
        (!first_id ||
         plan.additions.size() - 1 > std::numeric_limits<std::uint64_t>::max() - first_id->value))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "chart object ID sequence is exhausted")};
    auto proposed = history.document;
    auto* chart = find_chart(proposed, chart_id);
    std::unordered_set<std::uint64_t> removed_ids;
    removed_ids.reserve(plan.removals.size());
    for (const auto id : plan.removals)
        removed_ids.insert(id.value);
    for (auto& measure : chart->measures)
        std::erase_if(measure.notes,
                      [&](const auto& note) { return removed_ids.contains(note.id.value); });
    prepared_note_paint_t prepared{.document = std::move(proposed), .painted_notes = {}};
    auto* prepared_chart = find_chart(prepared.document, chart_id);
    std::unordered_map<std::uint64_t, measure_chunk_t*> prepared_measures;
    prepared_measures.reserve(prepared_chart->measures.size());
    for (auto& measure : prepared_chart->measures)
        prepared_measures.emplace(measure.id.value, &measure);
    auto next_id = first_id ? first_id->value : 0;
    if (!plan.additions.empty() && sound && !is_paint_sample_resource(history.document, *sound))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")};
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto end_note_paint_gesture(editor_edit_context_t& context, lane_id_t lane,
                            local_position_t position, bool pointer_captured,
                            std::optional<bmson_source_geometry_t> geometry)
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
    if (pending.intent != note_paint_intent_t::toggle)
        state.pending_note_paint_gesture.reset();
    if (pending.document_revision != context.history.document.revision) {
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "note paint gesture is stale after a document edit")});
    }
    auto planned = plan_note_paint(context.scope.chart, context.scope.measures, *pending.targets,
                                   pending.kind, pending.intent);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return diagnostic_result(context.history, state, *diagnostics);
    const auto& plan = std::get<note_paint_plan_t>(planned);
    if (plan.additions.empty() && plan.removals.empty() &&
        pending.intent == note_paint_intent_t::toggle)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_id, "paint gesture contains no note changes")});
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
    auto prepared = prepare_note_paint_document(
        context.history, state.chart, plan, pending.kind, pending.sound, pending.layers,
        pending.source_geometry ? pending.source_geometry : geometry);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&prepared)) {
        state.pending_note_paint_gesture.reset();
        return diagnostic_result(context.history, state, *diagnostics);
    }
    auto result = std::get<prepared_note_paint_t>(std::move(prepared));
    auto transaction = document_command_transaction_t::from(context.history);
    transaction.adopt_document(std::move(result.document));
    auto command =
        transaction.commit("Paint notes");
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto begin_hold_gesture(editor_edit_context_t& context, lane_id_t lane, local_position_t position,
                        hold_gesture_source_t source, std::optional<resource_id_t> sound,
                        bool displace, std::optional<bmson_source_geometry_t> geometry)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (state.pending_hold || state.pending_note_duration_gesture ||
        state.pending_note_move_gesture)
        return diagnostic_result(
            context.history, state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    if (source == hold_gesture_source_t::keyboard_lane)
        if (auto removed = apply_keyboard_entry(context, lane, position, sound, displace, geometry))
            return std::move(*removed);
    if (sound && !is_paint_sample_resource(context.history.document, *sound))
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")});
    const auto before = state;
    state.active_lane = lane;
    auto result = dispatch_nested_action(context, begin_hold_action_t{position});
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
        if (geometry)
            state.pending_hold->source_geometry =
                std::make_shared<const bmson_source_geometry_t>(*geometry);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto update_hold_gesture(editor_edit_context_t& context, hold_gesture_source_t source,
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
    const auto target = snapped_position(context.scope.chart, context.scope.measures, position,
                                         edit_snap_denominator(context.scope.chart, state));
    state.pending_hold->gesture_source.reset();
    auto result =
        dispatch_nested_action(context, update_hold_preview_action_t{target.value_or(position)});
    if (state.pending_hold)
        state.pending_hold->gesture_source = source;
    if (!result)
        return result;
    state.pending_hold->preview_canvas_row.reset();
    if (source == hold_gesture_source_t::keyboard_lane &&
        !interaction_detail::keep_cursor_visible(context.scope.chart, context.scope.measures,
                                                 state)) {
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto release_hold_gesture(editor_edit_context_t& context, hold_gesture_source_t source,
                          local_position_t position,
                          std::optional<bmson_source_geometry_t> geometry)
    -> editor_interaction_result_t {
    auto& state = context.state;
    if (!state.pending_hold || state.pending_hold->gesture_source != source)
        return diagnostic_result(context.history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "hold gesture release does not match the active input")});
    const auto target = snapped_position(context.scope.chart, context.scope.measures, position,
                                         edit_snap_denominator(context.scope.chart, state));
    state.pending_hold->gesture_source.reset();
    auto result =
        dispatch_nested_action(context, commit_hold_action_t{target.value_or(position), geometry});
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto begin_hold_action(editor_edit_context_t& context, local_position_t position,
                       std::optional<resource_id_t> sound,
                       std::optional<bmson_source_geometry_t> geometry)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                state.active_lane, position);
    const auto* snapped = std::get_if<local_position_t>(&resolved);
    if (!snapped)
        return diagnostic_result(context.history, state, std::get<diagnostic_list_t>(resolved));
    if (sound && !is_paint_sample_resource(history.document, *sound))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")});
    const auto plan = plan_hold_placement<hold_preview_removals_t>(
        context.scope.chart, context.scope.measures, state.active_lane, *snapped, *snapped);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    state.pending_hold = pending_hold_t{
        .position = *snapped,
        .lane = state.active_lane,
        .document_revision = history.document.revision,
        .placement_preview = std::get<pending_hold_placement_preview_t>(plan),
        .sound = sound,
        .kind = state.armed_note_kind,
        .layers = state.armed_note_layers,
        .source_geometry =
            geometry ? std::make_shared<const bmson_source_geometry_t>(*geometry) : nullptr,
    };
    place_cursor_at(state, *snapped);
    return space_selection_view_result(make_view(history, state));
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto validate_pending_hold_start(const editor_edit_context_t& context,
                                 const pending_hold_t& pending)
    -> std::optional<editor_interaction_result_t> {
    if (pending.document_revision == context.history.document.revision &&
        snapped_position(context.scope.chart, context.scope.measures, pending.position,
                         edit_snap_denominator(context.scope.chart, context.state)) ==
            pending.position)
        return std::nullopt;
    return diagnostic_result(
        context.history, context.state,
        {error(diagnostic_code_t::invalid_hold,
               "pending hold start is stale after a document or snap change")});
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto update_hold_preview(editor_edit_context_t& context, local_position_t position)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold, "hold preview requires a pending hold start")});
    const auto pending = *state.pending_hold;
    if (auto stale = validate_pending_hold_start(context, pending))
        return *stale;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                pending.lane, position);
    const auto* snapped = std::get_if<local_position_t>(&resolved);
    if (!snapped)
        return diagnostic_result(context.history, state, std::get<diagnostic_list_t>(resolved));
    const auto plan = plan_hold_placement<hold_preview_removals_t>(
        context.scope.chart, context.scope.measures, pending.lane, pending.position, *snapped,
        pending.kind);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    state.pending_hold->preview_end =
        *snapped == pending.position ? std::optional<local_position_t>{} : std::optional{*snapped};
    state.pending_hold->placement_preview = std::get<pending_hold_placement_preview_t>(plan);
    place_cursor_at(state, *snapped);
    state.active_lane = pending.lane;
    return space_selection_view_result(make_view(history, state));
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto prepare_stem_hold_document(const editor_edit_context_t& context, const pending_hold_t& pending,
                                const hold_placement_plan_t& placement, object_id_t channel,
                                std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    note_paint_plan_t plan{.additions = {}, .removals = placement.removals};
    if (placement.adds_note)
        plan.additions.push_back({pending.lane, placement.head});
    auto prepared = prepare_stem_note_paint(context.history, context.state.chart, plan, channel,
                                            pending.layers, geometry);
    if (std::holds_alternative<diagnostic_list_t>(prepared))
        return prepared;
    auto& result = std::get<prepared_note_paint_t>(prepared);
    auto* chart = find_chart(result.document, context.state.chart);
    for (const auto id : result.painted_notes) {
        auto* note = find_note(*chart, id);
        note->kind = placement.tail ? held_note_kind(pending.kind) : pending.kind;
        note->end = placement.tail;
        auto& events =
            std::ranges::find(chart->sound_channels, channel, &sound_channel_t::id)->events;
        std::ranges::find(events, *note->sound_channel_event, &sound_channel_event_t::id)->end =
            placement.tail;
    }
    return prepared;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto commit_stem_hold(editor_edit_context_t& context, const pending_hold_t& pending,
                      const hold_placement_plan_t& placement, local_position_t target,
                      std::optional<bmson_source_geometry_t> geometry)
    -> std::optional<editor_interaction_result_t> {
    const auto resolved =
        entry_stem_channel(context.history.document, context.scope.chart, pending.sound);
    const auto* channel = std::get_if<std::optional<object_id_t>>(&resolved);
    if (channel && !*channel)
        return std::nullopt;
    context.state.pending_hold.reset();
    if (const auto* failure = std::get_if<diagnostic_list_t>(&resolved))
        return diagnostic_result(context.history, context.state, *failure);
    auto prepared = prepare_stem_hold_document(context, pending, placement, **channel, geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&prepared))
        return diagnostic_result(context.history, context.state, *failure);
    auto document = std::get<prepared_note_paint_t>(std::move(prepared));
    const auto created = document.painted_notes.empty()
                             ? std::optional<object_id_t>{}
                             : std::optional{document.painted_notes.front()};
    auto transaction = document_command_transaction_t::from(context.history);
    transaction.adopt_document(std::move(document.document));
    const auto committed = transaction.commit("Place linked BMSON stem note");
    if (!committed)
        return diagnostic_result(context.history, context.state, committed.diagnostics);
    place_cursor_at(context.state, target);
    context.state.active_lane = pending.lane;
    auto result = interaction_view_result(context.history, context.state, true);
    result.created_note = created;
    return result;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto commit_hold(editor_edit_context_t& context, local_position_t position,
                 std::optional<bmson_source_geometry_t> geometry) -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_hold,
                                        "hold completion requires a pending hold start")});
    const auto pending = *state.pending_hold;
    if (pending.source_geometry)
        geometry = *pending.source_geometry;
    if (auto stale = validate_pending_hold_start(context, pending))
        return *stale;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                pending.lane, position);
    const auto* snapped = std::get_if<local_position_t>(&resolved);
    if (!snapped)
        return diagnostic_result(context.history, state, std::get<diagnostic_list_t>(resolved));
    if (*snapped == pending.position &&
        (pending.kind == note_kind_t::hold || pending.kind == note_kind_t::roll))
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold,
                   "Move to a different row to finish the Hold or Roll gesture.")});
    if (pending.sound && !is_paint_sample_resource(history.document, *pending.sound))
        return diagnostic_result(history, state,
                                 {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")});
    const auto plan = plan_hold_placement(context.scope.chart, context.scope.measures, pending.lane,
                                          pending.position, *snapped, pending.kind, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return diagnostic_result(history, state, *diagnostics);
    const auto& placement = std::get<hold_placement_plan_t>(plan);
    if (auto stem = commit_stem_hold(context, pending, placement, *snapped, geometry))
        return *stem;
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto* chart = find_chart(proposed, state.chart);
    std::optional<object_id_t> id;
    if (placement.adds_note) {
        id = next_object_id(*chart);
        if (!id)
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::invalid_id, "chart object ID sequence is exhausted")});
    }
    erase_notes(*chart, placement.removals);
    if (placement.adds_note) {
        const auto measure =
            std::ranges::find(chart->measures, placement.head.measure, &measure_chunk_t::id);
        if (measure == chart->measures.end())
            return diagnostic_result(history, state,
                                     {error(diagnostic_code_t::unknown_measure,
                                            "normalized hold head refers to an unknown measure")});
        measure->notes.push_back(
            {.id = *id,
             .position = placement.head,
             .lane = pending.lane,
             .kind = placement.tail ? held_note_kind(pending.kind) : pending.kind,
             .end = placement.tail,
             .sound = pending.sound,
             .semantic_layers = resolve_placement_layers(*chart, pending.layers, pending.sound)});
    }
    const auto label = placement.adds_note
                           ? (placement.tail ? "Place merged hold note" : "Place tap note")
                           : "Remove note";
    auto command = transaction.commit(label);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto refresh_keyboard_hold_revisions(editor_edit_context_t& context) -> void {
    auto& state = context.state;
    if (state.pending_hold)
        state.pending_hold->document_revision = context.history.document.revision;
    for (auto& hold : state.additional_keyboard_holds)
        hold.document_revision = context.history.document.revision;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto begin_lane_key_hold_gesture(editor_edit_context_t& context, lane_id_t lane,
                                 std::optional<resource_id_t> sound, bool displace,
                                 std::optional<bmson_source_geometry_t> geometry)
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto before = state;
    if (state.pending_hold) {
        if (state.pending_hold->lane == lane ||
            std::ranges::any_of(state.additional_keyboard_holds,
                                [lane](const auto& hold) { return hold.lane == lane; }))
            return diagnostic_result(
                context.history, state,
                {error(diagnostic_code_t::invalid_hold, "lane key is already held")});
        state.additional_keyboard_holds.push_back(*state.pending_hold);
        state.pending_hold.reset();
    }
    if (state.virtual_bms_cursor_row) {
        if (sound && !is_paint_sample_resource(context.history.document, *sound)) {
            state = before;
            return diagnostic_result(context.history, state,
                                     {error(diagnostic_code_t::invalid_id,
                                            "paint sound is not an existing sample resource")});
        }
        state.pending_hold = pending_hold_t{
            .position = state.cursor,
            .lane = lane,
            .document_revision = context.history.document.revision,
            .anchor_canvas_row = *state.virtual_bms_cursor_row,
            .preview_canvas_row = *state.virtual_bms_cursor_row,
            .gesture_source = hold_gesture_source_t::keyboard_lane,
            .sound = sound,
            .kind = state.armed_note_kind,
            .layers = state.armed_note_layers,
            .source_geometry =
                geometry ? std::make_shared<const bmson_source_geometry_t>(*geometry) : nullptr,
        };
        auto view = make_view(context.history, state);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&view)) {
            state = before;
            return diagnostic_result(context.history, state, *diagnostics);
        }
        return {.accepted = true,
                .view = std::get<editor_interaction_view_model_t>(std::move(view))};
    }
    auto result =
        begin_hold_gesture(context, lane, state.cursor, hold_gesture_source_t::keyboard_lane, sound,
                           displace, geometry);
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto release_lane_key_hold_gesture(editor_edit_context_t& context, lane_id_t action_lane)
    -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    if (!state.pending_hold)
        return space_selection_view_result(make_view(history, state));
    const auto before = state;
    const auto lane = action_lane.value == 0 ? state.pending_hold->lane : action_lane;
    const auto found =
        std::ranges::find(state.additional_keyboard_holds, lane, &pending_hold_t::lane);
    const bool primary = state.pending_hold->lane == lane;
    if (!primary && found == state.additional_keyboard_holds.end())
        return diagnostic_result(
            history, state,
            {error(diagnostic_code_t::invalid_hold, "released lane key has no pending hold")});
    if (!primary)
        std::swap(*state.pending_hold, *found);
    const auto position = state.pending_hold->preview_end.value_or(state.pending_hold->position);
    editor_interaction_result_t result;
    if (state.pending_hold->anchor_canvas_row || state.pending_hold->preview_canvas_row) {
        const auto resolved_anchor =
            state.pending_hold->anchor_canvas_row
                ? state.pending_hold->anchor_canvas_row
                : absolute_row(context.scope.measures, state.pending_hold->position);
        if (!resolved_anchor)
            return diagnostic_result(
                history, state,
                {error(diagnostic_code_t::row_out_of_range, "hold anchor is unavailable")});
        const auto anchor = *resolved_anchor;
        const auto end = state.pending_hold->preview_canvas_row.value_or(anchor);
        const auto hold_lane = state.pending_hold->lane;
        const auto sound = state.pending_hold->sound;
        state.pending_hold->gesture_source.reset();
        result = dispatch_nested_action(
            context, place_virtual_bms_hold_action_t{anchor, end, hold_lane, sound});
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
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
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto handle_editor_cancellation(document_history_t& history, editor_interaction_state_t& state,
                                const editor_interaction_action_t& action)
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
        clear_editor_gestures(state);
        return recovered_editor_view(history, state);
    }
    if (std::holds_alternative<cancel_editor_area_selection_action_t>(action)) {
        visit_editor_gestures(state, [](auto kind, auto& field) {
            if (kind == editor_gesture_kind_t::area)
                clear_editor_gesture(field);
        });
        return recovered_editor_view(history, state);
    }
    return std::nullopt;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto keyboard_hold_action_allowed(const editor_interaction_action_t& action) -> bool {
    return gesture_action_is<move_edit_cursor_action_t, page_edit_cursor_action_t,
                             scroll_editor_view_action_t, begin_lane_key_hold_gesture_action_t,
                             release_lane_key_hold_gesture_action_t,
                             place_virtual_bms_hold_action_t>(action);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto hold_gesture_action_allowed(const editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> bool {
    if (!state.pending_hold || !state.pending_hold->gesture_source)
        return true;
    if (*state.pending_hold->gesture_source == hold_gesture_source_t::keyboard_lane)
        return keyboard_hold_action_allowed(action);
    return gesture_action_is<update_alt_pointer_hold_gesture_action_t,
                             release_alt_pointer_hold_gesture_action_t>(action);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto gesture_action_allowed(editor_gesture_kind_t kind, const editor_interaction_state_t& state,
                            const editor_interaction_action_t& action) -> bool {
    switch (kind) {
    case editor_gesture_kind_t::hold:
        return hold_gesture_action_allowed(state, action);
    case editor_gesture_kind_t::keyboard_holds:
        // The authorized lane action delegates its begin/preview/commit to these
        // legacy verbs.
        return keyboard_hold_action_allowed(action) ||
               gesture_action_is<begin_hold_action_t, update_hold_preview_action_t,
                                 commit_hold_action_t, place_virtual_bms_hold_action_t>(action);
    case editor_gesture_kind_t::duration:
        return gesture_action_is<update_note_duration_gesture_action_t>(action);
    case editor_gesture_kind_t::move:
        return gesture_action_is<update_note_move_gesture_action_t>(action);
    case editor_gesture_kind_t::paint:
        return gesture_action_is<update_note_paint_gesture_action_t,
                                 end_note_paint_gesture_action_t>(action);
    case editor_gesture_kind_t::semantic_paint:
        return gesture_action_is<update_semantic_layer_paint_gesture_action_t>(action);
    case editor_gesture_kind_t::area:
        return gesture_action_is<update_editor_area_selection_action_t>(action);
    case editor_gesture_kind_t::range:
        return gesture_action_is<space_range_selection_action_t, move_edit_cursor_action_t,
                                 page_edit_cursor_action_t, jump_edit_cursor_action_t,
                                 cycle_edit_snap_action_t, set_edit_snap_action_t,
                                 adjust_editor_zoom_action_t, scroll_editor_view_action_t>(action);
    }
    return false;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto gesture_finish_message(editor_gesture_kind_t kind) -> const char* {
    switch (kind) {
    case editor_gesture_kind_t::hold:
    case editor_gesture_kind_t::keyboard_holds:
        return "finish or cancel the active hold input gesture first";
    case editor_gesture_kind_t::duration:
        return "finish or cancel the active note duration gesture first";
    case editor_gesture_kind_t::move:
        return "finish or cancel the active note move gesture first";
    case editor_gesture_kind_t::paint:
        return "finish or cancel the active note paint gesture first";
    case editor_gesture_kind_t::semantic_paint:
        return "finish or cancel the active semantic-layer paint gesture first";
    case editor_gesture_kind_t::area:
        return "finish or cancel the active area selection gesture first";
    case editor_gesture_kind_t::range:
        return "finish or cancel the active Space range selection first";
    }
    return "finish or cancel the active gesture first";
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto validate_active_gesture_action(const document_history_t& history,
                                    const editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action)
    -> std::optional<editor_interaction_result_t> {
    std::optional<editor_gesture_kind_t> blocked;
    visit_editor_gestures(state, [&](auto kind, const auto& field) {
        if (!blocked && editor_gesture_pending(field) &&
            !gesture_action_allowed(kind, state, action))
            blocked = kind;
    });
    if (!blocked)
        return std::nullopt;
    return diagnostic_result(
        history, state, {error(diagnostic_code_t::invalid_hold, gesture_finish_message(*blocked))});
}
} // namespace wormedit
