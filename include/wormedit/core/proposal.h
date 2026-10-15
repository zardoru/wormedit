#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

#include <concepts>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace wormedit {

// Failed editors retain display state without retaining an applicable proposal.
// The base keeps existing validators and their vector storage unchanged.
template <class Report, class Stamp = chart_stamp_t>
struct proposal_diagnostics_t : diagnostic_list_t {
    using diagnostic_list_t::operator=;
    using report_type = Report;

    Stamp stamp;
    std::optional<Report> report;
    auto operator==(const proposal_diagnostics_t&) const -> bool = default;

    [[nodiscard]] static auto from(Stamp stamp, Report report,
        diagnostic_list_t diagnostics = {}) -> proposal_diagnostics_t {
        return {std::move(diagnostics), stamp, std::move(report)};
    }
};

// Applicable state has one stamp; diagnostics belong to the build/revision outcome.
template <class Payload, class Stamp = chart_stamp_t> struct proposal_t {
    Stamp stamp;
    Payload payload;

    auto operator==(const proposal_t&) const -> bool = default;

    template <class Diagnostics> requires requires { typename Diagnostics::report_type; }
    [[nodiscard]] static auto report(diagnosed_t<std::optional<proposal_t>, Diagnostics>& outcome)
        noexcept -> typename Diagnostics::report_type* {
        if (outcome.value) return &outcome.value->payload.report;
        return outcome.diagnostics.report ? &*outcome.diagnostics.report : nullptr;
    }

    template <class Diagnostics> requires requires { typename Diagnostics::report_type; }
    [[nodiscard]] static auto report(const diagnosed_t<std::optional<proposal_t>, Diagnostics>& outcome)
        noexcept -> const typename Diagnostics::report_type* {
        if (outcome.value) return &outcome.value->payload.report;
        return outcome.diagnostics.report ? &*outcome.diagnostics.report : nullptr;
    }

    template <class Diagnostics> requires requires { typename Diagnostics::report_type; }
    [[nodiscard]] static auto captured_stamp(
        const diagnosed_t<std::optional<proposal_t>, Diagnostics>& outcome) noexcept -> const Stamp& {
        return outcome.value ? outcome.value->stamp : outcome.diagnostics.stamp;
    }

    // Report-only payloads move between applicable state and rejected editor data.
    // Their owner calls this after validating its operation; no diagnostics are built here.
    template <class Diagnostics> requires requires { typename Diagnostics::report_type; }
    static void complete(diagnosed_t<std::optional<proposal_t>, Diagnostics>& outcome,
                         bool is_accepted) {
        if (is_accepted) {
            if (!outcome.value && outcome.diagnostics.report) {
                outcome.value = proposal_t{
                    .stamp = outcome.diagnostics.stamp,
                    .payload = {.report = std::move(*outcome.diagnostics.report)}};
            }
            outcome.diagnostics.report.reset();
            outcome.diagnostics.stamp = {};
        } else if (outcome.value) {
            outcome.diagnostics.stamp = outcome.value->stamp;
            outcome.diagnostics.report.emplace(std::move(outcome.value->payload.report));
            outcome.value.reset();
        }
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

    // Failed editor data still has a captured stamp for rejection feedback.
    // Its owner must have a value before publishing any document edit.
    template <class Diagnostics> requires requires { typename Diagnostics::report_type; }
    [[nodiscard]] static auto transaction(
        const diagnosed_t<std::optional<proposal_t>, Diagnostics>& outcome,
        document_history_t& history, std::string_view label,
        document_identity_policy_t policy = document_identity_policy_t::project_and_revision,
        diagnostic_code_t stale_code = diagnostic_code_t::invalid_id) -> document_command_transaction_t {
        return document_command_transaction_t::from(history, captured_stamp(outcome), label, policy, stale_code);
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
        if constexpr (requires { outcome.diagnostics.report.has_value(); }) {
            const auto& diagnostics = outcome.diagnostics;
            return {.diagnostic = diagnostics.empty() ? nullptr : &diagnostics.front(),
                    .stamp = diagnostics.report ? &diagnostics.stamp : nullptr,
                    .report = diagnostics.report ? &*diagnostics.report : nullptr};
        } else if (!outcome.diagnostics.empty()) {
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
