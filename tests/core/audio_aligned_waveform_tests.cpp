#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/channel_waveform.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

namespace {

auto chart_fixture() -> wormedit::chart_document_t {
    std::ifstream stream(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" /
                             "published-vox-slicing.bmson",
                         std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    const auto parsed =
        wormedit::parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song);
    return parsed.song->charts.front();
}

auto pulse_waveform() -> wormedit::audio::waveform_t {
    using namespace wormedit::audio;
    waveform_t waveform;
    waveform.key = {{"fixture:aligned", "pcm:v1"}, 441};
    waveform.sample_rate = output_sample_rate;
    waveform.total_frames = 2U * output_sample_rate;
    waveform.bins.resize(200);
    for (auto& bin : waveform.bins) {
        bin.frame_count = 441;
    }
    auto& pulse = waveform.bins[100];
    pulse.minimum = {-1.0f, -0.5f};
    pulse.maximum = {1.0f, 0.5f};
    pulse.absolute_peak = {1.0f, 0.5f};
    pulse.rms = {0.5f, 0.25f};
    return waveform;
}

auto loudest_row(const wormedit::audio::aligned_waveform_result_t& result) -> double {
    const auto loudest = std::ranges::max_element(
        result.rows, {}, [](const auto& row) { return row.absolute_peak[0]; });
    return loudest->y_pixels;
}

} // namespace

TEST_CASE("aligned waveform follows chart timing, offset, and viewport scroll",
          "[unit][audio][waveform][aligned]") {
    auto chart = chart_fixture();
    const auto waveform = pulse_waveform();
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    const wormedit::audio::aligned_waveform_request_t request{.viewport_scroll_pixels = 0.0,
                                                              .viewport_height_pixels = 960.0,
                                                              .pixels_per_standard_measure = 960.0};
    const auto base = wormedit::audio::project_aligned_waveform(waveform, *timing.index, request);
    REQUIRE(base);
    REQUIRE_FALSE(base.rows.empty());
    CHECK(loudest_row(base) >= 475.0);
    CHECK(loudest_row(base) <= 485.0);

    chart.beat_zero_audio_offset_seconds = {1, 2};
    const auto shifted_timing = wormedit::build_timing_index(chart);
    REQUIRE(shifted_timing);
    const auto shifted =
        wormedit::audio::project_aligned_waveform(waveform, *shifted_timing.index, request);
    REQUIRE(shifted);
    REQUIRE_FALSE(shifted.rows.empty());
    CHECK(loudest_row(shifted) >= 235.0);
    CHECK(loudest_row(shifted) <= 245.0);

    chart.beat_zero_audio_offset_seconds = {0, 1};
    auto scrolled_request = request;
    scrolled_request.viewport_scroll_pixels = 240.0;
    const auto scrolled =
        wormedit::audio::project_aligned_waveform(waveform, *timing.index, scrolled_request);
    REQUIRE(scrolled);
    REQUIRE_FALSE(scrolled.rows.empty());
    CHECK(loudest_row(scrolled) >= 235.0);
    CHECK(loudest_row(scrolled) <= 245.0);

    auto too_tall = request;
    too_tall.viewport_height_pixels = 4'097.0;
    CHECK(wormedit::audio::project_aligned_waveform(waveform, *timing.index, too_tall).status ==
          wormedit::audio::aligned_waveform_status_t::capacity_exceeded);
}

TEST_CASE("song overview retains source peaks while its window follows chart timing",
          "[unit][audio][waveform][aligned]") {
    auto chart = chart_fixture();
    const auto waveform = pulse_waveform();
    const wormedit::audio::aligned_waveform_request_t viewport{
        .viewport_scroll_pixels = 0.0,
        .viewport_height_pixels = 480.0,
        .pixels_per_standard_measure = 960.0,
    };
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    const auto base =
        wormedit::audio::project_waveform_overview(waveform, *timing.index, viewport, 200);
    REQUIRE(base);
    REQUIRE(base.rows.size() == 200);
    const auto loudest = std::ranges::max_element(
        base.rows, {}, &wormedit::audio::waveform_overview_row_t::absolute_peak);
    CHECK(loudest->audio_fraction >= 0.49);
    CHECK(loudest->audio_fraction <= 0.51);
    CHECK(base.visible_begin_fraction == 0.0);
    CHECK(base.visible_end_fraction == 0.5);
    REQUIRE(base.beat_zero_fraction);
    CHECK(*base.beat_zero_fraction == 0.0);

    chart.beat_zero_audio_offset_seconds = {1, 2};
    const auto shifted_timing = wormedit::build_timing_index(chart);
    REQUIRE(shifted_timing);
    const auto shifted =
        wormedit::audio::project_waveform_overview(waveform, *shifted_timing.index, viewport, 200);
    REQUIRE(shifted);
    CHECK(shifted.visible_begin_fraction == 0.25);
    CHECK(shifted.visible_end_fraction == 0.75);
    REQUIRE(shifted.beat_zero_fraction);
    CHECK(*shifted.beat_zero_fraction == 0.25);
    CHECK(shifted.rows[100].absolute_peak == base.rows[100].absolute_peak);

    chart.beat_zero_audio_offset_seconds = {-1, 2};
    const auto early_timing = wormedit::build_timing_index(chart);
    REQUIRE(early_timing);
    const auto early =
        wormedit::audio::project_waveform_overview(waveform, *early_timing.index, viewport, 200);
    REQUIRE(early);
    CHECK_FALSE(early.beat_zero_fraction);

    auto scrolled = viewport;
    scrolled.viewport_scroll_pixels = 240.0;
    const auto moved =
        wormedit::audio::project_waveform_overview(waveform, *timing.index, scrolled, 200);
    REQUIRE(moved);
    CHECK(moved.visible_begin_fraction == 0.25);
    CHECK(moved.visible_end_fraction == 0.75);
    CHECK(
        wormedit::audio::project_waveform_overview(waveform, *timing.index, viewport, 513).status ==
        wormedit::audio::aligned_waveform_status_t::capacity_exceeded);
}

