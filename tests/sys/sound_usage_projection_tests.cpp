#include "sys/resources/sound_usage_view.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

void require_sound_usage(bool value) {
    REQUIRE(value);
}
void check_sound_usage(bool value) {
    CHECK(value);
}

auto usage_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 7;
    song.audio_assets = {{.id = asset_id_t{10}, .source_path = "shared.wav"},
                         {.id = asset_id_t{11}, .source_path = "other.wav"}};
    song.resources = {{.id = resource_id_t{1}, .name = "Selected", .asset = asset_id_t{10}},
                      {.id = resource_id_t{2}, .name = "Alias", .asset = asset_id_t{10}},
                      {.id = resource_id_t{3}, .name = "Other", .asset = asset_id_t{11}}};
    chart_document_t first;
    first.id = chart_id_t{20};
    first.metadata.name = "First";
    measure_chunk_t late{.id = measure_id_t{99}};
    late.notes = {
        {.id = object_id_t{4}, .position = {measure_id_t{99}, 2}, .sound = resource_id_t{1}},
        {.id = object_id_t{5}, .position = {measure_id_t{99}, 2}, .sound = resource_id_t{2}}};
    late.bgm_events = {
        {.id = object_id_t{6}, .position = {measure_id_t{99}, 2}, .sound = resource_id_t{1}}};
    measure_chunk_t early{.id = measure_id_t{4}};
    early.notes = {
        {.id = object_id_t{7}, .position = {measure_id_t{4}, 8}, .sound = resource_id_t{1}}};
    first.measures = {late, early};
    sound_channel_t channel;
    channel.id = object_id_t{30};
    channel.name = "Channel";
    channel.source_clip = {.id = clip_id_t{31}, .source = asset_id_t{10}};
    channel.events = {
        {.id = object_id_t{32}, .position = {measure_id_t{99}, 4}, .lane = lane_id_t{1}}};
    first.sound_channels = {channel};
    chart_document_t second;
    second.id = chart_id_t{10};
    second.metadata.name = "Second";
    measure_chunk_t measure{.id = measure_id_t{8}};
    measure.notes = {
        {.id = object_id_t{9}, .position = {measure_id_t{8}, 0}, .sound = resource_id_t{1}}};
    second.measures = {measure};
    song.charts = {first, second};
    return song;
}

} // namespace

TEST_CASE("sound usage distinguishes direct refs, shared-resource refs and asset-backed channels",
          "[unit][sys][sounds][usage]") {
    const auto song = usage_song();
    const auto result = sound_usage_view_t::from(song, resource_id_t{1});
    require_sound_usage(result.valid);
    check_sound_usage(result.song == song_id_t{1});
    check_sound_usage(result.revision == 7);
    check_sound_usage(result.asset == asset_id_t{10});
    check_sound_usage(result.rows.size() == 7);

    check_sound_usage(result.rows[0].chart == chart_id_t{20});
    check_sound_usage(result.rows[0].kind == sound_usage_kind_t::sound_channel_source);
    check_sound_usage(!(result.rows[0].position.has_value()));
    check_sound_usage(result.rows[0].channel == object_id_t{30});
    check_sound_usage(result.rows[0].clip == clip_id_t{31});
    check_sound_usage(result.rows[1].kind == sound_usage_kind_t::note);
    check_sound_usage(result.rows[1].position == local_position_t{measure_id_t{99}, 2});
    check_sound_usage(result.rows[1].scope == sound_usage_scope_t::resource_reference);
    check_sound_usage(result.rows[2].kind == sound_usage_kind_t::note);
    check_sound_usage(result.rows[2].scope == sound_usage_scope_t::shared_asset_reference);
    check_sound_usage(result.rows[2].referenced_resource == resource_id_t{2});
    check_sound_usage(result.rows[3].kind == sound_usage_kind_t::bgm);
    check_sound_usage(result.rows[3].scope == sound_usage_scope_t::resource_reference);
    check_sound_usage(result.rows[4].kind == sound_usage_kind_t::sound_channel_event);
    check_sound_usage(result.rows[4].position == local_position_t{measure_id_t{99}, 4});
    check_sound_usage(result.rows[5].position == local_position_t{measure_id_t{4}, 8});
    check_sound_usage(result.rows[6].chart == chart_id_t{10});
    check_sound_usage(result.rows[6].kind == sound_usage_kind_t::note);
}

TEST_CASE("unused, missing and absent sound resources produce deterministic empty projections",
          "[unit][sys][sounds][usage]") {
    auto song = usage_song();
    song.resources.push_back({.id = resource_id_t{4}, .name = "Unused", .asset = asset_id_t{11}});
    const auto unused = sound_usage_view_t::from(song, resource_id_t{4});
    require_sound_usage(unused.valid);
    check_sound_usage(unused.rows.empty());
    check_sound_usage(unused.total_matching == 0);
    const auto absent = sound_usage_view_t::from(song, resource_id_t{999});
    check_sound_usage(!(absent.valid));
    check_sound_usage(absent.rows.empty());

    song.resources[0].missing = true;
    const auto missing = sound_usage_view_t::from(song, resource_id_t{1});
    check_sound_usage(missing.valid);
    check_sound_usage(missing.rows.size() == 7);
}

TEST_CASE("sound usage cap reports truncation and keeps storage bounded",
          "[unit][sys][sounds][usage]") {
    song_document_t song;
    song.id = song_id_t{1};
    song.resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1}}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    measure_chunk_t measure{.id = measure_id_t{1}};
    measure.notes.reserve(sound_usage_projection_limit + 1);
    for (std::size_t index = 0; index <= sound_usage_projection_limit; ++index) {
        const auto value = static_cast<std::uint64_t>(index + 1);
        measure.notes.push_back({.id = object_id_t{value},
                                 .position = {measure_id_t{1}, value},
                                 .sound = resource_id_t{1}});
    }
    sound_channel_t channel;
    channel.id = object_id_t{9000};
    channel.source_clip = {.id = clip_id_t{9001}, .source = asset_id_t{1}};
    chart.sound_channels = {channel};
    chart.measures = {measure};
    song.charts = {chart};
    const auto result = sound_usage_view_t::from(song, resource_id_t{1});
    check_sound_usage(result.total_matching == sound_usage_projection_limit + 2);
    check_sound_usage(result.truncated);
    check_sound_usage(result.rows.size() == sound_usage_projection_limit);
    check_sound_usage(result.rows.front().kind == sound_usage_kind_t::sound_channel_source);
}
