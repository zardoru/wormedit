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

    auto before_start = request;
    before_start.viewport_scroll_pixels = -192.0;
    const auto padded =
        wormedit::audio::project_aligned_waveform(waveform, *timing.index, before_start);
    REQUIRE(padded);
    REQUIRE_FALSE(padded.rows.empty());
    CHECK(loudest_row(padded) >= 667.0);
    CHECK(loudest_row(padded) <= 677.0);

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

namespace {
struct detail_follow_simulation_t {
    std::optional<wormedit::audio::aligned_waveform_detail_range_t> pending;
    std::optional<wormedit::audio::aligned_waveform_detail_range_t> completed;
    wormedit::audio::aligned_waveform_style_t style;
    int remaining{};
    int requests{};
    int discarded{};
    int rejected{};
    bool reject_next{};
    bool stable{true};
    bool ready{};

    void advance(wormedit::audio::aligned_waveform_window_t window) {
        using namespace wormedit::audio;
        update_aligned_waveform_style(style, window.frames_per_pixel);
        if (pending && --remaining == 0) {
            if (reject_next) { reject_next = false; ++rejected; }
            else if (pending->covers(window)) { completed = pending; ready = true; }
            else ++discarded;
            pending.reset();
        }
        if (ready) stable = stable && completed->covers(window) && style.detail_level && !style.sample_points;
        if (!pending && (!completed || completed->needs_refill(window, 1))) {
            pending = padded_aligned_waveform_range(window);
            remaining = 5;
            ++requests;
        }
    }
};

auto filled_waveform(std::uint32_t level, std::uint64_t begin, std::uint64_t end,
                     std::uint64_t total) -> wormedit::audio::waveform_t {
    using namespace wormedit::audio;
    waveform_t waveform;
    waveform.key = {{"fake:follow", "content:v1"}, level, begin, end};
    waveform.total_frames = total;
    waveform.bins.resize((end - begin + level - 1U) / level);
    for (auto& bin : waveform.bins) {
        bin.minimum = {-0.5f, -0.25f};
        bin.maximum = {0.5f, 0.25f};
        bin.absolute_peak = {0.5f, 0.25f};
        bin.rms = {0.25f, 0.125f};
        bin.frame_count = level;
    }
    return waveform;
}

auto stable_sample_follow() -> bool {
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    chart.bpms.front().beats_per_minute = {12000, 1};
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return false;
    const auto overview = filled_waveform(256, 0, 88200, 88200);
    const auto detail = filled_waveform(32, 0, 512, 88200);
    decoded_block_cache_t cache;
    std::vector<float> pcm(maximum_read_frames * output_channel_count, 0.25f);
    aligned_waveform_style_t style;
    bool saw_pcm{}, saw_detail{}, saw_overview{};
    for (std::size_t frame = 0; frame < 600; ++frame) {
        if (frame % 60 == 0) {
            cache.clear();
            if (frame % 120 == 0)
                static_cast<void>(cache.store({overview.key.source, 0}, maximum_read_frames, pcm));
        }
        const aligned_waveform_request_t request{.viewport_scroll_pixels = frame * 3.0,
            .viewport_height_pixels = 64, .pixels_per_standard_measure = 1923};
        const auto window = aligned_waveform_window(overview, *timing.index, request);
        if (!window) return false;
        update_aligned_waveform_style(style, window->frames_per_pixel);
        const auto projected = project_composed_aligned_waveform(overview, &detail, *timing.index,
                                                                  request, &cache, style);
        if (!projected || projected.rows.empty() || !style.sample_points || !style.detail_level ||
            projected.rows.size() > request.maximum_rows || projected.chosen_frames_per_bin != 32) return false;
        saw_pcm = saw_pcm || projected.pcm_spans != 0;
        saw_detail = saw_detail || projected.detail_spans != 0;
        saw_overview = saw_overview || projected.overview_spans != 0;
        if (!std::ranges::all_of(projected.rows, [](const auto& row) { return row.sample_point; })) return false;
    }
    return saw_pcm && saw_detail && saw_overview;
}
}

TEST_CASE("aligned waveform follows 600 advancing frames without discarding covering detail", "[audio][waveform]") {
    detail_follow_simulation_t simulation;
    for (std::uint64_t frame = 0; frame < 600; ++frame) {
        const auto begin = 65000 + frame * 3U * 128U;
        simulation.advance({begin, begin + 400U * 128U, 128.0});
    }
    CHECK(simulation.ready);
    CHECK(simulation.stable);
    CHECK(simulation.discarded == 0);
    CHECK(simulation.requests >= 3);
}

