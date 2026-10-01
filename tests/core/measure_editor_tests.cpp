#include <wormedit/core/measure_editor.h>
#include <wormedit/core/timing.h>

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

namespace { void check_audio_extent(bool value) { CHECK(value); } }

TEST_CASE("audio extent appends measures using final BPM and offset without moving notes",
          "[core][measure-editor][audio-extent]") {
    auto song = measure_song();
    auto& chart = song.charts.front();
    chart.beat_zero_audio_offset_seconds = {-1, 1};
    chart.bpms.push_back({.id = object_id_t{31}, .position = {measure_id_t{11}, 0},
                          .beats_per_minute = {60, 1}});
    check_audio_extent(extend_chart_to_audio_end(chart, {48'000, 2, 48'000 * 13, true}));
    REQUIRE(chart.measures.size() == 4);
    check_audio_extent(chart.measures.front().id == measure_id_t{10});
    check_audio_extent(chart.measures[2].notes.front().id == object_id_t{30});
    check_audio_extent(chart.measures[2].notes.front().position == (local_position_t{measure_id_t{12}, 0}));
    check_audio_extent(chart.measures.back().id == measure_id_t{13});
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    check_audio_extent(timing.index->rows.back().after_stops_seconds == (signed_rational_t{13, 1}));
    check_audio_extent(extend_chart_to_audio_end(chart, {48'000, 2, 48'000 * 13, true}));
    check_audio_extent(chart.measures.size() == 4);
    check_audio_extent(validate_document(song).empty());
}

TEST_CASE("audio extent rejects excessive growth atomically and ignores unknown duration",
          "[core][measure-editor][audio-extent]") {
    auto chart = measure_song().charts.front();
    CHECK_FALSE(extend_chart_to_audio_end(chart, {48'000, 2, 48'000 * 10, true},
                                         {.maximum_measures = 4}));
    CHECK(chart.measures.size() == 3);
    CHECK(chart.measures.back().id == measure_id_t{12});
    CHECK(extend_chart_to_audio_end(chart, {}));
    CHECK(chart.measures.size() == 3);
}

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

namespace {

auto removal_song() -> song_document_t {
    auto song = measure_song();
    auto& chart = song.charts.front();
    chart.bms_measure_length_projections.clear();
    chart.measures[0].notes.push_back({.id = object_id_t{40},
                                       .position = {measure_id_t{10}, 240},
                                       .lane = lane_id_t{20},
                                       .kind = note_kind_t::hold,
                                       .end = local_position_t{measure_id_t{11}, 480}});
    chart.measures[1].notes.push_back(
        {.id = object_id_t{41}, .position = {measure_id_t{11}, 100}, .lane = lane_id_t{20}});
    chart.bpms.push_back({.id = object_id_t{42},
                          .position = {measure_id_t{11}, 480},
                          .beats_per_minute = {150, 1}});
    REQUIRE(validate_document(song).empty());
    return song;
}

} // namespace

TEST_CASE("measure removal proposal counts contents without mutating",
          "[core][measure-editor][removal]") {
    const auto song = removal_song();
    const auto& chart = song.charts.front();
    const auto proposal = propose_measure_removal(chart, measure_id_t{11});
    REQUIRE(proposal);
    CHECK(proposal.deleted_notes == 1);
    CHECK(proposal.deleted_bgm_events == 0);
    CHECK(proposal.shortened_holds == 1);
    CHECK(proposal.moved_positions == 1);
    CHECK(chart.measures.size() == 3);
    CHECK(chart.measures[1].notes.size() == 1);
}

TEST_CASE("removing a measure with contents is one undoable command",
          "[core][measure-editor][removal]") {
    auto history = make_document_history(removal_song());
    const auto applied = apply_measure_edit(
        history, history.document.revision,
        remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{11}});
    REQUIRE(applied);

    const auto& chart = history.document.charts.front();
    REQUIRE(chart.measures.size() == 2);
    CHECK(chart.measures[0].notes.size() == 1);
    CHECK(chart.measures[0].notes.front().end == local_position_t{measure_id_t{12}, 0});
    CHECK(chart.measures[1].notes.front().id == object_id_t{30});
    const auto moved = std::ranges::find(chart.bpms, object_id_t{42}, &bpm_event_t::id);
    REQUIRE(moved != chart.bpms.end());
    CHECK(moved->position == local_position_t{measure_id_t{12}, 0});

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.size() == 3);
    CHECK(history.document.charts.front().measures[1].notes.size() == 1);
}

TEST_CASE("measure removal rejects stale, last-measure timing and sole measures",
          "[core][measure-editor][removal][negative]") {
    auto history = make_document_history(removal_song());
    const auto revision = history.document.revision;
    const remove_measure_with_contents_action_t remove_middle{chart_id_t{2}, measure_id_t{11}};
    CHECK_FALSE(apply_measure_edit(history, revision + 1, remove_middle));
    CHECK_FALSE(apply_measure_edit(history, revision,
                                   remove_measure_with_contents_action_t{chart_id_t{2},
                                                                         measure_id_t{99}}));

    auto proposed = history.document;
    proposed.charts.front().bpms.push_back({.id = object_id_t{50},
                                            .position = {measure_id_t{12}, 480},
                                            .beats_per_minute = {90, 1}});
    REQUIRE(commit_document_command(history, std::move(proposed), "Add BPM"));
    const auto last = apply_measure_edit(
        history, history.document.revision,
        remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{12}});
    CHECK_FALSE(last);
    CHECK(has_code(last, diagnostic_code_t::invalid_measure));
    CHECK(history.document.charts.front().measures.size() == 3);

    song_document_t sole = measure_song();
    sole.charts.front().measures.resize(1);
    sole.charts.front().bms_measure_length_projections.clear();
    auto sole_history = make_document_history(std::move(sole));
    CHECK_FALSE(apply_measure_edit(
        sole_history, sole_history.document.revision,
        remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{10}}));
}
