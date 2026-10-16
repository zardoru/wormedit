#include <wormedit/core/command.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>
#include <utility>

using namespace wormedit;

static_assert(std::is_aggregate_v<chart_document_t>);

namespace {

auto make_indices_song() -> song_document_t {
    return {
        .id = song_id_t{1},
        .charts = {{
            .id = chart_id_t{1},
            .divisions_per_standard_measure = 480,
            .measures = {{.id = measure_id_t{1}}},
            .lane_layouts = {{
                .id = object_id_t{1},
                .position = {measure_id_t{1}, 0},
                .layout = {
                    .profile_id = "indices-test",
                    .lanes = {{.id = lane_id_t{1}, .name = "Lane"}},
                },
            }},
            .bpms = {{
                .id = object_id_t{2},
                .position = {measure_id_t{1}, 0},
                .beats_per_minute = {120, 1},
            }},
        }},
    };
}

} // namespace

TEST_CASE("Committed chart index reads reuse their immutable value", "[core][chart-indices]") {
    const auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    CHECK(first->measures.has_value());
    CHECK(first->timing.value.has_value());
    CHECK(history.indices(chart_id_t{1}) == first);
    CHECK(history.indices(chart_id_t{99}) == nullptr);
    CHECK_FALSE(is_document_dirty(history));
}

TEST_CASE("Snapshot copies share their built chart indices", "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    const auto copied = history;
    CHECK(copied.indices(chart_id_t{1}) == first);
}

TEST_CASE("Valid measure geometry remains usable when timing fails", "[core][chart-indices]") {
    auto document = make_indices_song();
    document.charts.front().bpms.clear();
    const auto history = document_history_t::from(std::move(document));
    const auto* indexed = history.indices(chart_id_t{1});
    REQUIRE(indexed != nullptr);
    CHECK(indexed->measures.has_value());
    CHECK_FALSE(indexed->timing.value.has_value());
    CHECK_FALSE(indexed->timing.diagnostics.empty());
    CHECK(history.indices(chart_id_t{1}) == indexed);
}

TEST_CASE("A reopened document with an equal revision starts with fresh indices",
          "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto reopened_document = history.document;
    reopened_document.charts.front().divisions_per_standard_measure = 960;
    const auto reopened = document_history_t::from(std::move(reopened_document));
    const auto* rebuilt = reopened.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(reopened.document.revision == history.document.revision);
    CHECK(rebuilt != first);
    CHECK(rebuilt->measures->total_rows == 960);
}

TEST_CASE("A working document drops committed indices before mutation", "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto transaction = document_command_transaction_t::from(history);
    transaction.copy_document().charts.front().divisions_per_standard_measure = 960;
    // Observe a detached copy without from(), whose reset would mask this boundary.
    const document_history_t detached{.document = std::as_const(transaction).document()};
    const auto* rebuilt = detached.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt != first);
    CHECK(rebuilt->measures->total_rows == 960);
    CHECK(history.indices(chart_id_t{1}) == first);
}

TEST_CASE("Adoption drops indices when replacing an existing working document",
          "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto candidate = history.document;
    candidate.charts.front().divisions_per_standard_measure = 960;
    auto transaction = document_command_transaction_t::from(history);
    static_cast<void>(transaction.copy_document());
    transaction.adopt_document(std::move(candidate));
    const document_history_t detached{.document = std::as_const(transaction).document()};
    const auto* rebuilt = detached.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt != first);
    CHECK(rebuilt->measures->total_rows == 960);
}

TEST_CASE("Working copies also drop the indices of untouched charts", "[core][chart-indices]") {
    auto document = make_indices_song();
    document.charts.push_back(document.charts.front());
    document.charts.back().id = chart_id_t{2};
    auto history = document_history_t::from(std::move(document));
    const auto* first = history.indices(chart_id_t{1});
    const auto* untouched = history.indices(chart_id_t{2});
    REQUIRE(first != nullptr);
    REQUIRE(untouched != nullptr);
    auto transaction = document_command_transaction_t::from(history);
    transaction.copy_document().charts.front().divisions_per_standard_measure = 960;
    const document_history_t detached{.document = std::as_const(transaction).document()};
    CHECK(detached.indices(chart_id_t{1}) != first);
    CHECK(detached.indices(chart_id_t{2}) != untouched);
    CHECK(detached.indices(chart_id_t{2})->measures->total_rows == 480);
}

