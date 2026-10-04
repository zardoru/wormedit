#include "sys/sound_list_projection.h"

#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

void require_sound_projection(bool value) { REQUIRE(value); }
void check_sound_projection(bool value) { CHECK(value); }

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

TEST_CASE("keysound list holds placeable samples in name order with their layers",
          "[unit][sys][sounds]") {
    auto song = catalog();
    song.resources[1].target_identities = {
        {.family = format_family_t::bms, .identifier = "0A", .namespace_name = "WAV"}};
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Vocal", .color = {1, 2, 3, 255}},
                             {.id = layer_id_t{8}, .name = "Drums", .color = {4, 5, 6, 255}}};
    chart.sound_layer_defaults = {{.resource = resource_id_t{2},
                                   .layers = {layer_id_t{8}, layer_id_t{99}, layer_id_t{7}}}};

    const auto list = project_keysound_list(song, &chart, 100);
    // The stem (1), the music/BGM file (3) and the BGA image (4) are not keysounds.
    REQUIRE(list.rows.size() == 1);
    CHECK(list.total == 1);
    CHECK_FALSE(list.truncated);
    REQUIRE(list.layers.size() == 2);
    CHECK(list.layers[1].name == "Drums");
    CHECK(list.layers[1].color == color_rgba_t{4, 5, 6, 255});
    const auto& row = list.rows.front();
    CHECK(row.resource == resource_id_t{2});
    CHECK(row.slot == "0A");
    CHECK(row.name == "Kick");
    CHECK_FALSE(row.missing);
    // The dangling layer 99 is ignored; both real layers are set.
    CHECK(row.layer_mask == 0b11);

    const auto without_chart = project_keysound_list(song, nullptr, 100);
    REQUIRE(without_chart.rows.size() == 1);
    CHECK(without_chart.rows.front().layer_mask == 0);
    CHECK(without_chart.layers.empty());
}

TEST_CASE("keysound dock natural and ID sorting happen before the row limit", "[sys][sounds][dock]") {
    song_document_t song;
    const std::array names{"snd10.wav", "snd2.wav", "snd1.wav"};
    const std::array slots{"0Z", "0a", "10"};
    for (std::size_t index = 0; index < names.size(); ++index) {
        const auto id = static_cast<std::uint64_t>(index + 1);
        song.audio_assets.push_back({.id = asset_id_t{id}, .source_path = names[index]});
        song.resources.push_back({.id = resource_id_t{id}, .name = names[index],
            .asset = asset_id_t{id}, .roles = {resource_role_t::sample},
            .target_identities = {{format_family_t::bms, slots[index], "WAV"}}});
    }
    const auto natural = project_keysound_list(song, nullptr, 2);
    require_sound_projection(natural.rows.size() == 2);
    check_sound_projection(natural.rows[0].name == "snd1.wav");
    check_sound_projection(natural.rows[1].name == "snd2.wav");
    const auto by_id = project_keysound_list(song, nullptr, 2, dock_sort_t::bms_id);
    require_sound_projection(by_id.rows.size() == 2);
    check_sound_projection(by_id.rows[0].slot == "0a");
    check_sound_projection(by_id.rows[1].slot == "0Z");
    check_sound_projection(by_id.bms_capacity.used == 3);
    check_sound_projection(by_id.bms_capacity.total == 1295);
    const auto without_ids = project_keysound_list(song, nullptr, 2, dock_sort_t::bms_id, false);
    require_sound_projection(without_ids.rows.size() == 2);
    check_sound_projection(without_ids.rows[0].name == "snd1.wav");
    check_sound_projection(without_ids.rows[1].name == "snd2.wav");
}

