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

    const auto catalog = sys::make_compose_resource_catalog(document, asset_id_t{1}, 2);

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
    CHECK(catalog.entries[1].target_index == "#BMPA1");
    CHECK(catalog.entries[1].source_name == "bg.png");
}

TEST_CASE("Compose resource catalog falls back to stable IDs and diagnoses absent assets",
          "[unit][frontend][compose][resources]") {
    song_document_t document;
    document.resources.push_back(
        {.id = resource_id_t{42}, .asset = asset_id_t{99}, .roles = {resource_role_t::sample}});

    const auto catalog = sys::make_compose_resource_catalog(document, std::nullopt, 4);

    REQUIRE(catalog.entries.size() == 1);
    CHECK_FALSE(catalog.truncated);
    CHECK(catalog.entries[0].target_index == "#42");
    CHECK(catalog.entries[0].name == "Resource 42");
    CHECK(catalog.entries[0].source_name.empty());
    CHECK(catalog.entries[0].missing);
}
