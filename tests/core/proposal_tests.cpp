#include "../support/stale_action_assertions.h"
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
