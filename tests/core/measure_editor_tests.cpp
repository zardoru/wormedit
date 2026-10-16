#include <wormedit/core/measure_editor.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <limits>

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
    auto history = document_history_t::from(measure_song());
    CHECK(absolute_note_row(history.document) == 1920);

    const auto inserted = apply_measure_edit(
        history, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 1});
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
        apply_measure_edit(history, update_measure_definition_action_t{
                                        .chart = chart_id_t{2},
                                        .measure = inserted_id,
                                        .definition = {.length = {3, 4}, .meter = meter_t{6, 8}}});
    REQUIRE(changed);
    const auto index = build_measure_index(history.document.charts.front());
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    CHECK(std::get<measure_index_t>(index).entries[1].row_capacity == 720);
    CHECK(absolute_note_row(history.document) == 2640);

    const auto removed = apply_measure_edit(
        history, delete_measure_action_t{.chart = chart_id_t{2}, .measure = inserted_id});
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
    auto history = document_history_t::from(std::move(song));

    const auto rejected =
        apply_measure_edit(history, update_measure_definition_action_t{
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
    auto history = document_history_t::from(measure_song());
    for (const auto id : {measure_id_t{10}, measure_id_t{12}}) {
        CAPTURE(id.value);
        const auto rejected = apply_measure_edit(
            history, delete_measure_action_t{.chart = chart_id_t{2}, .measure = id});
        CHECK_FALSE(rejected.applied);
        CHECK_FALSE(rejected.diagnostics.empty());
        CHECK(history.document.charts.front().measures.size() == 3);
    }

    auto single = measure_song();
    auto& chart = single.charts.front();
    chart.measures.erase(chart.measures.begin() + 1, chart.measures.end());
    chart.bms_measure_length_projections.clear();
    REQUIRE(validate_document(single).empty());
    auto single_history = document_history_t::from(std::move(single));
    const auto final_measure =
        apply_measure_edit(single_history, delete_measure_action_t{.chart = chart_id_t{2},
                                                                   .measure = measure_id_t{10}});
    CHECK_FALSE(final_measure.applied);
    CHECK(has_code(final_measure, diagnostic_code_t::invalid_measure));
    CHECK(single_history.document.charts.front().measures.size() == 1);
}

TEST_CASE("measure editor validates IDs definitions indexes and caps",
          "[unit][core][measure-editor][bounds]") {
    auto history = document_history_t::from(measure_song());

    CHECK_FALSE(apply_measure_edit(
        history, insert_measure_action_t{.chart = chart_id_t{99}, .before_index = 1}));
    CHECK_FALSE(apply_measure_edit(
        history, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 4}));

    const auto inexact = apply_measure_edit(
        history, insert_measure_action_t{.chart = chart_id_t{2},
                                         .before_index = 1,
                                         .definition = {.length = {1, 7}, .meter = std::nullopt}});
    CHECK_FALSE(inexact.applied);
    CHECK(has_code(inexact, diagnostic_code_t::invalid_measure));

    const auto meter_mismatch =
        apply_measure_edit(history, update_measure_definition_action_t{
                                        .chart = chart_id_t{2},
                                        .measure = measure_id_t{11},
                                        .definition = {.length = {3, 4}, .meter = meter_t{4, 4}}});
    CHECK_FALSE(meter_mismatch.applied);
    CHECK(has_code(meter_mismatch, diagnostic_code_t::invalid_measure));

    const auto cap = apply_measure_edit(
        history, insert_measure_action_t{.chart = chart_id_t{2}, .before_index = 1},
        {.maximum_measures = 3});
    CHECK_FALSE(cap.applied);
    CHECK(has_code(cap, diagnostic_code_t::invalid_resolution));

    const auto row_cap =
        apply_measure_edit(history,
                           update_measure_definition_action_t{
                               .chart = chart_id_t{2},
                               .measure = measure_id_t{11},
                               .definition = {.length = {2, 1}, .meter = meter_t{8, 4}}},
                           {.maximum_rows_per_measure = 1'000});
    CHECK_FALSE(row_cap.applied);
    CHECK(has_code(row_cap, diagnostic_code_t::invalid_resolution));

    const auto no_op = apply_measure_edit(
        history, update_measure_definition_action_t{
                     .chart = chart_id_t{2},
                     .measure = measure_id_t{11},
                     .definition = history.document.charts.front().measures[1].definition});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());

    const auto unknown = apply_measure_edit(
        history, delete_measure_action_t{.chart = chart_id_t{2}, .measure = measure_id_t{999}});
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
    auto history = document_history_t::from(removal_song());
    const auto applied = apply_measure_edit(
        history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{11}});
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

TEST_CASE("measure removal deletes contained BGA events and moves timing out of the measure",
          "[core][measure-editor][removal]") {
    auto song = removal_song();
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "/tmp/bga.png"}};
    song.resources = {{.id = resource_id_t{1}, .name = "bga", .asset = asset_id_t{1}}};
    auto& chart = song.charts.front();
    chart.bga_events.push_back({.id = object_id_t{43},
                                .position = {measure_id_t{11}, 240},
                                .resource = resource_id_t{1},
                                .provenance = {.source_order = 20}});
    chart.bga_events.push_back({.id = object_id_t{44},
                                .position = {measure_id_t{10}, 240},
                                .resource = resource_id_t{1},
                                .provenance = {.source_order = 21}});
    REQUIRE(validate_document(song).empty());
    const auto proposal = propose_measure_removal(chart, measure_id_t{11});
    REQUIRE(proposal);
    CHECK(proposal.deleted_bga_events == 1);
    CHECK(chart.bga_events.size() == 2);

    auto history = document_history_t::from(std::move(song));
    REQUIRE(apply_measure_edit(
        history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{11}}));
    const auto& edited = history.document.charts.front();
    REQUIRE(edited.bga_events.size() == 1);
    CHECK(edited.bga_events.front().id == object_id_t{44});
    const auto moved = std::ranges::find(edited.bpms, object_id_t{42}, &bpm_event_t::id);
    REQUIRE(moved != edited.bpms.end());
    CHECK(moved->position == local_position_t{measure_id_t{12}, 0});
}

