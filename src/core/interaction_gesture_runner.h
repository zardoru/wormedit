#pragma once

#include "interaction_internal.h"

namespace wormedit {

template<class Gesture, class BeginArgument, class... Arguments>
concept editor_gesture = requires(Gesture& gesture, editor_edit_context_t& context,
                                  BeginArgument begin_argument, Arguments... arguments) {
    { Gesture::begin(context, begin_argument) }
        -> std::same_as<diagnosed_t<std::optional<Gesture>>>;
    { Gesture::kind } -> std::convertible_to<editor_gesture_kind_t>;
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
};

// Captured state is validated at the runner boundary, including direct core callers.
inline auto validate_gesture_capture(editor_edit_context_t& context)
    -> std::optional<editor_interaction_result_t> {
    if (!is_editor_gestures_stale(context.state, context.history.document.revision))
        return std::nullopt;
    clear_editor_gestures(context.state);
    return interaction_detail::diagnostic_result(context.history, context.state,
        {error(diagnostic_code_t::invalid_hold, stale_action_message("Editor gesture"))});
}

template<class Gesture, class... Arguments>
[[nodiscard]] auto begin_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    if (auto stale = validate_gesture_capture(context)) return std::move(*stale);
    if (has_pending_editor_gesture(context.state))
        return interaction_detail::diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    auto planned = Gesture::begin(context, std::forward<Arguments>(arguments)...);
    if (!planned.ok() || !planned.value)
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(planned.diagnostics));
    context.state.active_gesture = proposal_t<Gesture, document_stamp_t>{
        .stamp = document_stamp_t::from(context.history.document.revision),
        .payload = std::move(*planned.value)};
    return editor_interaction_result_t::from(context.history, context.state);
}

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto update_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    if (auto stale = validate_gesture_capture(context)) return std::move(*stale);
    auto* gesture = context.state.gesture<Gesture>();
    if (!gesture)
        return interaction_detail::diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, std::string(Gesture::update_message))});
    auto diagnostics = gesture->update(context, std::forward<Arguments>(arguments)...);
    if (diagnostic_queries::has_errors(diagnostics))
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(diagnostics));
    return editor_interaction_result_t::from(context.history, context.state);
}

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto commit_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto updated = update_gesture<Gesture>(context, std::forward<Arguments>(arguments)...);
    if (!updated) {
        clear_editor_gestures(context.state);
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(updated.diagnostics));
    }
    // End the capture before applying so the returned view cannot retain a ghost preview.
    auto gesture = std::move(*context.state.gesture<Gesture>());
    context.state.active_gesture.reset();
    return gesture.apply(context);
}

inline auto cancel_gesture(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    clear_editor_gestures(state);
    auto result = editor_interaction_result_t::from(history, state);
    result.accepted = true;
    return result;
}

} // namespace wormedit