TEST_CASE("keysound list is bounded and reports the full count",
          "[unit][sys][sounds]") {
    song_document_t song;
    for (std::uint64_t index = 1; index <= 40; ++index) {
        song.audio_assets.push_back({.id = asset_id_t{index}, .source_path = "k.wav"});
        song.resources.push_back({.id = resource_id_t{index}, .asset = asset_id_t{index},
                                  .roles = {resource_role_t::sample}});
    }
    const auto list = project_keysound_list(song, nullptr, 16);
    CHECK(list.rows.size() == 16);
    CHECK(list.total == 40);
    CHECK(list.truncated);
    CHECK(list.rows.front().name == "Resource 1");
    CHECK(list.rows.front().slot.empty());
}

TEST_CASE("sound layer conflicts distinguish matching layers from unlayered notes",
          "[unit][sys][sounds][sound-note-layers]") {
    auto song = catalog();
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}}, {.id = layer_id_t{8}}};
    CHECK(project_keysound_list(song, &chart, 100).rows.front().note_layer_conflict_mask == 0);
    chart.measures.front().notes.front().semantic_layers = {layer_id_t{7}};
    CHECK(project_keysound_list(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b10);
    chart.sound_layer_defaults = {{.resource = resource_id_t{2}, .layers = {layer_id_t{7}}}};
    CHECK(project_keysound_list(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b10);
    chart.measures.front().notes.front().semantic_layers.push_back(layer_id_t{8});
    CHECK(project_keysound_list(song, &chart, 100).rows.front().note_layer_conflict_mask == 0b01);
}

TEST_CASE("BMS slots project empty and occupied IDs in a bounded window",
          "[unit][sys][sounds][sound-slots]") {
    auto song = catalog();
    song.resources[1].target_identities = {{format_family_t::bms, "0a", "wav"}};
    song.resources[2].target_identities = {{format_family_t::bms, "ZZ", "WAV"}};
    auto list = project_sound_slots(song, {.first_row = 8, .row_limit = 3});
    require_sound_projection(list.rows.size() == 3);
    check_sound_projection(list.total_matching == 1295);
    check_sound_projection(list.first_row == 8);
    check_sound_projection(list.rows[0].identifier == "09");
    check_sound_projection(!list.rows[0].resource);
    check_sound_projection(list.rows[0].name == "(empty)");
    check_sound_projection(list.rows[1].identifier == "0A");
    check_sound_projection(list.rows[1].resource == resource_id_t{2});
    check_sound_projection(list.rows[1].use_count == 2);
    check_sound_projection(list.rows[1].placeable);
    check_sound_projection(!list.rows[1].missing);
    list = project_sound_slots(song, {.search = "zz", .selected_slot = 1295});
    check_sound_projection(list.selected_row == 0);
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows[0].number == 1295);
    check_sound_projection(list.rows[0].missing);
    check_sound_projection(!list.rows[0].placeable);
}

TEST_CASE("BMS slot window clamps limits and filters names without changing assignments",
          "[unit][sys][sounds][sound-slots]") {
    auto song = catalog();
    song.resources[1].target_identities = {{format_family_t::bms, "01", "WAV"}};
    const auto identity = song.resources[1].target_identities.front().identifier;
    auto list = project_sound_slots(song, {.row_limit = 2000});
    check_sound_projection(list.rows.size() == 128);
    check_sound_projection(list.rows.front().number == 1);
    list = project_sound_slots(song, {.search = "kIcK", .selected_slot = 1});
    check_sound_projection(list.selected_row == 0);
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows.front().identifier == "01");
    check_sound_projection(list.total_matching == 1);
    list = project_sound_slots(song, {.first_row = 1294, .row_limit = 64});
    require_sound_projection(list.rows.size() == 1);
    check_sound_projection(list.rows.front().identifier == "ZZ");
    list = project_sound_slots(song, {.first_row = SIZE_MAX, .row_limit = SIZE_MAX});
    check_sound_projection(list.rows.empty());
    check_sound_projection(list.first_row == 1295);
    check_sound_projection(song.resources[1].target_identities.front().identifier == identity);
}
