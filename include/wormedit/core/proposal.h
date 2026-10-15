#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>
#include <wormedit/core/proposal_apply_result.h>

#include <concepts>
#include <iterator>
#include <optional>
#include <span>
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

enum class proposal_status_t { inactive, active, applied, cancelled, rejected };
enum class proposal_application_stage_t { inactive, preflight, locked, expired, binding, prepared, published };
enum class proposal_operation_t { apply, revise, inspect };
enum class proposal_lock_order_t { before_stamp, after_stamp };
enum class proposal_completion_policy_t { no_diagnostics, applied, applied_or_no_diagnostics };

template<class Diagnostics> struct proposal_input_t {
    bool is_timing_locked{};
    Diagnostics diagnostics{};
    auto operator==(const proposal_input_t&) const -> bool
        requires std::equality_comparable<Diagnostics> = default;
};
using proposal_application_input_t = proposal_input_t<std::span<const diagnostic_t>>;
using proposal_revision_input_t = proposal_input_t<diagnostic_list_t>;

// Policies retain the distinctions between early binding failures and edit failures.
struct proposal_lifecycle_policy_t {
    proposal_lock_order_t lock_order{proposal_lock_order_t::before_stamp};
    diagnostic_code_t locked_code{diagnostic_code_t::invalid_binding};
    std::string_view locked_message;
    std::string_view inactive_apply_message;
    std::string_view inactive_inspect_message;
    bool can_echo_inactive_diagnostics{};
    proposal_status_t locked_status{proposal_status_t::cancelled};
    proposal_status_t expired_status{proposal_status_t::cancelled};
    proposal_status_t binding_status{proposal_status_t::cancelled};
    proposal_status_t preparation_failure_status{proposal_status_t::active};
    proposal_status_t revision_failure_status{proposal_status_t::active};
    proposal_status_t unchanged_status{proposal_status_t::applied};
    proposal_completion_policy_t completion{proposal_completion_policy_t::no_diagnostics};
    auto operator==(const proposal_lifecycle_policy_t&) const -> bool = default;
};

class proposal_lifecycle_t {
public:
    proposal_lifecycle_t() = default;
    [[nodiscard]] auto status() const noexcept -> proposal_status_t { return status_; }
    [[nodiscard]] auto is_active() const noexcept -> bool { return status_ == proposal_status_t::active; }
    auto operator==(const proposal_lifecycle_t&) const -> bool = default;

    void begin() noexcept { status_ = proposal_status_t::active; }
    auto cancel() noexcept -> bool {
        if (!is_active()) return false;
        status_ = proposal_status_t::cancelled;
        return true;
    }
    void invalidate(const proposal_lifecycle_policy_t& policy, proposal_application_stage_t stage) noexcept {
        switch (stage) {
        case proposal_application_stage_t::locked: status_ = policy.locked_status; break;
        case proposal_application_stage_t::expired: status_ = policy.expired_status; break;
        case proposal_application_stage_t::binding: status_ = policy.binding_status; break;
        case proposal_application_stage_t::preflight: status_ = policy.preparation_failure_status; break;
        case proposal_application_stage_t::inactive:
        case proposal_application_stage_t::prepared:
        case proposal_application_stage_t::published: break;
        }
    }
    void finish(const proposal_lifecycle_policy_t& policy, const document_command_result_t& result,
                proposal_application_stage_t stage) noexcept {
        if (stage == proposal_application_stage_t::prepared) {
            status_ = result.diagnostics.empty() ? policy.unchanged_status : policy.preparation_failure_status;
        } else if (stage == proposal_application_stage_t::published) {
            bool is_complete{};
            switch (policy.completion) {
            case proposal_completion_policy_t::no_diagnostics:
                is_complete = result.diagnostics.empty(); break;
            case proposal_completion_policy_t::applied:
                is_complete = result.applied; break;
            case proposal_completion_policy_t::applied_or_no_diagnostics:
                is_complete = result.applied || result.diagnostics.empty(); break;
            }
            status_ = is_complete ? proposal_status_t::applied : policy.preparation_failure_status;
        } else invalidate(policy, stage);
    }
    void finish_revision(const proposal_lifecycle_policy_t& policy, bool has_proposal,
                         bool has_invalid_target) noexcept {
        if (has_proposal) return;
        status_ = has_invalid_target ? policy.binding_status : policy.revision_failure_status;
    }
private:
    proposal_status_t status_{proposal_status_t::inactive};
};

