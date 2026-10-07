#include "sys/stem_waveform_window.h"

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <iostream>

using namespace wormedit;

namespace {
void link_stem_fixture(chart_document_t& chart) {
    chart.source_family = format_family_t::bmson;
    chart.bgm_source_lanes.clear();
    for (std::size_t index = 0; index < chart.sound_channels.size(); ++index) {
        chart.bgm_source_lanes.push_back({.id = object_id_t{10000 + index},
            .source_order = index, .stem_channel = chart.sound_channels[index].id});
    }
}
} // namespace

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

    link_stem_fixture(chart);
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
    link_stem_fixture(chart);
    const auto after_stop = sys::visible_stem_channels(song, chart, measure_id_t{2}, 0);
    CHECK(after_stop.empty());
    chart.sound_channels = {
        {.id = object_id_t{103},
         .source_clip = {.source = asset_id_t{1}, .end_frame = 1},
         .events = {{.id = object_id_t{204}, .position = {measure_id_t{1}, 0}},
                    {.id = object_id_t{205}, .position = {measure_id_t{20}, 0}}}}};
    link_stem_fixture(chart);
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{10}, 0).empty());
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{20}, 0).size() == 1);
    // Unknown future audio keeps its placeholder only after the event can reach the window.
    chart.sound_channels = {
        {.id = object_id_t{102},
         .source_clip = {.source = asset_id_t{99}},
         .events = {{.id = object_id_t{202}, .position = {measure_id_t{40}, 0}}}}};
    link_stem_fixture(chart);
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
    link_stem_fixture(chart);
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto near_end =
        sys::visible_stem_channels(song, chart, measure_id_t{75}, 0, {}, &*timing.index);
    CHECK(near_end.size() == 2);
    // The first stem ends near measure 81; the second restart remains active at measure 85.
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{85}, 0).size() == 1);
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{90}, 0).size() == 1);
}

namespace {
auto make_indexed_stem_fixture() -> std::pair<song_document_t, std::vector<audio::decoded_source_binding_t>> {
    song_document_t song;
    song.id = song_id_t{7};
    song.revision = 9;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {44'100, 2, 300'000, true}}};
    auto& chart = song.charts.emplace_back();
    chart.id = chart_id_t{44};
    chart.divisions_per_standard_measure = 960;
    chart.bpms = {{.id = object_id_t{900}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    for (std::uint64_t m = 1; m <= 32; ++m) chart.measures.push_back({.id = measure_id_t{m}});
    for (std::uint64_t i = 0; i < 181; ++i) {
        auto events = std::vector<sound_channel_event_t>{{.id = object_id_t{2000 + i},
            .position = {measure_id_t{1 + (i % 32)}, 0}}};
        if (i == 180) events.push_back({.id = object_id_t{3000},
            .position = {measure_id_t{1}, 0}}); // Out of order stays conservatively visible.
        chart.sound_channels.push_back({.id = object_id_t{1000 + i},
            .source_clip = {.source = asset_id_t{1}},
            .events = std::move(events)});
    }
    link_stem_fixture(chart);
    chart.bgm_source_lanes[0].id = object_id_t{9101};
    chart.bgm_source_lanes[1].id = object_id_t{9100};
    chart.warps = {{.id = object_id_t{9300}, .span = {
        .begin = {measure_id_t{10}, 0}, .end = {measure_id_t{11}, 0}}}};
    chart.measures.front().bgm_events.push_back({.id = object_id_t{9200},
        .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{9101},
        .sound_channel_event = object_id_t{2000}});
    std::vector<audio::decoded_source_binding_t> bindings{{.asset = asset_id_t{1},
        .source_sample_rate = 44'100, .source_frame_count = 300'000}};
    return {std::move(song), std::move(bindings)};
}

void expect_index_matches_reference(sys::stem_index_t& index, const song_document_t& song,
    const chart_document_t& chart, measure_id_t measure, std::size_t window,
    const std::vector<audio::decoded_source_binding_t>& bindings) {
    const auto expected = sys::visible_stem_channels(song, chart, measure, window, bindings);
    const auto actual = sys::indexed_visible_stem_channels(index, song, chart, measure, window, bindings);
    REQUIRE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size(); ++i) CHECK(actual[i]->id == expected[i]->id);
}

