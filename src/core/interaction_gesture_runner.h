#pragma once

#include "interaction_internal.h"
#include "interaction_edit_commit.h"

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
    const bool stale = state.active_gesture && std::visit([&](const auto& proposal) {
        using gesture_type = std::remove_cvref_t<decltype(proposal.payload)>;
        if constexpr (requires { gesture_type::document_owned; })
            return !proposal.stamp.is_current(history.document);
        else return !proposal.stamp.is_revision_current(history.document.revision);
    }, *state.active_gesture);
    if (!stale) return std::nullopt;
    state.clear();
    return editor_interaction_result_t{
        .diagnostics = {
            error(diagnostic_code_t::invalid_hold, stale_action_message("Editor gesture"))}};
}

template<class Gesture, editor_gesture_context Context>
inline auto validate_gesture_capture(Context& context)
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
    template<editor_gesture_context Context>
    explicit editor_gesture_state_transaction_t(Context& context, bool enabled = true)
        : state_(context.state) {
        if (enabled) before_ = state_;
    }
    ~editor_gesture_state_transaction_t() { rollback(); }
    void rollback() {
        if (!before_) return;
        state_ = std::move(*before_);
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
    editor_interaction_state_t& state_;
    std::optional<editor_interaction_state_t> before_;
};

template<class Gesture, editor_gesture_context Context>
void capture_gesture(Context& context, Gesture&& gesture) {
    auto stamp = context.capture_stamp();
    context.state.active_gesture = proposal_t<std::remove_cvref_t<Gesture>, document_stamp_t>{
        .stamp = std::move(stamp),
        .payload = std::forward<Gesture>(gesture)};
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

template<class Gesture, editor_gesture_context Context, class... Arguments>
[[nodiscard]] auto require_active_gesture(Context& context, const Arguments&... arguments)
    -> result_t<Gesture*, editor_interaction_result_t> {
    if (auto stale = validate_gesture_capture<Gesture>(context))
        return std::unexpected{std::move(*stale)};
    auto* gesture = context.state.template gesture<Gesture>();
    if (!gesture)
        return std::unexpected{editor_interaction_result_t{
            .diagnostics = {error(diagnostic_code_t::invalid_hold,
                                  std::string(gesture_revision_message<Gesture>(arguments...)))}}};
    return gesture;
}

template<class Gesture, editor_gesture_context Context>
[[nodiscard]] auto finish_gesture(Context& context, bool is_valid,
                                  diagnostic_list_t diagnostics) -> editor_interaction_result_t {
    if (!is_valid) {
        context.state.clear();
        return editor_interaction_result_t{.diagnostics = std::move(diagnostics)};
    }
    auto applying = context;
    applying.gesture_capture = *context.state.template gesture_stamp<Gesture>();
    auto gesture = std::move(*context.state.template gesture<Gesture>());
    context.state.clear();
    auto result = gesture.apply(applying);
    if constexpr (requires { gesture.has_pending(); }) {
        if (result && gesture.has_pending()) {
            capture_gesture(applying, std::move(gesture));
            auto refreshed = context.result();
            result.view = std::move(refreshed.view);
        }
    }
    return result;
}

template<class Gesture, editor_gesture_context Context>
[[nodiscard]] auto publish_gesture(Context& context) -> editor_interaction_result_t {
    if constexpr (requires(Gesture& gesture) { gesture.has_immediate_edit(); }) {
        if (context.state.template gesture<Gesture>()->has_immediate_edit())
            return finish_gesture<Gesture>(context, true, {});
    }
    return context.result();
}

template <class Gesture, editor_gesture_context Context, class... Arguments>
[[nodiscard]] auto begin_gesture_impl(Context& context, Arguments&&... arguments)
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

template <class Gesture, editor_gesture_context Context, class... Arguments>
[[nodiscard]] auto begin_gesture(Context& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto result = begin_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    context.ensure_failure_view(result);
    return result;
}

template <class Gesture, editor_gesture_context Context, class... Arguments>
    requires requires(Gesture& gesture, Context& context, Arguments... arguments) {
        { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
        { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
    }
[[nodiscard]] auto revise_gesture_impl(Context& context, Arguments&&... arguments)
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

template <class Gesture, editor_gesture_context Context, class... Arguments>
    requires requires(Gesture& gesture, Context& context, Arguments... arguments) {
        { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
        { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
    }
[[nodiscard]] auto revise_gesture(Context& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto result = revise_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    context.ensure_failure_view(result);
    return result;
}

template<class Gesture, editor_gesture_context Context, class... Arguments> requires requires(Gesture& gesture, Context& context, Arguments... arguments) {
    { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto apply_gesture(Context& context, Arguments&&... arguments)
    -> editor_interaction_result_t {
    auto revised = revise_gesture_impl<Gesture>(context, std::forward<Arguments>(arguments)...);
    auto result = finish_gesture<Gesture>(context, static_cast<bool>(revised),
                                          std::move(revised.diagnostics));
    context.ensure_failure_view(result);
    return result;
}

enum class editor_gesture_apply_view_policy_t { rebuild, captured };

template<class Gesture, editor_gesture_context Context, class... Arguments> requires requires(Gesture& gesture, Context& context, Arguments... arguments) {
    { gesture.revise(context, arguments...) } -> std::same_as<diagnostic_list_t>;
    { gesture.apply(context) } -> std::same_as<editor_interaction_result_t>;
}
[[nodiscard]] auto apply_gesture(Context& context, editor_gesture_apply_view_policy_t policy,
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
    const bool document_owned = state.gesture_kind() == editor_gesture_kind_t::storyboard_edit || !state.chart;
    (state).clear();
    auto result = document_owned ? editor_interaction_result_t{.accepted = true}
                                 : editor_interaction_result_t::from(history, state);
    result.accepted = true;
    return result;
}

// The shared document boundary owns captured transaction construction and
// command-to-interaction completion, just as run_editor_edit owns chart edits.
template<class Stage>
[[nodiscard]] auto run_document_gesture_edit(editor_document_context_t& context, Stage stage)
    -> editor_interaction_result_t {
    document_command_transaction_t transaction{context.history, context.capture_stamp(), "Editor gesture"};
    const auto revision = context.history.document.revision;
    auto command = transaction.finish_execution(stage(transaction), command_noop_policy_t::accept_without_diagnostics);
    const bool requires_application = command.applied || diagnostic_queries::has_errors(command.diagnostics);
    return finish_editor_edit(context.history, context.state, revision, std::move(command),
        {.has_document_changes = requires_application, .retain_commit_diagnostics = true,
         .view_policy = editor_edit_view_policy_t::omit}, preserve_editor_selection_t{});
}

} // namespace wormedit