template<class Payload>
concept stateful_proposal_payload = requires { std::remove_cvref_t<Payload>::lifecycle_policy; };

// A retained display report and a direct payload share the same state owner.
template<class Payload> requires stateful_proposal_payload<Payload>
[[nodiscard]] auto proposal_lifecycle_of(Payload& payload) -> decltype(auto) {
    if constexpr (requires { payload.lifecycle; }) return (payload.lifecycle);
    else return (payload.report.lifecycle);
}

template<class Stamp> class proposal_revision_scope_t {
public:
    proposal_revision_scope_t(const document_history_t& history, const Stamp& captured,
        diagnostic_list_t& diagnostics, const chart_document_t* target)
        : history_(history), captured_stamp_(captured), diagnostics_(diagnostics), target_(target) {}
    [[nodiscard]] auto history() const noexcept -> const document_history_t& { return history_; }
    [[nodiscard]] auto captured_stamp() const noexcept -> const Stamp& { return captured_stamp_; }
    [[nodiscard]] auto diagnostics() const noexcept -> diagnostic_list_t& { return diagnostics_; }
    [[nodiscard]] auto target() const noexcept -> const chart_document_t* { return target_; }
    [[nodiscard]] auto has_invalid_target() const noexcept -> bool { return has_invalid_target_; }
    [[nodiscard]] auto reject_binding(diagnostic_list_t diagnostics) -> diagnostic_list_t {
        has_invalid_target_ = true;
        return diagnostics;
    }
private:
    const document_history_t& history_;
    const Stamp& captured_stamp_;
    diagnostic_list_t& diagnostics_;
    const chart_document_t* target_{};
    bool has_invalid_target_{};
};

class proposal_lifecycle_runner_t;

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
    proposal_edit_scope_t(document_command_transaction_t& transaction, const Stamp& captured,
                          const proposal_application_input_t& input)
        : document_edit_scope_t(transaction), captured_stamp_(captured), input_(input) {}
    [[nodiscard]] auto captured_stamp() const noexcept -> const Stamp& { return captured_stamp_; }
    [[nodiscard]] auto input() const noexcept -> const proposal_application_input_t& { return input_; }
    [[nodiscard]] auto target() const noexcept -> const chart_document_t* { return target_; }
private:
    const Stamp& captured_stamp_;
    const proposal_application_input_t& input_;
    const chart_document_t* target_{};
    friend class proposal_lifecycle_runner_t;
};

