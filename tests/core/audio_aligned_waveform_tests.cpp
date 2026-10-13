#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/channel_waveform.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
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
                                              .viewport_height_pixels = 1440.0,
                                              .pixels_per_standard_measure = 960.0};
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto base = project_channel_waveform(waveform, chart, chart.sound_channels.front(),
                                               *timing.index, viewport, source);
    REQUIRE(base);
    REQUIRE_FALSE(base.rows.empty());
    // The audio restarts at row 720, so the pulse (one second into the sample) sounds again two
    // beats after it: at row 1200, not where one linear run from the first point would put it.
    CHECK(loudest_row(base) >= 1195.0);
    CHECK(loudest_row(base) <= 1205.0);
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
    CHECK(loudest_row(scrolled) >= 955.0);
    CHECK(loudest_row(scrolled) <= 965.0);

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
            else if (pending->is_covering_window(window)) { completed = pending; ready = true; }
            else ++discarded;
            pending.reset();
        }
        if (ready) stable = stable && completed->is_covering_window(window) && style.detail_level && !style.sample_points;
        if (!pending && (!completed || completed->is_refill_needed(window, 1))) {
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

auto rational_as_double(wormedit::signed_rational_t value) -> double {
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

auto expected_seconds(const wormedit::timing_index_t& timing, double row,
                      wormedit::timing_boundary_side_t side) -> std::optional<double> {
    const auto boundary = std::ranges::find_if(timing.rows, [row](const auto& item) {
        return static_cast<double>(item.absolute_row) == row;
    });
    if (boundary != timing.rows.end()) {
        if (side == wormedit::timing_boundary_side_t::before_delays)
            return rational_as_double(boundary->before_delays_seconds);
        if (side == wormedit::timing_boundary_side_t::after_stops)
            return rational_as_double(boundary->after_stops_seconds);
        return rational_as_double(boundary->event_seconds);
    }
    const auto after = std::ranges::upper_bound(timing.segments, row, {},
        [](const auto& item) { return static_cast<double>(item.begin_row); });
    if (after == timing.segments.begin()) return std::nullopt;
    const auto& segment = *std::prev(after);
    if (row >= static_cast<double>(segment.end_row)) return std::nullopt;
    const auto begin = rational_as_double(segment.begin_seconds);
    const auto end = rational_as_double(segment.end_seconds);
    const auto fraction = (row - static_cast<double>(segment.begin_row)) /
        static_cast<double>(segment.end_row - segment.begin_row);
    return begin + (end - begin) * fraction;
}

auto display_table_matches_forward_clock() -> bool {
    auto chart = chart_fixture();
    const auto first_measure = chart.measures.front().id;
    const auto second_measure = chart.measures.at(1).id;
    chart.pauses.push_back({.id = wormedit::object_id_t{7001},
        .position = {first_measure, 480}, .kind = wormedit::pause_kind_t::delay,
        .duration = {1, 10}, .source_unit = wormedit::pause_unit_t::seconds});
    chart.pauses.push_back({.id = wormedit::object_id_t{7002},
        .position = {first_measure, 840}, .kind = wormedit::pause_kind_t::stop,
        .duration = {1, 5}, .source_unit = wormedit::pause_unit_t::seconds});
    chart.warps.push_back({.id = wormedit::object_id_t{7003},
        .span = {{second_measure, 480}, {second_measure, 540}}});
    const auto built = wormedit::build_timing_index(chart);
    if (!built) return false;
    using namespace wormedit::audio;
    display_row_table_t table;
    aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 32, .pixels_per_standard_measure = 960};
    const display_row_table_identity_t identity{1, chart.id.value, 3};
    if (!prepare_display_row_table(table, *built.index, request, identity)) return false;
    for (const auto row : {480.0, 480.5, 840.0, 840.5, 1440.0, 1500.0}) {
        for (const auto side : {wormedit::timing_boundary_side_t::before_delays,
                                wormedit::timing_boundary_side_t::event,
                                wormedit::timing_boundary_side_t::after_stops}) {
            const auto expected = expected_seconds(*built.index, row, side);
            const auto actual = display_seconds_at(table, row, side);
            if (expected.has_value() != actual.has_value()) return false;
            if (expected && std::abs(*expected - *actual) > 1e-9) return false;
        }
    }
    return true;
}

auto display_table_reuses_scrolled_rows() -> bool {
    auto chart = chart_fixture();
    const auto built = wormedit::build_timing_index(chart);
    if (!built) return false;
    using namespace wormedit::audio;
    display_row_table_t table;
    aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 10, .pixels_per_standard_measure = 960};
    const display_row_table_identity_t identity{1, chart.id.value, 1};
    if (!prepare_display_row_table(table, *built.index, request, identity) ||
        table.rows.size() != 12 || table.built_rows != 12 || table.reused_rows != 0) return false;
    const auto generation = table.generation;
    if (!prepare_display_row_table(table, *built.index, request, identity) ||
        table.generation != generation || table.built_rows != 12 || table.reused_rows != 0)
        return false;
    request.viewport_scroll_pixels = 1;
    if (!prepare_display_row_table(table, *built.index, request, identity) ||
        table.built_rows != 1 || table.reused_rows != 11) return false;
    return true;
}

