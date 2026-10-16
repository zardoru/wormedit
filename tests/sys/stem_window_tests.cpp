#include "sys/stem_waveform_window.h"

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
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
        sys::visible_stem_channels(song, chart, measure_id_t{75}, 0, {}, wormedit::sys::stem_geometry_indices_t{.timing = &*timing.index});
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
    const auto built_at_warp = index.value().selection.build_count();
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{10}, 0, bindings));
    CHECK(index.value().selection.build_count() == built_at_warp);
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{11}, 0, bindings));
    CHECK(index.value().selection.build_count() == built_at_warp + 1);
    static_cast<void>(sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{32}, 3, bindings));
    const auto cached_selection = index.value().selection.value();
    const auto build_count = index.value().selection.build_count();
    const auto same_window = sys::indexed_visible_stem_channels(index, song, chart, measure_id_t{32}, 3, bindings);
    CHECK(index.value().selection.value() == cached_selection);
    CHECK(index.value().selection.build_count() == build_count);
    CHECK(same_window.size() == cached_selection.size());

}

void check_binding_refresh(sys::stem_index_t& index, const song_document_t& song,
    const chart_document_t& chart, std::vector<audio::decoded_source_binding_t>& bindings) {
    bindings.front().source_frame_count = 44'100;
    expect_index_matches_reference(index, song, chart, measure_id_t{32}, 3, bindings);
    CHECK(index.key()->bindings == bindings);
    CHECK(index.value().channels[0].source_lane == 9101);
    CHECK(index.value().channels[1].source_lane == 9100);
}

