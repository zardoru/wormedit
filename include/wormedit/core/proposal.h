#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>
#include <wormedit/core/proposal_apply_result.h>

#include <concepts>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace wormedit {

namespace proposal_detail {
inline void append_diagnostics(diagnostic_list_t& target, diagnostic_list_t&& incoming) {
    if (target.empty()) target = std::move(incoming);
    else target.insert(target.end(), std::make_move_iterator(incoming.begin()),
                        std::make_move_iterator(incoming.end()));
}
} // namespace proposal_detail

struct proposal_apply_policy_t {
    std::string_view stale_label;
    document_identity_policy_t identity{document_identity_policy_t::project_and_revision};
    diagnostic_code_t stale_code{diagnostic_code_t::invalid_id};
    auto operator==(const proposal_apply_policy_t&) const -> bool = default;
};

// Numeric edits and captured proposals borrow their transaction for one dispatch.
class document_edit_scope_t {
public:
    explicit document_edit_scope_t(document_command_transaction_t& transaction) : transaction_(transaction) {}
    [[nodiscard]] auto transaction() const noexcept -> document_command_transaction_t& { return transaction_; }
private:
    document_command_transaction_t& transaction_;
};

template<class Stamp> class proposal_edit_scope_t : public document_edit_scope_t {
public:
    proposal_edit_scope_t(document_command_transaction_t& transaction, const Stamp& captured)
        : document_edit_scope_t(transaction), captured_stamp_(captured) {}
    [[nodiscard]] auto captured_stamp() const noexcept -> const Stamp& { return captured_stamp_; }
private:
    const Stamp& captured_stamp_;
};

enum class proposal_application_stage_t { preflight, expired, prepared, published };

namespace proposal_detail {
// One projection keeps typed domain failures separate from commit diagnostics.
template<class Diagnostics> struct application_result_t {
    using result_type = std::conditional_t<std::same_as<Diagnostics, diagnostic_list_t>,
        document_command_result_t, command_result_with_t<Diagnostics>>;

    static void merge(document_command_transaction_t& transaction, Diagnostics& diagnostics,
                      proposal_application_stage_t stage) {
        if constexpr (std::same_as<Diagnostics, diagnostic_list_t>) {
            if (diagnostics.empty()) return;
            if (stage == proposal_application_stage_t::preflight) {
                append_diagnostics(diagnostics, std::move(transaction.outcome.diagnostics));
                transaction.outcome.diagnostics = std::move(diagnostics);
            } else append_diagnostics(transaction.outcome.diagnostics, std::move(diagnostics));
            diagnostics.clear();
        }
    }

    template<class Edit>
    [[nodiscard]] static auto finish(document_command_result_t command, Diagnostics diagnostics,
        proposal_application_stage_t stage) -> result_type {
        if constexpr (std::same_as<Diagnostics, diagnostic_list_t>) {
            append_diagnostics(command.diagnostics, std::move(diagnostics));
            return command;
        } else {
            if (stage == proposal_application_stage_t::expired) {
                diagnostics.issue = std::remove_cvref_t<Edit>::apply_stale_issue;
                diagnostics.message = std::move(command.diagnostics.front().message);
                command.diagnostics.clear();
            } else if (stage == proposal_application_stage_t::published && !command.applied) {
                diagnostics.issue = std::remove_cvref_t<Edit>::apply_commit_failure_issue;
                diagnostics.message = std::remove_cvref_t<Edit>::apply_commit_failure_message;
            }
            return {.command = std::move(command), .payload = std::move(diagnostics)};
        }
    }
};
} // namespace proposal_detail

template<class Edit, class Scope, class... Arguments>
using proposal_edit_outcome_t = decltype(std::declval<Edit>().apply(
    std::declval<Scope&>(), std::declval<Arguments>()...));

template<class Edit, class Scope, class... Arguments>
using proposal_edit_diagnostics_t = decltype(
    std::declval<proposal_edit_outcome_t<Edit, Scope, Arguments...>>().diagnostics);

// A prepared edit supplies its Undo label; an empty value is a no-op.
template<class Edit, class Scope, class... Arguments>
concept proposal_edit = std::derived_from<Scope, document_edit_scope_t> &&
    requires(Edit&& edit, Scope& scope, Arguments&&... arguments) {
        { std::forward<Edit>(edit).apply(scope, std::forward<Arguments>(arguments)...) }
            -> std::same_as<diagnosed_t<std::optional<std::string>,
                proposal_edit_diagnostics_t<Edit, Scope, Arguments...>>>;
    };

