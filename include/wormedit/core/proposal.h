#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace wormedit {

// Applicable state has one stamp; diagnostics belong to the build/revision outcome.
template <class Payload, class Stamp = chart_stamp_t> struct proposal_t {
    Stamp stamp;
    Payload payload;

    auto operator==(const proposal_t&) const -> bool = default;

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
    : std::bool_constant<requires(const Payload& payload, ProposeArguments... proposed,
                                 ReviseArguments... revised) {
          { Payload::propose(std::forward<ProposeArguments>(proposed)...) }
              -> std::same_as<proposal_outcome_t<Payload, Stamp, Diagnostics>>;
          { payload.revise(std::forward<ReviseArguments>(revised)...) }
              -> std::same_as<proposal_outcome_t<Payload, Stamp, Diagnostics>>;
      }> {};

} // namespace proposal_detail

template <class Payload, class ProposeArguments, class ReviseArguments,
          class Stamp = chart_stamp_t, class Diagnostics = diagnostic_list_t>
concept staged_proposal = std::is_aggregate_v<proposal_t<Payload, Stamp>> &&
    (std::same_as<Stamp, chart_stamp_t> || std::same_as<Stamp, document_stamp_t>) &&
    proposal_detail::lifecycle_t<Payload, Stamp, Diagnostics, ProposeArguments, ReviseArguments>::value;

} // namespace wormedit
