#include "sys/compose_note_to_bgm.h"

#include <wormedit/core/note_to_bgm.h>

#include <algorithm>
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

} // namespace

auto move_selected_notes_to_bgm(document_history_t& history, editor_interaction_state_t& state,
                                std::optional<object_id_t> lane, bool only_stem_taps, bool keep_selected)
    -> note_to_bgm_outcome_t {
    const auto chart = std::ranges::find(history.document.charts, state.chart, &chart_document_t::id);
    if (chart == history.document.charts.end()) return {};
    // A note can be in the selection lists, in the object list, or in both.
    std::vector<object_id_t> candidates = state.selected_notes;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::note) candidates.push_back(object.id);
    std::ranges::sort(candidates, {}, &object_id_t::value);
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());
    std::vector<object_id_t> moving;
    for (const auto id : candidates)
        if (qualifies(*chart, id, only_stem_taps)) moving.push_back(id);
    if (moving.empty()) return {};
    note_to_bgm_outcome_t outcome{.handled = true};
    outcome.command = move_notes_to_bgm(history, history.document.revision, chart->id, moving, lane);
    if (!outcome.command.applied) return outcome;
    const auto is_moved = [&](object_id_t id) { return std::ranges::find(moving, id) != moving.end(); };
    std::erase_if(state.selected_notes, is_moved);
    std::erase_if(state.selected_objects, [&](const auto& object) {
        return object.kind == editor_selection_object_kind_t::note && is_moved(object.id);
    });
    if (keep_selected)
        for (const auto id : moving)
            state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, id});
    outcome.selection_exhausted = state.selected_notes.empty() && state.selected_objects.empty();
    return outcome;
}

auto move_selected_bgm_to_lane(document_history_t& history, editor_interaction_state_t& state, lane_id_t lane,
                               std::optional<object_id_t> also) -> note_to_bgm_outcome_t {
    std::vector<object_id_t> moving;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event) moving.push_back(object.id);
    if (also && std::ranges::find(moving, *also) == moving.end()) moving.push_back(*also);
    if (moving.empty() || !lane) return {};
    note_to_bgm_outcome_t outcome{.handled = true};
    outcome.command = move_bgm_events_to_lane(history, history.document.revision, state.chart, moving, lane);
    if (!outcome.command.applied) return outcome;
    const auto is_moved = [&](object_id_t id) { return std::ranges::find(moving, id) != moving.end(); };
    std::erase_if(state.selected_objects, [&](const auto& object) {
        return object.kind == editor_selection_object_kind_t::bgm_event && is_moved(object.id);
    });
    for (const auto id : moving) {
        state.selected_notes.push_back(id);
        state.selected_objects.push_back({editor_selection_object_kind_t::note, id});
    }
    outcome.selection_exhausted = false;
    return outcome;
}

} // namespace wormedit::sys
