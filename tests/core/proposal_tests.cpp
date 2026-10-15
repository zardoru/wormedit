#include "../support/stale_action_assertions.h"
#include "core/position_validation.h"
#include <wormedit/core/proposal.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {

struct metadata_payload_t {
    std::string title;

    auto operator==(const metadata_payload_t&) const -> bool = default;

    static auto propose(const song_document_t& document, std::string title)
        -> proposal_outcome_t<metadata_payload_t, document_stamp_t> {
        if (title.empty())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Title is empty.")}};
        return {.value = proposal_t<metadata_payload_t, document_stamp_t>{
                    .stamp = document_stamp_t::from(document), .payload = {.title = std::move(title)}}};
    }

    auto revise(const song_document_t& document, std::string title) const
        -> proposal_outcome_t<metadata_payload_t, document_stamp_t> {
        return propose(document, std::move(title));
    }
};

// State-machine revisions may reject the original state while returning no proposal.
struct mutable_payload_t {
    bool is_rejected{};
    static auto propose() -> proposal_outcome_t<mutable_payload_t> {
        return {.value = proposal_t<mutable_payload_t>{}};
    }
    auto revise(bool reject) -> proposal_outcome_t<mutable_payload_t> {
        is_rejected = reject;
        if (reject) return {};
        return {.value = proposal_t<mutable_payload_t>{.payload = *this}};
    }
};
static_assert(staged_proposal<mutable_payload_t, std::tuple<>, std::tuple<bool>>);

struct editable_report_t {
    std::string text;
    auto operator==(const editable_report_t&) const -> bool = default;
};
struct reported_payload_t;
using reported_diagnostics_t = proposal_diagnostics_t<editable_report_t, document_stamp_t>;
using reported_outcome_t = proposal_outcome_t<reported_payload_t, document_stamp_t, reported_diagnostics_t>;
struct reported_payload_t {
    editable_report_t report;
    auto operator==(const reported_payload_t&) const -> bool = default;

    static auto propose(const song_document_t& document, std::string text) -> reported_outcome_t {
        reported_outcome_t outcome{.value = proposal_t<reported_payload_t, document_stamp_t>{
            .stamp = document_stamp_t::from(document), .payload = {.report = {std::move(text)}}}};
        const auto is_accepted = !outcome.value->payload.report.text.empty();
        if (!is_accepted)
            outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id, "Editor value is empty."));
        proposal_t<reported_payload_t, document_stamp_t>::complete(outcome, is_accepted);
        return outcome;
    }
    static auto revise(reported_outcome_t outcome, bool is_accepted) -> reported_outcome_t {
        outcome.diagnostics.clear();
        if (!is_accepted)
            outcome.diagnostics.push_back(error(diagnostic_code_t::invalid_id, "Editor value was rejected."));
        proposal_t<reported_payload_t, document_stamp_t>::complete(outcome, is_accepted);
        return outcome;
    }
};
using reported_proposal_t = proposal_t<reported_payload_t, document_stamp_t>;
using reported_view_t = proposal_preview_view_t<reported_payload_t, document_stamp_t, reported_diagnostics_t>;
static_assert(std::is_aggregate_v<reported_diagnostics_t>);
static_assert(!std::is_polymorphic_v<reported_diagnostics_t>);
static_assert(std::is_nothrow_move_constructible_v<reported_diagnostics_t>);
static_assert(staged_proposal<reported_payload_t,
    std::tuple<const song_document_t&, std::string>, std::tuple<reported_outcome_t, bool>,
    document_stamp_t, reported_diagnostics_t>);

void check_rejected_report(const reported_outcome_t& outcome, const char* storage) {
    CHECK_FALSE(outcome.value);
    REQUIRE(outcome.diagnostics.report);
    CHECK(outcome.diagnostics.report->text.data() == storage);
    CHECK_FALSE(outcome.ok());
    CHECK(reported_view_t::from(outcome).diagnostic == &outcome.diagnostics.front());
}

void check_repaired_report(const reported_outcome_t& outcome, const char* storage) {
    REQUIRE(outcome.value);
    CHECK_FALSE(outcome.diagnostics.report);
    CHECK(outcome.diagnostics.empty());
    CHECK(outcome.value->payload.report.text.data() == storage);
    CHECK(reported_view_t::from(outcome).report == &outcome.value->payload.report);
}

struct wrong_outcome_payload_t {
    static auto propose() -> bool;
    auto revise() const -> bool;
};

enum class private_payload_issue_t { negative_number };
struct private_payload_diagnostic_t {
    diagnostic_severity_t severity{diagnostic_severity_t::error};
    private_payload_issue_t code{private_payload_issue_t::negative_number};
    std::string message;
    auto operator==(const private_payload_diagnostic_t&) const -> bool = default;
};
using private_payload_diagnostics_t = std::vector<private_payload_diagnostic_t>;