auto display_table_rebuilds_on_clock_inputs() -> bool {
    auto chart = chart_fixture();
    const auto built = wormedit::build_timing_index(chart);
    if (!built) return false;
    using namespace wormedit::audio;
    display_row_table_t table;
    aligned_waveform_request_t request{.viewport_scroll_pixels = 1,
        .viewport_height_pixels = 11, .pixels_per_standard_measure = 960};
    const display_row_table_identity_t identity{1, chart.id.value, 1};
    if (!prepare_display_row_table(table, *built.index, request, identity)) return false;
    auto changed_identity = identity;
    ++changed_identity.revision;
    if (!prepare_display_row_table(table, *built.index, request, changed_identity) ||
        table.reused_rows != 0) return false;
    ++changed_identity.project;
    if (!prepare_display_row_table(table, *built.index, request, changed_identity) ||
        table.reused_rows != 0) return false;
    ++changed_identity.chart;
    if (!prepare_display_row_table(table, *built.index, request, changed_identity) ||
        table.reused_rows != 0) return false;
    request.pixels_per_standard_measure = 1200;
    if (!prepare_display_row_table(table, *built.index, request, changed_identity) ||
        table.reused_rows != 0) return false;
    request.viewport_height_pixels = 12;
    const auto before_height_change = table.generation;
    if (!prepare_display_row_table(table, *built.index, request, changed_identity) ||
        table.rows.size() != 14 || table.generation != before_height_change + 1) return false;
    auto changed_timing = *built.index;
    ++changed_timing.source_fingerprint;
    if (!prepare_display_row_table(table, changed_timing, request, changed_identity) ||
        table.reused_rows != 0) return false;
    changed_timing.discontinuity_profile = wormedit::timing_discontinuity_profile_t::bms_legacy;
    if (!prepare_display_row_table(table, changed_timing, request, changed_identity) ||
        table.reused_rows != 0) return false;
    changed_timing.bounded_clock = true;
    ++changed_timing.bounded_clock_roundings;
    return prepare_display_row_table(table, changed_timing, request, changed_identity) &&
        table.reused_rows == 0;
}