void check_generated_windows(sys::stem_index_t& index, const song_document_t& song,
    const chart_document_t& chart, const std::vector<audio::decoded_source_binding_t>& bindings) {
    for (std::uint64_t measure = 1; measure <= 32; ++measure)
        expect_index_matches_reference(index, song, chart, measure_id_t{measure}, 3, bindings);
    expect_index_matches_reference(index, song, chart, measure_id_t{10}, 0, bindings);
}

void check_selection_reuse(sys::stem_index_t& index, const song_document_t& song,
    const chart_document_t& chart, const std::vector<audio::decoded_source_binding_t>& bindings) {
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{10}, 0, bindings));
    const auto built_at_warp = index.selection_build_count;
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{10}, 0, bindings));
    CHECK(index.selection_build_count == built_at_warp);
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{11}, 0, bindings));
    CHECK(index.selection_build_count == built_at_warp + 1);
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{32}, 3, bindings));
    const auto cached_selection = index.selected;
    const auto build_count = index.selection_build_count;
    const auto same_window = sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{32}, 3, bindings);
    CHECK(index.selected == cached_selection);
    CHECK(index.selection_build_count == build_count);
    CHECK(same_window.size() == cached_selection.size());

}

void check_binding_refresh(sys::stem_index_t& index, const song_document_t& song,
    const chart_document_t& chart, std::vector<audio::decoded_source_binding_t>& bindings) {
    bindings.front().source_frame_count = 44'100;
    expect_index_matches_reference(index, song, chart, measure_id_t{32}, 3, bindings);
    CHECK(index.bindings == bindings);
    CHECK(index.channels[0].source_lane == 9101);
    CHECK(index.channels[1].source_lane == 9100);
}

void check_revision_and_document_invalidation(sys::stem_index_t& index, song_document_t& song,
    const chart_document_t& chart, const std::vector<audio::decoded_source_binding_t>& bindings) {
    const auto short_selection_size = index.selected.size();
    ++song.revision;
    const auto revision_result = sys::indexed_visible_stem_channels(index, song, chart,
        measure_id_t{32}, 3, bindings);
    CHECK(index.revision == song.revision);
    CHECK(revision_result.size() == short_selection_size);

    song_document_t replacement;
    replacement.id = song_id_t{8};
    replacement.revision = song.revision;
    auto& replacement_chart = replacement.charts.emplace_back();
    replacement_chart.id = chart.id;
    replacement_chart.divisions_per_standard_measure = 960;
    replacement_chart.measures = {{.id = measure_id_t{1}}};
    replacement_chart.bpms = {{.id = object_id_t{901}, .position = {measure_id_t{1}, 0},
                               .beats_per_minute = {120, 1}}};
    replacement_chart.sound_channels = {{.id = object_id_t{3000}, .events = {
        {.id = object_id_t{3001}, .position = {measure_id_t{1}, 0}}}}};
    link_stem_fixture(replacement_chart);
    const auto result = sys::indexed_visible_stem_channels(index, replacement,
        replacement_chart, measure_id_t{1}, 0, {});
    REQUIRE(result.size() == 1);
    CHECK(result.front()->id == object_id_t{3000});
    CHECK(index.document == replacement.id);
}

void expand_stem_benchmark_chart(chart_document_t& chart) {
    for (std::size_t channel = 0; channel < chart.sound_channels.size(); ++channel) {
        auto& events = chart.sound_channels[channel].events;
        for (std::uint64_t measure = 1; measure <= 32; ++measure)
            events.push_back({.id = object_id_t{50'000 + channel * 32 + measure},
                .position = {measure_id_t{measure}, 0}});
        std::ranges::sort(events, [](const auto& left, const auto& right) {
            return std::tie(left.position.measure.value, left.position.row) <
                   std::tie(right.position.measure.value, right.position.row);
        });
    }
}

void verify_stem_benchmark_windows(sys::stem_index_t& index,
    const song_document_t& document, const chart_document_t& chart,
    const std::vector<audio::decoded_source_binding_t>& bindings) {
    for (std::uint64_t measure = 1; measure <= 32; ++measure) {
        const auto expected = sys::visible_stem_channels(document, chart,
            measure_id_t{measure}, 3, bindings);
        const auto actual = sys::indexed_visible_stem_channels(index, document, chart,
            measure_id_t{measure}, 3, bindings);
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i)
            CHECK(actual[i]->id == expected[i]->id);
    }
}