TEST_CASE("measure removal rejects unknown targets, last-measure timing and sole measures",
          "[core][measure-editor][removal][negative]") {
    auto history = document_history_t::from(removal_song());
    CHECK_FALSE(apply_measure_edit(
        history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{99}}));

    auto proposed = history.document;
    proposed.charts.front().bpms.push_back({.id = object_id_t{50},
                                            .position = {measure_id_t{12}, 480},
                                            .beats_per_minute = {90, 1}});
    REQUIRE(commit_document_command(history, std::move(proposed), "Add BPM"));
    const auto last = apply_measure_edit(
        history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{12}});
    CHECK_FALSE(last);
    CHECK(has_code(last, diagnostic_code_t::invalid_measure));
    CHECK(history.document.charts.front().measures.size() == 3);

    song_document_t sole = measure_song();
    sole.charts.front().measures.resize(1);
    sole.charts.front().bms_measure_length_projections.clear();
    auto sole_history = document_history_t::from(std::move(sole));
    CHECK_FALSE(apply_measure_edit(
        sole_history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{10}}));
}

TEST_CASE("mass measure definition update is one atomic undo step",
          "[unit][core][measure-editor]") {
    auto history = document_history_t::from(measure_song());
    const auto definition = measure_definition_t{.length = {3, 4}, .meter = meter_t{3, 4}};

    const auto unknown = apply_measure_edit(
        history,
        update_measures_definition_action_t{.chart = chart_id_t{2},
                                            .measures = {measure_id_t{10}, measure_id_t{999}},
                                            .definition = definition});
    CHECK_FALSE(unknown.applied);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
    CHECK(history.document.charts.front().measures[0].definition.length == rational_t{1, 1});

    const auto duplicate = apply_measure_edit(
        history,
        update_measures_definition_action_t{.chart = chart_id_t{2},
                                            .measures = {measure_id_t{10}, measure_id_t{10}},
                                            .definition = definition});
    CHECK_FALSE(duplicate.applied);

    const auto bad =
        apply_measure_edit(history, update_measures_definition_action_t{
                                        .chart = chart_id_t{2},
                                        .measures = {measure_id_t{10}},
                                        .definition = {.length = {3, 4}, .meter = meter_t{4, 4}}});
    CHECK_FALSE(bad.applied);
    CHECK(history.undo.empty());

    const auto ok = apply_measure_edit(
        history,
        update_measures_definition_action_t{.chart = chart_id_t{2},
                                            .measures = {measure_id_t{10}, measure_id_t{12}},
                                            .definition = definition});
    REQUIRE(ok);
    const auto& measures = history.document.charts.front().measures;
    CHECK(measures[0].definition.length == rational_t{3, 4});
    CHECK(measures[1].definition.length == rational_t{1, 1});
    CHECK(measures[2].definition.length == rational_t{3, 4});
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].definition.length == rational_t{1, 1});
    CHECK(history.document.charts.front().measures[2].definition.length == rational_t{1, 1});
}

