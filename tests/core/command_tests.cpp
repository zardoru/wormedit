#include "../support/stale_action_assertions.h"
#include <catch2/generators/catch_generators.hpp>
#include <wormedit/core/proposal_apply_result.h>
#include <type_traits>
#include <wormedit/core/command.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <memory>
#include <ranges>

using namespace wormedit;

static_assert(std::is_aggregate_v<document_history_t>);
static_assert(std::is_aggregate_v<document_stamp_t>);
static_assert(std::is_aggregate_v<chart_stamp_t>);

namespace {

auto make_song(std::uint64_t row = 130) -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .charts =
            {
                {
                    .id = chart_id_t{1},
                    .measures = {{.id = measure_id_t{1}}},
                    .lane_layouts =
                        {
                            {
                                .id = object_id_t{1},
                                .position = {measure_id_t{1}, 0},
                                .layout =
                                    {
                                        .profile_id = "command-test",
                                        .lanes = {{.id = lane_id_t{1}, .name = "Lane"}},
                                    },
                            },
                        },
                },
            },
    };
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{2},
        .position = {measure_id_t{1}, row},
        .lane = lane_id_t{1},
    });
    REQUIRE(validate_document(song).empty());
    return song;
}

auto note_row(const document_history_t& history) -> std::uint64_t {
    return history.document.charts.front().measures.front().notes.front().position.row;
}

} // namespace

TEST_CASE("Stamped command transactions reject stale captures before copying",
          "[core][command][stamped-transaction]") {
    const auto different_project = GENERATE(false, true);
    auto history = document_history_t::from(make_song());
    auto stamp = chart_stamp_t::from(history.document, chart_id_t{1});
    if (different_project) ++stamp.document.project.value;
    else ++stamp.document.revision;
    auto transaction = document_command_transaction_t::from(history, stamp, "Staged command");
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    test::check_stale_action_message(transaction.outcome.diagnostics, "Staged command");
    CHECK(history.undo.empty());
}

