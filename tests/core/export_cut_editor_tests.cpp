#include <wormedit/core/export_cut.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <array>
#include <limits>

namespace {
using namespace wormedit;

auto cut_song() -> song_document_t {
    return {.id = song_id_t{1}, .revision = 4,
        .charts = {{.id = chart_id_t{2}, .measures = {{.id = measure_id_t{3}}},
            .bpms = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {120, 1}}}}},
        .export_cuts = {{.id = object_id_t{50}, .label = "Intro", .begin_milliseconds = 1000, .end_milliseconds = 2000}}};
}

auto cut_timeline(std::initializer_list<export_cut_t> cuts) -> export_cut_timeline_t {
    auto result = export_cut_timeline_t::from(std::span{cuts.begin(), cuts.size()});
    REQUIRE(result.ok());
    return std::move(result.value);
}

auto one_cut() -> export_cut_timeline_t {
    return cut_timeline({{.id = object_id_t{1}, .begin_milliseconds = 1000, .end_milliseconds = 2000}});
}
} // namespace

TEST_CASE("Export cuts use song-wide identities and one Undo while retaining chart timing", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    const auto before = history.document.charts.front().bpms;
    const auto applied = apply_export_cut_edit(history, insert_edit_t<export_cut_edit_value_t>{
        .value = {.label = "Break", .begin_milliseconds = 1500, .end_milliseconds = 3000}});
    REQUIRE(applied.applied);
    CHECK(history.document.export_cuts.back().id == object_id_t{51});
    CHECK(history.document.export_cuts.size() == 2);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().bpms == before);
    CHECK(history.document.revision == 5);
}

TEST_CASE("Export-cut updates preserve identity and Undo restores the exact value", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    const auto original = history.document.export_cuts.front();
    const auto edit = update_edit_t<export_cut_edit_value_t>{.id = original.id,
        .value = {.label = "Changed", .begin_milliseconds = 1200, .end_milliseconds = 2400}};
    REQUIRE(apply_export_cut_edit(history, edit).applied);
    CHECK(history.document.export_cuts.front() == edit.value.with_id(original.id));
    CHECK_FALSE(apply_export_cut_edit(history, edit).applied);
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.export_cuts.front() == original);
}

TEST_CASE("Export-cut deletion is one reversible command", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    const auto original = history.document.export_cuts;
    REQUIRE(apply_export_cut_edit(history, delete_edit_t<export_cut_edit_value_t>{.id = object_id_t{50}}).applied);
    CHECK(history.document.export_cuts.empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.export_cuts == original);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.export_cuts.empty());
}

TEST_CASE("Export-cut insertion does not reuse an identity retained by Redo", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    const auto edit = insert_edit_t<export_cut_edit_value_t>{.value = {.begin_milliseconds = 3000, .end_milliseconds = 4000}};
    REQUIRE(apply_export_cut_edit(history, edit).applied);
    const auto first = history.document.export_cuts.back().id;
    REQUIRE(undo_document_command(history));
    REQUIRE(apply_export_cut_edit(history, edit).applied);
    CHECK(history.document.export_cuts.back().id.value > first.value);
}

TEST_CASE("Invalid export-cut spans and labels reject atomically", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    const auto original = history.document.export_cuts;
    const auto value = GENERATE(
        export_cut_edit_value_t{.begin_milliseconds = -1, .end_milliseconds = 1},
        export_cut_edit_value_t{.begin_milliseconds = 2, .end_milliseconds = 2},
        export_cut_edit_value_t{.begin_milliseconds = 3, .end_milliseconds = 2},
        export_cut_edit_value_t{.label = std::string(4097, 'x'), .begin_milliseconds = 0, .end_milliseconds = 1},
        export_cut_edit_value_t{.label = std::string(1, '\xff'), .begin_milliseconds = 0, .end_milliseconds = 1});
    const auto result = apply_export_cut_edit(history, insert_edit_t<export_cut_edit_value_t>{.value = value});
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.diagnostics.empty());
    CHECK(history.document.export_cuts == original);
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 4);
}