TEST_CASE("resizing a measure keeps every object on its row from the start of the song",
          "[unit][core][measure-editor][keep-rows]") {
    auto song = measure_song();
    song.charts.front().measures[0].notes.push_back(
        {.id = object_id_t{31}, .position = {measure_id_t{10}, 800}, .lane = lane_id_t{20}});
    song.charts.front().bpms.push_back(
        {.id = object_id_t{32}, .position = {measure_id_t{11}, 480}, .beats_per_minute = {90, 1}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(song);

    const auto shrunk =
        apply_measure_edit(history, resize_measure_keep_rows_action_t{
                                        .chart = chart_id_t{2},
                                        .measure_indices = {0},
                                        .definition = {.length = {3, 4}, .meter = meter_t{3, 4}}});
    REQUIRE(shrunk);
    const auto& chart = history.document.charts.front();
    CHECK(chart.measures[0].definition.length == rational_t{3, 4});
    // Row 800 now lies past the 720-row measure, in the next one.
    CHECK(chart.measures[0].notes.empty());
    REQUIRE(chart.measures[1].notes.size() == 1);
    CHECK(chart.measures[1].notes[0].position == (local_position_t{measure_id_t{11}, 80}));
    // The next measure's own objects keep their absolute rows too: 960 + 480 = 1440 -> 720 + 720.
    CHECK(chart.bpms.back().position == (local_position_t{measure_id_t{11}, 720}));
    CHECK(absolute_note_row(history.document) == 1920);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].notes.size() == 1);
}

TEST_CASE("resizing materializes virtual tail measures and keeps coverage",
          "[unit][core][measure-editor][keep-rows]") {
    auto history = document_history_t::from(measure_song());
    REQUIRE(has_chart_virtual_tail(history.document.charts.front()));
    const auto grown =
        apply_measure_edit(history, resize_measure_keep_rows_action_t{
                                        .chart = chart_id_t{2},
                                        .measure_indices = {5},
                                        .definition = {.length = {3, 4}, .meter = meter_t{3, 4}}});
    REQUIRE(grown);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.measures.size() == 6);
    CHECK(chart.measures[5].definition.length == rational_t{3, 4});
    CHECK(chart.measures[4].definition.length == rational_t{1, 1});

    const auto beyond =
        apply_measure_edit(history, resize_measure_keep_rows_action_t{
                                        .chart = chart_id_t{2},
                                        .measure_indices = {100000},
                                        .definition = {.length = {3, 4}, .meter = meter_t{3, 4}}});
    CHECK_FALSE(beyond.applied);
    CHECK(has_code(beyond, diagnostic_code_t::invalid_measure));
}

TEST_CASE("shortening measures extends a native chart back over its audio",
          "[unit][core][measure-editor][keep-rows]") {
    auto song = measure_song();
    song.audio_assets.push_back({.id = asset_id_t{7}, .stream = {48'000, 2, 48'000 * 6, true}});
    song.resources.push_back({.id = resource_id_t{8}, .asset = asset_id_t{7},
                              .roles = {resource_role_t::music}});
    song.charts.front().source_family = format_family_t::native_project;
    auto history = document_history_t::from(song);
    const auto before = history.document.charts.front().measures.size();
    const auto resized =
        apply_measure_edit(history, resize_measure_keep_rows_action_t{
                                        .chart = chart_id_t{2},
                                        .measure_indices = {0, 1, 2},
                                        .definition = {.length = {1, 4}, .meter = meter_t{1, 4}}});
    REQUIRE(resized);
    CHECK(history.document.charts.front().measures.size() > before);
}