class proposal_lifecycle_runner_t {
public:
    template<class Edit> requires stateful_proposal_payload<Edit>
    [[nodiscard]] static auto preflight(Edit& edit, const proposal_application_input_t& input,
        diagnostic_list_t& diagnostics) -> std::optional<proposal_application_stage_t> {
        const auto& policy = std::remove_cvref_t<Edit>::lifecycle_policy;
        if (!proposal_lifecycle_of(edit).is_active()) {
            if (policy.can_echo_inactive_diagnostics)
                diagnostics.assign(input.diagnostics.begin(), input.diagnostics.end());
            else if (!policy.inactive_apply_message.empty())
                diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                                            std::string(policy.inactive_apply_message)));
            return proposal_application_stage_t::inactive;
        }
        if (policy.lock_order == proposal_lock_order_t::before_stamp && input.is_timing_locked) {
            diagnostics.push_back(error(policy.locked_code, std::string(policy.locked_message)));
            return proposal_application_stage_t::locked;
        }
        return std::nullopt;
    }

    template<class Edit> requires stateful_proposal_payload<Edit>
    [[nodiscard]] static auto prepare_application(Edit& edit, proposal_edit_scope_t<chart_stamp_t>& scope)
        -> std::optional<proposal_application_stage_t> {
        const auto& policy = std::remove_cvref_t<Edit>::lifecycle_policy;
        auto& diagnostics = scope.transaction().outcome.diagnostics;
        if (policy.lock_order == proposal_lock_order_t::after_stamp && scope.input().is_timing_locked) {
            diagnostics.push_back(error(policy.locked_code, std::string(policy.locked_message)));
            return proposal_application_stage_t::locked;
        }
        if constexpr (requires { edit.is_unchanged(); }) {
            if (edit.is_unchanged()) return proposal_application_stage_t::prepared;
        }
        auto target = edit.validate_target(scope.transaction().document(), scope.captured_stamp().chart,
                                           proposal_operation_t::apply);
        if (!target) {
            diagnostics.push_back(std::move(target.error()));
            return proposal_application_stage_t::binding;
        }
        scope.target_ = *target;
        return std::nullopt;
    }
};

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
    [[nodiscard]] static auto finish(Edit& edit, document_command_result_t command, Diagnostics diagnostics,
        proposal_application_stage_t stage) -> result_type {
        if constexpr (std::same_as<Diagnostics, diagnostic_list_t>) {
            append_diagnostics(command.diagnostics, std::move(diagnostics));
            if constexpr (stateful_proposal_payload<Edit>)
                proposal_lifecycle_of(edit).finish(std::remove_cvref_t<Edit>::lifecycle_policy, command, stage);
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
        return application_result::finish(edit, transaction.finish_without_commit(), {},
            proposal_application_stage_t::expired);
    if constexpr (stateful_proposal_payload<Edit>) {
        if (auto stopped = proposal_lifecycle_runner_t::prepare_application(edit, scope))
            return application_result::finish(edit, transaction.finish_without_commit(), {}, *stopped);
    }
    auto prepared = std::forward<Edit>(edit).apply(scope, std::forward<Arguments>(arguments)...);
    application_result::merge(transaction, prepared.diagnostics, proposal_application_stage_t::prepared);
    const bool can_commit = prepared.value.has_value() && prepared.ok() &&
        !diagnostic_queries::has_errors(transaction.outcome.diagnostics);
    if (!can_commit) {
        return application_result::finish(edit, transaction.finish_without_commit(),
            std::move(prepared.diagnostics), proposal_application_stage_t::prepared);
    }
    return application_result::finish(edit, transaction.commit(std::move(*prepared.value)),
        std::move(prepared.diagnostics), proposal_application_stage_t::published);
}

// Applicable state has one stamp; diagnostics belong to the build/revision outcome.
template <class Payload, class Stamp = chart_stamp_t> struct proposal_t {
    using payload_type = Payload;
    using stamp_type = Stamp;

    Stamp stamp;
    Payload payload;

    auto operator==(const proposal_t&) const -> bool = default;

    template<class Value> requires std::same_as<std::remove_cvref_t<Value>, Payload>
    [[nodiscard]] static auto from(Stamp captured, Value&& value) -> proposal_t {
        proposal_t pending{.stamp = captured, .payload = std::forward<Value>(value)};
        if constexpr (stateful_proposal_payload<Payload>) proposal_lifecycle_of(pending.payload).begin();
        return pending;
    }

    template<class... Arguments> requires stateful_proposal_payload<Payload> &&
        requires(Payload& value, proposal_revision_scope_t<Stamp>& scope, Arguments&&... arguments) {
            value.revise(scope, std::forward<Arguments>(arguments)...);
        }
    [[nodiscard]] auto revise(const document_history_t& history, proposal_revision_input_t input,
                             Arguments&&... arguments) {
        using outcome_type = decltype(payload.revise(std::declval<proposal_revision_scope_t<Stamp>&>(),
            std::forward<Arguments>(arguments)...));
        auto& lifecycle = proposal_lifecycle_of(payload);
        const auto& policy = Payload::lifecycle_policy;
        if (!lifecycle.is_active()) return outcome_type{.diagnostics = std::move(input.diagnostics)};
        input.diagnostics.clear();
        if (input.is_timing_locked) {
            input.diagnostics.push_back(error(policy.locked_code, std::string(policy.locked_message)));
            lifecycle.invalidate(policy, proposal_application_stage_t::locked);
            return outcome_type{.diagnostics = std::move(input.diagnostics)};
        }
        if (!is_current(history, Payload::apply_policy.identity)) {
            input.diagnostics.push_back(error(Payload::apply_policy.stale_code,
                stale_action_message(Payload::apply_policy.stale_label)));
            lifecycle.invalidate(policy, proposal_application_stage_t::expired);
            return outcome_type{.diagnostics = std::move(input.diagnostics)};
        }
        auto target = payload.validate_target(history.document, stamp.chart, proposal_operation_t::revise);
        if (!target) {
            input.diagnostics.push_back(std::move(target.error()));
            lifecycle.invalidate(policy, proposal_application_stage_t::binding);
            return outcome_type{.diagnostics = std::move(input.diagnostics)};
        }
        proposal_revision_scope_t<Stamp> scope{history, stamp, input.diagnostics, *target};
        auto revised = payload.revise(scope, std::forward<Arguments>(arguments)...);
        lifecycle.finish_revision(policy, revised.value.has_value(), scope.has_invalid_target());
        return revised;
    }

    [[nodiscard]] auto inspect(const document_history_t& history,
        std::span<const diagnostic_t> recent_diagnostics = {}) const
        -> result_t<const chart_document_t*, diagnostic_list_t> requires stateful_proposal_payload<Payload> {
        diagnostic_list_t diagnostics;
        const auto& policy = Payload::lifecycle_policy;
        if (!proposal_lifecycle_of(payload).is_active()) {
            diagnostics.assign(recent_diagnostics.begin(), recent_diagnostics.end());
            if (diagnostics.empty()) diagnostics.push_back(error(diagnostic_code_t::invalid_binding,
                std::string(policy.inactive_inspect_message)));
            return std::unexpected(std::move(diagnostics));
        }
        if (!is_current(history, Payload::apply_policy.identity)) {
            diagnostics.push_back(error(Payload::apply_policy.stale_code,
                stale_action_message(Payload::apply_policy.stale_label)));
            return std::unexpected(std::move(diagnostics));
        }
        auto target = payload.validate_target(history.document, stamp.chart, proposal_operation_t::inspect);
        if (!target) {
            diagnostics.push_back(std::move(target.error()));
            return std::unexpected(std::move(diagnostics));
        }
        return *target;
    }

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
    proposal_application_input_t lifecycle_input;
    if constexpr (stateful_proposal_payload<edit_type>) {
        static_assert(std::same_as<diagnostics_type, diagnostic_list_t>);
        if constexpr (sizeof...(Arguments) > 0) lifecycle_input = std::get<0>(std::tie(arguments...));
        if (auto stopped = proposal_lifecycle_runner_t::preflight(pending.payload, lifecycle_input, preflight))
            return application_result::finish(pending.payload,
                document_command_transaction_t::rejection(history.document.revision, diagnostic_list_t{}),
                std::move(preflight), *stopped);
    }
    if constexpr (requires { pending.payload.validate_apply(pending.stamp, arguments...); }) {
        preflight = pending.payload.validate_apply(pending.stamp, arguments...);
        if (diagnostic_queries::has_errors(preflight))
            return application_result::finish(pending.payload,
                document_command_transaction_t::rejection(history.document.revision, diagnostic_list_t{}),
                std::move(preflight), proposal_application_stage_t::preflight);
    }
    const auto& policy = std::remove_cvref_t<Pending>::payload_type::apply_policy;
    auto transaction = pending.transaction(history, policy.stale_label, policy.identity, policy.stale_code);
    application_result::merge(transaction, preflight, proposal_application_stage_t::preflight);
    scope_type scope{transaction, pending.stamp, lifecycle_input};
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
