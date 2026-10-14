#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

#include <string>
#include <string_view>

namespace wormedit::sys {
struct document_edit_inputs_t;

enum class compose_diagnostic_feedback_policy_t { first, rejected_only };

enum class compose_feedback_delivery_t {
    none,
    message,
    notification,
    first_diagnostic,
    diagnostic_message
};

enum class compose_interaction_message_policy_t { rejection_diagnostic, supplied };

enum class compose_rebind_failure_policy_t { diagnostic_or_message, message_only };

enum class compose_rebind_feedback_policy_t { edit_then_document, document_only, action_only };

struct compose_action_description_t {
    std::string_view success;
    std::string_view unchanged;
    std::string_view rebind_failure;
    notification_source_t source{notification_source_t::edit};
    compose_diagnostic_feedback_policy_t diagnostic_policy{
        compose_diagnostic_feedback_policy_t::first};
    notification_severity_t unchanged_severity{notification_severity_t::info};
    bool is_unchanged_accepted{true};
    notification_severity_t unknown_severity{notification_severity_t::error};
    compose_rebind_failure_policy_t rebind_failure_policy{
        compose_rebind_failure_policy_t::diagnostic_or_message};

    compose_rebind_feedback_policy_t rebind_feedback_policy{
        compose_rebind_feedback_policy_t::edit_then_document};
    auto operator==(const compose_action_description_t&) const -> bool = default;
};

struct compose_action_outcome_t {
    std::string message;
    notification_severity_t severity{notification_severity_t::success};
    notification_source_t source{notification_source_t::edit};
    bool mutated{};
    bool accepted{true};
    diagnostic_list_t diagnostics;
    compose_feedback_delivery_t delivery{compose_feedback_delivery_t::message};
    compose_rebind_feedback_policy_t rebind_feedback_policy{
        compose_rebind_feedback_policy_t::edit_then_document};
    notification_severity_t unknown_severity{notification_severity_t::error};
    // Rebind labels belong to static command descriptions, not handler-local strings.
    std::string_view rebind_failure;
    compose_rebind_failure_policy_t rebind_failure_policy{
        compose_rebind_failure_policy_t::diagnostic_or_message};
    std::string_view rebind_success_message;
    bool clear_semantic_layer_paint_after_rebind{};
    std::optional<workspace_select_asset_action_t> asset_selection;
    bool is_rebind_severity_diagnostic{true};

    [[nodiscard]] static auto
    blocked(std::string message, notification_source_t source = notification_source_t::edit,
            compose_feedback_delivery_t delivery = compose_feedback_delivery_t::message)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(std::string message, notification_severity_t severity,
                                   notification_source_t source,
                                   compose_feedback_delivery_t delivery)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(std::string message, notification_severity_t severity,
                                   notification_source_t source = notification_source_t::edit,
                                   bool mutated = false, bool accepted = true)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto
    from(const diagnostic_t& diagnostic, notification_source_t source = notification_source_t::edit,
         notification_severity_t unknown_severity = notification_severity_t::error)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(diagnostic_list_t&& diagnostics, notification_source_t source,
                                   compose_feedback_delivery_t delivery)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(editor_interaction_result_t&& interaction,
                                   std::string_view accepted,
                                   notification_source_t source = notification_source_t::input,
                                   compose_interaction_message_policy_t message_policy =
                                       compose_interaction_message_policy_t::rejection_diagnostic)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(document_command_result_t&& command,
                                   const compose_action_description_t& description)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(document_command_result_t&& command,
                                   const compose_action_description_t& description,
                                   std::string&& success) -> compose_action_outcome_t;
};

auto finish_compose_action(const document_edit_inputs_t& document,
                           const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t;

// Transport and navigation outcomes do not mutate documents.
auto finish_compose_action(const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t;

} // namespace wormedit::sys