TEST_CASE("moving a bar line changes only the two neighboring measures",
          "[unit][core][measure-editor][keep-rows]") {
    auto song = measure_song();
    song.charts.front().measures[1].notes.push_back(
        {.id = object_id_t{31}, .position = {measure_id_t{11}, 100}, .lane = lane_id_t{20}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(song);
    const auto moved = apply_measure_edit(
        history, move_bar_line_action_t{.chart = chart_id_t{2},
                                        .measure_index = 0,
                                        .before = {.length = {3, 4}, .meter = meter_t{3, 4}},
                                        .after = {.length = {5, 4}, .meter = meter_t{5, 4}}});
    REQUIRE(moved);
    const auto& chart = history.document.charts.front();
    CHECK(chart.measures[0].definition.length == rational_t{3, 4});
    CHECK(chart.measures[1].definition.length == rational_t{5, 4});
    CHECK(chart.measures[2].definition.length == rational_t{1, 1});
    // The row 1060 from the song start is now 340 into the second measure; the third is untouched.
    CHECK(chart.measures[1].notes.front().position == (local_position_t{measure_id_t{11}, 340}));
    CHECK(absolute_note_row(history.document) == 1920);
    CHECK(history.undo.size() == 1);

    const auto uneven = apply_measure_edit(
        history, move_bar_line_action_t{.chart = chart_id_t{2},
                                        .measure_index = 0,
                                        .before = {.length = {1, 2}, .meter = meter_t{2, 4}},
                                        .after = {.length = {1, 1}, .meter = meter_t{4, 4}}});
    CHECK_FALSE(uneven.applied);
    CHECK(has_code(uneven, diagnostic_code_t::invalid_measure));
}

TEST_CASE("removing the first measure keeps layout, BPM and a validating document",
          "[core][measure-editor][removal]") {
    auto song = measure_song();
    auto& chart = song.charts.front();
    chart.bms_measure_length_projections.clear();
    chart.measures[0].notes.push_back({.id = object_id_t{31},
                                       .position = {measure_id_t{10}, 480},
                                       .lane = lane_id_t{20}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    REQUIRE(apply_measure_edit(
        history, remove_measure_with_contents_action_t{chart_id_t{2}, measure_id_t{10}}));
    CHECK(validate_document(history.document).empty());
    const auto& edited = history.document.charts.front();
    CHECK(edited.lane_layouts.front().position == local_position_t{measure_id_t{11}, 0});
    CHECK(edited.bpms.front().position == local_position_t{measure_id_t{11}, 0});
    const auto index = build_measure_index(edited);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    const auto timing = build_timing_index(edited, {.allow_bounded_clock = true});
    CHECK(timing.index.has_value());
}

TEST_CASE("an empty leading measure that anchors BPM and layout cannot be deleted as empty",
          "[core][measure-editor][removal]") {
    auto song = measure_song();
    auto& chart = song.charts.front();
    chart.bms_measure_length_projections.clear();
    chart.bpms.front().provenance.source_order = 0;
    chart.bms_bpm_projections.push_back({.event = object_id_t{22}, .source_order = 0,
                                         .imported_position = {measure_id_t{10}, 0},
                                         .imported_beats_per_minute = {120, 1}});
    REQUIRE(validate_document(song).empty());
    const auto& base = song.charts.front();
    CHECK(has_measure_positioned_objects(base, measure_id_t{10}));
    CHECK_FALSE(has_measure_positioned_objects(base, measure_id_t{11}));
    CHECK_FALSE(propose_measure_removal(base, measure_id_t{10}));
    auto history = document_history_t::from(std::move(song));
    const auto plain =
        apply_measure_edit(history, delete_measure_action_t{chart_id_t{2}, measure_id_t{10}});
    CHECK_FALSE(plain);
    CHECK(history.document.charts.front().measures.size() == 3);
}

TEST_CASE("Measure split preserves all existing heads absolute rows and duration", "[core][measure-editor][measure-split]") {
    auto document = measure_song();
    auto& chart = document.charts.front();
    chart.measures[0].notes = {{.id = object_id_t{40}, .position = {measure_id_t{10},120},
        .lane = lane_id_t{20}, .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{10},800}}};
    chart.bgm_source_lanes = {{.id = object_id_t{43}, .name = "BGM"}};
    chart.measures[0].bgm_events = {{.id = object_id_t{41}, .position = {measure_id_t{10},600},
        .source_lane = object_id_t{43},
        .end = local_position_t{measure_id_t{11},100}}};
    chart.bpms.push_back({.id=object_id_t{42}, .position={measure_id_t{10},600}, .beats_per_minute={150,1}});
    auto history = document_history_t::from(document);
    const auto before = build_measure_index(chart);
    const auto before_timing = build_timing_index(chart);
    const auto result =
        apply_measure_edit(history, split_measure_action_t{chart.id, {measure_id_t{10}, 480}});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(result.applied);
    const auto& split = history.document.charts.front();
    const auto after = build_measure_index(split);
    const auto& index = std::get<measure_index_t>(after);
    check_audio_extent(split.measures.size() == 4);
    check_audio_extent(index.total_rows == std::get<measure_index_t>(before).total_rows);
    for (const auto& measure : document.charts.front().measures)
        check_audio_extent(absolute_row(index, {measure.id,0}) == absolute_row(std::get<measure_index_t>(before),{measure.id,0}));
    check_audio_extent(split.measures[1].id == measure_id_t{13});
    check_audio_extent(absolute_row(index,split.measures[0].notes.front().position) == 120);
    check_audio_extent(absolute_row(index,*split.measures[0].notes.front().end) == 800);
    check_audio_extent(absolute_row(index,split.measures[1].bgm_events.front().position) == 600);
    check_audio_extent(absolute_row(index,*split.measures[1].bgm_events.front().end) == 1060);
    check_audio_extent(absolute_row(index,split.bpms.back().position) == 600);
    const auto timing = build_timing_index(split);
    REQUIRE(timing);
    REQUIRE(before_timing);
    check_audio_extent(timing.index->rows.back().after_stops_seconds == before_timing.index->rows.back().after_stops_seconds);
    check_audio_extent(validate_document(history.document).empty());
    check_audio_extent(history.undo.size() == 1);
    check_audio_extent(undo_document_command(history));
    check_audio_extent(history.document.charts.front().measures.size() == 3);
    check_audio_extent(history.document.charts.front().measures[0].bgm_events.front().position.row == 600);
    check_audio_extent(redo_document_command(history));
    check_audio_extent(history.document.charts.front().measures.size() == 4);
}

TEST_CASE("Measure split head invalid and limit boundaries leave history unchanged", "[core][measure-editor][measure-split]") {
    auto history = document_history_t::from(measure_song());
    check_audio_extent(
        !apply_measure_edit(history, split_measure_action_t{chart_id_t{2}, {measure_id_t{10}, 0}})
             .applied);
    check_audio_extent(
        !apply_measure_edit(history, split_measure_action_t{chart_id_t{2}, {measure_id_t{10}, 960}})
             .applied);
    check_audio_extent(
        !apply_measure_edit(history, split_measure_action_t{chart_id_t{2}, {measure_id_t{10}, 480}},
                            {.maximum_measures = 3})
             .applied);
    check_audio_extent(history.undo.empty());
    check_audio_extent(history.document.revision == 5);
}

TEST_CASE("Measure split supports imported BMS without moving exported later heads", "[core][measure-editor][measure-split]") {
    const std::string source = "#PLAYER 1\n#BPM 120\n#WAV01 kick.wav\n#00111:01\n#00211:01\n";
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{source.data(),source.size()}), {.lane_profile_id="bme-7k-outside-scratch"});
    REQUIRE(parsed.value.song);
    auto history = document_history_t::from(*parsed.value.song);
    const auto& chart = history.document.charts.front();
    const auto id = chart.id;
    const auto measure = chart.measures.front().id;
    const auto half = chart.divisions_per_standard_measure/2;
    REQUIRE(apply_measure_edit(history, split_measure_action_t{id, {measure, half}}).applied);
    const auto written = write_bms(history.document);
    for (const auto& diagnostic : written.diagnostics) UNSCOPED_INFO(diagnostic.message);
    REQUIRE(written.value);
    const auto reloaded = parse_bms_bytes(std::as_bytes(std::span{written.value->data(),written.value->size()}), {.lane_profile_id="bme-7k-outside-scratch"});
    REQUIRE(reloaded.value.song);
    const auto& after = reloaded.value.song->charts.front();
    const auto index = build_measure_index(after);
    check_audio_extent(absolute_row(std::get<measure_index_t>(index),after.measures[2].notes.front().position) == after.divisions_per_standard_measure);
    check_audio_extent(absolute_row(std::get<measure_index_t>(index),after.measures[3].notes.front().position) == after.divisions_per_standard_measure*2);
}

TEST_CASE("Measure split preserves retained BMSON source heads and tails", "[core][measure-editor][measure-split]") {
    const std::string source = R"({"version":"1.0.0","info":{"title":"split","mode_hint":"beat-7k","init_bpm":120,"resolution":240},"lines":[{"y":0},{"y":960},{"y":1920}],"sound_channels":[{"name":"kick.wav","notes":[{"x":1,"y":240,"l":480,"c":false},{"x":0,"y":600,"l":600,"c":false}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{source.data(),source.size()}));
    REQUIRE(parsed.value);
    auto history = document_history_t::from(*parsed.value);
    const auto& chart = history.document.charts.front();
    const auto id = chart.id; const auto measure = chart.measures.front().id;
    REQUIRE(apply_measure_edit(
                history,
                split_measure_action_t{id, {measure, chart.divisions_per_standard_measure / 2}})
                .applied);
    const auto& split = history.document.charts.front();
    const auto index = build_measure_index(split);
    const auto& rows = std::get<measure_index_t>(index);
    const auto& events = split.sound_channels.front().events;
    check_audio_extent(absolute_row(rows,events[0].position) == 240);
    check_audio_extent(absolute_row(rows,*events[0].end) == 720);
    check_audio_extent(absolute_row(rows,events[1].position) == 600);
    check_audio_extent(absolute_row(rows,*events[1].end) == 1200);
    const auto written = write_bmson(history.document);
    REQUIRE(written.value);
    const auto reloaded = parse_bmson_bytes(std::as_bytes(std::span{written.value->data(),written.value->size()}));
    REQUIRE(reloaded.value);
    check_audio_extent(validate_document(*reloaded.value).empty());
}

namespace {
auto pause_conversion_song() -> song_document_t {
    auto song = measure_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.push_back({.id = object_id_t{50},
        .position = {measure_id_t{10}, 480}, .lane = lane_id_t{20}, .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{12}, 240}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{54}, .name = "BGM"});
    chart.measures.back().bgm_events.push_back({.id = object_id_t{51},
        .position = {measure_id_t{12}, 48}, .source_lane = object_id_t{54}});
    chart.scrolls.push_back({.id = object_id_t{52}, .position = {measure_id_t{12}, 96}, .multiplier = {2, 1}});
    chart.speeds.push_back({.id = object_id_t{53}, .position = {measure_id_t{10}, 720},
        .multiplier = {2, 1}, .transition_beats = {6, 1}});
    return song;
}
auto pause_conversion_preserves_times(bool resize, pause_kind_t kind) -> bool {
    auto history = document_history_t::from(pause_conversion_song());
    const auto original = history.document;
    const auto old_timing = build_timing_index(original.charts.front());
    const auto result = apply_measure_edit(
        history, convert_range_to_pause_action_t{
                     chart_id_t{2}, {{measure_id_t{11}, 0}, {measure_id_t{11}, 48}}, kind, resize});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    if (!result.applied || history.undo.size() != 1 || !old_timing) return false;
    const auto& chart = history.document.charts.front();
    const auto after = build_timing_index(chart);
    UNSCOPED_INFO("pause " << chart.pauses.front().duration.numerator << "/" << chart.pauses.front().duration.denominator);
    if (!after || chart.pauses.size() != 1 || chart.pauses.front().duration != signed_rational_t{1, 5}) return false;
    for (const auto row : {480U, 720U, 1920U, 1968U, 2016U, 2160U}) {
        const auto old = old_timing.index->seconds_at(row);
        const auto changed = after.index->seconds_at(row >= 1008 ? row - 48 : row);
        if (!old || !changed || old.seconds != changed.seconds) return false;
    }
    const auto capacity = validate_measure(960, chart.measures[1].definition).row_capacity;
    if (!capacity || *capacity != (resize ? 912 : 960)) return false;
    UNSCOPED_INFO("transition " << chart.speeds.front().transition_beats.numerator << "/" << chart.speeds.front().transition_beats.denominator);
    if (chart.speeds.front().transition_beats != rational_t{29, 5}) return false;
    if (!undo_document_command(history)) return false;
    auto restored = history.document;
    restored.revision = original.revision;
    return write_native_project(restored).value == write_native_project(original).value;
}
auto pause_conversion_boundaries() -> bool {
    auto song = measure_song();
    song.charts.front().measures[1].notes.push_back({.id = object_id_t{60},
        .position = {measure_id_t{11}, 48}, .lane = lane_id_t{20}});
    auto history = document_history_t::from(song);
    const position_span_t range{{measure_id_t{11}, 0}, {measure_id_t{11}, 48}};
    if (apply_measure_edit(
            history, convert_range_to_pause_action_t{chart_id_t{2}, range, pause_kind_t::stop})
            .applied)
        return false;
    if (!history.undo.empty()) return false;
    if (!apply_measure_edit(
             history, convert_range_to_pause_action_t{chart_id_t{2}, range, pause_kind_t::delay})
             .applied)
        return false;
    const auto& chart = history.document.charts.front();
    return chart.measures[1].notes.front().position.row == 0 && chart.pauses.front().kind == pause_kind_t::delay;
}
auto pause_conversion_bms_round_trip(bool resize) -> bool {
    const std::string source_bytes = "#TITLE Convert\n#BPM 120\n#WAV01 kick.wav\n#00111:01\n#00211:01\n";
    auto imported = parse_bms_bytes(std::as_bytes(std::span{source_bytes.data(), source_bytes.size()}), {.lane_profile_id = "bme-7k-outside-scratch"});
    if (!imported.value.song) return false;
    auto history = document_history_t::from(*imported.value.song);
    const auto& source = history.document.charts.front();
    const auto id = source.id;
    const auto resolution = source.divisions_per_standard_measure;
    const auto measure = source.measures.front().id;
    const auto result = apply_measure_edit(
        history,
        convert_range_to_pause_action_t{id,
                                        {{measure, resolution / 4}, {measure, resolution / 2}},
                                        pause_kind_t::stop,
                                        resize});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    if (!result.applied) return false;
    const auto written = write_bms(history.document, {.branch_policy = bms_branch_policy_t::flatten_active});
    for (const auto& diagnostic : written.diagnostics) UNSCOPED_INFO(diagnostic.message);
    if (!written.value) return false;
    const auto parsed = parse_bms_bytes(std::as_bytes(std::span{written.value->data(), written.value->size()}), {.lane_profile_id = "bme-7k-outside-scratch"});
    if (!parsed.value.song) return false;
    const auto timing = build_timing_index(parsed.value.song->charts.front());
    if (!timing) return false;
    const auto built = build_measure_index(parsed.value.song->charts.front());
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures) return false;
    std::vector<signed_rational_t> times;
    for (const auto& chunk : parsed.value.song->charts.front().measures)
        for (const auto& note : chunk.notes) {
            const auto row = absolute_row(*measures, note.position);
            if (!row) return false;
            const auto at = timing.index->seconds_at(*row);
            if (!at) return false;
            times.push_back(*at.seconds);
        }
    return times == std::vector<signed_rational_t>{{2, 1}, {4, 1}};
}
} // namespace