auto display_channel_matches_reference() -> bool {
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    auto waveform = pulse_waveform();
    waveform.total_frames = 4U * output_sample_rate;
    waveform.bins.resize(400);
    for (auto& bin : waveform.bins) bin.frame_count = 441;
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return false;
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 1440, .pixels_per_standard_measure = 960};
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto& channel = chart.sound_channels.front();
    const auto slices = wormedit::derive_sound_channel_slices(
        chart, channel, source.sample_rate, source.total_frames);
    display_row_table_t table;
    if (!slices || !prepare_display_row_table(table, *timing.index, request,
        {chart.id.value, chart.id.value, 1})) return false;
    const auto reference = project_channel_waveform(waveform, chart, channel,
        *timing.index, request, source);
    const auto projected = project_channel_waveform(waveform, channel, table,
        request, slices, source);
    return reference && projected && !reference.rows.empty() && !projected.rows.empty() &&
        std::abs(loudest_row(reference) - loudest_row(projected)) <= 1.0 &&
        projected.sampled_bins <= projected.visited_slices * 8;
}

auto channel_peaks_contained(const wormedit::audio::aligned_waveform_result_t& source,
    const wormedit::audio::aligned_waveform_result_t& target) -> bool {
    for (const auto& row : source.rows) {
        std::array<float, 2> peaks{};
        for (const auto& other : target.rows) {
            if (std::abs(other.y_pixels - row.y_pixels) > 1.0) continue;
            for (std::size_t channel = 0; channel < peaks.size(); ++channel)
                peaks[channel] = std::max(peaks[channel], other.absolute_peak[channel]);
        }
        for (std::size_t channel = 0; channel < peaks.size(); ++channel)
            if (row.absolute_peak[channel] > peaks[channel]) return false;
    }
    return true;
}

auto channel_projection_matches_reference(wormedit::chart_document_t& chart,
    const wormedit::audio::waveform_t& waveform,
    wormedit::audio::channel_waveform_source_geometry_t source) -> bool {
    using namespace wormedit::audio;
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return false;
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 1920, .pixels_per_standard_measure = 960};
    const auto& channel = chart.sound_channels.front();
    const auto slices = wormedit::derive_sound_channel_slices(
        chart, channel, source.sample_rate, source.total_frames);
    display_row_table_t table;
    if (!slices || !prepare_display_row_table(table, *timing.index, request,
        {chart.id.value, chart.id.value, 1})) return false;
    const auto reference = project_channel_waveform(waveform, chart, channel,
        *timing.index, request, source);
    const auto projected = project_channel_waveform(waveform, channel, table,
        request, slices, source);
    return reference && projected && !reference.rows.empty() && !projected.rows.empty() &&
        channel_peaks_contained(reference, projected) &&
        channel_peaks_contained(projected, reference);
}

auto display_channel_tempo_clip_and_warp_match_reference() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    const auto waveform = pulse_waveform();
    const channel_waveform_source_geometry_t normalized_source{output_sample_rate,
                                                                waveform.total_frames};
    auto tempo_chart = chart_fixture();
    tempo_chart.bpms.push_back({.id = object_id_t{8201},
        .position = {tempo_chart.measures.front().id, 480}, .beats_per_minute = {60, 1}});
    if (!channel_projection_matches_reference(tempo_chart, waveform, normalized_source))
        return false;

    auto clipped_chart = chart_fixture();
    clipped_chart.sound_channels.front().source_clip.begin_frame = 24'000;
    const channel_waveform_source_geometry_t native_source{48'000, 96'000};
    if (!channel_projection_matches_reference(clipped_chart, waveform, native_source))
        return false;

    auto warped_chart = chart_fixture();
    warped_chart.warps.push_back({.id = object_id_t{8202},
        .span = {{warped_chart.measures.front().id, 480},
                 {warped_chart.measures.front().id, 600}}});
    return channel_projection_matches_reference(warped_chart, waveform, normalized_source);
}

