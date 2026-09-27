#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

namespace {

auto fixture() -> wormedit::song_document_t {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                            "published-vox-slicing.bmson", std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input),
                            std::istreambuf_iterator<char>()};
    const auto parsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    return *parsed.song;
}

} // namespace

TEST_CASE("BMSON restart toggle updates one channel pulse and survives export and undo",
          "[unit][core][bmson][restart]") {
    auto history = wormedit::make_document_history(fixture());
    const auto chart_id = history.document.charts.front().id;
    const auto& initial_chart = history.document.charts.front();
    REQUIRE(initial_chart.sound_channels.size() == 1);
    const auto event_id = initial_chart.sound_channels.front().events.back().id;
    REQUIRE_FALSE(initial_chart.sound_channels.front().events.back().playable);
    REQUIRE(initial_chart.sound_channels.front().events.back().continuation);
    const auto revision = history.document.revision;
    const auto before_scene = wormedit::build_editor_field_scene_index(initial_chart, revision);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(before_scene));
    REQUIRE(std::get<wormedit::editor_field_scene_index_t>(before_scene).bgm_events.size() == 1);
    CHECK(std::get<wormedit::editor_field_scene_index_t>(before_scene)
              .bgm_events.front().bmson_restart == false);

    const auto applied = wormedit::toggle_bmson_sound_channel_restart(
        history, revision, chart_id, event_id);
    REQUIRE(applied);
    CHECK_FALSE(history.document.charts.front().sound_channels.front().events.back().continuation);
    const auto after_scene = wormedit::build_editor_field_scene_index(
        history.document.charts.front(), history.document.revision);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(after_scene));
    CHECK(std::get<wormedit::editor_field_scene_index_t>(after_scene)
              .bgm_events.front().bmson_restart == true);
    const auto slices = wormedit::derive_sound_channel_slices(
        history.document.charts.front(), history.document.charts.front().sound_channels.front(),
        48'000, 200'000);
    REQUIRE(slices);
    CHECK(slices.slices.back().restart);
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.song->charts.front().sound_channels.front().events.back().continuation);

    CHECK_FALSE(wormedit::toggle_bmson_sound_channel_restart(
        history, revision, chart_id, event_id));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.back().continuation);
}

TEST_CASE("BMSON BGM marker move preserves source link and rejects channel collisions",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto source_measure = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(source_measure != chart.measures.end());
    const auto bgm = source_measure->bgm_events.front();
    REQUIRE(bgm.sound_channel_event.has_value());
    REQUIRE(chart.measures.back().id != bgm.position.measure);
    const auto target = wormedit::local_position_t{chart.measures.back().id, 0};
    const auto revision = history.document.revision;
    const auto applied = wormedit::move_bmson_bgm_marker(
        history, revision, chart_id, bgm.id, target);
    REQUIRE(applied);
    const auto& moved_chart = history.document.charts.front();
    const auto moved_measure = std::ranges::find(moved_chart.measures, target.measure,
                                                 &wormedit::measure_chunk_t::id);
    REQUIRE(moved_measure != moved_chart.measures.end());
    const auto moved_item = std::ranges::find(moved_measure->bgm_events, bgm.id,
                                               &wormedit::bgm_event_t::id);
    REQUIRE(moved_item != moved_measure->bgm_events.end());
    const auto& moved = *moved_item;
    CHECK(moved.id == bgm.id);
    CHECK(moved.position == target);
    CHECK(moved.sound_channel_event == bgm.sound_channel_event);
    CHECK(moved_chart.sound_channels.front().events.back().position == target);
    CHECK(wormedit::validate_document(history.document).empty());
    const auto slices = wormedit::derive_sound_channel_slices(
        moved_chart, moved_chart.sound_channels.front(), 48'000, 200'000);
    REQUIRE(slices);
    CHECK(slices.slices.back().absolute_row > 0);

    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.back().position == target);

    CHECK_FALSE(wormedit::move_bmson_bgm_marker(
        history, history.document.revision, chart_id, bgm.id,
        moved_chart.sound_channels.front().events.front().position));
    CHECK_FALSE(wormedit::move_bmson_bgm_marker(
        history, history.document.revision, chart_id, bgm.id, target));
    REQUIRE(wormedit::undo_document_command(history));
    const auto& restored = history.document.charts.front();
    const auto restored_measure = std::ranges::find(restored.measures, bgm.position.measure,
                                                    &wormedit::measure_chunk_t::id);
    REQUIRE(restored_measure != restored.measures.end());
    CHECK(std::ranges::any_of(restored_measure->bgm_events, [&](const auto& event) {
        return event.id == bgm.id && event.position == bgm.position;
    }));
}

