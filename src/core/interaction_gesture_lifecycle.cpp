#include "interaction_edit_commit.h"
#include "interaction_gesture_runner.h"
#include <wormedit/core/interaction_cursor_pose.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/time_shift.h>
namespace wormedit {
using namespace interaction_detail;

auto pending_note_duration_gesture_t::begin(editor_edit_context_t& context, object_id_t reference_id)
    -> diagnosed_t<std::optional<pending_note_duration_gesture_t>> {
    const auto& state = context.state;
    const auto* reference = context.scope.chart.find_note(reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "duration gesture requires a tap or hold reference note", reference_id)}};
    auto targets = editor_quantize_selection(state).notes;
    if (std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    const auto initial_end = reference->end.value_or(reference->position);
    auto planned = make_duration_plan(context, targets, reference_id, initial_end, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return {.diagnostics = *diagnostics};
    auto& plan = std::get<duration_resize_plan_t>(planned);
    return {.value = pending_note_duration_gesture_t{
        .reference_note = reference_id,
        .notes = std::move(plan.notes),
        .preview_end = plan.reference_end,
        .preview = duration_previews(plan),
    }};
}

auto pending_note_duration_gesture_t::revise(editor_edit_context_t& context, local_position_t requested_end)
    -> diagnostic_list_t {
    const auto& state = context.state;
    const auto* reference = context.scope.chart.find_note(reference_note);
    if (!reference)
        return {error(diagnostic_code_t::invalid_id,
                   "duration gesture reference note no longer exists", reference_note)};
    auto snapped_end = snap_position(context.scope.chart, context.scope.measures, requested_end,
                                        edit_snap_grid(context.scope.chart, state));
    if (!snapped_end)
        return {error(diagnostic_code_t::row_out_of_range,
                      "duration gesture end cannot be snapped inside chart")};
    const auto head = context.scope.measures.row_of(reference->position);
    const auto target = context.scope.measures.row_of(*snapped_end);
    auto exact_head = false;
    if (head && target && *target < *head) {
        snapped_end = reference->position;
        exact_head = true;
    }
    auto planned = make_duration_plan(context, notes, reference_note, *snapped_end, exact_head);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return *diagnostics;
    const auto& plan = std::get<duration_resize_plan_t>(planned);
    preview_end = plan.reference_end;
    preview = duration_previews(plan);
    return {};
}

auto pending_note_duration_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    auto planned = make_duration_plan(context, notes, reference_note, preview_end, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    return apply_duration_resize_plan(context.history, context.state, std::get<duration_resize_plan_t>(planned));
}


void prepare_rejected_bgm_preview(const chart_document_t& chart, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    auto lane = pending.preferred_bgm_lane.value_or(
        chart.bgm_source_lanes.empty() ? object_id_t{std::numeric_limits<std::uint64_t>::max()}
                                       : chart.bgm_source_lanes.front().id);
    if (chart.bgm_source_lanes.empty())
        pending.new_bgm_lanes.push_back({lane, 0});
    if (pending.to_bgm) {
        for (const auto& moved : plan.moved) {
            const auto* note = chart.find_note(moved.id);
            pending.bgm_preview.push_back(
                {.note = moved.id, .lane = lane, .position = moved.position, .end = moved.end_position, .sound = note->sound});
        }
        return;
    }
    for (const auto id : plan.occupants) {
        const auto* note = chart.find_note(id);
        if (note)
            pending.bgm_preview.push_back({.note = id, .lane = lane, .position = note->position, .end = note->end, .sound = note->sound});
    }
}

void prepare_selected_bgm_preview(editor_edit_context_t& context, const note_move_plan_t& plan,
                                  pending_note_move_gesture_t& pending) {
    if (plan.staged_bgm_document) {
        const auto* destination = (*plan.staged_bgm_document).find_chart(context.state.chart);
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
    auto* chart = proposed.find_chart(context.state.chart);
    const auto note_failure = relocate_moved_notes(*chart, plan, context.history, context.state);
    const auto failures = note_failure ? diagnostic_list_t{*note_failure}
                                       : relocate_selected_bgm(proposed, context.state.chart, plan);
    for (const auto& failure : failures)
        pending.bgm_diagnostics.push_back(failure.message);
    if (!failures.empty())
        pending.conflicting_notes = std::max(std::size_t{1}, pending.conflicting_notes);
}

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
    const auto& chart = *(std::get<song_document_t>(staged)).find_chart(context.state.chart);
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
                {.note = event.id, .lane = event.source_lane, .position = event.position, .end = event.end, .sound = event.sound});
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

auto pending_note_move_gesture_t::begin(editor_edit_context_t& context, object_id_t reference_id,
                             bool displace_to_bgm) -> diagnosed_t<std::optional<pending_note_move_gesture_t>> {
    auto& state = context.state;
    const auto* reference = context.scope.chart.find_note(reference_id);
    const auto* bgm_reference = context.scope.chart.find_bgm_event(reference_id);
    if ((!reference && !bgm_reference) ||
        (reference && reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "Move requires a tap, hold or BGM reference", reference_id)}};
    auto targets = editor_quantize_selection(state).notes;
    if (!bgm_reference && std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    auto planned = plan_move_gesture(context, targets, reference_id,
                                  reference ? reference->position : bgm_reference->position,
                                  lane_id_t{}, true, std::nullopt, std::nullopt);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return {.diagnostics = *diagnostics};
    auto& plan = std::get<note_move_plan_t>(planned);
    return {.value = pending_note_move_gesture_t{
        .reference_note = reference_id,
        .notes = std::move(plan.notes),
        .preview_position = plan.reference_position,
        .preview_lane = reference ? reference->lane : lane_id_t{},
        .preview = move_previews(plan),
        .occupants = plan.occupants,
        .conflicting_notes = move_conflict_count(plan),
        .displace_to_bgm = displace_to_bgm,
        .bar_lines = editor_shift_selection(state, 0).bar_lines,
        .bar_line_time = state.bar_line_time,
    }};
}

// The drag's row component for its bar lines, measured from the reference object's original row.
// Objects the drag relocated itself are named so the row shift never moves them twice.
auto drag_bar_line_shift(const editor_edit_context_t& context, const pending_note_move_gesture_t& pending,
                         const note_move_plan_t& plan) -> std::optional<shift_selection_action_t> {
    if (pending.bar_lines.empty()) return std::nullopt;
    const auto& chart = context.scope.chart;
    const auto* note = chart.find_note(pending.reference_note);
    const auto* event = note ? nullptr : chart.find_bgm_event(pending.reference_note);
    const auto from = note ? context.scope.measures.row_of(note->position)
        : event ? context.scope.measures.row_of(event->position) : std::nullopt;
    const auto to = context.scope.measures.row_of(plan.reference_position);
    if (!from || !to) return std::nullopt;
    shift_selection_action_t action{.chart = context.state.chart, .notes = {}, .bgm_events = {},
        .rows = static_cast<std::int64_t>(*to) - static_cast<std::int64_t>(*from),
        .bar_lines = pending.bar_lines, .bar_line_time = pending.bar_line_time,
        .include_timing = true, .already_moved_notes = plan.notes, .already_moved_bgm_events = {}};
    for (const auto& moved : plan.bgm_moves) action.already_moved_bgm_events.push_back(moved.note);
    return action;
}

auto stage_drag_bar_lines(song_document_t& document, const std::optional<shift_selection_action_t>& shift)
    -> diagnostic_list_t {
    if (!shift) return {};
    return stage_shift_selection(document, *shift).command.diagnostics;
}
auto pending_note_move_gesture_t::revise(editor_edit_context_t& context, local_position_t requested_position,
                              lane_id_t requested_lane, bool requested_to_bgm,
                              std::optional<object_id_t> requested_bgm_lane,
                              std::optional<std::uint64_t> requested_bgm_row)
    -> diagnostic_list_t {
    auto planned = plan_move_gesture(context, notes, reference_note,
                                  requested_position, requested_to_bgm ? lane_id_t{} : requested_lane, false,
                                  requested_bgm_lane, requested_bgm_row);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned)) {
        bgm_preview.clear();
        new_bgm_lanes.clear();
        conflicting_notes = 1;
        bgm_diagnostics = {
            diagnostics->empty() ? "Move target unavailable" : diagnostics->front().message};
        return *diagnostics;
    }
    const auto& plan = std::get<note_move_plan_t>(planned);
    virtual_bgm_row = requested_bgm_row;
    preview_position = plan.reference_position;
    preview_lane = requested_lane;
    preview = move_previews(plan);
    occupants = plan.occupants;
    conflicting_notes = move_conflict_count(plan);
    to_bgm = requested_to_bgm;
    preferred_bgm_lane = requested_bgm_lane;
    prepare_bgm_move_preview(context, plan, *this);
    return {};
}
// Moves notes into BGM (or displaces their occupants there) in one commit, carrying the
// selection's bar lines with the drag's rows.
auto commit_bgm_note_move(editor_edit_context_t& context, const pending_note_move_gesture_t& pending,
                          const note_move_plan_t& plan,
                          const std::optional<shift_selection_action_t>& bar_line_shift)
    -> editor_interaction_result_t {
    auto staged = stage_bgm_note_move(context.history, context.state, plan, pending);
    if (auto* failures = std::get_if<diagnostic_list_t>(&staged))
        return editor_interaction_result_t{.diagnostics = *failures};
    if (auto failures = stage_drag_bar_lines(std::get<song_document_t>(staged), bar_line_shift);
        !failures.empty())
        return editor_interaction_result_t{.diagnostics = std::move(failures)};
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(std::get<song_document_t>(staged)));
    return commit_editor_edit(
        context, transaction,
        {.label = pending.to_bgm ? "Move selected notes to BGM" : "Move notes and displace to BGM",
         .selection = pending.to_bgm ? std::vector<object_id_t>{} : plan.notes,
         .cursor = plan.reference_position},
        [&](editor_interaction_state_t& selected) {
            for (const auto& moved : plan.bgm_moves)
                selected.selected_objects.push_back(
                    {editor_selection_object_kind_t::bgm_event, moved.note});
            if (pending.to_bgm) {
                for (const auto id : plan.notes)
                    selected.selected_objects.push_back(
                        {editor_selection_object_kind_t::bgm_event, id});
            }
            reselect_bar_lines(selected, bar_line_shift ? &*bar_line_shift : nullptr);
        });
}

