#include "sys/sound_list_projection.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto catalog() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.audio_assets = {
        {.id = asset_id_t{1}, .source_path = "assets/1-drums.ogg",
         .stream = {.sample_rate = 48'000, .channels = 2, .frame_count = 480'000, .seekable = true}},
        {.id = asset_id_t{2}, .source_path = "assets/2-kick.wav",
         .stream = {.sample_rate = 44'100, .channels = 1, .frame_count = 4'410, .seekable = true}},
        {.id = asset_id_t{3}, .source_path = "assets/3-song.mp3",
         .stream = {.sample_rate = 44'100, .channels = 2, .frame_count = std::nullopt}}};
    song.visual_assets = {{.id = asset_id_t{9}, .source_path = "bg.png"}};
    song.resources = {
        {.id = resource_id_t{1}, .name = "Drums", .asset = asset_id_t{1},
         .roles = {resource_role_t::sample, resource_role_t::bgm},
         .sound_kind = sound_kind_t::stem, .tags = {"Loop", "drums"}},
        {.id = resource_id_t{2}, .name = "Kick", .asset = asset_id_t{2},
         .roles = {resource_role_t::sample}, .tags = {"drums"}},
        {.id = resource_id_t{3}, .name = "Song", .asset = asset_id_t{3},
         .roles = {resource_role_t::music, resource_role_t::bgm}, .missing = true},
        {.id = resource_id_t{4}, .name = "Background", .asset = asset_id_t{9},
         .roles = {resource_role_t::bga}}};
    chart_document_t chart;
    chart.id = chart_id_t{2};
    measure_chunk_t measure;
    measure.id = measure_id_t{3};
    measure.notes = {{.id = object_id_t{10}, .sound = resource_id_t{2}},
                     {.id = object_id_t{11}, .sound = resource_id_t{2}}};
    measure.bgm_events = {{.id = object_id_t{12}, .sound = resource_id_t{1}}};
    chart.measures = {measure};
    song.charts = {chart};
    return song;
}

} // namespace

TEST_CASE("sound list shows audio resources only, with counts and inferred kinds",
          "[unit][sys][sounds]") {
    const auto list = project_sound_list(catalog(), {});
    REQUIRE(list.rows.size() == 3);
    CHECK(list.total_matching == 3);
    CHECK(list.rows[0].name == "Drums");
    CHECK(list.rows[0].kind == sound_kind_t::stem);
    CHECK(list.rows[0].use_count == 1);
    CHECK(list.rows[0].tags_text == "Loop, drums");
    CHECK(list.rows[0].duration_frames == 480'000);
    CHECK(list.rows[1].kind == sound_kind_t::keysound);
    CHECK(list.rows[1].use_count == 2);
    CHECK(list.rows[2].kind == sound_kind_t::stem);
    CHECK(list.rows[2].missing);
    CHECK(list.rows[2].duration_frames == 0);
}

TEST_CASE("sound list filters by name, tag and exact tag ignoring case", "[unit][sys][sounds]") {
    auto list = project_sound_list(catalog(), {.search = "KI"});
    REQUIRE(list.rows.size() == 1);
    CHECK(list.rows[0].name == "Kick");
    list = project_sound_list(catalog(), {.search = "loop"});
    REQUIRE(list.rows.size() == 1);
    CHECK(list.rows[0].name == "Drums");
    list = project_sound_list(catalog(), {.tag = "DRUMS"});
    CHECK(list.total_matching == 2);
    list = project_sound_list(catalog(), {.tag = "drum"});
    CHECK(list.total_matching == 0);
    CHECK(list.all_tags == std::vector<std::string>{"drums", "Loop"});
}

TEST_CASE("sound list windows its rows and clamps the start", "[unit][sys][sounds]") {
    auto song = catalog();
    for (std::uint64_t id = 10; id < 40; ++id) {
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = "x.wav"});
        song.resources.push_back({.id = resource_id_t{id}, .name = "s" + std::to_string(id),
                                  .asset = asset_id_t{id}, .roles = {resource_role_t::sample}});
    }
    const auto list = project_sound_list(song, {.first_row = 30, .row_limit = 5});
    CHECK(list.total_matching == 33);
    REQUIRE(list.rows.size() == 3);
    CHECK(list.rows.front().name == "s37");
    const auto past = project_sound_list(song, {.first_row = 500, .row_limit = 5});
    CHECK(past.rows.empty());
    CHECK(past.first_row == 33);
}

TEST_CASE("sound properties describe one audio resource", "[unit][sys][sounds]") {
    const auto properties = project_sound_properties(catalog(), resource_id_t{1});
    REQUIRE(properties.valid);
    CHECK(properties.source_path == "assets/1-drums.ogg");
    CHECK(properties.channels == 2);
    CHECK(properties.sample_rate == 48'000);
    CHECK(properties.tags == std::vector<std::string>{"Loop", "drums"});
    CHECK(properties.use_count == 1);
    CHECK_FALSE(project_sound_properties(catalog(), resource_id_t{4}).valid);
    CHECK_FALSE(project_sound_properties(catalog(), resource_id_t{99}).valid);
    CHECK_FALSE(project_sound_properties(catalog(), resource_id_t{}).valid);
}