TEST_CASE("Publishing a replaced chart drops any slots carried by the replacement",
          "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto replacement = history.document.charts.front();
    replacement.divisions_per_standard_measure = 960;
    auto transaction = document_command_transaction_t::from(history);
    *transaction.chart(chart_id_t{1}) = std::move(replacement);
    REQUIRE(transaction.commit("Replace chart").applied);
    const auto* rebuilt = history.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt != first);
    CHECK(rebuilt->measures->total_rows == 960);
}

TEST_CASE("Direct document publication drops copied chart indices", "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto candidate = history.document;
    candidate.charts.front().divisions_per_standard_measure = 960;
    REQUIRE(commit_document_command(history, std::move(candidate), "Resize chart").applied);
    const auto* rebuilt = history.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(rebuilt != first);
    CHECK(rebuilt->measures->total_rows == 960);
}

TEST_CASE("Undo restores the original usable chart indices", "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    const auto* first = history.indices(chart_id_t{1});
    REQUIRE(first != nullptr);
    auto transaction = document_command_transaction_t::from(history);
    transaction.chart(chart_id_t{1})->divisions_per_standard_measure = 960;
    REQUIRE(transaction.commit("Resize chart").applied);
    static_cast<void>(history.indices(chart_id_t{1}));
    REQUIRE(undo_document_command(history));
    CHECK(history.indices(chart_id_t{1}) == first);
    CHECK(history.indices(chart_id_t{1})->measures->total_rows == 480);
}

TEST_CASE("Redo exposes usable indices for the edited chart", "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    static_cast<void>(history.indices(chart_id_t{1}));
    auto transaction = document_command_transaction_t::from(history);
    transaction.chart(chart_id_t{1})->divisions_per_standard_measure = 960;
    REQUIRE(transaction.commit("Resize chart").applied);
    static_cast<void>(history.indices(chart_id_t{1}));
    REQUIRE(undo_document_command(history));
    REQUIRE(redo_document_command(history));
    CHECK(history.indices(chart_id_t{1})->measures->total_rows == 960);
}

TEST_CASE("A chart index failure is retained with the snapshot", "[core][chart-indices]") {
    auto document = make_indices_song();
    document.charts.front().divisions_per_standard_measure = 0;
    auto history = document_history_t::from(std::move(document));
    const auto* failed = history.indices(chart_id_t{1});
    REQUIRE(failed != nullptr);
    CHECK_FALSE(failed->measures.has_value());
    CHECK_FALSE(failed->timing.value.has_value());
    const auto copied = history;
    CHECK(history.indices(chart_id_t{1}) == failed);
    CHECK(copied.indices(chart_id_t{1}) == failed);
}

TEST_CASE("Reopening an equal revision also drops cached index failures",
          "[core][chart-indices]") {
    auto document = make_indices_song();
    document.charts.front().divisions_per_standard_measure = 0;
    auto history = document_history_t::from(std::move(document));
    const auto* failed = history.indices(chart_id_t{1});
    REQUIRE(failed != nullptr);
    auto fixed = history.document;
    fixed.charts.front().divisions_per_standard_measure = 480;
    const auto reopened = document_history_t::from(std::move(fixed));
    const auto* rebuilt = reopened.indices(chart_id_t{1});
    REQUIRE(rebuilt != nullptr);
    CHECK(reopened.document.revision == history.document.revision);
    CHECK(rebuilt != failed);
    CHECK(rebuilt->measures.has_value());
    CHECK(rebuilt->timing.value.has_value());
}

TEST_CASE("An index lease retains aliased measure geometry through equal-revision replacement",
          "[core][chart-indices]") {
    auto history = document_history_t::from(make_indices_song());
    auto lease = history.indices_snapshot(chart_id_t{1});
    REQUIRE(lease);
    const auto* original = lease.get();
    CHECK(original == history.indices(chart_id_t{1}));
    std::shared_ptr<const measure_index_t> measures{lease, &lease->measures.value()};
    auto replacement = make_indices_song();
    replacement.charts.front().divisions_per_standard_measure = 960;
    history = document_history_t::from(std::move(replacement));
    lease.reset();
    const auto* rebuilt = history.indices(chart_id_t{1});
    REQUIRE(rebuilt);
    CHECK(measures->total_rows == 480);
    CHECK(rebuilt->measures.value().total_rows == 960);
    CHECK(rebuilt != original);
}
