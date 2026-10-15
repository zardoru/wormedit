#include "../support/stale_action_assertions.h"
#include "core/position_validation.h"
#include <wormedit/core/proposal.h>

#include <catch2/catch_test_macros.hpp>
#include <memory>

using namespace wormedit;

namespace {

struct metadata_payload_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Title proposal"};
    std::string title;

    auto apply(proposal_edit_scope_t<document_stamp_t>& scope, bool is_rejected_after_edit = false,
               bool is_rejected_by_transaction = false) const
        -> diagnosed_t<std::optional<std::string>> {
        auto& transaction = scope.transaction();
        if (title.empty())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Title is empty.")}};
        if (transaction.document().metadata.title == title) return {};
        transaction.copy_document().metadata.title = title;
        if (is_rejected_after_edit)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Prepared title rejected.")}};
        if (is_rejected_by_transaction)
            transaction.outcome.diagnostics.push_back(
                error(diagnostic_code_t::invalid_id, "Transaction rejected title."));
        return {.value = "Set title"};
    }

    auto operator==(const metadata_payload_t&) const -> bool = default;

    static auto propose(const song_document_t& document, std::string title)
        -> proposal_outcome_t<metadata_payload_t, document_stamp_t> {
        if (title.empty())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Title is empty.")}};
        return {.value = proposal_t<metadata_payload_t, document_stamp_t>{
                    .stamp = document_stamp_t::from(document), .payload = {.title = std::move(title)}}};
    }

    auto revise(const document_stamp_t& stamp, const song_document_t& document, std::string title) const
        -> proposal_outcome_t<metadata_payload_t, document_stamp_t> {
        CHECK(stamp == document_stamp_t::from(document));
        return propose(document, std::move(title));
    }
};

struct warned_title_t : metadata_payload_t {
    static auto validate_apply(const document_stamp_t&) -> diagnostic_list_t {
        return {{.severity = diagnostic_severity_t::warning,
                 .code = diagnostic_code_t::unsupported_feature, .message = "Review title."}};
    }
};

struct consuming_title_t {
    static constexpr proposal_capture_policy_t apply_policy{
        .stale_label = "Consuming title", .identity = document_identity_policy_t::revision_only};
    std::unique_ptr<std::string> title;
    auto apply(proposal_edit_scope_t<document_stamp_t>& scope) &&
        -> diagnosed_t<std::optional<std::string>> {
        auto& transaction = scope.transaction();
        if (scope.captured_stamp().project != transaction.document().id)
            return {.diagnostics = {error(diagnostic_code_t::collision, "Title belongs to another project.")}};
        auto consumed = std::move(title);
        transaction.copy_document().metadata.title = std::move(*consumed);
        return {.value = "Consume title"};
    }
};
static_assert(!std::copy_constructible<consuming_title_t>);
static_assert(proposal_edit<consuming_title_t, proposal_edit_scope_t<document_stamp_t>>);
static_assert(!proposal_edit<consuming_title_t&, proposal_edit_scope_t<document_stamp_t>>);

// State-machine revisions may reject the original state while returning no proposal.
struct mutable_payload_t {
    bool is_rejected{};
    static auto propose() -> proposal_outcome_t<mutable_payload_t> {
        return {.value = proposal_t<mutable_payload_t>{}};
    }
    auto revise(const chart_stamp_t& stamp, bool reject) -> proposal_outcome_t<mutable_payload_t> {
        is_rejected = reject;
        if (reject) return {};
        return {.value = proposal_t<mutable_payload_t>{.stamp = stamp, .payload = *this}};
    }
};
static_assert(staged_proposal<mutable_payload_t, std::tuple<>, std::tuple<const chart_stamp_t&, bool>>);

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
    std::tuple<const document_stamp_t&, const song_document_t&, std::string>, document_stamp_t>);
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