auto pending_note_move_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    const auto& pending = *this;
    auto planned =
        plan_move_gesture(context, pending.notes, pending.reference_note, pending.preview_position,
                       to_bgm ? lane_id_t{} : pending.preview_lane, !virtual_bgm_row.has_value(),
                       preferred_bgm_lane, virtual_bgm_row);
    if (auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return editor_interaction_result_t{.diagnostics = std::move(*diagnostics)};
    const auto& plan = std::get<note_move_plan_t>(planned);
    const auto bar_line_shift = drag_bar_line_shift(context, pending, plan);
    if (pending.to_bgm || (pending.displace_to_bgm && !plan.occupants.empty()))
        return commit_bgm_note_move(context, pending, plan, bar_line_shift);
    const auto conflicts = move_conflict_count(plan);
    if (conflicts != 0) {
        auto warning =
            error(diagnostic_code_t::collision, std::to_string(conflicts) + " notes would overlap");
        warning.severity = diagnostic_severity_t::warning;
        return editor_interaction_result_t{.diagnostics = {std::move(warning)}};
    }
    return apply_note_move_plan(context.history, context.state, plan,
                                bar_line_shift ? &*bar_line_shift : nullptr);
}

auto pending_bar_line_move_gesture_t::begin(editor_edit_context_t& context,
                                             measure_id_t reference)
    -> diagnosed_t<std::optional<pending_bar_line_move_gesture_t>> {
    const auto position = local_position_t{reference, 0};
    const auto row = context.scope.measures.row_of(position);
    auto selection = editor_shift_selection(context.state, 0);
    if (!row || std::ranges::find(selection.bar_lines, reference) == selection.bar_lines.end())
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                   "Bar-line move requires a selected bar line", object_id_t{reference.value})}};
    return {.value = pending_bar_line_move_gesture_t{
        .reference_bar_line = reference,
        .reference_row = *row,
        .preview_position = position,
        .selection = std::move(selection),
    }};
}