TEST_CASE("Empty rows convert to beat-based Stop or Delay preserving times and one full Undo", "[core][range-pause][T560]") {
    check_audio_extent(pause_conversion_preserves_times(false, pause_kind_t::stop));
    check_audio_extent(pause_conversion_preserves_times(true, pause_kind_t::stop));
    check_audio_extent(pause_conversion_preserves_times(false, pause_kind_t::delay));
    check_audio_extent(pause_conversion_preserves_times(true, pause_kind_t::delay));
}
TEST_CASE("Range pause conversion respects the occupied side of a boundary", "[core][range-pause][T560]") {
    check_audio_extent(pause_conversion_boundaries());
}
TEST_CASE("Range pause conversion survives explicit flattened BMS export and reimport", "[core][range-pause][T560]") {
    check_audio_extent(pause_conversion_bms_round_trip(false));
    check_audio_extent(pause_conversion_bms_round_trip(true));
}

namespace {
auto pause_conversion_rejects_atomically(unsigned scenario) -> bool {
    auto song = pause_conversion_song();
    auto& chart = song.charts.front();
    const local_position_t middle{measure_id_t{11}, 24};
    switch (scenario) {
    case 0: chart.measures[1].notes.push_back({.id = object_id_t{80}, .position = middle, .lane = lane_id_t{20}}); break;
    case 1: chart.measures[1].bgm_events.push_back({.id = object_id_t{80}, .position = middle}); break;
    case 2: chart.bpms.push_back({.id = object_id_t{80}, .position = middle, .beats_per_minute = {180, 1}}); break;
    case 3: chart.pauses.push_back({.id = object_id_t{80}, .position = middle, .duration = {1, 2}}); break;
    case 4: chart.warps.push_back({.id = object_id_t{80}, .span = {{measure_id_t{10}, 720}, {measure_id_t{11}, 240}}}); break;
    case 5: chart.measures.front().notes.front().end = middle; break;
    case 6: chart.speeds.front().transition_beats = {11, 10}; break; // row 984, inside the removed interval
    case 7: chart.scrolls.front().id = object_id_t{std::numeric_limits<std::uint64_t>::max()}; break;
    default: break;
    }
    auto history = document_history_t::from(song);
    const auto before = write_native_project(history.document).value;
    const auto result = apply_measure_edit(
        history,
        convert_range_to_pause_action_t{
            chart.id, {{measure_id_t{11}, 0}, {measure_id_t{11}, 48}}, pause_kind_t::stop, true});
    return !result.applied && !result.diagnostics.empty() && history.undo.empty() &&
        write_native_project(history.document).value == before;
}
auto pause_conversion_bmson_aliases() -> bool {
    const std::string source_bytes = R"({"version":"1.0.0","info":{"title":"convert","mode_hint":"beat-7k","init_bpm":120,"resolution":240},"lines":[{"y":0},{"y":960},{"y":1920}],"sound_channels":[{"name":"kick.wav","notes":[{"x":1,"y":240,"l":480,"c":false},{"x":0,"y":840,"l":480,"c":false}]}]})";
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{source_bytes.data(), source_bytes.size()}));
    if (!parsed.value) return false;
    auto history = document_history_t::from(*parsed.value);
    const auto& source = history.document.charts.front();
    const auto measure = source.measures.front().id;
    const auto resolution = source.divisions_per_standard_measure;
    const auto result = apply_measure_edit(
        history,
        convert_range_to_pause_action_t{source.id,
                                        {{measure, resolution / 20}, {measure, resolution / 10}},
                                        pause_kind_t::stop,
                                        true});
    if (!result.applied || !validate_document(history.document).empty()) return false;
    const auto& chart = history.document.charts.front();
    const auto built = build_measure_index(chart);
    const auto* measures = std::get_if<measure_index_t>(&built);
    if (!measures || chart.sound_channels.front().events.size() != 2) return false;
    const auto& retained = chart.sound_channels.front().events.front();
    const auto& note = chart.measures.front().notes.front();
    return note.sound_channel_event == retained.id && note.position == retained.position && note.end == retained.end &&
        absolute_row(*measures, note.position) == resolution / 5 &&
        absolute_row(*measures, *note.end) == resolution * 7 / 10;
}
} // namespace