auto time_stem_benchmark_queries(sys::stem_index_t& index, const song_document_t& document,
    const chart_document_t& chart, const std::vector<audio::decoded_source_binding_t>& bindings,
    bool indexed) -> double {
    auto maximum_ms = 0.0;
    for (int run = 0; run < 5; ++run) {
        const auto start = std::chrono::steady_clock::now();
        for (std::uint64_t measure = 1; measure <= 32; ++measure) {
            if (indexed) static_cast<void>(sys::indexed_visible_stem_channels(index, document,
                chart, measure_id_t{measure}, 3, bindings));
            else static_cast<void>(sys::visible_stem_channels(document, chart,
                measure_id_t{measure}, 3, bindings));
        }
        maximum_ms = std::max(maximum_ms, std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count());
    }
    return maximum_ms;
}
} // namespace

TEST_CASE("indexed stem visibility matches the compatibility scan and reuses its frame result",
          "[sys][stem][index]") {
    auto [song, bindings] = make_indexed_stem_fixture();
    const auto& chart = song.charts.front();
    sys::stem_index_t index;
    check_generated_windows(index, song, chart, bindings);
    check_selection_reuse(index, song, chart, bindings);
    check_binding_refresh(index, song, chart, bindings);
    check_revision_and_document_invalidation(index, song, chart, bindings);
}

TEST_CASE("indexed stem lane uses its explicit link despite marker encounter order",
          "[sys][stem][index]") {
    auto [song, bindings] = make_indexed_stem_fixture();
    auto& chart = song.charts.front();
    chart.sound_channels[0].events = {
        {.id = object_id_t{2000}, .position = {measure_id_t{2}, 0}},
        {.id = object_id_t{3001}, .position = {measure_id_t{1}, 0}}};
    chart.measures[0].bgm_events = {{.id = object_id_t{9201},
        .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{9102},
        .sound_channel_event = object_id_t{3001}}};
    chart.measures[1].bgm_events = {{.id = object_id_t{9202},
        .position = {measure_id_t{2}, 0}, .source_lane = object_id_t{9103},
        .sound_channel_event = object_id_t{2000}}};
    chart.bgm_source_lanes[0].id = object_id_t{9102};

    sys::stem_index_t index;
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart,
        measure_id_t{1}, 0, bindings));
    CHECK(index.channels[0].source_lane == 9102);
}

TEST_CASE("expired continuation events do not make a stem visible again",
          "[sys][stem][continuation]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {44'100, 2, 44'100, true}}};
    auto& chart = song.charts.emplace_back();
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t i = 1; i <= 24; ++i) chart.measures.push_back({.id = measure_id_t{i}});
    chart.bpms = {{.id = object_id_t{900}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.sound_channels = {{.id = object_id_t{1}, .source_clip = {.source = asset_id_t{1}},
        .events = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0}},
                   {.id = object_id_t{3}, .position = {measure_id_t{20}, 0}, .continuation = true}}}};
    link_stem_fixture(chart);
    const auto expected = sys::visible_stem_channels(song, chart, measure_id_t{20}, 0);
    sys::stem_index_t index;
    const auto actual = sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{20}, 0, {});
    CHECK(actual.size() == expected.size());
    CHECK(actual.empty());
}