TEST_CASE("Export-cut edits reject missing or ambiguous identities", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    SECTION("missing or zero") {
        const auto id = GENERATE(object_id_t{}, object_id_t{999});
        CHECK_FALSE(apply_export_cut_edit(history, delete_edit_t<export_cut_edit_value_t>{.id = id}).applied);
    }
    SECTION("ambiguous") {
        history.document.export_cuts.push_back(history.document.export_cuts.front());
        CHECK_FALSE(apply_export_cut_edit(history, delete_edit_t<export_cut_edit_value_t>{.id = object_id_t{50}}).applied);
    }
    CHECK(history.undo.empty());
    CHECK(history.document.revision == 4);
}

TEST_CASE("Export-cut identity exhaustion rejects without mutation", "[core][export-cut][marker-editor]") {
    auto history = document_history_t::from(cut_song());
    history.document.export_cuts.front().id = object_id_t{std::numeric_limits<std::uint64_t>::max()};
    const auto original = history.document.export_cuts;
    const auto result = apply_export_cut_edit(history, insert_edit_t<export_cut_edit_value_t>{
        .value = {.begin_milliseconds = 0, .end_milliseconds = 1}});
    CHECK_FALSE(result.applied);
    CHECK(history.document.export_cuts == original);
    CHECK(history.undo.empty());
}

TEST_CASE("Cut union merges overlaps and adjacency without changing authored order", "[core][export-cut]") {
    const std::array cuts{
        export_cut_t{.id = object_id_t{1}, .begin_milliseconds = 5000, .end_milliseconds = 6000},
        export_cut_t{.id = object_id_t{2}, .begin_milliseconds = 1500, .end_milliseconds = 2500},
        export_cut_t{.id = object_id_t{3}, .begin_milliseconds = 1000, .end_milliseconds = 2000},
        export_cut_t{.id = object_id_t{4}, .begin_milliseconds = 2500, .end_milliseconds = 3000}};
    const auto timeline = export_cut_timeline_t::from(cuts);
    REQUIRE(timeline.ok());
    REQUIRE(timeline.value.intervals().size() == 2);
    CHECK(timeline.value.intervals()[0] == export_cut_interval_t{1000, 3000});
    CHECK(timeline.value.intervals()[1] == export_cut_interval_t{5000, 6000});
    CHECK(timeline.value.removed_milliseconds() == 3000);
    CHECK(cuts[0].begin_milliseconds == 5000);
}

TEST_CASE("Cut timeline uses half-open membership and right-biased seams", "[core][export-cut]") {
    const auto timeline = one_cut();
    CHECK(timeline.contains(1000));
    CHECK_FALSE(timeline.contains(2000));
    CHECK(timeline.collapsed_milliseconds(1500) == 1000);
    CHECK(timeline.collapsed_milliseconds(2000) == 1000);
    CHECK(timeline.source_milliseconds(1000) == 2000);
    CHECK(timeline.next_kept_milliseconds(1500) == 2000);
}

TEST_CASE("Exact cut clocks preserve fractional sample-frame precision and preroll", "[core][export-cut]") {
    const auto timeline = one_cut();
    const signed_rational_t source{2000001, 1000000};
    const signed_rational_t collapsed{1000001, 1000000};
    CHECK(timeline.collapsed_seconds(source) == collapsed);
    CHECK(timeline.source_seconds(collapsed) == source);
    CHECK(timeline.source_seconds(signed_rational_t{1, 1}) == signed_rational_t{2, 1});
    CHECK(timeline.collapsed_seconds(signed_rational_t{-1, 48000}) == signed_rational_t{-1, 48000});
    CHECK(timeline.source_seconds(signed_rational_t{-1, 48000}) == signed_rational_t{-1, 48000});
    CHECK(timeline.contains_seconds(signed_rational_t{3, 2}));
}

