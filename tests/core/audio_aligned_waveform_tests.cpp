#include <wormedit/audio/aligned_waveform.h>
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

    chart.beat_zero_audio_offset_seconds = {1, 2};
    const auto shifted_timing = wormedit::build_timing_index(chart);
    REQUIRE(shifted_timing);
    const auto shifted =
        wormedit::audio::project_waveform_overview(waveform, *shifted_timing.index, viewport, 200);
    REQUIRE(shifted);
    CHECK(shifted.visible_begin_fraction == 0.25);
    CHECK(shifted.visible_end_fraction == 0.75);
    CHECK(shifted.rows[100].absolute_peak == base.rows[100].absolute_peak);

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