TEST_CASE("BMSON BGM marker deletion removes its source event and is undoable",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto initial_sound_event_count = chart.sound_channels.front().events.size();
    const auto measure = std::ranges::find_if(chart.measures, [](const auto& item) {
        return !item.bgm_events.empty();
    });
    REQUIRE(measure != chart.measures.end());
    const auto bgm = measure->bgm_events.front();
    REQUIRE(bgm.sound_channel_event.has_value());
    const auto revision = history.document.revision;
    REQUIRE(wormedit::delete_bmson_bgm_marker(history, revision, chart_id, bgm.id));
    const auto& changed = history.document.charts.front();
    CHECK(std::ranges::none_of(changed.measures, [&](const auto& item) {
        return std::ranges::any_of(item.bgm_events, [&](const auto& event) {
            return event.id == bgm.id;
        });
    }));
    CHECK(std::ranges::none_of(changed.sound_channels.front().events, [&](const auto& event) {
        return event.id == *bgm.sound_channel_event;
    }));
    CHECK(wormedit::validate_document(history.document).empty());
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.size() ==
          initial_sound_event_count - 1);
    CHECK_FALSE(wormedit::delete_bmson_bgm_marker(history, revision, chart_id, bgm.id));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(std::ranges::any_of(history.document.charts.front().sound_channels.front().events,
                              [&](const auto& event) {
                                  return event.id == *bgm.sound_channel_event;
                              }));
}

TEST_CASE("BMSON marker insertion links a new source pulse and BGM in one undoable edit",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& original = history.document.charts.front();
    const auto chart_id = original.id;
    const auto source_count = original.sound_channels.front().events.size();
    const auto source_measure = std::ranges::find_if(original.measures, [](const auto& item) {
        return !item.bgm_events.empty();
    });
    REQUIRE(source_measure != original.measures.end());
    const auto reference = source_measure->bgm_events.front();
    const wormedit::local_position_t target{reference.position.measure,
                                             reference.position.row - 120};
    const auto revision = history.document.revision;
    REQUIRE(wormedit::insert_bmson_bgm_marker(
        history, revision, chart_id, reference.id, target, true));
    const auto& changed = history.document.charts.front();
    const auto& source = changed.sound_channels.front().events.back();
    CHECK(source.position == target);
    CHECK_FALSE(source.playable);
    CHECK_FALSE(source.continuation);
    CHECK(source.id != *reference.sound_channel_event);
    CHECK(changed.sound_channels.front().events.size() == source_count + 1);
    const auto changed_measure = std::ranges::find(changed.measures, target.measure,
                                                   &wormedit::measure_chunk_t::id);
    REQUIRE(changed_measure != changed.measures.end());
    const auto semantic = std::ranges::find_if(changed_measure->bgm_events,
        [&](const auto& event) { return event.sound_channel_event == source.id; });
    REQUIRE(semantic != changed_measure->bgm_events.end());
    CHECK(semantic->id != reference.id);
    CHECK(semantic->position == target);
    CHECK(semantic->source_lane == reference.source_lane);
    CHECK(semantic->sound == reference.sound);
    CHECK(wormedit::validate_document(history.document).empty());
    const auto slices = wormedit::derive_sound_channel_slices(
        changed, changed.sound_channels.front(), 48'000, 200'000);
    REQUIRE(slices);
    CHECK(std::ranges::any_of(slices.slices, [&](const auto& slice) {
        return slice.restart && std::ranges::find(slice.events, source.id) != slice.events.end();
    }));
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(std::ranges::any_of(reparsed.song->charts.front().sound_channels.front().events,
                              [&](const auto& event) {
                                  return event.position == target && !event.playable &&
                                         !event.continuation;
                              }));

    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id, target, false));
    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id,
        changed.sound_channels.front().events.front().position, false));
    CHECK_FALSE(wormedit::insert_bmson_bgm_marker(
        history, revision, chart_id, reference.id,
        {target.measure, target.row + 1}, false));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.size() == source_count);
    REQUIRE(wormedit::insert_bmson_bgm_marker(
        history, history.document.revision, chart_id, reference.id, target, false));
    CHECK(history.document.charts.front().sound_channels.front().events.back().continuation);
}

