#include "sys/compose_navigation_input.h"
namespace wormedit::sys {
using namespace compose_navigation_input_detail;
auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string {
    if (result.accepted) {
        return std::string(accepted);
    }
    return result.diagnostics.empty() ? "Compose action was rejected."
                                      : result.diagnostics.front().message;
}

auto compose_view_for_state(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t> {
    const auto inspected = wormedit::inspect_editor_interaction(history, state);
    if (const auto* view = std::get_if<wormedit::editor_interaction_view_model_t>(&inspected)) {
        return *view;
    }
    return std::nullopt;
}

auto compose_navigation_action(WormUiComposeNavigationAction action)
    -> std::optional<wormedit::editor_navigation_action_t> {
    using namespace wormedit;
    switch (action) {
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD:
        return editor_navigation_action_t::cursor_backward;
    case WORM_UI_COMPOSE_NAVIGATION_CURSOR_FORWARD:
        return editor_navigation_action_t::cursor_forward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_BACKWARD:
        return editor_navigation_action_t::page_backward;
    case WORM_UI_COMPOSE_NAVIGATION_PAGE_FORWARD:
        return editor_navigation_action_t::page_forward;
    case WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_BACKWARD:
        return editor_navigation_action_t::double_page_backward;
    case WORM_UI_COMPOSE_NAVIGATION_DOUBLE_PAGE_FORWARD:
        return editor_navigation_action_t::double_page_forward;
    }
    return std::nullopt;
}

namespace compose_navigation_input_detail {

auto post_input_feedback(const wormedit::notification_sink_t* notifications,
                         wormedit::notification_severity_t severity,
                         wormedit::notification_source_t source, std::string_view message) -> void {
    if (notifications && !message.empty()) {
        notifications->post_feedback(severity, source, message);
    }
}

auto staged_input_severity(bool staged, wormedit::notification_severity_t fallback)
    -> wormedit::notification_severity_t {
    return staged ? wormedit::notification_severity_t::hint : fallback;
}

auto navigation_measure_count(wormedit::editor_navigation_action_t action) -> std::uint64_t {
    using enum wormedit::editor_navigation_action_t;
    switch (action) {
    case page_backward:
    case page_forward:
        return 1;
    case double_page_backward:
    case double_page_forward:
        return 2;
    default:
        return 0;
    }
}

auto navigation_minimum_rows(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             wormedit::editor_navigation_action_t action) -> std::uint64_t {
    const auto found =
        history.document.find_chart(state.chart);
    if (found == nullptr)
        return 0;
    const auto divisions = found->divisions_per_standard_measure;
    return navigation_measure_count(action) == 2 ? divisions : divisions / 2 + divisions % 2;
}

auto navigation_page_message(wormedit::editor_navigation_action_t action) -> std::string_view {
    using enum wormedit::editor_navigation_action_t;
    switch (action) {
    case double_page_backward:
        return "Edit cursor paged backward by two measures (at least four beats).";
    case double_page_forward:
        return "Edit cursor paged forward by two measures (at least four beats).";
    case page_backward:
        return "Edit cursor paged backward by one measure (at least two beats).";
    default:
        return "Edit cursor paged forward by one measure (at least two beats).";
    }
}

// While the preview plays, navigation skips the playhead instead of moving the
// edit cursor.

auto skip_playhead_for_navigation(wormedit::editor_navigation_action_t action,
                                  const wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state,
                                  application_command_state_t& application)
    -> compose_action_outcome_t {
    using namespace wormedit;
    const auto found =
        history.document.find_chart(state.chart);
    if (found == nullptr)
        return compose_action_outcome_t::blocked("The selected difficulty is no longer available.",
                                                 notification_source_t::input);
    const auto forward = action == editor_navigation_action_t::cursor_forward ||
                         action == editor_navigation_action_t::page_forward ||
                         action == editor_navigation_action_t::double_page_forward;
    const auto measures = navigation_measure_count(action);
    const auto paged = measures != 0;
    return skip_playing_playhead(application, *found, state.active_snap_denominator,
                                 paged ? playhead_skip_unit_t::page : playhead_skip_unit_t::beat,
                                 (forward ? 1.0 : -1.0) *
                                     (paged ? static_cast<double>(measures) : 1.0));
}

} // namespace compose_navigation_input_detail

auto dispatch_compose_navigation_actions(const wormedit::editor_navigation_action_batch_t& actions,
                                         wormedit::document_history_t& history,
                                         wormedit::editor_interaction_state_t& state,
                                         application_command_state_t* application,
                                         const wormedit::notification_sink_t* notifications)
    -> std::string {
    using namespace wormedit;
    auto status = std::string{};
    for (std::size_t index = 0; index < actions.count; ++index) {
        if (application && is_preview_playing(*application)) {
            auto feedback =
                skip_playhead_for_navigation(actions[index], history, state, *application);
            auto finished = finish_compose_action(
                notifications ? *notifications : notification_sink_t::muted(), std::move(feedback));
            status = std::move(finished.message);
            continue;
        }
        editor_interaction_result_t result;
        std::string_view accepted;
        switch (actions[index]) {
        case editor_navigation_action_t::cursor_backward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::backward});
            accepted = "Edit cursor moved backward.";
            break;
        case editor_navigation_action_t::cursor_forward:
            result = dispatch_editor_action(
                history, state, move_edit_cursor_action_t{interaction_direction_t::forward});
            accepted = "Edit cursor moved forward.";
            break;
        case editor_navigation_action_t::page_backward:
        case editor_navigation_action_t::double_page_backward:
            result = dispatch_editor_action(
                history, state,
                page_edit_cursor_action_t{interaction_direction_t::backward,
                                          navigation_measure_count(actions[index]),
                                          navigation_minimum_rows(history, state, actions[index])});
            accepted = navigation_page_message(actions[index]);
            break;
        case editor_navigation_action_t::page_forward:
        case editor_navigation_action_t::double_page_forward:
            result = dispatch_editor_action(
                history, state,
                page_edit_cursor_action_t{interaction_direction_t::forward,
                                          navigation_measure_count(actions[index]),
                                          navigation_minimum_rows(history, state, actions[index])});
            accepted = navigation_page_message(actions[index]);
            break;
        }
        status = compose_result_status(result, accepted);
        const auto severity = result.accepted ? notification_severity_t::hint
                              : !result.diagnostics.empty()
                                  ? to_notification_severity(result.diagnostics.front().severity)
                                  : notification_severity_t::warning;
        post_input_feedback(notifications, severity, notification_source_t::input, status);
        if (!result.accepted)
            break;
    }
    return status;
}

} // namespace wormedit::sys