// Synchronous edits and captured proposals share ready/error/no-op/publication policy.
template<class Edit, class Scope, class... Arguments> requires proposal_edit<Edit, Scope, Arguments...>
[[nodiscard]] auto apply_proposal_edit(Scope& scope, Edit&& edit, Arguments&&... arguments) {
    using diagnostics_type = proposal_edit_diagnostics_t<Edit, Scope, Arguments...>;
    using application_result = proposal_detail::application_result_t<diagnostics_type>;
    auto& transaction = scope.transaction();
    if (!transaction.is_ready())
        return application_result::template finish<Edit>(transaction.finish_without_commit(), {},
            proposal_application_stage_t::expired);
    auto prepared = std::forward<Edit>(edit).apply(scope, std::forward<Arguments>(arguments)...);
    application_result::merge(transaction, prepared.diagnostics, proposal_application_stage_t::prepared);
    const bool can_commit = prepared.value.has_value() && prepared.ok() &&
        !diagnostic_queries::has_errors(transaction.outcome.diagnostics);
    if (!can_commit)
        return application_result::template finish<Edit>(transaction.finish_without_commit(),
            std::move(prepared.diagnostics), proposal_application_stage_t::prepared);
    return application_result::template finish<Edit>(transaction.commit(std::move(*prepared.value)),
        std::move(prepared.diagnostics), proposal_application_stage_t::published);
}

// Applicable state has one stamp; diagnostics belong to the build/revision outcome.
template <class Payload, class Stamp = chart_stamp_t> struct proposal_t {
    using payload_type = Payload;
    using stamp_type = Stamp;

    Stamp stamp;
    Payload payload;

    auto operator==(const proposal_t&) const -> bool = default;

    template <class... Arguments>
        requires requires(Payload& value, const Stamp& captured, Arguments&&... arguments) {
            value.revise(captured, std::forward<Arguments>(arguments)...);
        }
    [[nodiscard]] auto revise(Arguments&&... arguments) -> decltype(auto) {
        return payload.revise(stamp, std::forward<Arguments>(arguments)...);
    }

    template <class... Arguments>
        requires requires(const Payload& value, const Stamp& captured, Arguments&&... arguments) {
            value.revise(captured, std::forward<Arguments>(arguments)...);
        }
    [[nodiscard]] auto revise(Arguments&&... arguments) const -> decltype(auto) {
        return payload.revise(stamp, std::forward<Arguments>(arguments)...);
    }

    [[nodiscard]] auto is_current(const document_history_t& history,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision)
        const noexcept -> bool {
        if constexpr (std::same_as<Stamp, chart_stamp_t>)
            return stamp.is_current(history.document, stamp.chart, policy);
        else
            return stamp.is_current(history.document, policy);
    }

    [[nodiscard]] auto transaction(document_history_t& history, std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id) const
        -> document_command_transaction_t {
        return document_command_transaction_t::from(history, stamp, label, policy, stale_code);
    }
};

template<class Pending>
concept applicable_proposal = requires {
    typename std::remove_cvref_t<Pending>::payload_type;
    typename std::remove_cvref_t<Pending>::stamp_type;
    requires std::same_as<std::remove_cvref_t<Pending>,
        proposal_t<typename std::remove_cvref_t<Pending>::payload_type,
                   typename std::remove_cvref_t<Pending>::stamp_type>>;
    std::remove_cvref_t<Pending>::payload_type::apply_policy;
};

template<class Pending, class... Arguments> requires applicable_proposal<Pending>
[[nodiscard]] auto apply_proposal(document_history_t& history,
    Pending&& pending, Arguments&&... arguments) {
    using scope_type = proposal_edit_scope_t<typename std::remove_cvref_t<Pending>::stamp_type>;
    using edit_type = decltype((std::forward<Pending>(pending).payload));
    using diagnostics_type = proposal_edit_diagnostics_t<edit_type, scope_type, Arguments...>;
    using application_result = proposal_detail::application_result_t<diagnostics_type>;
    diagnostics_type preflight;
    if constexpr (requires { pending.payload.validate_apply(pending.stamp, arguments...); }) {
        preflight = pending.payload.validate_apply(pending.stamp, arguments...);
        if (diagnostic_queries::has_errors(preflight))
            return application_result::template finish<edit_type>(
                document_command_transaction_t::rejection(history.document.revision, diagnostic_list_t{}),
                std::move(preflight), proposal_application_stage_t::preflight);
    }
    const auto& policy = std::remove_cvref_t<Pending>::payload_type::apply_policy;
    auto transaction = pending.transaction(history, policy.stale_label, policy.identity, policy.stale_code);
    application_result::merge(transaction, preflight, proposal_application_stage_t::preflight);
    scope_type scope{transaction, pending.stamp};
    return apply_proposal_edit(scope, std::forward<Pending>(pending).payload,
                               std::forward<Arguments>(arguments)...);
}