TEST_CASE("BMSON marker batch validates every pulse and commits one history step",
          "[unit][core][bmson][marker]") {
    auto song = fixture();
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = wormedit::make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    const auto reference_measure = std::ranges::find_if(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    REQUIRE(reference_measure != chart.measures.end());
    const auto reference = reference_measure->bgm_events.front().id;
    const auto reference_measure_id = reference_measure->id;
    const auto source_count = chart.sound_channels.front().events.size();
    const auto bgm_count = chart.measures.front().bgm_events.size() +
                           reference_measure->bgm_events.size();
    const std::vector<wormedit::bmson_bgm_marker_insertion_t> markers{
        {{chart.measures.front().id, 480}, false},
        {{reference_measure_id, 600}, true},
        {{reference_measure_id, 840}, false},
    };
    const auto revision = history.document.revision;
    const std::vector<wormedit::bmson_bgm_marker_insertion_t> too_many(257, markers.front());
    CHECK_FALSE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, too_many));
    auto duplicates = markers;
    duplicates.push_back(markers.front());
    CHECK_FALSE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, duplicates));
    CHECK(history.document.revision == revision);
    auto outside = markers;
    outside[2].position.row = 999'999;
    CHECK_FALSE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, outside));
    CHECK(history.document.revision == revision);
    auto occupied = markers;
    occupied[2].position = chart.sound_channels.front().events.front().position;
    CHECK_FALSE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, occupied));
    CHECK(history.document.revision == revision);
    REQUIRE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, markers));
    const auto& changed = history.document.charts.front();
    CHECK(changed.sound_channels.front().events.size() == source_count + markers.size());
    const auto changed_reference = std::ranges::find(changed.measures,
        reference_measure_id, &wormedit::measure_chunk_t::id);
    REQUIRE(changed_reference != changed.measures.end());
    CHECK(changed.measures.front().bgm_events.size() +
          changed_reference->bgm_events.size() == bgm_count + markers.size());
    for (std::size_t index = 0; index < markers.size(); ++index) {
        const auto& source = changed.sound_channels.front().events[source_count + index];
        CHECK(source.position == markers[index].position);
        CHECK(source.continuation == !markers[index].restart);
        CHECK(std::ranges::any_of(changed.measures, [&](const auto& measure) {
            return std::ranges::any_of(measure.bgm_events, [&](const auto& event) {
                return event.position == source.position &&
                       event.sound_channel_event == source.id;
            });
        }));
    }
    CHECK(wormedit::validate_document(history.document).empty());
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().sound_channels.front().events.size() ==
          source_count + markers.size());
    CHECK_FALSE(wormedit::insert_bmson_bgm_markers(
        history, revision, chart_id, reference, markers));
    REQUIRE(wormedit::undo_document_command(history));
    CHECK(history.document.charts.front().sound_channels.front().events.size() == source_count);
    CHECK(history.undo.empty());
}

TEST_CASE("BMSON razor path fills exact grid cells across measures in pointer order",
          "[unit][core][bmson][razor]") {
    const auto chart = fixture().charts.front();
    REQUIRE(chart.measures.size() >= 2);
    const wormedit::local_position_t begin{chart.measures[0].id, 720};
    const wormedit::local_position_t end{chart.measures[1].id, 240};
    const auto forward = wormedit::interpolate_bmson_razor_path(chart, begin, end, 8);
    REQUIRE(forward);
    const std::vector<wormedit::local_position_t> expected{
        {chart.measures[0].id, 840}, {chart.measures[1].id, 0},
        {chart.measures[1].id, 120}, {chart.measures[1].id, 240}};
    CHECK(forward.positions == expected);
    const auto backward = wormedit::interpolate_bmson_razor_path(chart, end, begin, 8);
    REQUIRE(backward);
    CHECK(backward.positions == (std::vector<wormedit::local_position_t>{
        {chart.measures[1].id, 120}, {chart.measures[1].id, 0},
        {chart.measures[0].id, 840}, {chart.measures[0].id, 720}}));
    const auto still = wormedit::interpolate_bmson_razor_path(chart, begin, begin, 8);
    REQUIRE(still);
    CHECK(still.positions.empty());
    CHECK_FALSE(wormedit::interpolate_bmson_razor_path(chart, begin, end, 8, 3));
    CHECK_FALSE(wormedit::interpolate_bmson_razor_path(
        chart, {chart.measures[0].id, 721}, end, 8));
}
