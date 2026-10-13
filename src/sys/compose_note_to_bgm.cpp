#include "sys/compose_note_to_bgm.h"

#include <wormedit/core/note_to_bgm.h>

#include <algorithm>
#include <vector>

namespace wormedit::sys {
namespace {

auto collect_selected_bgm_ids(const editor_interaction_state_t& state,
                              std::optional<object_id_t> also) -> std::vector<object_id_t> {
    std::vector<object_id_t> ids;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event) ids.push_back(object.id);
    if (also && std::ranges::find(ids, *also) == ids.end()) ids.push_back(*also);
    return ids;
}

} // namespace

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
    return {.command = {.applied = result.accepted, .revision = history.document.revision,
                        .diagnostics = result.diagnostics},
            .payload = {.handled = true,
                        .selection_exhausted = state.selected_objects.empty() && state.selected_notes.empty()}};
}

} // namespace wormedit::sys