void check_published_title(const document_history_t& history) {
    CHECK(history.document.metadata.title == "Proposed");
    REQUIRE(history.undo.size() == 1);
    CHECK(history.undo.front().label == "Set title");
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
    const auto revised = built.value->revise(history.document, "");
    check_failed_title(revised);
    CHECK(built.value->payload.title == "Proposed");
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposals publish through the lazy command transaction", "[core][proposal]") {
    auto history = make_history();
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value.has_value());
    CHECK(built.value->is_current(history));
    REQUIRE(apply_proposal(history, *built.value));
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
    initial.value->stamp = chart_stamp_t::from(make_history().document, chart_id_t{7});
    const auto stamp = initial.value->stamp;
    const auto rejected = initial.value->revise(true);
    CHECK_FALSE(rejected.value);
    CHECK(initial.value->payload.is_rejected);
    const auto revised = initial.value->revise(false);
    REQUIRE(revised.value);
    CHECK(revised.value->stamp == stamp);
    CHECK_FALSE(revised.value->payload.is_rejected);
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

namespace {
struct action_test_context_t {
    using outcome_type = document_command_result_t;
    document_command_transaction_t& transaction;
    unsigned validated{};
    unsigned performed{};
};
struct action_test_t {
    // A move-only field proves that the runner borrows its action.
    std::unique_ptr<int> token;
    auto validate(action_test_context_t& context) const
        -> std::optional<document_command_result_t> {
        ++context.validated;
        if (!context.transaction.is_ready()) return context.transaction.outcome;
        return std::nullopt;
    }
    auto perform(action_test_context_t& context) const -> document_command_result_t {
        ++context.performed;
        context.transaction.copy_document();
        return context.transaction.finish_without_commit();
    }
};
struct wrong_action_test_t {
    auto validate(action_test_context_t&) -> bool;
    auto perform(action_test_context_t&) -> document_command_result_t;
};
static_assert(compose_action<action_test_t, action_test_context_t>);
static_assert(!compose_action<wrong_action_test_t, action_test_context_t>);
}

TEST_CASE("Compose action validation prevents perform and a stale document copy", "[core][proposal][compose-action]") {
    auto history = make_history();
    const auto stamp = document_stamp_t::from(history.document);
    ++history.document.revision;
    auto transaction = document_command_transaction_t::from(history, stamp, "Test action");
    action_test_context_t context{transaction};
    action_test_t action;
    const auto outcome = run_compose_action(action, context);
    CHECK(context.validated == 1);
    CHECK(context.performed == 0);
    CHECK_FALSE(transaction.has_copy());
    CHECK(outcome.diagnostics == transaction.outcome.diagnostics);
    test::check_stale_action_message(outcome.diagnostics, "Test action");
    CHECK(history.undo.empty());
}

