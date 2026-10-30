#include "interaction_edit_commit.h"
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/note_to_bgm.h>

#include <algorithm>
#include <limits>
#include <optional>
#include <ranges>
#include <span>
#include <tuple>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit {
using interaction_detail::find_active_lane;
using interaction_detail::sync_linked_sound_events;

namespace {

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

auto apply_note_lane_assignment(chart_document_t& chart, const song_document_t& source,
                                std::span<const assignment_candidate_t> candidates, lane_id_t lane,
                                std::string label) -> attempt_t<document_edit_effect_t> {
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
            sync_linked_sound_events(chart, plan, source))
        return {.diagnostics = {*failure}};
    return {.value =
                document_edit_effect_t{.changed = true, .command_label = std::move(label)}};
}

auto stage_note_lane_assignment(document_command_transaction_t& transaction, chart_id_t chart_id,
                                std::span<const assignment_candidate_t> candidates, lane_id_t lane,
                                std::string label) -> attempt_t<document_edit_effect_t> {
    return transaction.stage(chart_id, "lane assignment chart is unavailable",
        [&](chart_document_t& chart, song_document_t&) {
            return apply_note_lane_assignment(chart, transaction.source_document(), candidates, lane, label);
        });
}

} // namespace

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

namespace {

struct lane_move_candidate_t {
    std::size_t measure{};
    std::uint64_t row{};
    editor_selection_object_kind_t kind{};
    object_id_t id;
    lane_id_t lane;
};

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

} // namespace

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

auto stage_assign_note_lanes(document_command_transaction_t& transaction,
                             const editor_interaction_state_t& state, lane_id_t lane)
    -> attempt_t<document_edit_effect_t> {
    return transaction.stage(state.chart, "lane assignment chart is unavailable",
        {.storage = chart_edit_storage_support_t::objects, .objects = state.selected_notes},
        [&](chart_document_t& chart, const song_document_t&, std::vector<object_id_t>&)
            -> attempt_t<document_edit_effect_t> {
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
            return apply_note_lane_assignment(chart, transaction.source_document(), candidates, lane,
                                              "Assign selected notes to lane");
        });
}


} // namespace wormedit