auto performance_pyramid_waveform(std::uint64_t total_frames) -> wormedit::audio::waveform_t {
    using namespace wormedit::audio;
    waveform_t waveform;
    waveform.key = {{"fixture:display-row", "content:pyramid"}, 256, 0, total_frames};
    waveform.sample_rate = output_sample_rate;
    waveform.total_frames = total_frames;
    waveform.bins.resize(static_cast<std::size_t>(total_frames / 256));
    for (auto& bin : waveform.bins) {
        bin.minimum = {-0.25f, -0.25f};
        bin.maximum = {0.25f, 0.25f};
        bin.absolute_peak = {0.25f, 0.25f};
        bin.rms = {0.125f, 0.125f};
        bin.frame_count = 256;
    }
    std::uint64_t frames_per_bin = 512;
    auto count = waveform.bins.size() / 2;
    while (count != 0) {
        waveform_level_t level{.frames_per_bin = frames_per_bin};
        level.bins.resize(count);
        for (auto& bin : level.bins) {
            bin = waveform.bins.front();
            bin.frame_count = frames_per_bin;
        }
        waveform.coarser_levels.push_back(std::move(level));
        frames_per_bin *= 2;
        count /= 2;
    }
    return waveform;
}

auto boundary_impulse_waveform() -> wormedit::audio::waveform_t {
    using namespace wormedit::audio;
    auto waveform = pulse_waveform();
    waveform.total_frames = 4U * output_sample_rate;
    waveform.bins.assign(400, {});
    for (auto& bin : waveform.bins) bin.frame_count = 441;
    waveform.bins[10].absolute_peak = {1.0f, 0.0f};
    waveform.bins[10].rms = {0.5f, 0.0f};
    waveform.bins[105].absolute_peak = {0.0f, 1.0f};
    waveform.bins[105].rms = {0.0f, 0.5f};
    return waveform;
}

auto channel_peak_near(const wormedit::audio::aligned_waveform_result_t& result,
                       double row, std::size_t channel) -> bool {
    return std::ranges::any_of(result.rows, [row, channel](const auto& item) {
        return std::abs(item.y_pixels - row) <= 1.0 && item.absolute_peak[channel] == 1.0f;
    });
}

auto continuation_delay_keeps_pre_event_source() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    const auto first_measure = chart.measures.front().id;
    for (auto& event : chart.sound_channels.front().events) {
        if (event.position.measure == first_measure && event.position.row == 720)
            event.continuation = true;
    }
    chart.pauses.push_back({.id = object_id_t{8104}, .position = {first_measure, 720},
        .kind = pause_kind_t::delay, .duration = {1, 10}, .source_unit = pause_unit_t::seconds});
    const auto waveform = boundary_impulse_waveform();
    const auto timing = build_timing_index(chart);
    if (!timing) return false;
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 1800, .pixels_per_standard_measure = 960};
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto& channel = chart.sound_channels.front();
    const auto slices = derive_sound_channel_slices(chart, channel, source.sample_rate,
                                                     source.total_frames);
    display_row_table_t table;
    if (!slices || !prepare_display_row_table(table, *timing.index, request)) return false;
    const auto reference = project_channel_waveform(waveform, chart, channel,
        *timing.index, request, source);
    const auto projected = project_channel_waveform(waveform, channel, table,
        request, slices, source);
    return reference && projected && channel_peak_near(reference, 720, 1) &&
        channel_peak_near(projected, 720, 1);
}

struct display_benchmark_result_t {
    bool valid{};
    std::int64_t reference_microseconds{};
    std::int64_t display_microseconds{};
    std::array<std::size_t, 4> visited{};
    std::array<std::size_t, 4> sampled{};
};