auto pending_bar_line_move_gesture_t::revise(editor_edit_context_t& context,
                                              local_position_t requested_position)
    -> diagnostic_list_t {
    const auto snapped = snap_position(
        context.scope.chart, context.scope.measures, requested_position,
        edit_snap_grid(context.scope.chart, context.state));
    const auto row = snapped ? context.scope.measures.row_of(*snapped) : std::nullopt;
    if (!row)
        return {error(diagnostic_code_t::row_out_of_range,
                      "Bar-line move target cannot be snapped inside the chart")};
    const auto delta = static_cast<std::int64_t>(*row) -
                       static_cast<std::int64_t>(reference_row);
    auto action = selection;
    action.rows = delta;
    auto transaction = document_command_transaction_t(context.history);
    auto staged = transaction.stage([&](song_document_t& proposed)
                                        -> attempt_t<document_edit_effect_t> {
        auto shifted = stage_shift_selection(proposed, action);
        if (diagnostic_queries::has_errors(shifted.command.diagnostics))
            return {.diagnostics = std::move(shifted.command.diagnostics)};
        return {.value = document_edit_effect_t{},
                .diagnostics = std::move(shifted.command.diagnostics)};
    });
    if (!staged.ok()) return std::move(staged.diagnostics);
    preview_rows = delta;
    preview_position = *snapped;
    return {};
}

auto pending_bar_line_move_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    auto action = selection;
    action.rows = preview_rows;
    return commit_editor_edit(
        context,
        {.label = "Move selected objects",
         .cursor = local_position_t{reference_bar_line, 0},
         .noop_policy = command_noop_policy_t::accept_without_diagnostics},
        [&] { return apply_shift_selection(context.history, action).command; });
}

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

auto add_measure_grid_paint_path(editor_edit_context_t& context, lane_id_t lane,
                                 std::uint64_t first, std::uint64_t last,
                                 pending_note_paint_gesture_t& pending,
                                 std::vector<note_paint_target_t>& additions)
    -> std::optional<diagnostic_t> {
    auto row = first;
    const bool forward = last >= first;
    const auto denominator = edit_snap_grid(context.scope.chart, context.state);
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

auto pending_note_paint_gesture_t::revise(editor_edit_context_t& context, lane_id_t lane,
                                         local_position_t position,
                                         std::optional<bmson_source_geometry_t> geometry)
    -> diagnostic_list_t {
    auto& state = context.state;
    auto& pending = *this;
    const auto target = snap_position(context.scope.chart, context.scope.measures, position,
                                         edit_snap_grid(context.scope.chart, state));
    if (!target)
        return {error(diagnostic_code_t::row_out_of_range,
                                        "paint target cannot be snapped inside chart")};
    const auto target_row = context.scope.measures.row_of(*target);
    const auto previous_row = context.scope.measures.row_of(pending.last_position);
    if (!target_row || !previous_row)
        return {error(diagnostic_code_t::row_out_of_range,
                                        "paint gesture lies outside chart geometry")};
    const auto same_lane = lane == pending.last_lane;
    std::vector<note_paint_target_t> additions;
    if (same_lane) {
        if (const auto invalid = add_measure_grid_paint_path(context, lane, *previous_row,
                                                             *target_row, pending, additions))
            return {*invalid};
    } else if (const auto invalid =
                   add_paint_path_cell(context, lane, *target_row, pending, additions)) {
        return {*invalid};
    }
    if (!additions.empty()) {
        auto expanded = std::make_shared<std::vector<note_paint_target_t>>(*pending.targets);
        expanded->reserve(expanded->size() + additions.size());
        for (const auto& addition : additions) {
            const auto row = context.scope.measures.row_of(addition.position);
            expanded->push_back(addition);
            pending.visited.insert({addition.lane, *row});
        }
        pending.targets = std::move(expanded);
    }
    pending.last_lane = lane;
    pending.last_position = *target;
    state.active_lane = lane;
    place_cursor_at(state, *target);
    if (!source_geometry) source_geometry = geometry;
    return {};
}

auto is_paint_sample_resource(const song_document_t& document, resource_id_t id) -> bool {
    const auto found = std::ranges::find(document.resources, id, &resource_entry_t::id);
    return found != document.resources.end() &&
           std::ranges::find(found->roles, resource_role_t::sample) != found->roles.end();
}

auto pending_note_paint_gesture_t::begin(editor_edit_context_t& context,
                                       const begin_note_paint_gesture_action_t& request)
    -> diagnosed_t<std::optional<pending_note_paint_gesture_t>> {
    const auto& [lane, position, pointer_captured, sound, intent, geometry] = request;
    auto& state = context.state;
    if (intent != note_paint_intent_t::toggle && intent != note_paint_intent_t::draw &&
        intent != note_paint_intent_t::erase)
        return {.diagnostics = {error(diagnostic_code_t::invalid_hold, "note paint intent is invalid")}};
    if (intent != note_paint_intent_t::erase &&
        (state.armed_note_kind == note_kind_t::hold || state.armed_note_kind == note_kind_t::roll))
        return {.diagnostics = {error(
                diagnostic_code_t::invalid_hold,
                "Use the hold gesture to place Hold or Roll notes; painting needs an explicit end.")}};
    if (!pointer_captured)
        return {.diagnostics = {error(diagnostic_code_t::invalid_hold,
                                        "pointer capture is required to begin note painting")}};
    if (intent != note_paint_intent_t::erase && sound &&
        !is_paint_sample_resource(context.history.document, *sound))
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")}};
    const auto resolved =
        resolve_row_placement(context.scope.chart, context.scope.measures, state, lane, position);
    const auto* target = std::get_if<local_position_t>(&resolved);
    if (!target)
        return {.diagnostics = std::get<diagnostic_list_t>(resolved)};
    const auto row = context.scope.measures.row_of(*target);
    if (!row)
        return {.diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                        "paint start lies outside chart geometry")}};
    auto targets = std::make_shared<std::vector<note_paint_target_t>>();
    targets->push_back({lane, *target});
    pending_note_paint_gesture_t pending{
        .targets = std::move(targets),
        .last_lane = lane,
        .last_position = *target,
        .sound = sound,
        .intent = intent,
        .note_kind = state.armed_note_kind,
        .layers = state.armed_note_layers,
        .source_geometry = geometry,
        .hitsample = state.armed_hitsample,
    };
    pending.visited.reserve(64);
    pending.visited.insert({lane, *row});
    state.active_lane = lane;
    place_cursor_at(state, *target);
    return {.value = std::move(pending)};
}

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

