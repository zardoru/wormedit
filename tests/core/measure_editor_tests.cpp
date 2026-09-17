#include <wormedit/core/measure_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

using namespace wormedit;

namespace {

auto measure_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 5,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{10}},
                                 {.id = measure_id_t{11}},
                                 {.id = measure_id_t{12},
                                  .notes = {{.id = object_id_t{30},
                                             .position = {measure_id_t{12}, 0},
                                             .lane = lane_id_t{20}}}}},
                    .lane_layouts = {{.id = object_id_t{21},
                                      .position = {measure_id_t{10}, 0},
                                      .layout = {.profile_id = "test-1k",
                                                 .display_name = "1K",
                                                 .lanes = {{.id = lane_id_t{20},
                                                            .name = "Key 1"}}}}},
                    .bpms = {{.id = object_id_t{22},
                              .position = {measure_id_t{10}, 0},
                              .beats_per_minute = {120, 1}}},
                    .bms_measure_length_projections =
                        {{.measure_index = 2, .source_order = 8, .imported_length = {1, 1}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto absolute_note_row(const song_document_t& song) -> std::uint64_t {
    const auto& chart = song.charts.front();
    const auto index = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    const auto row = absolute_row(std::get<measure_index_t>(index),
                                  chart.measures.back().notes.front().position);
    REQUIRE(row.has_value());
    return *row;
}

} // namespace

TEST_CASE("measure insertion length editing and deletion preserve stable local identities",
          "[unit][core][measure-editor]") {
    auto history = make_document_history(measure_song());
    CHECK(absolute_note_row(history.document) == 1920);

    const auto inserted = apply_measure_edit(
        history, 5, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 1});
    REQUIRE(inserted);
    REQUIRE(history.document.charts.front().measures.size() == 4);
    const auto inserted_id = history.document.charts.front().measures[1].id;
    CHECK(inserted_id == measure_id_t{13});
    CHECK(history.document.charts.front().measures[2].id == measure_id_t{11});
    CHECK(history.document.charts.front().bms_measure_length_projections.front().measure_index ==
          3);
    CHECK(absolute_note_row(history.document) == 2880);
    CHECK(is_document_dirty(history));

    const auto changed =
        apply_measure_edit(history, 6,
                           update_measure_definition_action_t{
                               .chart = chart_id_t{2},
                               .measure = inserted_id,
                               .definition = {.length = {3, 4}, .meter = meter_t{6, 8}}});
    REQUIRE(changed);
    const auto index = build_measure_index(history.document.charts.front());
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    CHECK(std::get<measure_index_t>(index).entries[1].row_capacity == 720);
    CHECK(absolute_note_row(history.document) == 2640);

    const auto removed = apply_measure_edit(
        history, 7, delete_measure_action_t{.chart = chart_id_t{2}, .measure = inserted_id});
    REQUIRE(removed);
    CHECK(history.document.charts.front().measures.size() == 3);
    CHECK(history.document.charts.front().bms_measure_length_projections.front().measure_index ==
          2);
    CHECK(absolute_note_row(history.document) == 1920);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[1].id == inserted_id);
    CHECK(history.document.charts.front().measures[1].definition.length == rational_t{3, 4});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().measures.size() == 3);
}

TEST_CASE("measure definition edits reject positions that no longer fit",
          "[unit][core][measure-editor][negative]") {
    auto song = measure_song();
    song.charts.front().measures.back().notes.front().position.row = 800;
    REQUIRE(validate_document(song).empty());
    auto history = make_document_history(std::move(song));

    const auto rejected =
        apply_measure_edit(history, 5,
                           update_measure_definition_action_t{
                               .chart = chart_id_t{2},
                               .measure = measure_id_t{12},
                               .definition = {.length = {3, 4}, .meter = meter_t{3, 4}}});
    CHECK_FALSE(rejected.applied);
    CHECK(has_code(rejected, diagnostic_code_t::row_out_of_range));
    CHECK(history.document.revision == 5);
    CHECK(history.document.charts.front().measures.back().definition.length == rational_t{1, 1});
    CHECK(history.undo.empty());
}

