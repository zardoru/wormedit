#include "sys/stem_waveform_window.h"

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

TEST_CASE("single-event BMSON stems remain visible for decoded audio duration",
          "[sys][stem][bmson]") {
    song_document_t song;
    auto& chart = song.charts.emplace_back();
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t id = 1; id <= 40; ++id)
        chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms = {
        {.id = object_id_t{900}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};

    std::vector<audio::decoded_source_binding_t> bindings;
    for (std::uint64_t id = 1; id <= 12; ++id) {
        song.audio_assets.push_back({.id = asset_id_t{id}});
        chart.sound_channels.push_back(
            {.id = object_id_t{100 + id},
             .source_clip = {.source = asset_id_t{id}},
             .events = {{.id = object_id_t{1000 + id}, .position = {measure_id_t{1}, 0}}}});
        bindings.push_back({.asset = asset_id_t{id},
                            .source_sample_rate = 44'100,
                            .source_frame_count = 6'880'000});
    }

    for (const auto measure : {1U, 20U, 40U}) {
        const auto visible =
            sys::visible_stem_channels(song, chart, measure_id_t{measure}, 2, bindings);
        REQUIRE(visible.size() == 12);
    }

    // A known short source ends and leaves the window; unknown geometry keeps a placeholder.
    bindings.front().source_frame_count = 44'100;
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{40}, 2, bindings).size() == 11);
    bindings.front().source_frame_count.reset();
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{40}, 2, bindings).size() == 12);
}

TEST_CASE("stem windows use BPM changes, stops, clip offsets, and actual source ends",
          "[sys][stem][bmson][timing]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {1, 1, 11, true}},
                         {.id = asset_id_t{2}, .stream = {44'100, 2, 44'100, true}}};
    auto& chart = song.charts.emplace_back();
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t id = 1; id <= 40; ++id)
        chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms = {
        {.id = object_id_t{900}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}},
        {.id = object_id_t{901}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {60, 1}}};
    chart.pauses = {{.id = object_id_t{902},
                     .position = {measure_id_t{2}, 0},
                     .kind = pause_kind_t::stop,
                     .duration = {10, 1}}};
    chart.sound_channels = {
        {.id = object_id_t{100},
         .source_clip = {.source = asset_id_t{1}},
         .events = {{.id = object_id_t{200}, .position = {measure_id_t{1}, 0}}}},
        {.id = object_id_t{101},
         .source_clip = {.source = asset_id_t{2}, .begin_frame = 40'000, .end_frame = 10'000'000},
         .events = {{.id = object_id_t{201}, .position = {measure_id_t{1}, 0}}}}};

    // The stop at measure 2 puts its event boundary at 12 seconds. The 11-second
    // source has ended, and the large authored clip end cannot exceed actual geometry.
    const auto after_stop = sys::visible_stem_channels(song, chart, measure_id_t{2}, 0);
    CHECK(after_stop.empty());
    chart.sound_channels = {
        {.id = object_id_t{103},
         .source_clip = {.source = asset_id_t{1}, .end_frame = 1},
         .events = {{.id = object_id_t{204}, .position = {measure_id_t{1}, 0}},
                    {.id = object_id_t{205}, .position = {measure_id_t{20}, 0}}}}};
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{10}, 0).empty());
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{20}, 0).size() == 1);
    // Unknown future audio keeps its placeholder only after the event can reach the window.
    chart.sound_channels = {
        {.id = object_id_t{102},
         .source_clip = {.source = asset_id_t{99}},
         .events = {{.id = object_id_t{202}, .position = {measure_id_t{40}, 0}}}}};
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{1}, 2).empty());
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{40}, 2).size() == 1);
}

TEST_CASE("a continuation consumes the remaining audio from its restart",
          "[sys][stem][bmson][continuation]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {44'100, 2, 160ULL * 44'100, true}}};
    auto& chart = song.charts.emplace_back();
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t id = 1; id <= 100; ++id)
        chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms = {
        {.id = object_id_t{900}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    chart.sound_channels = {
        {.id = object_id_t{100},
         .source_clip = {.source = asset_id_t{1}},
         .events = {
             {.id = object_id_t{200}, .position = {measure_id_t{1}, 0}},
             {.id = object_id_t{201}, .position = {measure_id_t{71}, 0}, .continuation = true}}},
        {.id = object_id_t{101},
         .source_clip = {.source = asset_id_t{1}},
         .events = {
             {.id = object_id_t{202}, .position = {measure_id_t{60}, 0}},
             {.id = object_id_t{203}, .position = {measure_id_t{71}, 0}, .continuation = true}}}};
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto near_end =
        sys::visible_stem_channels(song, chart, measure_id_t{75}, 0, {}, &*timing.index);
    CHECK(near_end.size() == 2);
    // The first stem ends near measure 81; the second restart remains active at measure 85.
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{85}, 0).size() == 1);
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{90}, 0).size() == 1);
}
