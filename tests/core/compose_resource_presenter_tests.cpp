#include "sys/compose_resource_presenter.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("Compose resource catalog retains indexed roles selection and missing state",
          "[unit][frontend][compose][resources]") {
    song_document_t document;
    document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = "audio/kick.wav"});
    document.visual_assets.push_back(
        {.id = asset_id_t{2}, .kind = asset_kind_t::image, .source_path = "images/bg.png"});
    document.resources = {
        {.id = resource_id_t{10},
         .name = "Kick",
         .asset = asset_id_t{1},
         .roles = {resource_role_t::sample, resource_role_t::bgm},
         .target_identities = {{format_family_t::bmson, "kick", "sound"},
                               {format_family_t::bms, "01", "WAV"}}},
        {.id = resource_id_t{11},
         .name = "Backdrop",
         .asset = asset_id_t{2},
         .roles = {resource_role_t::bga},
         .target_identities = {{format_family_t::bms, "A1", "BMP"}}},
        {.id = resource_id_t{12},
         .asset = asset_id_t{99},
         .roles = {resource_role_t::sample},
         .missing = true},
        {.id = resource_id_t{13},
         .name = "Song",
         .asset = asset_id_t{1},
         .roles = {resource_role_t::music}},
    };

    const auto catalog = sys::compose_resource_catalog_t::from(document, asset_id_t{1}, 2,
                                                             asset_id_t{2});

    REQUIRE(catalog.entries.size() == 2);
    CHECK(catalog.total_count == 3);
    CHECK(catalog.truncated);
    CHECK(catalog.entries[0].sample);
    CHECK(catalog.entries[0].bgm);
    CHECK_FALSE(catalog.entries[0].bga);
    CHECK(catalog.entries[0].audio_asset);
    CHECK(catalog.entries[0].selected_audio_asset);
    CHECK(catalog.entries[0].target_index == "#WAV01");
    CHECK(catalog.entries[0].source_name == "kick.wav");
    CHECK(catalog.entries[1].bga);
    CHECK_FALSE(catalog.entries[1].audio_asset);
    CHECK(catalog.entries[1].image_asset);
    CHECK(catalog.entries[1].selected_visual_asset);
    CHECK(catalog.entries[1].target_index == "#BMPA1");
    CHECK(catalog.entries[1].source_name == "bg.png");
}

TEST_CASE("Compose resource catalog falls back to stable IDs and diagnoses absent assets",
          "[unit][frontend][compose][resources]") {
    song_document_t document;
    document.resources.push_back(
        {.id = resource_id_t{42}, .asset = asset_id_t{99}, .roles = {resource_role_t::sample}});

    const auto catalog = sys::compose_resource_catalog_t::from(document, std::nullopt, 4);

    REQUIRE(catalog.entries.size() == 1);
    CHECK_FALSE(catalog.truncated);
    CHECK(catalog.entries[0].target_index == "#42");
    CHECK(catalog.entries[0].name == "Resource 42");
    CHECK(catalog.entries[0].source_name.empty());
    CHECK(catalog.entries[0].missing);
}

TEST_CASE("Compose note sound projection keeps BMS identity and full resource name",
          "[unit][frontend][compose][resources]") {
    const resource_entry_t sample{
        .id = resource_id_t{10},
        .name = "assets/percussion/kick.wav",
        .target_identities = {{format_family_t::bmson, "kick", "sound"},
                              {format_family_t::bms, "01", "WAV"}}};
    sys::compose_sound_labels_t labels;
    labels.emplace(sample.id.value, sys::compose_resource_sound_label(sample));

    const auto assigned = sys::compose_note_sound_text(sample.id, labels);
    CHECK(std::string{assigned.label} == "01");
    CHECK(std::string{assigned.detail} == "assets/percussion/kick.wav");

    const resource_entry_t fallback{.id = resource_id_t{11}, .name = "sounds/snare.wav"};
    const auto fallback_identity = sys::compose_resource_sound_label(fallback);
    CHECK(fallback_identity.first == "snare");
    CHECK(fallback_identity.second == "sounds/snare.wav");
}