TEST_CASE("measure deletion rejects every referenced or final measure atomically",
          "[unit][core][measure-editor][negative]") {
    auto history = make_document_history(measure_song());
    for (const auto id : {measure_id_t{10}, measure_id_t{12}}) {
        CAPTURE(id.value);
        const auto rejected = apply_measure_edit(
            history, 5, delete_measure_action_t{.chart = chart_id_t{2}, .measure = id});
        CHECK_FALSE(rejected.applied);
        CHECK_FALSE(rejected.diagnostics.empty());
        CHECK(history.document.charts.front().measures.size() == 3);
    }

    auto single = measure_song();
    auto& chart = single.charts.front();
    chart.measures.erase(chart.measures.begin() + 1, chart.measures.end());
    chart.bms_measure_length_projections.clear();
    REQUIRE(validate_document(single).empty());
    auto single_history = make_document_history(std::move(single));
    const auto final_measure = apply_measure_edit(
        single_history, 5,
        delete_measure_action_t{.chart = chart_id_t{2}, .measure = measure_id_t{10}});
    CHECK_FALSE(final_measure.applied);
    CHECK(has_code(final_measure, diagnostic_code_t::invalid_measure));
    CHECK(single_history.document.charts.front().measures.size() == 1);
}

TEST_CASE("measure editor validates revisions IDs definitions indexes and caps",
          "[unit][core][measure-editor][bounds]") {
    auto history = make_document_history(measure_song());

    CHECK_FALSE(apply_measure_edit(
        history, 4, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 1}));
    CHECK_FALSE(apply_measure_edit(
        history, 5, insert_measure_action_t{.chart = chart_id_t{99}, .before_index = 1}));
    CHECK_FALSE(apply_measure_edit(
        history, 5, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 4}));

    const auto inexact = apply_measure_edit(
        history, 5,
        insert_measure_action_t{.chart = chart_id_t{2},
                                .before_index = 1,
                                .definition = {.length = {1, 7}, .meter = std::nullopt}});
    CHECK_FALSE(inexact.applied);
    CHECK(has_code(inexact, diagnostic_code_t::invalid_measure));

    const auto meter_mismatch =
        apply_measure_edit(history, 5,
                           update_measure_definition_action_t{
                               .chart = chart_id_t{2},
                               .measure = measure_id_t{11},
                               .definition = {.length = {3, 4}, .meter = meter_t{4, 4}}});
    CHECK_FALSE(meter_mismatch.applied);
    CHECK(has_code(meter_mismatch, diagnostic_code_t::invalid_measure));

    const auto cap = apply_measure_edit(
        history, 5, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 1},
        {.maximum_measures = 3});
    CHECK_FALSE(cap.applied);
    CHECK(has_code(cap, diagnostic_code_t::invalid_resolution));

    const auto row_cap =
        apply_measure_edit(history, 5,
                           update_measure_definition_action_t{
                               .chart = chart_id_t{2},
                               .measure = measure_id_t{11},
                               .definition = {.length = {2, 1}, .meter = meter_t{8, 4}}},
                           {.maximum_rows_per_measure = 1'000});
    CHECK_FALSE(row_cap.applied);
    CHECK(has_code(row_cap, diagnostic_code_t::invalid_resolution));

    const auto no_op = apply_measure_edit(
        history, 5,
        update_measure_definition_action_t{
            .chart = chart_id_t{2},
            .measure = measure_id_t{11},
            .definition = history.document.charts.front().measures[1].definition});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());

    const auto unknown = apply_measure_edit(
        history, 5, delete_measure_action_t{.chart = chart_id_t{2}, .measure = measure_id_t{999}});
    CHECK_FALSE(unknown.applied);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
    CHECK(history.document.revision == 5);
    CHECK(history.undo.empty());
}