auto prepare_stem_note_paint(const document_history_t& history, chart_id_t chart_id,
                             const note_paint_plan_t& plan, object_id_t channel,
                             const std::optional<std::vector<layer_id_t>>& armed_layers,
                             std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    auto scratch = document_history_t::from(history.document);
    if (!plan.removals.empty()) {
        editor_interaction_state_t selection;
        selection.chart = chart_id;
        selection.selected_notes = plan.removals;
        auto removed = stage_editor_selection_delete(scratch, selection);
        if (!removed.value || !removed.ok())
            return std::move(removed.diagnostics);
        if (removed.value->document)
            scratch.document = std::move(*removed.value->document);
    }
    std::vector<object_id_t> notes;
    for (const auto& target : plan.additions) {
        const auto placed =
            insert_bmson_playable_stem_point(scratch, chart_id, channel,
                                             target.position, target.lane, geometry);
        if (!placed && !placed.diagnostics.empty())
            return placed.diagnostics;
        auto* chart = scratch.document.find_chart(chart_id);
        for (auto& measure : chart->measures)
            for (auto& note : measure.notes) {
                if (note.position != target.position || note.lane != target.lane)
                    continue;
                if (!(*history.document.find_chart(chart_id)).find_note(note.id))
                    note.semantic_layers =
                        resolve_placement_layers(*chart, armed_layers, note.sound);
                notes.push_back(note.id);
            }
    }
    // A stroke may visit two lanes at one pulse; retain only its final
    // assignment.
    const auto* chart = scratch.document.find_chart(chart_id);
    std::erase_if(notes, [&](const auto id) { return !(*chart).find_note(id); });
    std::ranges::sort(notes);
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return prepared_note_paint_t{std::move(scratch.document), std::move(notes)};
}

auto prepare_note_paint_document(const document_history_t& history, chart_id_t chart_id,
                                 const note_paint_plan_t& plan, note_kind_t kind,
                                 std::optional<resource_id_t> sound,
                                 const std::optional<std::vector<layer_id_t>>& armed_layers,
                                 std::optional<bmson_source_geometry_t> geometry)
    -> std::variant<prepared_note_paint_t, diagnostic_list_t> {
    const auto stem =
        entry_stem_channel(history.document, *history.document.find_chart(chart_id), sound);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&stem))
        return *failure;
    if (const auto channel = std::get<std::optional<object_id_t>>(stem))
        return prepare_stem_note_paint(history, chart_id, plan, *channel, armed_layers, geometry);
    const auto first_id = plan.additions.empty()
                              ? std::optional<object_id_t>{}
                              : next_object_id(*history.document.find_chart(chart_id));
    if (!plan.additions.empty() &&
        (!first_id ||
         plan.additions.size() - 1 > std::numeric_limits<std::uint64_t>::max() - first_id->value))
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "chart object ID sequence is exhausted")};
    auto proposed = history.document;
    auto* chart = proposed.find_chart(chart_id);
    std::unordered_set<std::uint64_t> removed_ids;
    removed_ids.reserve(plan.removals.size());
    for (const auto id : plan.removals)
        removed_ids.insert(id.value);
    for (auto& measure : chart->measures)
        std::erase_if(measure.notes,
                      [&](const auto& note) { return removed_ids.contains(note.id.value); });
    prepared_note_paint_t prepared{.document = std::move(proposed), .painted_notes = {}};
    auto* prepared_chart = prepared.document.find_chart(chart_id);
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

void prepared_note_paint_t::set_hitsample(chart_id_t chart_id, const std::optional<osu_hitsample_t>& sample) {
    if (!sample) return;
    std::unordered_set<std::uint64_t> ids;
    for (const auto id : painted_notes) ids.insert(id.value);
    for (auto& measure : document.find_chart(chart_id)->measures)
        for (auto& note : measure.notes)
            if (ids.contains(note.id.value)) note.osu_hitsample = sample;
}

auto pending_note_paint_gesture_t::apply(editor_edit_context_t& context) const
    -> editor_interaction_result_t {
    auto& state = context.state;
    const auto& pending = *this;
    auto planned = plan_note_paint(context.scope.chart, context.scope.measures, *pending.targets,
                                   pending.note_kind, pending.intent);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    const auto& plan = std::get<note_paint_plan_t>(planned);
    if (plan.additions.empty() && plan.removals.empty() &&
        pending.intent == note_paint_intent_t::toggle)
        return editor_interaction_result_t{
            .diagnostics = {
                error(diagnostic_code_t::invalid_id, "paint gesture contains no note changes")}};
    if (plan.additions.empty() && plan.removals.empty())
        return editor_interaction_result_t::from(context.history, state);
    auto prepared = prepare_note_paint_document(
        context.history, state.chart, plan, pending.note_kind, pending.sound, pending.layers,
        pending.source_geometry);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&prepared))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    auto result = std::get<prepared_note_paint_t>(std::move(prepared));
    result.set_hitsample(state.chart, pending.hitsample);
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(result.document));
    return commit_editor_edit(context, transaction,
                              {.label = "Paint notes",
                               .selection = std::move(result.painted_notes),
                               .cursor = pending.last_position,
                               .lane = pending.last_lane});
}


auto hold_start_t::validate_sound(const song_document_t& document) const -> diagnostic_list_t {
    if (!sound || is_paint_sample_resource(document, *sound)) return {};
    return {error(diagnostic_code_t::invalid_id, "paint sound is not an existing sample resource")};
}