auto compare_display_channel_work() -> display_benchmark_result_t {
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return {};
    const auto& channel = chart.sound_channels.front();
    display_benchmark_result_t result{.valid = true};
    std::size_t case_index{};
    for (const auto frames : {262'144U, 1'048'576U}) {
        auto waveform = performance_pyramid_waveform(frames);
        const channel_waveform_source_geometry_t source{output_sample_rate, frames};
        const auto slices = wormedit::derive_sound_channel_slices(
            chart, channel, source.sample_rate, source.total_frames);
        if (!slices) return {};
        for (const auto zoom : {480.0, 3840.0}) {
            const aligned_waveform_request_t request{.viewport_scroll_pixels = zoom / 4,
                .viewport_height_pixels = 600, .pixels_per_standard_measure = zoom};
            display_row_table_t table;
            if (!prepare_display_row_table(table, *timing.index, request,
                {chart.id.value, chart.id.value, 1})) return {};
            const auto reference_start = std::chrono::steady_clock::now();
            const auto reference = project_channel_waveform(waveform, chart, channel,
                *timing.index, request, source);
            const auto reference_end = std::chrono::steady_clock::now();
            const auto display_start = std::chrono::steady_clock::now();
            const auto projected = project_channel_waveform(waveform, channel, table,
                request, slices, source);
            const auto display_end = std::chrono::steady_clock::now();
            result.reference_microseconds += std::chrono::duration_cast<std::chrono::microseconds>(
                reference_end - reference_start).count();
            result.display_microseconds += std::chrono::duration_cast<std::chrono::microseconds>(
                display_end - display_start).count();
            if (!reference || !projected ||
                projected.visited_slices > table.rows.size() + slices.slices.size() ||
                projected.sampled_bins > projected.visited_slices *
                    (8 + 2 * std::numeric_limits<std::uint64_t>::digits)) return {};
            result.visited[case_index] = projected.visited_slices;
            result.sampled[case_index] = projected.sampled_bins;
            ++case_index;
        }
    }
    return result;
}

auto base_only_low_zoom_keeps_channel_peaks() -> bool {
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return false;
    auto waveform = performance_pyramid_waveform(1'048'576);
    waveform.coarser_levels.clear();
    const auto& channel = chart.sound_channels.front();
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto slices = wormedit::derive_sound_channel_slices(
        chart, channel, source.sample_rate, source.total_frames);
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 32, .pixels_per_standard_measure = 120};
    display_row_table_t table;
    if (!slices || !prepare_display_row_table(table, *timing.index, request)) return false;
    const auto projected = project_channel_waveform(waveform, channel, table,
        request, slices, source);
    return projected && !projected.rows.empty() &&
        std::ranges::any_of(projected.rows, [](const auto& row) {
            return row.absolute_peak[0] >= 0.25f;
        });
}

auto display_slice_boundaries_keep_delay_and_stop_audio() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    auto& channel = chart.sound_channels.front();
    const auto first_measure = chart.measures.front().id;
    const auto second_measure = chart.measures.at(1).id;
    chart.pauses.push_back({.id = object_id_t{8101}, .position = {first_measure, 720},
        .kind = pause_kind_t::delay, .duration = {1, 10}, .source_unit = pause_unit_t::seconds});
    chart.pauses.push_back({.id = object_id_t{8102}, .position = {second_measure, 0},
        .kind = pause_kind_t::stop, .duration = {1, 5}, .source_unit = pause_unit_t::seconds});
    channel.events.push_back({.id = object_id_t{8103}, .position = {second_measure, 0},
        .lane = lane_id_t{1}, .continuation = false});
    const auto waveform = boundary_impulse_waveform();
    const auto timing = build_timing_index(chart);
    if (!timing) return false;
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 1800, .pixels_per_standard_measure = 960};
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto slices = derive_sound_channel_slices(chart, channel, source.sample_rate,
                                                     source.total_frames);
    display_row_table_t table;
    if (!slices || !prepare_display_row_table(table, *timing.index, request,
        {chart.id.value, chart.id.value, 1})) return false;
    const auto reference = project_channel_waveform(waveform, chart, channel,
        *timing.index, request, source);
    const auto projected = project_channel_waveform(waveform, channel, table, request, slices, source);
    return reference && projected && channel_peak_near(reference, 720, 1) &&
        channel_peak_near(projected, 720, 1) && channel_peak_near(reference, 960, 0) &&
        channel_peak_near(projected, 960, 0) && continuation_delay_keeps_pre_event_source();
}

