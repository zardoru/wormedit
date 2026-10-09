#include "sys/compose_note_to_bgm.h"

#include <wormedit/core/note_to_bgm.h>

#include <algorithm>
#include <tuple>
#include <vector>

namespace wormedit::sys {
namespace {

auto qualifies(const chart_document_t& chart, object_id_t id, bool only_stem_taps) -> bool {
    return std::ranges::any_of(chart.measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.notes, [&](const auto& note) {
            return note.id == id && (!only_stem_taps || (note.sound_channel_event &&
                                      chart.source_family == format_family_t::bmson));
        });
    });
}

struct lane_move_candidate_t {
    std::size_t measure{};
    std::uint64_t row{};
    editor_selection_object_kind_t kind{};
    object_id_t id;
    lane_id_t lane;
};

auto collect_selected_note_ids(const editor_interaction_state_t& state)
    -> std::vector<object_id_t> {
    auto ids = state.selected_notes;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::note) ids.push_back(object.id);
    std::ranges::sort(ids, {}, &object_id_t::value);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

auto collect_selected_bgm_ids(const editor_interaction_state_t& state,
                              std::optional<object_id_t> also) -> std::vector<object_id_t> {
    std::vector<object_id_t> ids;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event) ids.push_back(object.id);
    if (also && std::ranges::find(ids, *also) == ids.end()) ids.push_back(*also);
    return ids;
}

auto precedes(const lane_move_candidate_t& left, const lane_move_candidate_t& right) -> bool {
    return std::tie(left.measure, left.row, left.kind, left.lane.value, left.id.value) <
           std::tie(right.measure, right.row, right.kind, right.lane.value, right.id.value);
}

auto first_selected_lane_object(const chart_document_t& chart,
                                std::span<const object_id_t> notes,
                                std::span<const object_id_t> bgm_events)
    -> std::optional<lane_move_candidate_t> {
    std::optional<lane_move_candidate_t> first;
    const auto consider = [&](lane_move_candidate_t candidate) {
        if (!first || precedes(candidate, *first)) first = candidate;
    };
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        const auto& measure = chart.measures[index];
        for (const auto& note : measure.notes)
            if (std::ranges::find(notes, note.id) != notes.end())
                consider({index, note.position.row, editor_selection_object_kind_t::note,
                          note.id, note.lane});
        for (const auto& event : measure.bgm_events)
            if (std::ranges::find(bgm_events, event.id) != bgm_events.end())
                consider({index, event.position.row,
                          editor_selection_object_kind_t::bgm_event, event.id,
                          lane_id_t{event.source_lane.value}});
    }
    return first;
}

} // namespace

auto move_selected_notes_to_bgm(document_history_t& history, editor_interaction_state_t& state,
                                std::optional<object_id_t> lane, bool only_stem_taps, bool keep_selected,
                                bool successive)
    -> note_to_bgm_outcome_t {
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) return {};
    const auto candidates = collect_selected_note_ids(state);
    std::vector<object_id_t> moving;
    for (const auto id : candidates)
        if (qualifies(*chart, id, only_stem_taps)) moving.push_back(id);
    if (moving.empty()) return {};
    if (successive) {
        const auto first = first_selected_lane_object(*chart, moving, {});
        if (!first) return {};
        moving = {first->id};
    }
    note_to_bgm_outcome_t outcome{.handled = true};
    outcome.command = move_notes_to_bgm(history, history.document.revision, chart->id, moving, lane);
    if (!outcome.command.applied) return outcome;
    const auto is_moved = [&](object_id_t id) { return std::ranges::find(moving, id) != moving.end(); };
    std::erase_if(state.selected_notes, is_moved);
    std::erase_if(state.selected_objects, [&](const auto& object) {
        return object.kind == editor_selection_object_kind_t::note && is_moved(object.id);
    });
    if (keep_selected && !successive)
        for (const auto id : moving)
            state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, id});
    outcome.selection_exhausted = state.selected_notes.empty() && state.selected_objects.empty();
    return outcome;
}

auto move_selected_bgm_to_lane(document_history_t& history, editor_interaction_state_t& state, lane_id_t lane,
                               std::optional<object_id_t> also, bool successive) -> note_to_bgm_outcome_t {
    auto proposed_state = state;
    if (also && std::ranges::none_of(proposed_state.selected_objects, [&](const auto& object) {
            return object.kind == editor_selection_object_kind_t::bgm_event && object.id == *also;
        }))
        proposed_state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, *also});
    if (collect_selected_bgm_ids(proposed_state, std::nullopt).empty() || !lane) return {};
    const auto result = dispatch_editor_action(
        history, proposed_state, assign_selected_objects_to_lane_action_t{lane, successive});
    if (result.accepted) state = std::move(proposed_state);
    return {.handled = true,
            .command = {.applied = result.accepted, .revision = history.document.revision,
                        .diagnostics = result.diagnostics},
            .selection_exhausted = state.selected_objects.empty() && state.selected_notes.empty()};
}

} // namespace wormedit::sys