template <class Payload, class Stamp = chart_stamp_t, class Diagnostics = diagnostic_list_t>
using proposal_outcome_t = diagnosed_t<std::optional<proposal_t<Payload, Stamp>>, Diagnostics>;

// Report-bearing proposals expose the same borrowed success/failure projection.
template <class Payload, class Stamp, class Diagnostics> struct proposal_preview_view_t {
    using report_type = std::remove_cvref_t<decltype(std::declval<Payload>().report)>;
    using diagnostic_type = typename Diagnostics::value_type;

    const proposal_t<Payload, Stamp>* proposal{};
    const diagnostic_type* diagnostic{};
    const Stamp* stamp{};
    const report_type* report{};
    auto operator==(const proposal_preview_view_t&) const -> bool = default;

    [[nodiscard]] static auto from(const proposal_outcome_t<Payload, Stamp, Diagnostics>& outcome)
        noexcept -> proposal_preview_view_t {
        if (outcome.value)
            return {.proposal = &*outcome.value, .stamp = &outcome.value->stamp,
                    .report = &outcome.value->payload.report};
        if (!outcome.diagnostics.empty()) {
            const auto& diagnostic = outcome.diagnostics.front();
            return {.diagnostic = &diagnostic, .stamp = &diagnostic.stamp,
                    .report = &diagnostic.report};
        }
        return {};
    }
};

namespace proposal_detail {

// Separate argument lists preserve owners whose build and revision inputs differ.
template <class Payload, class Stamp, class Diagnostics, class ProposeArguments, class ReviseArguments>
struct lifecycle_t : std::false_type {};

template <class Payload, class Stamp, class Diagnostics, class... ProposeArguments, class... ReviseArguments>
struct lifecycle_t<Payload, Stamp, Diagnostics, std::tuple<ProposeArguments...>, std::tuple<ReviseArguments...>>
    : std::bool_constant<requires(Payload& payload, ProposeArguments... proposed,
                                 ReviseArguments... revised) {
          { Payload::propose(std::forward<ProposeArguments>(proposed)...) }
              -> std::derived_from<proposal_outcome_t<Payload, Stamp, Diagnostics>>;
          { payload.revise(std::forward<ReviseArguments>(revised)...) }
              -> std::derived_from<proposal_outcome_t<Payload, Stamp, Diagnostics>>;
          requires std::same_as<decltype(Payload::propose(std::forward<ProposeArguments>(proposed)...)),
              decltype(payload.revise(std::forward<ReviseArguments>(revised)...))>;
      }> {};

} // namespace proposal_detail

template <class Payload, class ProposeArguments, class ReviseArguments,
          class Stamp = chart_stamp_t, class Diagnostics = diagnostic_list_t>
concept staged_proposal = std::is_aggregate_v<proposal_t<Payload, Stamp>> &&
    (std::same_as<Stamp, chart_stamp_t> || std::same_as<Stamp, document_stamp_t>) &&
    proposal_detail::lifecycle_t<Payload, Stamp, Diagnostics, ProposeArguments, ReviseArguments>::value;

// A context names the outcome at its boundary; core does not depend on frontend types.
// Validation returns an early outcome, or permits perform to run exactly once.
template <class Action, class Context>
concept compose_action = std::is_aggregate_v<std::remove_cvref_t<Action>> &&
    requires(Action& action, Context& context) {
        { action.validate(context) } -> std::same_as<std::optional<typename Context::outcome_type>>;
        { action.perform(context) } -> std::same_as<typename Context::outcome_type>;
    };

template <class Action, class Context> requires compose_action<Action, Context>
[[nodiscard]] auto run_compose_action(Action&& action, Context& context)
    -> typename Context::outcome_type {
    if (auto rejected = action.validate(context)) return std::move(*rejected);
    return action.perform(context);
}

} // namespace wormedit