auto pending_hold_t::from(editor_edit_context_t& context, const hold_start_t& start)
    -> diagnosed_t<std::optional<pending_hold_t>> {
    if (start.source) {
        auto diagnostics = start.validate_sound(context.history.document);
        if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    }
    if (start.source == hold_gesture_source_t::keyboard_lane && context.state.virtual_bms_cursor_row)
        return {.value = pending_hold_t{.position = context.state.cursor, .lane = start.lane,
            .anchor_canvas_row = context.state.virtual_bms_cursor_row,
            .preview_canvas_row = context.state.virtual_bms_cursor_row,
            .gesture_source = start.source, .sound = start.sound,
            .kind = context.state.armed_note_kind, .layers = context.state.armed_note_layers,
            .source_geometry = start.geometry ? std::make_shared<const bmson_source_geometry_t>(*start.geometry) : nullptr,
            .hitsample = context.state.armed_hitsample}};
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures,
        context.state, start.lane, start.position);
    const auto* position = std::get_if<local_position_t>(&resolved);
    if (!position) return {.diagnostics = std::get<diagnostic_list_t>(resolved)};
    if (!start.source) {
        auto diagnostics = start.validate_sound(context.history.document);
        if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    }
    const auto plan = plan_hold_placement<hold_preview_removals_t>(context.scope.chart,
        context.scope.measures, start.lane, *position, *position);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return {.diagnostics = *diagnostics};
    return {.value = pending_hold_t{.position = *position, .lane = start.lane,
        .placement_preview = std::get<pending_hold_placement_preview_t>(plan),
        .gesture_source = start.source, .sound = start.sound,
        .kind = context.state.armed_note_kind, .layers = context.state.armed_note_layers,
        .source_geometry = start.geometry ? std::make_shared<const bmson_source_geometry_t>(*start.geometry) : nullptr,
        .hitsample = context.state.armed_hitsample}};
}

auto pending_hold_t::validate_start(const editor_edit_context_t& context) const -> diagnostic_list_t {
    if (snap_position(context.scope.chart, context.scope.measures, position,
        edit_snap_grid(context.scope.chart, context.state)) == position) return {};
    return {error(diagnostic_code_t::invalid_hold,
        "pending hold start is stale after a document or snap change")};
}

auto pending_hold_t::revise(editor_edit_context_t& context, local_position_t target) -> diagnostic_list_t {
    auto diagnostics = validate_start(context);
    if (!diagnostics.empty()) return diagnostics;
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures,
        context.state, lane, target);
    const auto* snapped = std::get_if<local_position_t>(&resolved);
    if (!snapped) return std::get<diagnostic_list_t>(resolved);
    const auto plan = plan_hold_placement<hold_preview_removals_t>(context.scope.chart,
        context.scope.measures, lane, position, *snapped, kind);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&plan)) return *failure;
    preview_end = *snapped == position ? std::optional<local_position_t>{} : std::optional{*snapped};
    placement_preview = std::get<pending_hold_placement_preview_t>(plan);
    return {};
}

auto hold_gesture_t::can_restore_begin(const hold_start_t& start) -> bool { return start.source.has_value(); }

auto hold_gesture_t::begin(editor_edit_context_t& context, const hold_start_t& start)
    -> diagnosed_t<std::optional<hold_gesture_t>> {
    if (start.source == hold_gesture_source_t::keyboard_lane && !context.state.virtual_bms_cursor_row) {
        auto entry = keyboard_entry_plan_t::from(context, start);
        if (!entry.ok()) return {.diagnostics = std::move(entry.diagnostics)};
        if (entry.value) return {.value = hold_gesture_t{std::move(*entry.value)}};
    }
    auto planned = pending_hold_t::from(context, start);
    if (!planned.ok() || !planned.value) return {.diagnostics = std::move(planned.diagnostics)};
    if (!(start.source == hold_gesture_source_t::keyboard_lane && context.state.virtual_bms_cursor_row)) {
        context.state.active_lane = planned.value->lane;
        place_cursor_at(context.state, planned.value->position);
    }
    return {.value = hold_gesture_t{std::move(*planned.value)}};
}

auto hold_gesture_t::revise(editor_edit_context_t& context, const begin_lane_key_hold_gesture_action_t& action)
    -> diagnostic_list_t {
    if (!is_keyboard())
        return {error(diagnostic_code_t::invalid_hold,
            update_alt_pointer_hold_gesture_action_t::missing_gesture_message)};
    if (primary_ && (primary_->lane == action.lane ||
        std::ranges::any_of(additional_, [&action](const auto& hold) { return hold.lane == action.lane; })))
        return {error(diagnostic_code_t::invalid_hold, "lane key is already held")};
    const auto start = hold_start_t::from(context.state, action);
    auto planned = begin(context, start);
    if (!planned.ok() || !planned.value) return std::move(planned.diagnostics);
    auto next = std::move(*planned.value);
    if (next.immediate_) {
        immediate_ = std::move(next.immediate_);
    } else {
        if (primary_) additional_.push_back(std::move(*primary_));
        primary_ = std::move(next.primary_);
    }
    return {};
}

auto hold_gesture_t::revise_primary(editor_edit_context_t& context, local_position_t position,
    bool keep_visible) -> diagnostic_list_t {
    if (!primary_) return {error(diagnostic_code_t::invalid_hold, revision_message)};
    editor_gesture_state_transaction_t transaction(context, keep_visible);
    auto diagnostics = primary_->revise(context, position);
    if (!diagnostics.empty()) return diagnostics;
    place_cursor_at(context.state, primary_->preview_end.value_or(primary_->position));
    context.state.active_lane = primary_->lane;
    if (keep_visible) {
        primary_->preview_canvas_row.reset();
        if (!keep_cursor_visible(context.scope.chart, context.scope.measures, context.state))
            return {error(diagnostic_code_t::invalid_resolution, "hold movement cannot keep the cursor visible")};
    }
    transaction.accept();
    return {};
}

auto hold_gesture_t::revise(editor_edit_context_t& context, const update_hold_preview_action_t& action)
    -> diagnostic_list_t { return revise_primary(context, action.position, false); }