TEST_CASE("Compose action performance follows one successful validation", "[core][proposal][compose-action]") {
    auto history = make_history();
    auto transaction = document_command_transaction_t::from(history);
    action_test_context_t context{transaction};
    const auto outcome = run_compose_action(action_test_t{}, context);
    CHECK(context.validated == 1);
    CHECK(context.performed == 1);
    CHECK(outcome.diagnostics.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("Proposal runner rejects expiry before an edit", "[core][proposal]") {
    auto history = make_history();
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value);
    ++history.document.revision;
    const auto result = apply_proposal(history, *built.value);
    CHECK_FALSE(result.applied);
    test::check_stale_action_message(result.diagnostics, "Title proposal");
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("Proposal runner discards rejected prepared changes", "[core][proposal]") {
    auto history = make_history();
    const auto revision = history.document.revision;
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value);
    const auto result = apply_proposal(history, *built.value, true);
    CHECK_FALSE(result.applied);
    const diagnostic_list_t expected{error(diagnostic_code_t::invalid_id, "Prepared title rejected.")};
    CHECK(result.diagnostics == expected);
    CHECK(history.document.metadata.title.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

TEST_CASE("Proposal runner leaves no-op captures reusable without history", "[core][proposal]") {
    auto history = make_history();
    history.document.metadata.title = "Proposed";
    const auto revision = history.document.revision;
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value);
    const auto result = apply_proposal(history, *built.value);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(built.value->is_current(history));
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

TEST_CASE("Proposal runner preserves transaction rejection", "[core][proposal]") {
    auto history = make_history();
    const auto revision = history.document.revision;
    const auto built = metadata_payload_t::propose(history.document, "Proposed");
    REQUIRE(built.value);
    const auto result = apply_proposal(history, *built.value, false, true);
    CHECK_FALSE(result.applied);
    const diagnostic_list_t expected{error(diagnostic_code_t::invalid_id, "Transaction rejected title.")};
    CHECK(result.diagnostics == expected);
    CHECK(history.document.metadata.title.empty());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposals preserve consuming payloads without a copy", "[core][proposal]") {
    auto history = make_history();
    proposal_t<consuming_title_t, document_stamp_t> pending{
        .stamp = document_stamp_t::from(history.document),
        .payload = {.title = std::make_unique<std::string>("Consumed")}};
    REQUIRE(apply_proposal(history, std::move(pending)));
    CHECK_FALSE(pending.payload.title);
    CHECK(history.document.metadata.title == "Consumed");
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title.empty());
}

TEST_CASE("Shared proposal scopes retain captured provenance before consuming", "[core][proposal]") {
    auto history = make_history();
    proposal_t<consuming_title_t, document_stamp_t> pending{
        .stamp = document_stamp_t::from(history.document),
        .payload = {.title = std::make_unique<std::string>("Consumed")}};
    history.document.id = song_id_t{2};
    const auto rejected = apply_proposal(history, std::move(pending));
    CHECK_FALSE(rejected.applied);
    REQUIRE(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().message == "Title belongs to another project.");
    REQUIRE(pending.payload.title);
    CHECK(*pending.payload.title == "Consumed");
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposal warnings remain before expiry diagnostics", "[core][proposal]") {
    auto history = make_history();
    const proposal_t<warned_title_t, document_stamp_t> pending{
        .stamp = document_stamp_t::from(history.document), .payload = {{.title = "Proposed"}}};
    ++history.document.revision;
    const auto result = apply_proposal(history, pending);
    CHECK_FALSE(result.applied);
    REQUIRE(result.diagnostics.size() == 2);
    CHECK(result.diagnostics.front().message == "Review title.");
    test::check_stale_action_message(result.diagnostics.back().message, "Title proposal");
    CHECK(history.undo.empty());
}

TEST_CASE("Shared proposals retain commit diagnostic replacement", "[core][proposal]") {
    auto history = make_history();
    const proposal_t<warned_title_t, document_stamp_t> pending{
        .stamp = document_stamp_t::from(history.document), .payload = {{.title = "Proposed"}}};
    const auto result = apply_proposal(history, pending);
    CHECK(result.applied);
    // commit_validated_document supplies validation diagnostics, replacing preflight warnings.
    CHECK(result.diagnostics.empty());
    CHECK(history.document.metadata.title == "Proposed");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Shared proposal construction transfers a move-only prepared payload", "[unit][proposal]") {
    const document_stamp_t captured{.project = song_id_t{77}, .revision = 3};
    auto title = std::make_unique<std::string>("Prepared title");
    const auto* original = title.get();
    result_t<consuming_title_t, diagnostic_t> prepared{consuming_title_t{.title = std::move(title)}};
    auto pending = proposal_t<consuming_title_t, document_stamp_t>::from(captured, std::move(prepared));
    REQUIRE(pending.value);
    CHECK(pending.value->payload.title.get() == original);
    CHECK_FALSE(prepared->title);
    CHECK(pending.value->stamp == captured);
    CHECK(pending.diagnostics.empty());
}