TEST_CASE("BMSON channel waveform maps exact source cuts into its musical viewport",
          "[unit][audio][waveform][channel]") {
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    auto waveform = pulse_waveform();
    waveform.total_frames = 4U * output_sample_rate;
    waveform.bins.resize(400);
    for (auto& bin : waveform.bins)
        bin.frame_count = 441;
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    const aligned_waveform_request_t viewport{.viewport_scroll_pixels = 0.0,
                                              .viewport_height_pixels = 960.0,
                                              .pixels_per_standard_measure = 960.0};
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto base = project_channel_waveform(waveform, chart, chart.sound_channels.front(),
                                               *timing.index, viewport, source);
    REQUIRE(base);
    REQUIRE_FALSE(base.rows.empty());
    CHECK(loudest_row(base) >= 715.0);
    CHECK(loudest_row(base) <= 725.0);
    const auto slices = wormedit::derive_sound_channel_slices(
        chart, chart.sound_channels.front(), source.sample_rate, source.total_frames);
    REQUIRE(slices);
    const auto cached = project_channel_waveform(waveform, chart.sound_channels.front(),
                                                 *timing.index, viewport, slices, source);
    REQUIRE(cached);
    CHECK(cached.rows.size() == base.rows.size());
    CHECK(loudest_row(cached) == loudest_row(base));

    auto scrolled_viewport = viewport;
    scrolled_viewport.viewport_scroll_pixels = 240.0;
    const auto scrolled = project_channel_waveform(waveform, chart, chart.sound_channels.front(),
                                                   *timing.index, scrolled_viewport, source);
    REQUIRE(scrolled);
    CHECK(loudest_row(scrolled) >= 475.0);
    CHECK(loudest_row(scrolled) <= 485.0);

    auto slower_chart = chart_fixture();
    slower_chart.bpms.push_back({.id = wormedit::object_id_t{999'999},
                                 .position = {slower_chart.measures.front().id, 480},
                                 .beats_per_minute = {60, 1}});
    auto longer_waveform = waveform;
    longer_waveform.total_frames = 8U * output_sample_rate;
    longer_waveform.bins.resize(800);
    for (auto& bin : longer_waveform.bins)
        bin.frame_count = 441;
    const auto slower_timing = wormedit::build_timing_index(slower_chart);
    REQUIRE(slower_timing);
    const auto slowed = project_channel_waveform(
        longer_waveform, slower_chart, slower_chart.sound_channels.front(), *slower_timing.index,
        viewport,
        {.sample_rate = output_sample_rate, .total_frames = longer_waveform.total_frames});
    REQUIRE(slowed);
    CHECK(loudest_row(slowed) >= 595.0);
    CHECK(loudest_row(slowed) <= 605.0);

    chart.beat_zero_audio_offset_seconds = {1, 2};
    const auto shifted_timing = wormedit::build_timing_index(chart);
    REQUIRE(shifted_timing);
    const auto shifted = project_channel_waveform(waveform, chart, chart.sound_channels.front(),
                                                  *shifted_timing.index, viewport, source);
    REQUIRE(shifted);
    CHECK(loudest_row(shifted) == loudest_row(base));

    chart.beat_zero_audio_offset_seconds = {0, 1};
    chart.sound_channels.front().source_clip.begin_frame = output_sample_rate / 2U;
    const auto clipped = project_channel_waveform(waveform, chart, chart.sound_channels.front(),
                                                  *timing.index, viewport, source);
    REQUIRE(clipped);
    CHECK(loudest_row(clipped) >= 475.0);
    CHECK(loudest_row(clipped) <= 485.0);

    chart.sound_channels.front().source_clip.begin_frame = 24'000;
    const auto native_48k =
        project_channel_waveform(waveform, chart, chart.sound_channels.front(), *timing.index,
                                 viewport, {.sample_rate = 48'000, .total_frames = 4U * 48'000U});
    REQUIRE(native_48k);
    CHECK(loudest_row(native_48k) >= 475.0);
    CHECK(loudest_row(native_48k) <= 485.0);

    auto shortened = chart_fixture();
    shortened.sound_channels.front().events.resize(2);
    shortened.sound_channels.front().source_clip.end_frame = output_sample_rate * 3U / 4U;
    const auto trimmed = project_channel_waveform(
        waveform, shortened, shortened.sound_channels.front(), *timing.index, viewport, source);
    REQUIRE(trimmed);
    CHECK(std::ranges::all_of(trimmed.rows,
                              [](const auto& row) { return row.absolute_peak[0] == 0.0f; }));

    auto too_tall = viewport;
    too_tall.viewport_height_pixels = 4097.0;
    CHECK(project_channel_waveform(waveform, chart, chart.sound_channels.front(), *timing.index,
                                   too_tall, source)
              .status == aligned_waveform_status_t::capacity_exceeded);
}
