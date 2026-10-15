#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

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

// A prepared edit supplies its Undo label; an empty value is a no-op.
template<class Edit, class... Arguments>
concept proposal_edit = requires(Edit&& edit, document_command_transaction_t& transaction,
                                 Arguments&&... arguments) {
    { std::forward<Edit>(edit).apply(transaction, std::forward<Arguments>(arguments)...) }
        -> std::same_as<diagnosed_t<std::optional<std::string>>>;
};

// Synchronous edits and captured proposals share ready/error/no-op/publication policy.
template<class Edit, class... Arguments> requires proposal_edit<Edit, Arguments...>
[[nodiscard]] auto apply_proposal_edit(document_command_transaction_t& transaction,
    Edit&& edit, Arguments&&... arguments) -> document_command_result_t {
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    auto prepared = std::forward<Edit>(edit).apply(transaction, std::forward<Arguments>(arguments)...);
    proposal_detail::append_diagnostics(transaction.outcome.diagnostics, std::move(prepared.diagnostics));
    const bool can_commit = prepared.value.has_value() &&
        !diagnostic_queries::has_errors(transaction.outcome.diagnostics);
    if (!can_commit) return transaction.finish_without_commit();
    return transaction.commit(std::move(*prepared.value));
}

// Applicable state has one stamp; diagnostics belong to the build/revision outcome.
template <class Payload, class Stamp = chart_stamp_t> struct proposal_t {
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

template<class Payload, class Stamp, class... Arguments>
    requires requires { Payload::apply_policy; }
[[nodiscard]] auto apply_proposal(document_history_t& history,
    const proposal_t<Payload, Stamp>& pending, Arguments&&... arguments) -> document_command_result_t {
    diagnostic_list_t preflight;
    if constexpr (requires { pending.payload.validate_apply(arguments...); }) {
        preflight = pending.payload.validate_apply(arguments...);
        if (diagnostic_queries::has_errors(preflight))
            return document_command_transaction_t::rejection(history.document.revision, std::move(preflight));
    }
    const auto& policy = Payload::apply_policy;
    auto transaction = pending.transaction(history, policy.stale_label, policy.identity, policy.stale_code);
    if constexpr (requires { pending.payload.validate_apply(arguments...); }) {
        proposal_detail::append_diagnostics(preflight, std::move(transaction.outcome.diagnostics));
        transaction.outcome.diagnostics = std::move(preflight);
    }
    if constexpr (std::same_as<Stamp, chart_stamp_t>)
        return apply_proposal_edit(transaction, pending.payload,
                                   pending.stamp.chart, std::forward<Arguments>(arguments)...);
    else
        return apply_proposal_edit(transaction, pending.payload,
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