TEST_CASE("Continuous cut clocks retain sub-millisecond values", "[core][export-cut]") {
    const auto timeline = one_cut();
    CHECK(timeline.collapsed_seconds(2.000001) == Catch::Approx(1.000001));
    CHECK(timeline.source_seconds(1.000001) == Catch::Approx(2.000001));
    CHECK(timeline.collapsed_seconds(-0.000001) == -0.000001);
    CHECK(timeline.source_seconds(-0.000001) == -0.000001);
}

TEST_CASE("Cut mapping checks full signed clock bounds without a preview-duration cap", "[core][export-cut]") {
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    const auto timeline = cut_timeline({{.id = object_id_t{1}, .begin_milliseconds = 0, .end_milliseconds = maximum}});
    CHECK(timeline.source_milliseconds(0) == maximum);
    CHECK_FALSE(timeline.source_milliseconds(1));
    CHECK(timeline.collapsed_milliseconds(maximum) == 0);
    CHECK(timeline.collapsed_milliseconds(std::numeric_limits<std::int64_t>::min()) == std::numeric_limits<std::int64_t>::min());
    CHECK_FALSE(timeline.source_seconds(signed_rational_t{1, 0}));
    CHECK_FALSE(timeline.collapsed_seconds(signed_rational_t{1, 0}));
}

TEST_CASE("Kept source intervals exclude precisely the clipped cut union", "[core][export-cut]") {
    const auto timeline = cut_timeline({{.id = object_id_t{1}, .begin_milliseconds = 0, .end_milliseconds = 1000},
        {.id = object_id_t{2}, .begin_milliseconds = 2000, .end_milliseconds = 3000}});
    CHECK(timeline.kept_intervals(0, 3000) == std::vector<export_cut_interval_t>{{1000, 2000}});
    CHECK(timeline.kept_intervals(0, 1000).empty());
    CHECK(timeline.kept_intervals(-100, 100) == std::vector<export_cut_interval_t>{{-100, 0}});
    CHECK_FALSE(timeline.intersects({1000, 2000}));
    CHECK(timeline.intersects({1500, 2500}));
}

TEST_CASE("Export-cut validation bounds authored count and total label bytes", "[core][export-cut]") {
    std::vector<export_cut_t> cuts(maximum_export_cuts + 1);
    CHECK_FALSE(validate_export_cuts(cuts).empty());
    cuts.clear();
    for (std::uint64_t id = 1; id <= 257; ++id)
        cuts.push_back({.id = object_id_t{id}, .label = std::string(4096, 'x'), .begin_milliseconds = 0, .end_milliseconds = 1});
    CHECK_FALSE(validate_export_cuts(cuts).empty());
}

TEST_CASE("Export-cut row conversion permits source times beyond twenty-four hours", "[core][export-cut]") {
    chart_document_t chart{.id = chart_id_t{2},
        .measures = {{.id = measure_id_t{3}, .definition = {.length = {1000, 1}}}},
        .bpms = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 0}, .beats_per_minute = {1, 1}}}};
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto span = export_cut_interval_t::from_rows(*timing.value, 400000, 400004);
    REQUIRE(span);
    CHECK(span->begin_milliseconds == 100000000);
    CHECK(span->end_milliseconds == 100001000);
}

TEST_CASE("Export-cut row conversion rejects reversed unresolved and zero-millisecond spans", "[core][export-cut]") {
    auto chart = cut_song().charts.front();
    chart.divisions_per_standard_measure = 1000000;
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    CHECK_FALSE(export_cut_interval_t::from_rows(*timing.value, 1, 1));
    CHECK_FALSE(export_cut_interval_t::from_rows(*timing.value, 2, 1));
    CHECK_FALSE(export_cut_interval_t::from_rows(*timing.value, 1, 2));
    CHECK_FALSE(export_cut_interval_t::milliseconds_at(*timing.value, 1000001));
}