TEST_CASE("warm indexed stem selection beats the compatibility scan for 181 stems",
          "[.performance][sys][stem]") {
    auto [song, bindings] = make_indexed_stem_fixture();
    auto& chart = song.charts.front();
    expand_stem_benchmark_chart(chart);
    const auto& document = song;
    const auto& selected_chart = song.charts.front();
    sys::stem_index_t index;
    static_cast<void>(sys::indexed_visible_stem_channels(index, document, selected_chart,
        measure_id_t{1}, 0, bindings));
    verify_stem_benchmark_windows(index, document, selected_chart, bindings);
    const auto indexed_ms = time_stem_benchmark_queries(index, document, selected_chart, bindings, true);
    const auto reference_ms = time_stem_benchmark_queries(index, document, selected_chart, bindings, false);
    std::cout << "warm 181-stem selection: indexed max-of-5=" << indexed_ms
              << " ms, reference max-of-5=" << reference_ms << " ms\n";
    CHECK(indexed_ms < 2'000.0);
    CHECK(indexed_ms < reference_ms);
}

TEST_CASE("indexed and compatibility windows retain all 300 visible channels",
          "[sys][stem][bmson][stem-300]") {
    song_document_t song;
    auto& chart = song.charts.emplace_back();
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    chart.measures.push_back({.id = measure_id_t{1}});
    chart.bpms.push_back({.id = object_id_t{900}, .position = {measure_id_t{1}, 0},
                         .beats_per_minute = {120, 1}});
    for (std::uint64_t id = 0; id < 300; ++id)
        chart.sound_channels.push_back({.id = object_id_t{1000 + id},
            .events = {{.id = object_id_t{2000 + id}, .position = {measure_id_t{1}, 0}}}});
    link_stem_fixture(chart);
    const std::vector<audio::decoded_source_binding_t> bindings;
    CHECK(sys::all_stem_channels(chart).size() == 300);
    CHECK(sys::visible_stem_channels(song, chart, measure_id_t{1}, 2, bindings).size() == 300);
    sys::stem_index_t index;
    CHECK(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{1}, 2, bindings).size() == 300);
}

namespace {
auto mixed_stem_window_fixture() -> song_document_t {
    song_document_t song;
    auto& chart = song.charts.emplace_back();
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    chart.measures.push_back({.id = measure_id_t{1}});
    chart.bpms.push_back({.id = object_id_t{5000}, .position = {measure_id_t{1}, 0},
                         .beats_per_minute = {120, 1}});
    for (std::uint64_t index = 0; index < 1003; ++index) {
        chart.sound_channels.push_back({.id = object_id_t{1000 + index},
            .events = {{.id = object_id_t{3000 + index}, .position = {measure_id_t{1}, 0}}}});
        if (index == 1 || index == 500 || index == 1001)
            chart.bgm_source_lanes.push_back({.id = object_id_t{6000 + index},
                .source_order = index, .stem_channel = object_id_t{1000 + index}});
    }
    chart.bgm_source_lanes.push_back({.id = object_id_t{9000}, .name = "BGM 1"});
    return song;
}

void check_mixed_stem_ids(const std::vector<const sound_channel_t*>& channels) {
    REQUIRE(channels.size() == 3);
    CHECK(channels[0]->id == object_id_t{1001});
    CHECK(channels[1]->id == object_id_t{1500});
    CHECK(channels[2]->id == object_id_t{2001});
}

void check_filtered_index_positions(const sys::stem_index_t& index) {
    REQUIRE(index.channels.size() == 3);
    CHECK(index.channels[0].chart_position == 1);
    CHECK(index.channels[1].chart_position == 500);
    CHECK(index.channels[2].chart_position == 1001);
}
} // namespace

TEST_CASE("hybrid stem index holds only three linked channels among one thousand packed channels",
          "[sys][stem][bmson][hybrid-lanes]") {
    auto song = mixed_stem_window_fixture();
    auto& chart = song.charts.front();
    sys::stem_index_t index;
    check_mixed_stem_ids(sys::all_stem_channels(chart));
    check_mixed_stem_ids(sys::visible_stem_channels(song, chart, measure_id_t{1}, 2));
    check_mixed_stem_ids(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{1}, 2, {}));
    check_filtered_index_positions(index);
}

TEST_CASE("hybrid stem compatibility fallbacks exclude every packed channel",
          "[sys][stem][bmson][hybrid-lanes]") {
    auto song = mixed_stem_window_fixture();
    auto& chart = song.charts.front();
    chart.measures.clear();
    sys::stem_index_t index;
    check_mixed_stem_ids(sys::visible_stem_channels(song, chart, measure_id_t{1}, 2));
    check_mixed_stem_ids(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{1}, 2, {}));
}