TEST_CASE("Revision-only stamp policy preserves older capture identity coverage",
          "[core][command][stamped-transaction]") {
    auto history = document_history_t::from(make_song());
    const auto stamp = document_stamp_t::from(history.document);
    ++history.document.id.value;
    CHECK_FALSE(stamp.is_current(history.document));
    CHECK(stamp.is_current(history.document, document_identity_policy_t::revision_only));
    auto transaction = document_command_transaction_t::from(history, stamp, "Legacy capture",
        document_identity_policy_t::revision_only);
    CHECK(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    ++history.document.revision;
    CHECK_FALSE(stamp.is_revision_current(history.document.revision));
    auto stale = document_command_transaction_t::from(history, stamp, "Legacy capture",
        document_identity_policy_t::revision_only);
    test::check_stale_action_message(stale.outcome.diagnostics, "Legacy capture");
}

TEST_CASE("document stamps reject replaced projects and changed revisions", "[unit][core][command]") {
    auto document = make_song();
    const auto stamp = document_stamp_t::from(document);
    CHECK(stamp.is_current(document));
    CHECK(stamp == document_stamp_t::from(document));
    ++document.revision;
    CHECK_FALSE(stamp.is_current(document));
    --document.revision;
    document.id = song_id_t{2};
    CHECK_FALSE(stamp.is_current(document));
}

TEST_CASE("chart stamps reject a changed selection without conflating missing targets",
          "[unit][core][command]") {
    const auto document = make_song();
    const auto stamp = chart_stamp_t::from(document, chart_id_t{1});
    CHECK(stamp.is_current(document, chart_id_t{1}));
    CHECK_FALSE(stamp.is_current(document, chart_id_t{2}));
    // Existence is a command validation error, separate from stamp freshness.
    const auto missing = chart_stamp_t::from(document, chart_id_t{99});
    CHECK(missing.is_current(document, chart_id_t{99}));
}

TEST_CASE("stale actions share the owner-approved message template", "[unit][core][command]") {
    CHECK(stale_action_message("Compose view action") ==
          "Compose view action ignored because the project, chart or document revision changed.");
}

TEST_CASE("quantization commits atomically through bounded document history",
          "[unit][core][command]") {
    auto history = document_history_t::from(make_song(), 2);
    CHECK(history.document.revision == 1);
    CHECK_FALSE(is_document_dirty(history));

    const auto applied = apply_quantize_command(history, chart_id_t{1}, {.divisions = 4});
    REQUIRE(applied);
    CHECK(note_row(history) == 240);
    CHECK(history.document.revision == 2);
    CHECK(is_document_dirty(history));
    REQUIRE(applied.payload.quantized_moves.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(note_row(history) == 130);
    CHECK(history.document.revision == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(note_row(history) == 240);
    CHECK(history.document.revision == 2);

    mark_document_saved(history);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(undo_document_command(history));
    CHECK(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK_FALSE(is_document_dirty(history));
}

TEST_CASE("rejected and unknown-chart quantization cannot mutate document history",
          "[unit][core][command][negative]") {
    auto song = make_song(100);
    song.charts.front().measures.front().notes.push_back({
        .id = object_id_t{3},
        .position = {measure_id_t{1}, 110},
        .lane = lane_id_t{1},
    });
    auto history = document_history_t::from(std::move(song));

    const auto rejected = apply_quantize_command(history, chart_id_t{1}, {.divisions = 4});
    CHECK_FALSE(rejected);
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
    CHECK(history.document.charts.front().measures.front().notes[0].position.row == 100);
    CHECK(history.document.charts.front().measures.front().notes[1].position.row == 110);

    const auto unknown = apply_quantize_command(history, chart_id_t{99}, {.divisions = 4});
    CHECK_FALSE(unknown);
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("new edits clear redo and retain only the configured undo depth",
          "[unit][core][command]") {
    auto history = document_history_t::from(make_song(), 1);
    REQUIRE(apply_quantize_command(history, chart_id_t{1}, {.divisions = 4}));
    REQUIRE(undo_document_command(history));

    auto proposed = history.document;
    proposed.metadata.title = "branched edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    CHECK(history.redo.empty());
    CHECK(history.undo.size() == 1);
    CHECK_FALSE(redo_document_command(history));

    proposed = history.document;
    proposed.metadata.artist = "second edit";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename artist"));
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.title == "branched edit");
    CHECK(history.document.metadata.artist.empty());
}

TEST_CASE("exhausted document revision rejects publication", "[unit][core][command][negative]") {
    auto song = make_song();
    song.revision = std::numeric_limits<std::uint64_t>::max();
    auto history = document_history_t::from(std::move(song));
    auto proposed = history.document;
    proposed.metadata.title = "not published";
    CHECK_FALSE(commit_document_command(history, std::move(proposed), "Rename song"));
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("published commands invalidate retained-source exact-save claims reversibly",
          "[unit][core][command][provenance]") {
    auto song = make_song();
    const auto bytes = std::make_shared<const std::vector<std::byte>>();
    song.metadata.format_groups.push_back({
        .family = format_family_t::bms,
        .encoding =
            {
                .detected = text_encoding_t::ascii,
                .original_bytes = bytes,
                .unchanged_since_import = true,
            },
    });
    song.charts.front().metadata.format_groups.push_back({
        .family = format_family_t::bmson,
        .encoding =
            {
                .detected = text_encoding_t::utf8,
                .original_bytes = bytes,
                .unchanged_since_import = true,
            },
    });
    auto history = document_history_t::from(std::move(song));

    auto proposed = history.document;
    proposed.metadata.title = "edited";
    REQUIRE(commit_document_command(history, std::move(proposed), "Rename song"));
    CHECK_FALSE(history.document.metadata.format_groups.front().encoding.unchanged_since_import);
    CHECK_FALSE(history.document.charts.front()
                    .metadata.format_groups.front()
                    .encoding.unchanged_since_import);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.metadata.format_groups.front().encoding.unchanged_since_import);
    CHECK(history.document.charts.front()
              .metadata.format_groups.front()
              .encoding.unchanged_since_import);
    REQUIRE(redo_document_command(history));
    CHECK_FALSE(history.document.metadata.format_groups.front().encoding.unchanged_since_import);
}

TEST_CASE("redivision commits exactly, undoes, and rejects inexact positions untouched",
          "[unit][core][command]") {
    auto history = document_history_t::from(make_song(130));
    const auto revision = history.document.revision;

    const auto applied = apply_redivision_command(history, chart_id_t{1}, 192);
    REQUIRE(applied);
    CHECK(history.document.charts.front().divisions_per_standard_measure == 192);
    CHECK(note_row(history) == 26);
    CHECK(applied.payload.moves.size() == 1);
    CHECK(history.undo.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().divisions_per_standard_measure == 960);
    CHECK(note_row(history) == 130);

    auto inexact = document_history_t::from(make_song(131));
    const auto rejected = apply_redivision_command(inexact, chart_id_t{1}, 192);
    CHECK_FALSE(rejected);
    CHECK(rejected.payload.moves.empty());
    CHECK_FALSE(rejected.command.diagnostics.empty());
    CHECK(inexact.undo.empty());
    CHECK(note_row(inexact) == 131);
}

TEST_CASE("redivision rejects unknown charts and ignores no-ops",
          "[unit][core][command][negative]") {
    auto history = document_history_t::from(make_song(130));
    const auto revision = history.document.revision;

    CHECK_FALSE(apply_redivision_command(history, chart_id_t{9}, 192));
    CHECK_FALSE(apply_redivision_command(history, chart_id_t{1}, 0));

    const auto same = apply_redivision_command(history, chart_id_t{1}, 960);
    CHECK_FALSE(same);
    CHECK(same.command.diagnostics.empty());
    CHECK(history.undo.empty());
    CHECK(history.document.revision == revision);
}

TEST_CASE("document transaction source queries do not copy", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    CHECK_FALSE(transaction.has_copy());
    REQUIRE(transaction.source_chart(chart_id_t{1}));
    CHECK_FALSE(transaction.has_copy());
    CHECK(transaction.chart(chart_id_t{999}) == nullptr);
    CHECK_FALSE(transaction.has_copy());
}

TEST_CASE("document transactions reuse their working snapshot", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    auto& copy = transaction.copy_document();
    copy.charts.front().metadata.name = "Edited";
    CHECK(&transaction.copy_document() == &copy);
    CHECK(transaction.chart(chart_id_t{1})->metadata.name == "Edited");
    CHECK(history.document.charts.front().metadata.name.empty());
}

TEST_CASE("document transactions publish one reversible edit", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    transaction.copy_document().charts.front().metadata.name = "Edited";
    REQUIRE(transaction.commit("Edit name"));
    CHECK(history.document.charts.front().metadata.name == "Edited");
    REQUIRE(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().metadata.name.empty());
}

TEST_CASE("rejecting a document transaction does not copy", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    const auto rejected = transaction.reject(diagnostic_code_t::invalid_id, "Rejected");
    CHECK_FALSE(rejected);
    CHECK(rejected.revision == history.document.revision);
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
}

TEST_CASE("rejected document transactions cannot publish", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    const auto rejected = transaction.reject(diagnostic_code_t::invalid_id, "Rejected");
    CHECK_FALSE(transaction.commit("Rejected edit"));
    CHECK_FALSE(transaction.has_copy());
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 1);
}

TEST_CASE("document transactions adopt staged snapshots and publish once", "[unit][core][command]") {
    auto history = document_history_t::from(make_song());
    auto staged = history.document;
    staged.charts.front().metadata.name = "Staged";
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(staged));
    CHECK(transaction.has_copy());
    REQUIRE(transaction.commit("Staged edit"));
    CHECK_FALSE(transaction.commit("Repeat edit"));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().metadata.name == "Staged");
}

namespace {
enum class command_payload_issue_t { none, rejected };
}

TEST_CASE("shared command payload results preserve proposal acceptance", "[unit][core][command]") {
    using result_t = proposal_apply_result_t<command_payload_issue_t>;
    static_assert(std::is_aggregate_v<result_t>);
    static_assert(std::is_aggregate_v<document_command_transaction_t>);
    static_assert(std::is_aggregate_v<command_result_with_t<std::size_t>>);
    result_t result{.command = {.applied = true},
                    .payload = {.issue = command_payload_issue_t::rejected}};
    CHECK_FALSE(result);
    result.payload.issue = command_payload_issue_t::none;
    CHECK(result);
    result.command.applied = false;
    CHECK_FALSE(result);
}

TEST_CASE("shared transaction rejections preserve diagnostic lists", "[unit][core][command]") {
    auto rejected = document_command_transaction_t::rejection(88, {
        error(diagnostic_code_t::invalid_id, "First"),
        error(diagnostic_code_t::collision, "Second")});
    CHECK_FALSE(rejected);
    CHECK(rejected.revision == 88);
    REQUIRE(rejected.diagnostics.size() == 2);
    CHECK(rejected.diagnostics[1].message == "Second");
}

TEST_CASE("Stamped transactions preserve a proposal's stale diagnostic code",
          "[core][command][stamped-transaction]") {
    const auto chart_capture = GENERATE(false, true);
    auto history = document_history_t::from(make_song());
    const auto stamp = chart_stamp_t::from(history.document, chart_id_t{1});
    ++history.document.revision;
    auto transaction = chart_capture
        ? document_command_transaction_t::from(history, stamp, "Binding proposal",
              document_identity_policy_t::revision_only, diagnostic_code_t::invalid_binding)
        : document_command_transaction_t::from(history, stamp.document, "Binding proposal",
              document_identity_policy_t::revision_only, diagnostic_code_t::invalid_binding);
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    REQUIRE(transaction.outcome.diagnostics.size() == 1);
    CHECK(transaction.outcome.diagnostics.front().code == diagnostic_code_t::invalid_binding);
    test::check_stale_action_message(transaction.outcome.diagnostics, "Binding proposal");
}

TEST_CASE("Finishing an unpublished transaction closes and discards its snapshot",
          "[core][command][stamped-transaction]") {
    auto history = document_history_t::from(make_song());
    auto transaction = document_command_transaction_t::from(history);
    transaction.copy_document().metadata.title = "Unpublished";
    const auto result = transaction.finish_without_commit();
    CHECK_FALSE(result.applied);
    CHECK_FALSE(transaction.is_ready());
    CHECK_FALSE(transaction.has_copy());
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}