auto hold_gesture_t::revise(editor_edit_context_t& context, const update_alt_pointer_hold_gesture_action_t& action)
    -> diagnostic_list_t {
    if (!primary_ || primary_->gesture_source != hold_gesture_source_t::alt_pointer)
        return {error(diagnostic_code_t::invalid_hold, action.missing_gesture_message)};
    auto diagnostics = revise_primary(context, action.position, false);
    if (diagnostics.empty()) primary_->preview_canvas_row.reset();
    return diagnostics;
}

auto hold_gesture_t::revise(editor_edit_context_t& context, local_position_t position) -> diagnostic_list_t {
    if (!primary_) return {};
    if (!is_keyboard())
        return {error(diagnostic_code_t::invalid_hold,
            update_alt_pointer_hold_gesture_action_t::missing_gesture_message)};
    editor_gesture_state_transaction_t transaction(context);
    place_cursor_at(context.state, context.state.cursor);
    for (std::size_t i = 0; i < additional_.size(); ++i) {
        std::swap(*primary_, additional_[i]);
        auto diagnostics = revise_primary(context, position, true);
        auto& current = *context.state.gesture<hold_gesture_t>();
        std::swap(*current.primary_, current.additional_[i]);
        if (!diagnostics.empty()) return diagnostics;
    }
    transaction.accept();
    return revise_primary(context, position, true);
}

auto hold_gesture_t::revise(editor_edit_context_t& context, std::uint64_t row) -> diagnostic_list_t {
    if (!primary_ || !has_chart_virtual_tail(context.scope.chart))
        return {error(diagnostic_code_t::invalid_hold, "canvas hold movement requires a BMS lane key")};
    primary_->preview_canvas_row = row;
    for (auto& hold : additional_) hold.preview_canvas_row = row;
    place_cursor_on_canvas_row(context.state, position_at(context.scope.measures, row), row);
    if (!keep_cursor_visible(context.scope.chart, context.scope.measures, context.state))
        return {error(diagnostic_code_t::invalid_resolution,
            "canvas hold movement cannot keep the cursor visible")};
    return {};
}

auto hold_gesture_t::selected_hold(lane_id_t lane) const -> const pending_hold_t* {
    if (primary_ && primary_->lane == lane) return &*primary_;
    const auto found = std::ranges::find(additional_, lane, &pending_hold_t::lane);
    return found == additional_.end() ? nullptr : &*found;
}

void hold_gesture_t::remove_hold(lane_id_t lane) {
    if (primary_ && primary_->lane == lane) {
        if (additional_.empty()) primary_.reset();
        else { primary_ = std::move(additional_.back()); additional_.pop_back(); }
    } else {
        const auto found = std::ranges::find(additional_, lane, &pending_hold_t::lane);
        if (found != additional_.end()) additional_.erase(found);
    }
}

auto hold_gesture_t::revise(editor_edit_context_t&, const commit_hold_action_t& action) -> diagnostic_list_t {
    if (!primary_) return {error(diagnostic_code_t::invalid_hold, action.missing_gesture_message)};
    release_ = release_t{.lane = primary_->lane, .position = action.position, .geometry = action.source_geometry};
    return {};
}

auto hold_gesture_t::revise(editor_edit_context_t& context, const release_alt_pointer_hold_gesture_action_t& action)
    -> diagnostic_list_t {
    if (!primary_ || primary_->gesture_source != hold_gesture_source_t::alt_pointer)
        return {error(diagnostic_code_t::invalid_hold, action.missing_gesture_message)};
    const auto target = snap_position(context.scope.chart, context.scope.measures, action.position,
        edit_snap_grid(context.scope.chart, context.state));
    release_ = release_t{.lane = primary_->lane, .position = target.value_or(action.position),
        .geometry = action.source_geometry};
    return {};
}

auto hold_gesture_t::revise(editor_edit_context_t& context, const release_lane_key_hold_gesture_action_t& action)
    -> diagnostic_list_t {
    if (!primary_) return {error(diagnostic_code_t::invalid_hold, "released lane key has no pending hold")};
    const auto lane = action.lane.value == 0 ? primary_->lane : action.lane;
    const auto* pending = selected_hold(lane);
    if (!pending) return {error(diagnostic_code_t::invalid_hold, "released lane key has no pending hold")};
    if (pending->gesture_source != hold_gesture_source_t::keyboard_lane)
        return {error(diagnostic_code_t::invalid_hold,
            release_alt_pointer_hold_gesture_action_t::missing_gesture_message)};
    release_t release{.lane = lane, .position = pending->preview_end.value_or(pending->position)};
    if (pending->anchor_canvas_row || pending->preview_canvas_row) {
        const auto anchor = pending->anchor_canvas_row ? pending->anchor_canvas_row
            : context.scope.measures.row_of(pending->position);
        if (!anchor) return {error(diagnostic_code_t::row_out_of_range, "hold anchor is unavailable")};
        release.canvas_rows = std::array{*anchor, pending->preview_canvas_row.value_or(*anchor)};
    }
    release_ = release;
    return {};
}

auto hold_gesture_t::apply(editor_edit_context_t& context) -> editor_interaction_result_t {
    if (immediate_) {
        auto result = immediate_->apply(context);
        immediate_.reset();
        return result;
    }
    if (!release_) return editor_interaction_result_t{.diagnostics = {error(diagnostic_code_t::invalid_hold, "hold completion requires a final target")}};
    const auto release = std::exchange(release_, std::nullopt);
    const auto* selected = selected_hold(release->lane);
    if (!selected) return editor_interaction_result_t{.diagnostics = {error(diagnostic_code_t::invalid_hold, "released lane key has no pending hold")}};
    const auto pending = *selected;
    editor_interaction_result_t result;
    if (release->canvas_rows) {
        const place_virtual_bms_hold_action_t action{release->canvas_rows->front(),
            release->canvas_rows->back(), pending.lane, pending.sound, release->geometry};
        const auto rows = resolve_virtual_hold_rows(context, action);
        if (const auto* failure = std::get_if<diagnostic_list_t>(&rows))
            return editor_interaction_result_t{.diagnostics = *failure};
        result = commit_virtual_hold(context, action, std::get<virtual_hold_rows_t>(rows), &pending);
    } else result = pending.apply(context, release->position, release->geometry);
    if (result) remove_hold(release->lane);
    return result;
}

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
    result.set_hitsample(context.state.chart, pending.hitsample);
    auto* chart = result.document.find_chart(context.state.chart);
    for (const auto id : result.painted_notes) {
        auto* note = (*chart).find_note(id);
        note->kind = placement.tail ? held_note_kind(pending.kind) : pending.kind;
        note->end = placement.tail;
        auto& events =
            std::ranges::find(chart->sound_channels, channel, &sound_channel_t::id)->events;
        std::ranges::find(events, *note->sound_channel_event, &sound_channel_event_t::id)->end =
            placement.tail;
    }
    return prepared;
}

