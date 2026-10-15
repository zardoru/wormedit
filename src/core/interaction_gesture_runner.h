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
template<class Gesture>
inline auto validate_gesture_capture(editor_edit_context_t& context)
    -> std::optional<editor_interaction_result_t> {
    if ((context.state).is_stale(context.history.document.revision)) {
        (context.state).clear();
        return interaction_detail::diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, stale_action_message("Editor gesture"))});
    }
    if constexpr (requires { Gesture::needs_initial_view; }) {
        if (Gesture::needs_initial_view && !context.initial_view)
            return interaction_detail::diagnostic_result(context.history, context.state,
                {error(diagnostic_code_t::invalid_resolution, "editor gesture requires a field view")});
    }
    return std::nullopt;
}

// Existing input transactions snapshot only the paths that already required rollback.
class editor_gesture_state_transaction_t {
public:
    explicit editor_gesture_state_transaction_t(editor_edit_context_t& context, bool enabled = true)
        : context_(context) {
        if (enabled) before_ = context.state;
    }
    ~editor_gesture_state_transaction_t() { rollback(); }
    void rollback() {
        if (!before_) return;
        context_.state = std::move(*before_);
        before_.reset();
    }
    void accept() { before_.reset(); }
    [[nodiscard]] auto finish(editor_interaction_result_t result) -> editor_interaction_result_t {
        if (!result && before_) {
            rollback();
            auto refreshed = editor_interaction_result_t::from(context_.history, context_.state);
            result.view = std::move(refreshed.view);
        } else accept();
        return result;
    }
private:
    editor_edit_context_t& context_;
    std::optional<editor_interaction_state_t> before_;
};

template<class Gesture>
void capture_gesture(editor_edit_context_t& context, Gesture&& gesture) {
    context.state.active_gesture = proposal_t<std::remove_cvref_t<Gesture>, document_stamp_t>{
        .stamp = document_stamp_t::from(context.history.document.revision),
        .payload = std::forward<Gesture>(gesture)};
}

template<class Gesture>
void refresh_gesture_stamp(editor_edit_context_t& context) {
    if (auto* stored = gesture_storage<Gesture>(context.state.active_gesture ? &*context.state.active_gesture : nullptr))
        stored->stamp = document_stamp_t::from(context.history.document.revision);
}

template<class Gesture, class... Arguments>
[[nodiscard]] auto gesture_update_message(const Arguments&...) -> std::string_view {
    if constexpr (sizeof...(Arguments) == 1) {
        using argument_type = std::tuple_element_t<0, std::tuple<Arguments...>>;
        if constexpr (requires { argument_type::missing_gesture_message; })
            return argument_type::missing_gesture_message;
    }
    return Gesture::update_message;
}

template<class Gesture, class... Arguments>
[[nodiscard]] auto require_active_gesture(editor_edit_context_t& context, const Arguments&... arguments)
    -> result_t<Gesture*, editor_interaction_result_t> {
    if (auto stale = validate_gesture_capture<Gesture>(context))
        return std::unexpected{std::move(*stale)};
    auto* gesture = context.state.gesture<Gesture>();
    if (!gesture)
        return std::unexpected{interaction_detail::diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, std::string(gesture_update_message<Gesture>(arguments...)))})};
    return gesture;
}

template<class Gesture>
[[nodiscard]] auto finish_gesture(editor_edit_context_t& context, bool is_valid,
                                  diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    if (!is_valid) {
        context.state.clear();
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(diagnostics));
    }
    auto gesture = std::move(*context.state.gesture<Gesture>());
    context.state.clear();
    auto result = gesture.apply(context);
    if (!result && !result.view)
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(result.diagnostics));
    if constexpr (requires { gesture.has_pending(); }) {
        if (result && gesture.has_pending()) {
            capture_gesture(context, std::move(gesture));
            auto refreshed = editor_interaction_result_t::from(context.history, context.state);
            result.view = std::move(refreshed.view);
        }
    }
    return result;
}

template<class Gesture>
[[nodiscard]] auto publish_gesture(editor_edit_context_t& context) -> editor_interaction_result_t {
    if constexpr (requires(Gesture& gesture) { gesture.has_immediate_edit(); }) {
        if (context.state.gesture<Gesture>()->has_immediate_edit())
            return finish_gesture<Gesture>(context, true, {});
    }
    return editor_interaction_result_t::from(context.history, context.state);
}

template<class Gesture, class... Arguments>
[[nodiscard]] auto begin_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    if (auto stale = validate_gesture_capture<Gesture>(context)) return std::move(*stale);
    if (context.state.is_pending())
        return interaction_detail::diagnostic_result(context.history, context.state,
            {error(diagnostic_code_t::invalid_hold, "another editor gesture is already active")});
    bool can_restore{};
    if constexpr (requires { Gesture::can_restore_begin(arguments...); })
        can_restore = Gesture::can_restore_begin(arguments...);
    editor_gesture_state_transaction_t transaction(context, can_restore);
    auto planned = Gesture::begin(context, std::forward<Arguments>(arguments)...);
    if (!planned.ok() || !planned.value) {
        transaction.rollback();
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(planned.diagnostics));
    }
    capture_gesture(context, std::move(*planned.value));
    return transaction.finish(publish_gesture<Gesture>(context));
}

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto update_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto active = require_active_gesture<Gesture>(context, arguments...);
    if (!active) return std::move(active.error());
    bool can_restore{};
    if constexpr (requires { Gesture::can_restore_update(arguments...); })
        can_restore = Gesture::can_restore_update(arguments...);
    editor_gesture_state_transaction_t transaction(context, can_restore);
    auto diagnostics = (*active)->update(context, std::forward<Arguments>(arguments)...);
    if (diagnostic_queries::has_errors(diagnostics)) {
        transaction.rollback();
        return interaction_detail::diagnostic_result(context.history, context.state, std::move(diagnostics));
    }
    return transaction.finish(publish_gesture<Gesture>(context));
}

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto commit_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto updated = update_gesture<Gesture>(context, std::forward<Arguments>(arguments)...);
    return finish_gesture<Gesture>(context, static_cast<bool>(updated), std::move(updated.diagnostics));
}

enum class editor_gesture_commit_view_policy_t { rebuild, captured };

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.update(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto commit_gesture(editor_edit_context_t& context, editor_gesture_commit_view_policy_t policy,
                                  Arguments&&... arguments) -> editor_interaction_result_t {
    if (policy == editor_gesture_commit_view_policy_t::rebuild)
        return commit_gesture<Gesture>(context, std::forward<Arguments>(arguments)...);
    auto active = require_active_gesture<Gesture>(context, arguments...);
    if (!active)
        return finish_gesture<Gesture>(context, false, std::move(active.error().diagnostics));
    auto diagnostics = (*active)->update(context, std::forward<Arguments>(arguments)...);
    const auto is_valid = !diagnostic_queries::has_errors(diagnostics);
    return finish_gesture<Gesture>(context, is_valid, std::move(diagnostics));
}

inline auto cancel_gesture(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    (state).clear();
    auto result = editor_interaction_result_t::from(history, state);
    result.accepted = true;
    return result;
}

} // namespace wormedit
