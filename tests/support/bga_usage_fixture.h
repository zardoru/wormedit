#pragma once
#include <wormedit/core/chart_editor.h>
#include <wormedit/core/lane_profiles.h>
namespace wormedit::test {
inline auto bga_usage_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1}, .revision = 4};
    song.visual_assets = {{.id = asset_id_t{10}, .source_path = "image.png"},
                          {.id = asset_id_t{11}, .kind = asset_kind_t::video, .source_path = "video.mp4"}};
    song.resources = {{.id = resource_id_t{20}, .name = "Image", .asset = asset_id_t{10}, .roles = {resource_role_t::bga}},
                      {.id = resource_id_t{21}, .name = "Alias", .asset = asset_id_t{10}, .roles = {resource_role_t::bga}},
                      {.id = resource_id_t{22}, .name = "Video", .asset = asset_id_t{11}, .roles = {resource_role_t::bga}},
                      {.id = resource_id_t{23}, .name = "Unused", .roles = {resource_role_t::bga}}};
    const auto layout = make_bms_5k_profile().layout;
    chart_document_t first{.id = chart_id_t{7}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{5}, .position = {measure_id_t{1}, 0}, .layout = layout}}};
    first.metadata.name = "First";
    first.bga_events = {
        {.id = object_id_t{30}, .position = {measure_id_t{1}, 480}, .resource = resource_id_t{20}, .kind = visual_event_kind_t::poor},
        {.id = object_id_t{31}, .position = {measure_id_t{1}, 240}, .resource = resource_id_t{20}, .kind = visual_event_kind_t::layer2},
        {.id = object_id_t{32}, .position = {measure_id_t{1}, 240}, .resource = resource_id_t{21}, .kind = visual_event_kind_t::layer},
        {.id = object_id_t{33}, .position = {measure_id_t{1}, 240}, .resource = resource_id_t{20}},
        {.id = object_id_t{34}, .position = {measure_id_t{1}, 0}, .resource = resource_id_t{22}, .kind = visual_event_kind_t::video}};
    chart_document_t second{.id = chart_id_t{8}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .lane_layouts = {{.id = object_id_t{6}, .position = {measure_id_t{2}, 0}, .layout = layout}}};
    second.metadata.name = "Second";
    second.bga_events = {{.id = object_id_t{35}, .position = {measure_id_t{2}, 720}, .resource = resource_id_t{20}}};
    song.charts = {first, second};
    return song;
}
}
