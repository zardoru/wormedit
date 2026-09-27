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