TEST_CASE("Range pause conversion rejects unsafe intervals without changing history", "[core][range-pause][T560]") {
    for (unsigned scenario = 0; scenario < 8; ++scenario)
        check_audio_extent(pause_conversion_rejects_atomically(scenario));
}
TEST_CASE("Range pause conversion keeps BMSON semantic and retained heads and tails coherent", "[core][range-pause][T560]") {
    check_audio_extent(pause_conversion_bmson_aliases());
}

namespace {
auto pause_conversion_whole_measure() -> bool {
    auto history = document_history_t::from(pause_conversion_song());
    const auto old = build_timing_index(history.document.charts.front());
    const auto result = apply_measure_edit(
        history, convert_range_to_pause_action_t{chart_id_t{2},
                                                 {{measure_id_t{11}, 0}, {measure_id_t{12}, 0}},
                                                 pause_kind_t::delay,
                                                 true});
    for (const auto& diagnostic : result.diagnostics) UNSCOPED_INFO(diagnostic.message);
    if (!result.applied || !old) return false;
    const auto& chart = history.document.charts.front();
    const auto after = build_timing_index(chart);
    if (!after || chart.measures.size() != 2 || chart.measures.back().id != measure_id_t{12}) return false;
    for (const auto row : {1920U, 1968U, 2016U, 2160U}) {
        const auto before_time = old.index->seconds_at(row);
        const auto after_time = after.index->seconds_at(row - 960);
        if (!before_time || !after_time || before_time.seconds != after_time.seconds) return false;
    }
    return chart.pauses.front().position == local_position_t{measure_id_t{12}, 0} &&
        chart.speeds.front().transition_beats == rational_t{2, 1};
}
} // namespace
TEST_CASE("Range pause conversion can remove a whole measure without losing surviving IDs or endpoints", "[core][range-pause][T560]") {
    check_audio_extent(pause_conversion_whole_measure());
}