TEST_CASE("aligned waveform mode uses zoom hysteresis independently of availability", "[audio][waveform]") {
    using namespace wormedit::audio;
    aligned_waveform_style_t style;
    update_aligned_waveform_style(style, 0.8);
    CHECK(style == aligned_waveform_style_t{true, true});
    update_aligned_waveform_style(style, 1.0);
    CHECK(style.sample_points);
    update_aligned_waveform_style(style, 1.2);
    CHECK_FALSE(style.sample_points);
    update_aligned_waveform_style(style, 260);
    CHECK(style.detail_level);
    update_aligned_waveform_style(style, 281);
    CHECK_FALSE(style.detail_level);
    update_aligned_waveform_style(style, 250);
    CHECK_FALSE(style.detail_level);
}

TEST_CASE("aligned waveform composes partial PCM and detail with one sample style for 600 frames", "[audio][waveform]") {
    CHECK(stable_sample_follow());
}

TEST_CASE("aligned waveform detail ranges align and bound both playback directions", "[audio][waveform]") {
    using namespace wormedit::audio;
    const aligned_waveform_window_t window{400000, 450000, 128};
    const auto forward = padded_aligned_waveform_range(window, 1);
    const auto reverse = padded_aligned_waveform_range(window, -1);
    REQUIRE(forward);
    REQUIRE(reverse);
    CHECK(forward->covers(window));
    CHECK(reverse->covers(window));
    CHECK(forward->begin_frame % 65536 == 0);
    CHECK(forward->end_frame % 65536 == 0);
}

TEST_CASE("aligned waveform retains covering detail and retries a rejected replacement", "[audio][waveform]") {
    detail_follow_simulation_t simulation;
    for (std::uint64_t frame = 0; frame < 600; ++frame) {
        if (frame == 10) simulation.reject_next = true;
        const auto begin = 65000 + frame * 3U * 128U;
        simulation.advance({begin, begin + 400U * 128U, 128.0});
    }
    CHECK(simulation.rejected == 1);
    CHECK(simulation.stable);
    CHECK(simulation.discarded == 0);
    CHECK(simulation.requests >= 4);
}

TEST_CASE("waveform range projection zooms into an absolute audio span",
          "[unit][audio][waveform][aligned][range]") {
    const auto waveform = pulse_waveform();
    using namespace wormedit::audio;

    const auto whole = project_waveform_range(waveform, 0.0, 1.0, 200);
    REQUIRE(whole);
    CHECK(whole.visible_begin_fraction == 0.0);
    CHECK(whole.visible_end_fraction == 1.0);
    const auto whole_loudest = std::ranges::max_element(
        whole.rows, {}, &waveform_overview_row_t::absolute_peak);
    CHECK(whole_loudest->audio_fraction >= 0.49);
    CHECK(whole_loudest->audio_fraction <= 0.51);

    const auto zoomed = project_waveform_range(waveform, 0.4, 0.6, 50);
    REQUIRE(zoomed);
    REQUIRE_FALSE(zoomed.rows.empty());
    CHECK(zoomed.rows.size() <= 50);
    for (const auto& row : zoomed.rows) {
        CHECK(row.audio_fraction >= 0.4);
        CHECK(row.audio_fraction <= 0.6);
    }
    const auto zoomed_loudest = std::ranges::max_element(
        zoomed.rows, {}, &waveform_overview_row_t::absolute_peak);
    CHECK(zoomed_loudest->absolute_peak == 1.0f);
    CHECK(zoomed_loudest->audio_fraction >= 0.49);
    CHECK(zoomed_loudest->audio_fraction <= 0.51);

    const auto away = project_waveform_range(waveform, 0.0, 0.25, 50);
    REQUIRE(away);
    CHECK(std::ranges::all_of(away.rows, [](const auto& row) { return row.absolute_peak == 0.0f; }));
}

TEST_CASE("waveform range projection rejects invalid spans and capacity",
          "[unit][audio][waveform][aligned][range]") {
    const auto waveform = pulse_waveform();
    using namespace wormedit::audio;
    CHECK(project_waveform_range(waveform, 0.5, 0.5, 10).status == aligned_waveform_status_t::invalid_request);
    CHECK(project_waveform_range(waveform, 0.6, 0.4, 10).status == aligned_waveform_status_t::invalid_request);
    CHECK(project_waveform_range(waveform, -0.1, 0.4, 10).status == aligned_waveform_status_t::invalid_request);
    CHECK(project_waveform_range(waveform, 0.0, 1.5, 10).status == aligned_waveform_status_t::invalid_request);
    CHECK(project_waveform_range(waveform, 0.0, 1.0, 0).status == aligned_waveform_status_t::invalid_request);
    CHECK(project_waveform_range(waveform, 0.0, 1.0, maximum_waveform_range_rows + 1).status ==
          aligned_waveform_status_t::capacity_exceeded);
    waveform_t empty;
    CHECK(project_waveform_range(empty, 0.0, 1.0, 10).status == aligned_waveform_status_t::invalid_waveform);
}