auto display_channel_rejects_invalid_slices() -> bool {
    using namespace wormedit;
    using namespace wormedit::audio;
    auto chart = chart_fixture();
    chart.warps.push_back({.id = object_id_t{8203},
        .span = {{chart.measures.front().id, 480}, {chart.measures.at(1).id, 0}}});
    const auto waveform = pulse_waveform();
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto timing = build_timing_index(chart);
    if (!timing) return false;
    const auto& channel = chart.sound_channels.front();
    const auto slices = derive_sound_channel_slices(chart, channel, source.sample_rate,
        source.total_frames);
    const aligned_waveform_request_t request{.viewport_height_pixels = 600,
        .pixels_per_standard_measure = 960};
    display_row_table_t table;
    if (slices || !prepare_display_row_table(table, *timing.index, request)) return false;
    const auto projected = project_channel_waveform(waveform, channel, table,
        request, slices, source);
    return projected.status == aligned_waveform_status_t::invalid_timing && projected.rows.empty();
}

auto display_channel_rejects_invalid_clock_and_request() -> bool {
    using namespace wormedit::audio;
    const auto chart = chart_fixture();
    const auto& channel = chart.sound_channels.front();
    const auto waveform = pulse_waveform();
    const channel_waveform_source_geometry_t source{output_sample_rate, waveform.total_frames};
    const auto timing = wormedit::build_timing_index(chart);
    if (!timing) return false;
    const auto slices = wormedit::derive_sound_channel_slices(
        chart, channel, source.sample_rate, source.total_frames);
    if (!slices) return false;
    const aligned_waveform_request_t good{.viewport_scroll_pixels = 0,
        .viewport_height_pixels = 32, .pixels_per_standard_measure = 960};
    display_row_table_t invalid_table;
    const auto no_clock = project_channel_waveform(waveform, channel, invalid_table,
                                                   good, slices, source);
    auto invalid_request = good;
    invalid_request.viewport_height_pixels = 0;
    display_row_table_t table;
    if (!prepare_display_row_table(table, *timing.index, good)) return false;
    const auto no_request = project_channel_waveform(waveform, channel, table,
        invalid_request, slices, source);
    if (no_clock.status != aligned_waveform_status_t::invalid_timing || !no_clock.rows.empty() ||
        no_request.status != aligned_waveform_status_t::invalid_request || !no_request.rows.empty())
        return false;
    auto short_waveform = waveform;
    short_waveform.total_frames = 4'410;
    short_waveform.bins.resize(10);
    for (auto& bin : short_waveform.bins) bin.frame_count = 441;
    const channel_waveform_source_geometry_t short_source{output_sample_rate,
                                                           short_waveform.total_frames};
    const auto short_slices = wormedit::derive_sound_channel_slices(
        chart, channel, short_source.sample_rate, short_source.total_frames);
    auto eof_view = good;
    eof_view.viewport_scroll_pixels = 1910;
    eof_view.viewport_height_pixels = 10;
    display_row_table_t eof_table;
    if (!short_slices || !prepare_display_row_table(eof_table, *timing.index, eof_view))
        return false;
    const auto eof = project_channel_waveform(short_waveform, channel, eof_table,
        eof_view, short_slices, short_source);
    return eof && eof.rows.empty();
}
}

TEST_CASE("channel_waveform display row table follows forward timing and reuses world rows",
          "[unit][audio][waveform][aligned][display][T440]") {
    CHECK(display_table_matches_forward_clock());
    CHECK(display_table_reuses_scrolled_rows());
    CHECK(display_table_rebuilds_on_clock_inputs());
}

TEST_CASE("display channel_waveform projection stays within one row of the timing reference",
          "[unit][audio][waveform][channel][display][T440]") {
    CHECK(display_channel_matches_reference());
    CHECK(display_channel_tempo_clip_and_warp_match_reference());
}