// Real sealed payloads keep private invariants while their proposal envelope stays an aggregate.
struct private_payload_t {
    static auto propose(const song_document_t& document, int number)
        -> proposal_outcome_t<private_payload_t, document_stamp_t, private_payload_diagnostics_t> {
        proposal_outcome_t<private_payload_t, document_stamp_t, private_payload_diagnostics_t> outcome;
        if (number < 0) {
            outcome.diagnostics.push_back({.message = "Negative number."});
            return outcome;
        }
        private_payload_t payload;
        payload.number_ = number;
        outcome.value = proposal_t<private_payload_t, document_stamp_t>{
            .stamp = document_stamp_t::from(document), .payload = payload};
        return outcome;
    }
    auto revise(const song_document_t& document, int number) const
        -> proposal_outcome_t<private_payload_t, document_stamp_t, private_payload_diagnostics_t> {
        return propose(document, number);
    }
    auto operator==(const private_payload_t&) const -> bool = default;

  private:
    int number_{};
};

static_assert(!std::is_aggregate_v<private_payload_t>);
static_assert(std::is_aggregate_v<proposal_t<private_payload_t, document_stamp_t>>);
static_assert(staged_proposal<private_payload_t,
    std::tuple<const song_document_t&, int>, std::tuple<const song_document_t&, int>,
    document_stamp_t, private_payload_diagnostics_t>);

static_assert(staged_proposal<metadata_payload_t,
    std::tuple<const song_document_t&, std::string>,
    std::tuple<const song_document_t&, std::string>, document_stamp_t>);
static_assert(!staged_proposal<wrong_outcome_payload_t, std::tuple<>, std::tuple<>>);
static_assert(std::is_aggregate_v<proposal_t<metadata_payload_t>>);
static_assert(std::equality_comparable<proposal_t<metadata_payload_t>>);
static_assert(std::is_aggregate_v<proposal_t<metadata_payload_t, document_stamp_t>>);

auto make_history() -> document_history_t {
    return document_history_t::from({.id = song_id_t{1}});
}

void check_failed_title(const proposal_outcome_t<metadata_payload_t, document_stamp_t>& outcome) {
    CHECK_FALSE(outcome.ok());
    CHECK_FALSE(outcome.value.has_value());
    REQUIRE(outcome.diagnostics.size() == 1);
    CHECK(outcome.diagnostics.front().message == "Title is empty.");
    CHECK(outcome.diagnostics.front().code == diagnostic_code_t::invalid_id);
}

void check_unpublished_title(const document_history_t& history,
                             const document_command_transaction_t& transaction) {
    CHECK_FALSE(transaction.has_copy());
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}

void check_published_title(const document_history_t& history) {
    CHECK(history.document.metadata.title == "Proposed");
    REQUIRE(history.undo.size() == 1);
    CHECK(history.undo.front().label == "Set title");
}

void apply_title_proposal(document_history_t& history,
                          const proposal_t<metadata_payload_t, document_stamp_t>& proposal) {
    auto transaction = proposal.transaction(history, "Set title");
    REQUIRE(transaction.is_ready());
    check_unpublished_title(history, transaction);
    transaction.copy_document().metadata.title = proposal.payload.title;
    CHECK(history.document.metadata.title.empty());
    REQUIRE(transaction.commit("Set title"));
}

} // namespace

TEST_CASE("Shared proposals reject stale stamps before copying", "[core][proposal]") {
    auto history = make_history();
    const proposal_t<metadata_payload_t> proposal{
        .stamp = chart_stamp_t::from(history.document, chart_id_t{1}),
        .payload = {.title = "Proposed"}};
    ++history.document.revision;
    CHECK_FALSE(proposal.is_current(history));
    auto transaction = proposal.transaction(history, "Proposal");
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    test::check_stale_action_message(transaction.outcome.diagnostics, "Proposal");
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposals preserve explicit revision-only identity policy", "[core][proposal]") {
    auto history = make_history();
    const proposal_t<metadata_payload_t> proposal{
        .stamp = chart_stamp_t::from(history.document, chart_id_t{1})};
    history.document.id = song_id_t{2};
    CHECK_FALSE(proposal.is_current(history));
    CHECK(proposal.is_current(history, document_identity_policy_t::revision_only));
    auto rejected = proposal.transaction(history, "Proposal");
    CHECK_FALSE(rejected.is_ready());
    CHECK_FALSE(rejected.has_copy());
    auto accepted = proposal.transaction(history, "Proposal", document_identity_policy_t::revision_only);
    CHECK(accepted.is_ready());
    CHECK_FALSE(accepted.has_copy());
}

