#pragma once

#include "interaction_internal.h"

namespace wormedit {

template<class Gesture, class BeginArgument, class... Arguments>
concept editor_gesture = requires(Gesture& gesture, editor_edit_context_t& context,
                                  BeginArgument begin_argument, Arguments... arguments) {
    { Gesture::begin(context, begin_argument) }
        -> std::same_as<diagnosed_t<std::optional<Gesture>>>;
    { Gesture::kind } -> std::convertible_to<editor_gesture_kind_t>;
    { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
};

// Shared boundary for dispatched actions and direct Core gesture entry points.
inline auto validate_gesture_capture(document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<editor_interaction_result_t> {
    if (!state.is_stale(history.document.revision)) return std::nullopt;
    state.clear();
    return editor_interaction_result_t{
        .diagnostics = {
            error(diagnostic_code_t::invalid_hold, stale_action_message("Editor gesture"))}};
}

template<class Gesture>
inline auto validate_gesture_capture(editor_edit_context_t& context)
    -> std::optional<editor_interaction_result_t> {
    if (!context.has_validated_capture()) {
        if (auto stale = validate_gesture_capture(context.history, context.state)) return stale;
    }
    if constexpr (requires { Gesture::needs_initial_view; }) {
        if (Gesture::needs_initial_view && !context.initial_view)
            return editor_interaction_result_t{
                .diagnostics = {error(diagnostic_code_t::invalid_resolution,
                                      "editor gesture requires a field view")}};
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
            result.view.reset();
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
[[nodiscard]] auto gesture_revision_message(const Arguments&...) -> std::string_view {
    if constexpr (sizeof...(Arguments) == 1) {
        using argument_type = std::tuple_element_t<0, std::tuple<Arguments...>>;
        if constexpr (requires { argument_type::missing_gesture_message; })
            return argument_type::missing_gesture_message;
    }
    return Gesture::revision_message;
}

template<class Gesture, class... Arguments>
[[nodiscard]] auto require_active_gesture(editor_edit_context_t& context, const Arguments&... arguments)
    -> result_t<Gesture*, editor_interaction_result_t> {
    if (auto stale = validate_gesture_capture<Gesture>(context))
        return std::unexpected{std::move(*stale)};
    auto* gesture = context.state.gesture<Gesture>();
    if (!gesture)
        return std::unexpected{editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  std::string(gesture_revision_message<Gesture>(arguments...)))}}};
    return gesture;
}

template<class Gesture>
[[nodiscard]] auto finish_gesture(editor_edit_context_t& context, bool is_valid,
                                  diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    if (!is_valid) {
        context.state.clear();
        return editor_interaction_result_t{.diagnostics = std::move(diagnostics)};
    }
    auto gesture = std::move(*context.state.gesture<Gesture>());
    context.state.clear();
    auto result = gesture.apply(context);
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

template <class Gesture, class... Arguments>
[[nodiscard]] auto begin_gesture_impl(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    if (auto stale = validate_gesture_capture<Gesture>(context)) return std::move(*stale);
    if (context.state.is_pending())
        return editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  "another editor gesture is already active")}};
    bool can_restore{};
    if constexpr (requires { Gesture::can_restore_begin(arguments...); })
        can_restore = Gesture::can_restore_begin(arguments...);
    editor_gesture_state_transaction_t transaction(context, can_restore);
    auto planned = Gesture::begin(context, std::forward<Arguments>(arguments)...);
    if (!planned.ok() || !planned.value) {
        transaction.rollback();
        return editor_interaction_result_t{.diagnostics = std::move(planned.diagnostics)};
    }
    capture_gesture(context, std::move(*planned.value));
    return transaction.finish(publish_gesture<Gesture>(context));
}

template <class Gesture, class... Arguments>
[[nodiscard]] auto begin_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto result = begin_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    context.ensure_failure_view(result);
    return result;
}

template <class Gesture, class... Arguments>
    requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
        { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
        { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
    }
[[nodiscard]] auto revise_gesture_impl(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto active = require_active_gesture<Gesture>(context, arguments...);
    if (!active) return std::move(active.error());
    bool can_restore{};
    if constexpr (requires { Gesture::can_restore_revision(arguments...); })
        can_restore = Gesture::can_restore_revision(arguments...);
    editor_gesture_state_transaction_t transaction(context, can_restore);
    auto diagnostics = (*active)->revise(context, std::forward<Arguments>(arguments)...);
    if (diagnostic_queries::has_errors(diagnostics)) {
        transaction.rollback();
        return editor_interaction_result_t{.diagnostics = std::move(diagnostics)};
    }
    return transaction.finish(publish_gesture<Gesture>(context));
}

template <class Gesture, class... Arguments>
    requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
        { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
        { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
    }
[[nodiscard]] auto revise_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto result = revise_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    context.ensure_failure_view(result);
    return result;
}

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto apply_gesture(editor_edit_context_t& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto revised = revise_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    auto result = finish_gesture<Gesture>(context, static_cast<bool>(revised),
                                          std::move(revised.diagnostics));
    context.ensure_failure_view(result);
    return result;
}

enum class editor_gesture_apply_view_policy_t { rebuild, captured };

template<class Gesture, class... Arguments> requires requires(Gesture& gesture, editor_edit_context_t& context, Arguments... arguments) {
    { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto apply_gesture(editor_edit_context_t& context, editor_gesture_apply_view_policy_t policy,
                                  Arguments&&... arguments) -> editor_interaction_result_t {
    if (policy == editor_gesture_apply_view_policy_t::rebuild)
        return apply_gesture<Gesture>(context, std::forward<Arguments>(arguments)...);
    auto active = require_active_gesture<Gesture>(context, arguments...);
    auto diagnostics = active ? (*active)->revise(context, std::forward<Arguments>(arguments)...)
                              : std::move(active.error().diagnostics);
    const auto is_valid = active && !diagnostic_queries::has_errors(diagnostics);
    auto result = finish_gesture<Gesture>(context, is_valid, std::move(diagnostics));
    context.ensure_failure_view(result);
    return result;
}

inline auto cancel_gesture(document_history_t& history, editor_interaction_state_t& state)
    -> editor_interaction_result_t {
    (state).clear();
    auto result = editor_interaction_result_t::from(history, state);
    result.accepted = true;
    return result;
}

} // namespace wormedit
