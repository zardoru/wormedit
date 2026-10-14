#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>

#include <string>
#include <string_view>

namespace wormedit::sys {
struct document_edit_inputs_t;

enum class compose_diagnostic_feedback_policy_t { first, rejected_only };

enum class compose_feedback_delivery_t { message, first_diagnostic };

enum class compose_rebind_failure_policy_t { diagnostic_or_message, message_only };

struct compose_action_outcome_t {
    std::string message;
    notification_severity_t severity{notification_severity_t::success};
    notification_source_t source{notification_source_t::edit};
    bool mutated{};
    bool accepted{true};
    diagnostic_list_t diagnostics;
    compose_feedback_delivery_t delivery{compose_feedback_delivery_t::message};
    bool publish_before_rebind_failure{true};
    // Rebind labels belong to static command descriptions, not handler-local strings.
    std::string_view rebind_failure;
    compose_rebind_failure_policy_t rebind_failure_policy{
        compose_rebind_failure_policy_t::diagnostic_or_message};

    [[nodiscard]] static auto blocked(std::string message,
        notification_source_t source = notification_source_t::edit) -> compose_action_outcome_t;
    [[nodiscard]] static auto from(std::string message, notification_severity_t severity,
        notification_source_t source = notification_source_t::edit, bool mutated = false,
        bool accepted = true) -> compose_action_outcome_t;
    [[nodiscard]] static auto from(const diagnostic_t& diagnostic,
        notification_source_t source = notification_source_t::edit) -> compose_action_outcome_t;
    [[nodiscard]] static auto from(diagnostic_list_t&& diagnostics,
        notification_source_t source, compose_feedback_delivery_t delivery) -> compose_action_outcome_t;
    [[nodiscard]] static auto from(document_command_result_t&& command, std::string_view success,
        std::string_view unchanged, std::string_view rebind_failure,
        notification_source_t source = notification_source_t::edit,
        compose_diagnostic_feedback_policy_t diagnostic_policy = compose_diagnostic_feedback_policy_t::first) -> compose_action_outcome_t;
};

auto finish_compose_action(const document_edit_inputs_t& document,
    const notification_sink_t& feedback, compose_action_outcome_t outcome) -> compose_action_outcome_t;

} // namespace wormedit::sys