TEST_CASE("Shared proposal builds and revisions carry no payload on failure", "[core][proposal]") {
    auto history = make_history();
    const auto failed = metadata_payload_t::propose(history.document, "");
    check_failed_title(failed);
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.ok());
    REQUIRE(built.value.has_value());
    const auto revised = built.value->payload.revise(history.document, "");
    check_failed_title(revised);
    CHECK(built.value->payload.title == "Proposed");
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposals publish through the lazy command transaction", "[core][proposal]") {
    auto history = make_history();
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value.has_value());
    CHECK(built.value->is_current(history));
    apply_title_proposal(history, *built.value);
    check_published_title(history);
    CHECK_FALSE(built.value->is_current(history));
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title.empty());
}

TEST_CASE("Shared proposals retain typed diagnostics and private payload invariants", "[core][proposal]") {
    const auto history = make_history();
    const auto failed = private_payload_t::propose(history.document, -1);
    CHECK_FALSE(failed.value.has_value());
    CHECK_FALSE(failed.ok());
    REQUIRE(failed.diagnostics.size() == 1);
    CHECK(failed.diagnostics.front().code == private_payload_issue_t::negative_number);
    const auto accepted = private_payload_t::propose(history.document, 4);
    CHECK(accepted.value.has_value());
}

TEST_CASE("Shared proposal revisions support mutable state machines", "[core][proposal]") {
    auto initial = mutable_payload_t::propose();
    REQUIRE(initial.value);
    const auto rejected = initial.value->payload.revise(true);
    CHECK_FALSE(rejected.value);
    CHECK(initial.value->payload.is_rejected);
    const auto revised = initial.value->payload.revise(false);
    REQUIRE(revised.value);
    CHECK_FALSE(revised.value->payload.is_rejected);
}

TEST_CASE("Shared proposal reports retain rejected editor data and can be repaired", "[core][proposal]") {
    const auto history = make_history();
    auto initial = reported_payload_t::propose(history.document, std::string(512, 'x'));
    REQUIRE(initial.value);
    const auto stamp = initial.value->stamp;
    const auto* storage = initial.value->payload.report.text.data();
    auto failed = reported_payload_t::revise(std::move(initial), false);
    check_rejected_report(failed, storage);
    CHECK(reported_proposal_t::captured_stamp(failed) == stamp);
    CHECK(reported_proposal_t::report(failed) == &*failed.diagnostics.report);
    const auto repaired = reported_payload_t::revise(std::move(failed), true);
    check_repaired_report(repaired, storage);
    CHECK(repaired.value->stamp == stamp);
}

TEST_CASE("Shared report diagnostics bind directly to existing vector validators", "[core][proposal]") {
    reported_diagnostics_t diagnostics;
    const chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{1}}}};
    CHECK_FALSE(validate_chart_position(chart, {measure_id_t{99}, 0}, diagnostics, "Timing event"));
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostic_queries::has_errors(diagnostics));
    diagnostics = diagnostic_list_t{};
    CHECK(diagnostics.empty());
}

TEST_CASE("Shared report completion does not fabricate an absent editor state", "[core][proposal]") {
    reported_outcome_t outcome;
    reported_proposal_t::complete(outcome, true);
    CHECK_FALSE(outcome.value);
    CHECK(reported_proposal_t::report(outcome) == nullptr);
    const auto view = reported_view_t::from(outcome);
    CHECK(view.report == nullptr);
    CHECK(view.stamp == nullptr);
}

TEST_CASE("Shared report diagnostics keep metadata when validators replace messages", "[core][proposal]") {
    const auto stamp = document_stamp_t::from(7);
    auto diagnostics = reported_diagnostics_t::from(stamp, {.text = "editor"},
        {error(diagnostic_code_t::invalid_id, "first")});
    diagnostics = diagnostic_list_t{error(diagnostic_code_t::invalid_id, "replacement")};
    REQUIRE(diagnostics.report);
    CHECK(diagnostics.report->text == "editor");
    CHECK(diagnostics.stamp == stamp);
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().message == "replacement");
}

TEST_CASE("Rejected proposal reports retain stamped transaction rejection feedback", "[core][proposal]") {
    auto history = make_history();
    const auto outcome = reported_payload_t::propose(history.document, "");
    REQUIRE_FALSE(outcome.value);
    ++history.document.revision;
    const auto transaction = reported_proposal_t::transaction(outcome, history, "Editor report");
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    test::check_stale_action_message(transaction.outcome.diagnostics, "Editor report");
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposal outcomes support inline single diagnostics", "[core][proposal]") {
    using outcome_t = proposal_outcome_t<metadata_payload_t, document_stamp_t,
        std::optional<diagnostic_t>>;
    outcome_t outcome;
    CHECK(outcome.ok());
    outcome.diagnostics = error(diagnostic_code_t::invalid_stage, "Rejected inline.");
    CHECK_FALSE(outcome.ok());
    CHECK_FALSE(outcome.value);
    outcome.diagnostics->severity = diagnostic_severity_t::warning;
    CHECK(outcome.ok());
    outcome.diagnostics.reset();
    CHECK(outcome.ok());
}