auto commit_stem_hold(editor_edit_context_t& context, const pending_hold_t& pending,
                      const hold_placement_plan_t& placement, local_position_t target,
                      std::optional<bmson_source_geometry_t> geometry)
    -> std::optional<editor_interaction_result_t> {
    const auto resolved =
        entry_stem_channel(context.history.document, context.scope.chart, pending.sound);
    const auto* channel = std::get_if<std::optional<object_id_t>>(&resolved);
    if (channel && !*channel)
        return std::nullopt;
    if (const auto* failure = std::get_if<diagnostic_list_t>(&resolved))
        return editor_interaction_result_t{.diagnostics = *failure};
    auto prepared = prepare_stem_hold_document(context, pending, placement, **channel, geometry);
    if (const auto* failure = std::get_if<diagnostic_list_t>(&prepared))
        return editor_interaction_result_t{.diagnostics = *failure};
    auto document = std::get<prepared_note_paint_t>(std::move(prepared));
    const auto created = document.painted_notes.empty()
                             ? std::optional<object_id_t>{}
                             : std::optional{document.painted_notes.front()};
    auto transaction = document_command_transaction_t(context.history);
    transaction.adopt_document(std::move(document.document));
    return commit_editor_edit(context, transaction,
                              {.label = "Place linked BMSON stem note",
                               .cursor = target,
                               .lane = pending.lane,
                               .created_note = created});
}

auto pending_hold_t::apply(editor_edit_context_t& context, local_position_t position,
    std::optional<bmson_source_geometry_t> geometry) const -> editor_interaction_result_t {
    auto& history = context.history;
    auto& state = context.state;
    const auto& pending = *this;
    if (pending.source_geometry)
        geometry = *pending.source_geometry;
    auto start_diagnostics = validate_start(context);
    if (!start_diagnostics.empty())
        return editor_interaction_result_t{.diagnostics = std::move(start_diagnostics)};
    const auto resolved = resolve_row_placement(context.scope.chart, context.scope.measures, state,
                                                pending.lane, position);
    const auto* snapped = std::get_if<local_position_t>(&resolved);
    if (!snapped)
        return editor_interaction_result_t{.diagnostics = std::get<diagnostic_list_t>(resolved)};
    if (*snapped == pending.position &&
        (pending.kind == note_kind_t::hold || pending.kind == note_kind_t::roll))
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  "Move to a different row to finish the Hold or Roll gesture.")}};
    if (pending.sound && !is_paint_sample_resource(history.document, *pending.sound))
        return editor_interaction_result_t{.diagnostics = {error(diagnostic_code_t::invalid_id,
                                        "paint sound is not an existing sample resource")}};
    const auto plan = plan_hold_placement(context.scope.chart, context.scope.measures, pending.lane,
                                          pending.position, *snapped, pending.kind, true);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&plan))
        return editor_interaction_result_t{.diagnostics = *diagnostics};
    const auto& placement = std::get<hold_placement_plan_t>(plan);
    if (auto stem = commit_stem_hold(context, pending, placement, *snapped, geometry))
        return *stem;
    auto transaction = document_command_transaction_t(history);
    auto& proposed = transaction.document();
    auto* chart = proposed.find_chart(state.chart);
    std::optional<object_id_t> id;
    if (placement.adds_note) {
        id = next_object_id(*chart);
        if (!id)
            return editor_interaction_result_t{
                .diagnostics = {
                    error(diagnostic_code_t::invalid_id, "chart object ID sequence is exhausted")}};
    }
    erase_notes(*chart, placement.removals);
    if (placement.adds_note) {
        const auto measure =
            chart->find_measure(placement.head.measure);
        if (measure == nullptr)
            return editor_interaction_result_t{.diagnostics = {error(diagnostic_code_t::unknown_measure,
                                            "normalized hold head refers to an unknown measure")}};
        measure->notes.push_back(
            {.id = *id,
             .position = placement.head,
             .lane = pending.lane,
             .kind = placement.tail ? held_note_kind(pending.kind) : pending.kind,
             .end = placement.tail,
             .sound = pending.sound,
             .semantic_layers = resolve_placement_layers(*chart, pending.layers, pending.sound),
             .osu_hitsample = pending.hitsample});
    }
    const auto label = placement.adds_note
                           ? (placement.tail ? "Place merged hold note" : "Place tap note")
                           : "Remove note";
    return commit_editor_edit(
        context, transaction,
        {.label = label, .cursor = *snapped, .lane = pending.lane, .created_note = id});
}

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
            return editor_interaction_result_t{
                .diagnostics = {error(diagnostic_code_t::invalid_id,
                                      "unknown editor gesture cancellation reason")}};
        }
        return cancel_gesture(history, state);
    }
    if (std::holds_alternative<cancel_editor_area_selection_action_t>(action)) {
        if (state.gesture<pending_editor_area_selection_t>()) state.clear();
        auto result = editor_interaction_result_t::from(history, state);
        result.accepted = true;
        return result;
    }
    return std::nullopt;
}

auto is_keyboard_hold_action_allowed(const editor_interaction_action_t& action) -> bool {
    return is_gesture_action<move_edit_cursor_action_t, page_edit_cursor_action_t,
                             scroll_editor_view_action_t, begin_lane_key_hold_gesture_action_t,
                             release_lane_key_hold_gesture_action_t,
                             place_virtual_bms_hold_action_t>(action);
}

