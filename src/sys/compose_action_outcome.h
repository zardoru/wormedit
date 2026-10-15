#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/command_registry.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

#include <string>
#include <string_view>
#include <utility>

namespace wormedit::sys {
struct document_edit_inputs_t;

enum class compose_diagnostic_feedback_policy_t { first, rejected_only };

enum class compose_feedback_delivery_t {
    none,
    message,
    notification,
    first_diagnostic,
    diagnostic_message,
    transient_hint
};

// Borrowed text remains alive until completion; static command messages need no allocation.
enum class compose_message_lifetime_t { owned, borrowed };
enum class compose_feedback_order_t { after_rebind, before_rebind };

enum class compose_interaction_message_policy_t { rejection_diagnostic, supplied };

enum class compose_rebind_failure_policy_t { diagnostic_or_message, message_only };

enum class compose_rebind_feedback_policy_t { edit_then_document, document_only, action_only };

struct compose_action_description_t {
    std::string_view success;
    std::string_view unchanged;
    std::string_view rebind_failure;
    notification_source_t source{notification_source_t::edit};
    std::optional<notification_source_t> rejected_source;
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
    bool has_bms_statement_feedback{};
    std::optional<notification_severity_t> rebind_failure_severity;
    std::optional<notification_source_t> rebind_failure_source;
    bool is_message_retained_on_rebind_failure{};
    bool is_workspace_refresh_required{};
    compose_feedback_order_t feedback_order{compose_feedback_order_t::after_rebind};
    std::string_view borrowed_message;
    bool has_rebind_failed{};
    bool are_command_diagnostics_retained{true};

    [[nodiscard]] auto message_text() const -> std::string_view {
        return borrowed_message.empty() ? std::string_view{message} : borrowed_message;
    }
    [[nodiscard]] static auto from(std::string_view message, notification_severity_t severity,
                                   notification_source_t source,
                                   compose_message_lifetime_t lifetime) -> compose_action_outcome_t;
    [[nodiscard]] static auto from(std::string message, notification_severity_t severity,
                                   notification_source_t source, diagnostic_code_t code)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(const editor_interaction_result_t& result,
                                   std::string_view accepted,
                                   notification_severity_t accepted_severity,
                                   notification_source_t source) -> compose_action_outcome_t;

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
    template <typename Diagnostic>
        requires requires(const Diagnostic& diagnostic) {
            std::string{diagnostic.message};
            to_notification_severity(diagnostic.severity);
        }
    [[nodiscard]] static auto
    from(const Diagnostic& diagnostic, notification_source_t source = notification_source_t::edit,
         notification_severity_t unknown_severity = notification_severity_t::error)
        -> compose_action_outcome_t {
        return from(std::string{diagnostic.message},
                    to_notification_severity(diagnostic.severity, unknown_severity), source, false,
                    false);
    }
    [[nodiscard]] static auto from(diagnostic_list_t&& diagnostics, notification_source_t source,
                                   compose_feedback_delivery_t delivery)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(editor_interaction_result_t&& interaction,
                                   std::string_view accepted,
                                   notification_source_t source = notification_source_t::input,
                                   compose_interaction_message_policy_t message_policy =
                                       compose_interaction_message_policy_t::rejection_diagnostic)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto
    from(document_command_result_t&& command, const compose_action_description_t& description,
         compose_message_lifetime_t lifetime = compose_message_lifetime_t::owned)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto
    from(document_command_result_t&& command, bool is_success,
         const compose_action_description_t& description,
         compose_message_lifetime_t lifetime = compose_message_lifetime_t::owned)
        -> compose_action_outcome_t;
    [[nodiscard]] static auto from(document_command_result_t&& command,
                                   const compose_action_description_t& description,
                                   std::string&& success) -> compose_action_outcome_t;

    template <typename Diagnostic>
        requires requires(const Diagnostic& diagnostic) {
            std::string{diagnostic.message};
            to_notification_severity(diagnostic.severity);
        }
    [[nodiscard]] static auto from(const Diagnostic& diagnostic,
                                   const compose_action_description_t& description,
                                   bool mutated = false) -> compose_action_outcome_t {
        auto outcome = from(diagnostic, description.source, description.unknown_severity);
        outcome.mutated = mutated;
        apply_description(outcome, description);
        return outcome;
    }

    template <typename Payload>
        requires requires(Payload& payload) { payload.format_diagnostics; }
    [[nodiscard]] static auto from(command_result_with_t<Payload>&& result,
                                   const compose_action_description_t& description)
        -> compose_action_outcome_t {
        const auto is_success = static_cast<bool>(result);
        if (is_success || !result.command.diagnostics.empty())
            return from(std::move(result.command), is_success, description);
        if (!result.payload.format_diagnostics.empty())
            return from(result.payload.format_diagnostics.front(), description,
                        result.command.applied);
        if constexpr (requires { result.payload.write_diagnostics; }) {
            if (!result.payload.write_diagnostics.empty())
                return from(result.payload.write_diagnostics.front(), description,
                            result.command.applied);
        }
        if constexpr (requires { result.payload.parse_diagnostics; }) {
            if (!result.payload.parse_diagnostics.empty())
                return from(result.payload.parse_diagnostics.front(), description,
                            result.command.applied);
        }
        return from(std::move(result.command), false, description);
    }

  private:
    static void apply_description(compose_action_outcome_t& outcome,
                                  const compose_action_description_t& description);
};

auto finish_compose_action(const document_edit_inputs_t& document,
                           const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t;

// Registered commands retain their diagnostic contract while sharing Compose completion.
auto finish_application_command(const notification_sink_t& feedback,
                                compose_action_outcome_t outcome,
                                const document_edit_inputs_t* document = nullptr)
    -> command_invocation_outcome_t;

// Transport and navigation outcomes do not mutate documents.
auto finish_compose_action(const notification_sink_t& feedback, compose_action_outcome_t outcome)
    -> compose_action_outcome_t;

} // namespace wormedit::sys