TEST_CASE("display channel_waveform assigns delay and stop time to the correct slice",
          "[unit][audio][waveform][channel][display][T440]") {
    CHECK(display_slice_boundaries_keep_delay_and_stop_audio());
    CHECK(display_channel_rejects_invalid_clock_and_request());
    CHECK(display_channel_rejects_invalid_slices());
}

TEST_CASE("display channel_waveform work stays bounded by rows and source pyramid",
          "[.performance][audio][waveform][channel][display][T440]") {
    const auto result = compare_display_channel_work();
    INFO("reference projection microseconds: " << result.reference_microseconds);
    INFO("display projection microseconds: " << result.display_microseconds);
    for (std::size_t index = 0; index < result.visited.size(); ++index) {
        const auto frames = index < 2 ? 262'144 : 1'048'576;
        const auto zoom = index % 2 == 0 ? 480 : 3840;
        SUCCEED("frames " << frames << " zoom " << zoom << " visited slices " << result.visited[index]
                << " sampled bins " << result.sampled[index]);
    }
    CHECK(result.valid);
    CHECK(base_only_low_zoom_keeps_channel_peaks());
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
    CHECK(forward->is_covering_window(window));
    CHECK(reverse->is_covering_window(window));
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

TEST_CASE("aligned waveform leaves a blank gap where a warp consumes no time",
          "[unit][audio][waveform][aligned][warp]") {
    using namespace wormedit;
    auto waveform = pulse_waveform();
    for (auto& bin : waveform.bins) {
        bin.minimum = {-0.5f, -0.5f};
        bin.maximum = {0.5f, 0.5f};
        bin.absolute_peak = {0.5f, 0.5f};
        bin.rms = {0.25f, 0.25f};
    }
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960};
    chart.measures = {{.id = measure_id_t{1}, .definition = {{1, 1}, meter_t{4, 4}}},
                      {.id = measure_id_t{2}, .definition = {{1, 1}, meter_t{4, 4}}}};
    chart.bpms = {{.id = object_id_t{10},
                   .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    const audio::aligned_waveform_request_t request{.viewport_scroll_pixels = 0.0,
                                                    .viewport_height_pixels = 1920.0,
                                                    .pixels_per_standard_measure = 960.0};
    const auto count_rows_between = [&](const audio::aligned_waveform_result_t& result,
                                        double begin, double end) {
        return std::ranges::count_if(result.rows, [&](const auto& row) {
            return row.y_pixels >= begin && row.y_pixels < end && row.absolute_peak[0] > 0.0f;
        });
    };

    const auto plain_timing = build_timing_index(chart);
    REQUIRE(plain_timing);
    const auto plain = audio::project_aligned_waveform(waveform, *plain_timing.index, request);
    REQUIRE(plain);
    CHECK(count_rows_between(plain, 480.0, 960.0) > 0);

    chart.warps.push_back(
        {.id = object_id_t{30}, .span = {{measure_id_t{1}, 480}, {measure_id_t{2}, 0}}});
    const auto warped_timing = build_timing_index(chart);
    REQUIRE(warped_timing);
    const auto warped = audio::project_aligned_waveform(waveform, *warped_timing.index, request);
    REQUIRE(warped);
    CHECK(count_rows_between(warped, 0.0, 479.0) > 0);
    CHECK(count_rows_between(warped, 482.0, 958.0) == 0);
    CHECK(count_rows_between(warped, 962.0, 1900.0) > 0);
}

namespace {
auto numeric_display_pyramid() -> wormedit::audio::waveform_t {
    auto waveform = performance_pyramid_waveform(1024);
    const std::array<float, 4> peaks{.1f, .2f, .4f, 1.f};
    const std::array<float, 4> rms{.1f, .3f, .2f, .8f};
    for (std::size_t index = 0; index < 4; ++index) {
        waveform.bins[index].minimum = {-peaks[index], -peaks[index]};
        waveform.bins[index].maximum = {peaks[index], peaks[index]};
        waveform.bins[index].absolute_peak = {peaks[index], peaks[index]};
        waveform.bins[index].rms = {rms[index], rms[index]};
    }
    auto& pairs = waveform.coarser_levels.front().bins;
    pairs[0].minimum = {-.2f, -.2f};
    pairs[0].maximum = {.2f, .2f};
    pairs[0].absolute_peak = {.2f, .2f};
    pairs[0].rms = {std::sqrt(.05f), std::sqrt(.05f)};
    pairs[1].minimum = {-1.f, -1.f};
    pairs[1].maximum = {1.f, 1.f};
    pairs[1].absolute_peak = {1.f, 1.f};
    pairs[1].rms = {std::sqrt(.34f), std::sqrt(.34f)};
    auto& total = waveform.coarser_levels.back().bins.front();
    total.minimum = {-1.f, -1.f};
    total.maximum = total.absolute_peak = {1.f, 1.f};
    total.rms = {std::sqrt(.195f), std::sqrt(.195f)};
    return waveform;
}
}
namespace {
void check_display_column(const std::optional<wormedit::audio::waveform_column_t>& column,
                          float peak, float rms) {
    REQUIRE(column);
    CHECK(column->absolute_peak[0] == peak);
    CHECK(column->rms[0] == rms);
}
}
TEST_CASE("channel_waveform display RMS uses matching pyramid bins and refines clipped edges",
          "[audio][waveform][channel][display][T440]") {
    const auto waveform = numeric_display_pyramid();
    std::size_t sampled{};
    const auto full = wormedit::audio::sample_display_waveform_span(waveform, 0, 1024, sampled);
    check_display_column(full, 1.f, std::sqrt(.34f));
    const auto clipped = wormedit::audio::sample_display_waveform_span(waveform, 0, 768, sampled);
    check_display_column(clipped, .4f, std::sqrt(.05f));
    CHECK(base_only_low_zoom_keeps_channel_peaks());
}

TEST_CASE("song overview clamps a viewport that extends past the audio end",
          "[unit][audio][waveform][aligned]") {
    const auto chart = chart_fixture();
    const auto waveform = pulse_waveform();
    const auto timing = wormedit::build_timing_index(chart);
    REQUIRE(timing);
    // The 2 s audio ends long before the 2880-row chart does, so these scrolls reach past it.
    for (const double scroll : {480.0, 960.0, 3840.0, 9600.0}) {
        const wormedit::audio::aligned_waveform_request_t viewport{
            .viewport_scroll_pixels = scroll,
            .viewport_height_pixels = 480.0,
            .pixels_per_standard_measure = 960.0,
        };
        const auto overview =
            wormedit::audio::project_waveform_overview(waveform, *timing.index, viewport, 200);
        REQUIRE(overview);
        CHECK(overview.visible_end_fraction == 1.0);
        CHECK(overview.visible_begin_fraction >= 0.0);
        CHECK(overview.visible_begin_fraction <= overview.visible_end_fraction);
    }
}

TEST_CASE("aligned waveform window includes the outward-rounded visible rows", "[unit][audio][helpers]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    const auto chart = chart_fixture();
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto waveform = pulse_waveform();
    const aligned_waveform_request_t fractional{.viewport_scroll_pixels = 10.25,
        .viewport_height_pixels = 10.5,
        .pixels_per_standard_measure = static_cast<double>(timing.index->divisions_per_standard_measure)};
    const auto actual = aligned_waveform_window(waveform, *timing.index, fractional);
    auto rounded = fractional;
    rounded.viewport_scroll_pixels = 10;
    rounded.viewport_height_pixels = 11;
    const auto expected = aligned_waveform_window(waveform, *timing.index, rounded);
    REQUIRE(actual);
    REQUIRE(expected);
    CHECK(actual->begin_frame == expected->begin_frame);
    CHECK(actual->end_frame == expected->end_frame);
}