auto is_hold_gesture_action_allowed(const editor_interaction_state_t& state,
                                 const editor_interaction_action_t& action) -> bool {
    if (!state.hold())
        return is_keyboard_hold_action_allowed(action) ||
            is_gesture_action<begin_hold_action_t, update_hold_preview_action_t, commit_hold_action_t>(action);
    if (!state.hold()->gesture_source) return true;
    if (*state.hold()->gesture_source == hold_gesture_source_t::keyboard_lane)
        return is_keyboard_hold_action_allowed(action);
    return is_gesture_action<update_alt_pointer_hold_gesture_action_t,
                             release_alt_pointer_hold_gesture_action_t>(action);
}

auto is_gesture_action_allowed(editor_gesture_kind_t kind, const editor_interaction_state_t& state,
                            const editor_interaction_action_t& action) -> bool {
    switch (kind) {
    case editor_gesture_kind_t::hold:
        return is_hold_gesture_action_allowed(state, action);
    case editor_gesture_kind_t::duration:
        return is_gesture_action<update_note_duration_gesture_action_t>(action);
    case editor_gesture_kind_t::move:
        return state.gesture<pending_bar_line_move_gesture_t>()
            ? is_gesture_action<update_bar_line_move_gesture_action_t>(action)
            : is_gesture_action<update_note_move_gesture_action_t>(action);
    case editor_gesture_kind_t::paint:
        return is_gesture_action<update_note_paint_gesture_action_t,
                                 end_note_paint_gesture_action_t>(action);
    case editor_gesture_kind_t::resource_paint:
        return is_gesture_action<update_resource_paint_gesture_action_t>(action);
    case editor_gesture_kind_t::semantic_paint:
        return is_gesture_action<update_semantic_layer_paint_gesture_action_t>(action);
    case editor_gesture_kind_t::area:
        return is_gesture_action<update_editor_area_selection_action_t>(action);
    case editor_gesture_kind_t::range:
        return is_gesture_action<space_range_selection_action_t, move_edit_cursor_action_t,
                                 page_edit_cursor_action_t, jump_edit_cursor_action_t,
                                 cycle_edit_snap_action_t, set_edit_snap_action_t,
                                 adjust_editor_zoom_action_t, scroll_editor_view_action_t>(action);
    }
    return false;
}

auto gesture_finish_message(editor_gesture_kind_t kind) -> const char* {
    switch (kind) {
    case editor_gesture_kind_t::hold:
        return "finish or cancel the active hold input gesture first";
    case editor_gesture_kind_t::duration:
        return "finish or cancel the active note duration gesture first";
    case editor_gesture_kind_t::move:
        return "finish or cancel the active note move gesture first";
    case editor_gesture_kind_t::paint:
        return "finish or cancel the active note paint gesture first";
    case editor_gesture_kind_t::resource_paint:
        return "finish or cancel the active keysound paint gesture first";
    case editor_gesture_kind_t::semantic_paint:
        return "finish or cancel the active semantic-layer paint gesture first";
    case editor_gesture_kind_t::area:
        return "finish or cancel the active area selection gesture first";
    case editor_gesture_kind_t::range:
        return "finish or cancel the active Space range selection first";
    }
    return "finish or cancel the active gesture first";
}

auto validate_active_gesture_action(const editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action)
    -> std::optional<editor_interaction_result_t> {
    const auto blocked = state.gesture_kind();
    if (!blocked || is_gesture_action_allowed(*blocked, state, action)) return std::nullopt;
    return editor_interaction_result_t{
        .diagnostics = {error(diagnostic_code_t::invalid_hold, gesture_finish_message(*blocked))}};
}
template <class Action> auto is_applying_action(const editor_interaction_action_t& action) -> bool {
    const auto* request = std::get_if<Action>(&action);
    return request && request->operation == proposal_operation_t::apply;
}

auto is_terminal_editor_gesture_action(const editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action) -> bool {
    return is_gesture_action<commit_hold_action_t, place_virtual_bms_hold_action_t,
                             release_alt_pointer_hold_gesture_action_t,
                             release_lane_key_hold_gesture_action_t, end_note_paint_gesture_action_t,
                             update_semantic_layer_paint_gesture_action_t,
                             update_editor_area_selection_action_t>(action) ||
           is_applying_action<update_resource_paint_gesture_action_t>(action) ||
           is_applying_action<update_note_duration_gesture_action_t>(action) ||
           is_applying_action<update_note_move_gesture_action_t>(action) ||
           (state.gesture<wormedit::pending_space_range_selection_t>() &&
            is_gesture_action<space_range_selection_action_t>(action));
}

static auto dispatch_gesture_action(document_history_t& history, editor_interaction_state_t& state,
                                    const editor_interaction_action_t& action)
    -> editor_interaction_result_t {
    if (auto canceled = handle_editor_cancellation(history, state, action))
        return *canceled;
    if (auto stale = validate_gesture_capture(history, state)) return std::move(*stale);
    if (auto rejected = validate_active_gesture_action(state, action))
        return std::move(*rejected);
    auto result = dispatch_editor_action_impl(history, state, action);
    if (!result && is_terminal_editor_gesture_action(state, action)) {
        (state).clear();
        result.view.reset();
    }
    return result;
}

auto dispatch_editor_action(document_history_t& history, editor_interaction_state_t& state,
                            const editor_interaction_action_t& action)
    -> editor_interaction_result_t {
    const auto* area = std::get_if<update_editor_area_selection_action_t>(&action);
    const auto selection_action = is_gesture_action<select_editor_notes_action_t, select_editor_objects_action_t,
        select_editor_lane_action_t, select_bms_canvas_measure_action_t, space_range_selection_action_t>(action) ||
        (area && area->operation == proposal_operation_t::apply);
    auto before = selection_action ? std::optional{editor_selection_snapshot_t::from(state)} : std::nullopt;
    const auto sequence = history.next_history_sequence;
    auto result = dispatch_gesture_action(history, state, action);
    if (before && result.accepted && !result.document_changed && history.next_history_sequence == sequence)
        history.selections.record(history, std::move(*before), editor_selection_snapshot_t::from(state));
    result.ensure_view(history, state);
    return result;
}

} // namespace wormedit