void check_revision_and_document_invalidation(sys::stem_index_t& index, song_document_t& song,
    const chart_document_t& chart, const std::vector<audio::decoded_source_binding_t>& bindings) {
    const auto short_selection_size = index.value().selection.value().size();
    ++song.revision;
    const auto revision_result = sys::indexed_visible_stem_channels(index, song, chart,
        measure_id_t{32}, 3, bindings);
    CHECK(index.key()->stamp.document.revision == song.revision);
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
    CHECK(index.key()->stamp.document.project == replacement.id);
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
    CHECK(index.value().channels[0].source_lane == 9102);
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
    REQUIRE(index.value().channels.size() == 3);
    CHECK(index.value().channels[0].chart_position == 1);
    CHECK(index.value().channels[1].chart_position == 500);
    CHECK(index.value().channels[2].chart_position == 1001);
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

namespace {
void t459_add_packed_event(chart_document_t& chart, std::uint64_t measure,
                           std::uint64_t row, std::uint64_t id, std::uint64_t lane,
                           std::optional<std::uint64_t> resource,
                           std::optional<object_id_t> channel_event = std::nullopt) {
    chart.measures[measure - 1].bgm_events.push_back({
        .id = object_id_t{id}, .position = {measure_id_t{measure}, row},
        .source_lane = object_id_t{lane},
        .sound = resource ? std::optional<resource_id_t>{resource_id_t{*resource}} : std::nullopt,
        .sound_channel_event = channel_event});
}

auto t459_packed_fixture() -> song_document_t {
    song_document_t song;
    song.audio_assets = {
        {.id = asset_id_t{1}, .stream = {100, 2, 1000, true}},
        {.id = asset_id_t{2}, .stream = {100, 2, 200, true}},
        {.id = asset_id_t{3}, .stream = {100, 2, 250, true}},
        {.id = asset_id_t{4}, .stream = {100, 2, 350, true}}};
    for (std::uint64_t id = 1; id <= 4; ++id)
        song.resources.push_back({.id = resource_id_t{id}, .name = "sample",
                                  .asset = asset_id_t{id}});
    auto& chart = song.charts.emplace_back();
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t id = 1; id <= 8; ++id) {
        chart.measures.push_back({.id = measure_id_t{id}});
    }
    chart.measures[1].definition = {{3, 4}, meter_t{3, 4}};
    chart.bpms = {
        {.id = object_id_t{900}, .position = {measure_id_t{1}, 0},
         .beats_per_minute = {120, 1}},
        {.id = object_id_t{901}, .position = {measure_id_t{2}, 0},
         .beats_per_minute = {60, 1}},
        {.id = object_id_t{902}, .position = {measure_id_t{4}, 0},
         .beats_per_minute = {180, 1}}};
    chart.bgm_source_lanes = {{.id = object_id_t{501}, .name = "BGM 1"},
                              {.id = object_id_t{502}, .name = "BGM 2"}};
    t459_add_packed_event(chart, 1, 0, 200, 501, 2); // exactly one authored measure
    t459_add_packed_event(chart, 2, 0, 201, 501, 3); // shorter than 3/4 at 60 BPM
    t459_add_packed_event(chart, 2, 240, 202, 501, 4); // longer than 3/4 at 60 BPM
    t459_add_packed_event(chart, 3, 0, 203, 502, 1);
    t459_add_packed_event(chart, 3, 120, 204, 501, 1); // another column cannot clip 203
    t459_add_packed_event(chart, 3, 240, 205, 502, std::nullopt); // soundless next event
    t459_add_packed_event(chart, 3, 480, 206, 502, 2); // short same-column next event
    chart.sound_channels.push_back({.id = object_id_t{1000}, .source_clip = {
        .source = asset_id_t{1}, .begin_frame = 100, .end_frame = 900},
        .events = {{.id = object_id_t{1100}, .position = {measure_id_t{4}, 0}}}});
    t459_add_packed_event(chart, 4, 0, 207, 502, 1, object_id_t{1100});
    return song;
}

auto t459_piece(const sys::packed_waveform_index_t& index, std::uint64_t id)
    -> const sys::packed_waveform_piece_t* {
    const auto found = std::ranges::find_if(index.value().pieces, [id](const auto& piece) {
        return piece.event == object_id_t{id};
    });
    return found == index.value().pieces.end() ? nullptr : &*found;
}

auto t459_threshold_clipping_and_native_clip() -> bool {
    auto song = t459_packed_fixture();
    auto& chart = song.charts.front();
    const auto timing = build_timing_index(chart);
    if (!timing.index) return false;
    sys::packed_waveform_index_t index;
    sys::prepare_packed_waveform_index(index, song, chart, {}, *timing.index);
    if (!index.has_value() || t459_piece(index, 200) || t459_piece(index, 201)) return false;
    const auto* fractional_long = t459_piece(index, 202);
    const auto* clipped = t459_piece(index, 203);
    const auto* native_clip = t459_piece(index, 207);
    if (!fractional_long || !clipped || !native_clip) return false;
    const auto expected_next = std::uint64_t{960 + 720 + 240};
    if (clipped->next_row != expected_next || std::abs(clipped->end_seconds - 6.0) > 1e-9)
        return false;
    if (fractional_long->measure_seconds != 3.0 || native_clip->clip.begin_frame != 100 ||
        native_clip->clip.end_frame != 900 || native_clip->sample_rate != 100 ||
        native_clip->source_frames != 1000) return false;
    const auto visible = sys::visible_packed_waveform_pieces(index, 5.5, 5.8);
    if (std::ranges::none_of(visible, [](const auto* piece) {
            return piece->event == object_id_t{203};
        })) return false;

    auto short_song = song;
    for (auto& asset : short_song.audio_assets) asset.stream.frame_count = 200;
    ++short_song.revision;
    auto& short_chart = short_song.charts.front();
    const auto short_timing = build_timing_index(short_chart);
    if (!short_timing.index) return false;
    sys::packed_waveform_index_t short_index;
    sys::prepare_packed_waveform_index(short_index, short_song, short_chart, {}, *short_timing.index);
    sys::prepare_packed_waveform_index(short_index, short_song, short_chart, {}, *short_timing.index);
    return short_index.has_value() && short_index.value().pieces.empty() && short_index.build_count() == 1 &&
        short_index.value().cache_hits == 1 &&
        sys::visible_packed_waveform_pieces(short_index, 0, 100).empty() &&
        short_index.value().queried_pieces == 0;
}

auto t459_unknown_dense_fixture() -> song_document_t {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {0, 2, std::nullopt, true}}};
    song.resources = {{.id = resource_id_t{1}, .name = "pending", .asset = asset_id_t{1}}};
    auto& chart = song.charts.emplace_back();
    chart.source_family = format_family_t::bmson;
    chart.divisions_per_standard_measure = 960;
    for (std::uint64_t measure = 1; measure <= 20; ++measure) {
        auto& chunk = chart.measures.emplace_back(measure_chunk_t{.id = measure_id_t{measure}});
        for (std::uint64_t event = 0; event < 200; ++event) {
            chunk.bgm_events.push_back({.id = object_id_t{measure * 1000 + event},
                .position = {measure_id_t{measure}, event * 4}, .source_lane = object_id_t{501},
                .sound = resource_id_t{1}});
        }
    }
    chart.bpms = {{.id = object_id_t{900}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.bgm_source_lanes = {{.id = object_id_t{501}, .name = "BGM 1"}};
    return song;
}

auto t459_dense_unknown_query_is_bounded() -> bool {
    auto song = t459_unknown_dense_fixture();
    auto& chart = song.charts.front();
    const auto timing = build_timing_index(chart);
    if (!timing.index) return false;
    sys::packed_waveform_index_t index;
    sys::prepare_packed_waveform_index(index, song, chart, {}, *timing.index);
    sys::prepare_packed_waveform_index(index, song, chart, {}, *timing.index);
    if (!index.has_value() || index.build_count() != 1 || index.value().cache_hits != 1 || index.value().pieces.size() != 4000)
        return false;
    const auto pending = t459_piece(index, 16'000);
    const auto selected = sys::visible_packed_waveform_pieces(index, 30.001, 30.005);
    if (!pending || pending->sample_rate != 0 || pending->source_frames ||
        !std::ranges::any_of(selected, [pending](const auto* piece) { return piece == pending; }) ||
        index.value().queried_pieces >= index.value().pieces.size() / 10) return false;

    auto short_song = t459_unknown_dense_fixture();
    short_song.audio_assets.front().stream = {100, 2, 50, true};
    auto& short_chart = short_song.charts.front();
    const auto short_timing = build_timing_index(short_chart);
    if (!short_timing.index) return false;
    sys::packed_waveform_index_t short_index;
    sys::prepare_packed_waveform_index(short_index, short_song, short_chart, {}, *short_timing.index);
    sys::prepare_packed_waveform_index(short_index, short_song, short_chart, {}, *short_timing.index);
    const auto empty = sys::visible_packed_waveform_pieces(short_index, 30.001, 30.005);
    return short_index.has_value() && short_index.build_count() == 1 && short_index.value().cache_hits == 1 &&
        short_index.value().pieces.empty() && empty.empty() && short_index.value().queried_pieces == 0;
}
} // namespace

TEST_CASE("packed waveform projection honors current tempo length, lane clipping and native clips",
          "[sys][stem][bmson][T459]") {
    CHECK(t459_threshold_clipping_and_native_clip());
}

TEST_CASE("packed waveform interval index keeps unknown dense event queries bounded",
          "[sys][stem][bmson][T459]") {
    CHECK(t459_dense_unknown_query_is_bounded());
}

namespace {
auto t459_stacked_piece_endpoints(bool reverse) -> bool {
    auto song = t459_packed_fixture();
    auto& chart = song.charts.front();
    for (auto& measure : chart.measures) measure.bgm_events.clear();
    t459_add_packed_event(chart, 1, 0, 2000, 501, 1);
    t459_add_packed_event(chart, 1, 0, 2001, 501, 4);
    t459_add_packed_event(chart, 1, 480, 2002, 501, std::nullopt);
    t459_add_packed_event(chart, 4, 0, 2003, 501, 1);
    t459_add_packed_event(chart, 4, 0, 2004, 501, 4);
    if (reverse) for (auto& measure : chart.measures) std::ranges::reverse(measure.bgm_events);
    const auto timing = build_timing_index(chart);
    if (!timing.index) return false;
    sys::packed_waveform_index_t index;
    sys::prepare_packed_waveform_index(index, song, chart, {}, *timing.index);
    if (index.value().pieces.size() != 4) return false;
    for (const auto id : {2000U, 2001U}) {
        const auto* piece = t459_piece(index, id);
        if (!piece || piece->next_row != 480 || piece->end_seconds != 1.0) return false;
    }
    const auto* first = t459_piece(index, 2003);
    const auto* second = t459_piece(index, 2004);
    if (!first || !second || first->next_row || second->next_row) return false;
    return std::abs(first->end_seconds - first->begin_seconds - 10.0) < 1e-9 &&
        std::abs(second->end_seconds - second->begin_seconds - 3.5) < 1e-9;
}
}
TEST_CASE("packed waveform stacks share the next strictly later column trigger",
          "[sys][stem][bmson][T459]") {
    CHECK(t459_stacked_piece_endpoints(false));
    CHECK(t459_stacked_piece_endpoints(true));
}