TEST_CASE("Compose note sound projection handles unassigned and missing Samples",
          "[unit][frontend][compose][resources]") {
    const sys::compose_sound_labels_t labels;

    const auto unassigned = sys::compose_note_sound_text(std::nullopt, labels);
    CHECK(std::string{unassigned.label}.empty());
    CHECK(std::string{unassigned.detail}.empty());

    const auto missing = sys::compose_note_sound_text(resource_id_t{99}, labels);
    CHECK(std::string{missing.label} == "?");
    CHECK(std::string{missing.detail} == "Unknown sound resource");
}

TEST_CASE("Compose resource uses retain typed chart object identities",
          "[unit][frontend][compose][resources]") {
    chart_document_t chart;
    chart.measures = {
        {.id = measure_id_t{1},
         .notes = {{.id = object_id_t{10},
                    .position = {},
                    .lane = lane_id_t{1},
                    .sound = resource_id_t{7}},
                   {.id = object_id_t{11},
                    .position = {},
                    .lane = lane_id_t{2},
                    .sound = resource_id_t{8}}},
         .bgm_events = {{.id = object_id_t{20},
                         .position = {},
                         .source_lane = object_id_t{30},
                         .sound = resource_id_t{7}}}},
        {.id = measure_id_t{2},
         .notes = {{.id = object_id_t{12},
                    .position = {},
                    .lane = lane_id_t{1},
                    .sound = resource_id_t{7}}}},
    };
    chart.bga_events = {
        {.id = object_id_t{40}, .position = {}, .resource = resource_id_t{7}},
        {.id = object_id_t{41}, .position = {}, .resource = resource_id_t{8}},
    };

    const auto uses = sys::find_compose_resource_uses(chart, resource_id_t{7});

    REQUIRE(uses.size() == 4);
    CHECK(uses[0] ==
          editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{10}});
    CHECK(uses[1] ==
          editor_selection_object_t{editor_selection_object_kind_t::bgm_event, object_id_t{20}});
    CHECK(uses[2] ==
          editor_selection_object_t{editor_selection_object_kind_t::note, object_id_t{12}});
    CHECK(uses[3] ==
          editor_selection_object_t{editor_selection_object_kind_t::bga_event, object_id_t{40}});
    CHECK(sys::find_compose_resource_uses(chart, resource_id_t{99}).empty());
}

namespace { void check_resource_metadata(bool value) { CHECK(value); } }
TEST_CASE("Catalog projects exact cross-chart uses retained aliases and full authored paths",
          "[frontend][compose][resources][sound-workflow]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "nested/Drums/kick.wav"}};
    song.visual_assets = {{.id = asset_id_t{2}, .source_path = "nested/Images/background.png"}};
    song.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{2}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{3}, .asset = asset_id_t{2}, .roles = {resource_role_t::bga}}};
    song.charts.resize(2);
    song.charts[0].measures = {{.notes = {{.sound = resource_id_t{1}}}}};
    song.charts[1].measures = {{.bgm_events = {{.sound = resource_id_t{1}}}}};
    song.charts[1].sound_channels = {{.source_clip = {.source = asset_id_t{1}}, .events = {{}, {}}}};
    song.charts[1].bga_events = {{.resource = resource_id_t{3}}};
    const auto catalog = sys::compose_resource_catalog_t::from(song, {}, 3);
    check_resource_metadata(catalog.entries.size() == 3);
    check_resource_metadata(catalog.entries[0].use_count == 4);
    check_resource_metadata(catalog.entries[1].use_count == 2);
    check_resource_metadata(catalog.entries[2].use_count == 1);
    check_resource_metadata(catalog.entries[0].source_name == "kick.wav");
    check_resource_metadata(catalog.entries[0].source_path == "nested/Drums/kick.wav");
    check_resource_metadata(catalog.entries[2].source_path == "nested/Images/background.png");
}
