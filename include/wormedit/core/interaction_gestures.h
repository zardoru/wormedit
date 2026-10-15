#pragma once

#include <wormedit/core/interaction.h>
#include <tuple>
#include <utility>

namespace wormedit {

// The only inventory of pending core gesture fields. Consumers supply policy, not inventories.
template<class State, class Visitor>
void visit_editor_gestures(State& state, Visitor&& visit) {
    const auto fields = std::tuple{
        std::pair{editor_gesture_kind_t::hold, &editor_interaction_state_t::pending_hold},
        std::pair{editor_gesture_kind_t::keyboard_holds, &editor_interaction_state_t::additional_keyboard_holds},
        std::pair{editor_gesture_kind_t::semantic_paint, &editor_interaction_state_t::pending_semantic_layer_paint_gesture},
        std::pair{editor_gesture_kind_t::area, &editor_interaction_state_t::pending_editor_area_selection},
        std::pair{editor_gesture_kind_t::range, &editor_interaction_state_t::pending_space_range_selection}};
    // Temporary: legacy fields join the active variant one kind per T-634 checkpoint.
    if (state.active_gesture)
        visit(std::visit([](const auto& proposal) { return proposal.payload.kind; }, *state.active_gesture),
              state.active_gesture);
    std::apply([&](const auto&... field) { (visit(field.first, state.*field.second), ...); }, fields);
}

template<class Field>
auto is_editor_gesture_pending(const Field& field) -> bool {
    if constexpr (requires { field.has_value(); }) return field.has_value();
    else return !field.empty();
}


template<class Field>
void clear_editor_gesture(Field& field) {
    if constexpr (requires { field.reset(); }) field.reset();
    else field.clear();
}

inline void clear_editor_gestures(editor_interaction_state_t& state) {
    visit_editor_gestures(state, [](auto, auto& field) { clear_editor_gesture(field); });
}

template<class Field>
auto is_editor_gesture_stale(const Field& field, std::uint64_t revision) -> bool {
    if constexpr (std::same_as<std::remove_cvref_t<Field>, std::optional<active_gesture_t>>)
        return field && std::visit([&](const auto& proposal) {
            return !proposal.stamp.is_revision_current(revision);
        }, *field);
    else if constexpr (requires { field.has_value(); })
        return field && !field->stamp.is_revision_current(revision);
    else {
        for (const auto& hold : field)
            if (!hold.stamp.is_revision_current(revision)) return true;
        return false;
    }
}


inline auto is_editor_gestures_stale(const editor_interaction_state_t& state,
                                  std::uint64_t revision) -> bool {
    bool stale{};
    visit_editor_gestures(state, [&](auto, const auto& field) {
        stale |= is_editor_gesture_stale(field, revision);
    });
    return stale;
}

} // namespace wormedit
