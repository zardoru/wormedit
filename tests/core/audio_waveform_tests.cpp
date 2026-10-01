#include <wormedit/audio/waveform.h>
#include <wormedit/audio/aligned_waveform.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <future>
#include <stop_token>
#include <string>
#include <string_view>

namespace {

auto fixture_path(std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" / std::string(name);
}

auto fixture_source() -> wormedit::audio::audio_source_identity_t {
    return {"fixture:sliced-seek", "ogg:generated-v1"};
}

auto bin_at_seconds(const wormedit::audio::waveform_t& waveform, double seconds)
    -> const wormedit::audio::waveform_bin_t& {
    const auto frame = static_cast<std::uint64_t>(seconds * wormedit::audio::output_sample_rate);
    return waveform.bins.at(static_cast<std::size_t>(frame / waveform.key.frames_per_bin));
}

} // namespace

TEST_CASE("MP3 project audio produces a complete coarse waveform", "[audio][waveform][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.mp3"), &error));
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    const auto result = wormedit::audio::analyze_waveform(decoder, blocks, waveforms,
                                                        fixture_source(), 256);
    CAPTURE(result.status);
    REQUIRE(result.waveform);
    CHECK(result.waveform->total_frames == decoder.info()->output_frame_count);
    CHECK(bin_at_seconds(*result.waveform, 0.2).absolute_peak[0] > 0.05F);
}

TEST_CASE("complete waveform analysis streams through the bounded decoded-block cache",
          "[unit][audio][waveform][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);

    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    constexpr std::uint32_t frames_per_bin = wormedit::audio::output_sample_rate / 20U;
    const auto result = wormedit::audio::analyze_waveform(decoder, blocks, waveforms,
                                                          fixture_source(), frames_per_bin);

    REQUIRE(result.status == wormedit::audio::waveform_analysis_status_t::generated);
    REQUIRE(result.waveform.has_value());
    const auto& waveform = *result.waveform;
    CHECK(waveform.sample_rate == wormedit::audio::output_sample_rate);
    CHECK(waveform.total_frames == decoder.info()->output_frame_count);
    CHECK(waveform.bins.size() == (waveform.total_frames + frames_per_bin - 1U) / frames_per_bin);
    CHECK(waveform.bins.back().frame_count > 0);
    CHECK(waveform.bins.back().frame_count <= frames_per_bin);
    REQUIRE_FALSE(waveform.coarser_levels.empty());
    CHECK(waveform.coarser_levels.back().bins.size() == 1);
    const auto zoom_level = wormedit::audio::waveform_level_for_zoom(
        waveform, static_cast<double>(frames_per_bin) * 2.0);
    REQUIRE(zoom_level.has_value());
    CHECK(zoom_level->frames_per_bin == frames_per_bin * 2U);
    CHECK(zoom_level->bins.size() == (waveform.bins.size() + 1U) / 2U);

    for (const auto tone_center : {0.20, 2.20, 4.20, 6.20, 8.20, 10.20}) {
        CAPTURE(tone_center);
        const auto& bin = bin_at_seconds(waveform, tone_center);
        CHECK(bin.absolute_peak[0] > 0.05F);
        CHECK(bin.absolute_peak[1] > 0.05F);
        CHECK(bin.rms[0] > 0.02F);
        CHECK(bin.rms[1] > 0.02F);
        CHECK(bin.minimum[0] < 0.0F);
        CHECK(bin.maximum[0] > 0.0F);
    }
    for (const auto silence_center : {1.0, 3.0, 5.0, 7.0, 9.0, 11.0}) {
        CAPTURE(silence_center);
        const auto& bin = bin_at_seconds(waveform, silence_center);
        CHECK(bin.absolute_peak[0] < 0.01F);
        CHECK(bin.absolute_peak[1] < 0.01F);
        CHECK(bin.rms[0] < 0.005F);
        CHECK(bin.rms[1] < 0.005F);
    }

    // The twelve-second source takes more than thirty decode blocks, but the
    // default per-asset PCM policy retains at most four after analysis.
    CHECK(blocks.stats().entries <= 4);
    CHECK(blocks.stats().cached_frames <= 65'536);
    CHECK(waveforms.stats().entries == 1);
}

TEST_CASE("waveform analysis cache is stable across later PCM seeks",
          "[unit][audio][waveform][cache][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    constexpr std::uint32_t frames_per_bin = wormedit::audio::output_sample_rate / 10U;

    const auto generated = wormedit::audio::analyze_waveform(decoder, blocks, waveforms,
                                                             fixture_source(), frames_per_bin);
    REQUIRE(generated.waveform.has_value());
    const auto expected = *generated.waveform;

    wormedit::audio::decode_session_t session;
    const auto token = session.start();
    REQUIRE(token.has_value());
    const wormedit::audio::audio_block_key_t sought{
        .source = fixture_source(),
        .block_index = 12,
    };
    const auto decoded =
        wormedit::audio::decode_and_cache_block(decoder, blocks, session, *token, sought);
    CHECK((decoded.status == wormedit::audio::block_decode_status_t::decoded ||
           decoded.status == wormedit::audio::block_decode_status_t::cache_hit));

    const auto cached = wormedit::audio::analyze_waveform(decoder, blocks, waveforms,
                                                          fixture_source(), frames_per_bin);
    REQUIRE(cached.status == wormedit::audio::waveform_analysis_status_t::cache_hit);
    REQUIRE(cached.waveform.has_value());
    CHECK(*cached.waveform == expected);
    CHECK(waveforms.stats().hits == 1);

    const auto coarser = wormedit::audio::waveform_level_for_zoom(
        *cached.waveform, static_cast<double>(frames_per_bin) * 4.0);
    REQUIRE(coarser.has_value());
    CHECK(coarser->frames_per_bin == frames_per_bin * 4U);
    CHECK(coarser->bins.size() < expected.bins.size());
    CHECK(waveforms.stats().entries == 1);
}

TEST_CASE("concurrent waveform levels share one decoder without interleaving seeks",
          "[unit][audio][waveform][concurrency][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t fine_cache;
    wormedit::audio::waveform_cache_t coarse_cache;

    auto fine_job = std::async(std::launch::async, [&] {
        return wormedit::audio::analyze_waveform(decoder, blocks, fine_cache, fixture_source(),
                                                 wormedit::audio::output_sample_rate / 40U);
    });
    auto coarse_job = std::async(std::launch::async, [&] {
        return wormedit::audio::analyze_waveform(decoder, blocks, coarse_cache, fixture_source(),
                                                 wormedit::audio::output_sample_rate / 10U);
    });
    const auto fine = fine_job.get();
    const auto coarse = coarse_job.get();
    REQUIRE(fine.status == wormedit::audio::waveform_analysis_status_t::generated);
    REQUIRE(coarse.status == wormedit::audio::waveform_analysis_status_t::generated);
    REQUIRE(fine.waveform.has_value());
    REQUIRE(coarse.waveform.has_value());
    CHECK(fine.waveform->total_frames == coarse.waveform->total_frames);
    CHECK(bin_at_seconds(*fine.waveform, 0.2).rms[0] > 0.02F);
    CHECK(bin_at_seconds(*fine.waveform, 1.0).rms[0] < 0.005F);
    CHECK(bin_at_seconds(*coarse.waveform, 2.2).rms[1] > 0.02F);
    CHECK(bin_at_seconds(*coarse.waveform, 3.0).rms[1] < 0.005F);
}

TEST_CASE("parallel waveform pyramid construction is bitwise deterministic",
          "[unit][audio][waveform][parallel][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t serial_cache;
    wormedit::audio::waveform_cache_t parallel_cache;
    constexpr std::uint32_t frames_per_bin = 67;

    const auto serial = wormedit::audio::analyze_waveform(
        decoder, blocks, serial_cache, fixture_source(), frames_per_bin,
        {.parallel_minimum_bins = 1'000'000, .maximum_parallel_workers = 4});
    const auto parallel = wormedit::audio::analyze_waveform(
        decoder, blocks, parallel_cache, fixture_source(), frames_per_bin,
        {.parallel_minimum_bins = 1, .maximum_parallel_workers = 4});

    REQUIRE(serial.status == wormedit::audio::waveform_analysis_status_t::generated);
    REQUIRE(parallel.status == wormedit::audio::waveform_analysis_status_t::generated);
    REQUIRE(serial.waveform.has_value());
    REQUIRE(parallel.waveform.has_value());
    CHECK(*parallel.waveform == *serial.waveform);
    CHECK(parallel.waveform->bins.back().frame_count < frames_per_bin);
    REQUIRE_FALSE(parallel.waveform->coarser_levels.empty());
    CHECK(parallel.waveform->coarser_levels.back().bins.size() == 1);
}

TEST_CASE("waveform analysis rejects invalid or unbounded requests deterministically",
          "[unit][audio][waveform]") {
    wormedit::audio::decoder_t decoder;
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    CHECK(
        wormedit::audio::analyze_waveform(decoder, blocks, waveforms, fixture_source(), 0).status ==
        wormedit::audio::waveform_analysis_status_t::invalid_request);
    CHECK(
        wormedit::audio::analyze_waveform(decoder, blocks, waveforms, {"", "source"}, 128).status ==
        wormedit::audio::waveform_analysis_status_t::invalid_request);
    CHECK(wormedit::audio::analyze_waveform(decoder, blocks, waveforms, fixture_source(), 128)
              .status == wormedit::audio::waveform_analysis_status_t::decoder_unavailable);

    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);
    wormedit::audio::waveform_cache_t one_bin_only({
        .maximum_entries = 1,
        .maximum_bytes = sizeof(wormedit::audio::waveform_bin_t),
        .maximum_bins_per_entry = 1,
    });
    CHECK(wormedit::audio::analyze_waveform(decoder, blocks, one_bin_only, fixture_source(), 1)
              .status == wormedit::audio::waveform_analysis_status_t::source_too_large);
    CHECK_FALSE(
        wormedit::audio::waveform_level_for_zoom(wormedit::audio::waveform_t{}, 1.0).has_value());
}

TEST_CASE("waveform projection emits capped exact-frame pixel columns",
          "[unit][audio][waveform][projection]") {
    using namespace wormedit::audio;
    std::vector<float> samples;
    for (int frame = 0; frame < 16; ++frame) {
        samples.push_back(static_cast<float>(frame));
        samples.push_back(static_cast<float>(-frame));
    }
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(samples));
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    const auto analyzed =
        analyze_waveform(decoder, blocks, waveforms, {"generated:projection", "pcm:v1"}, 1);
    REQUIRE(analyzed.waveform.has_value());

    const auto projected = project_waveform(
        *analyzed.waveform,
        {.begin_frame = 0, .end_frame = 16, .pixel_width = 4, .maximum_columns = 4});
    REQUIRE(projected);
    CHECK(projected.level_frames_per_bin == 4);
    REQUIRE(projected.columns.size() == 4);
    for (std::size_t index = 0; index < projected.columns.size(); ++index) {
        const auto& column = projected.columns[index];
        CHECK(column.begin_frame == index * 4);
        CHECK(column.end_frame == (index + 1) * 4);
        CHECK(column.minimum[0] == static_cast<float>(index * 4));
        CHECK(column.maximum[0] == static_cast<float>(index * 4 + 3));
        CHECK(column.minimum[1] == -static_cast<float>(index * 4 + 3));
        CHECK(column.maximum[1] == -static_cast<float>(index * 4));
    }

    const auto sample_columns = project_waveform(
        *analyzed.waveform,
        {.begin_frame = 4, .end_frame = 7, .pixel_width = 12, .maximum_columns = 12});
    REQUIRE(sample_columns);
    CHECK(sample_columns.columns.size() == 3);
    CHECK(sample_columns.level_frames_per_bin == 1);
    CHECK(sample_columns.columns.front().begin_frame == 4);
    CHECK(sample_columns.columns.back().end_frame == 7);

    CHECK(project_waveform(
              *analyzed.waveform,
              {.begin_frame = 0, .end_frame = 16, .pixel_width = 5, .maximum_columns = 4})
              .status == waveform_projection_status_t::capacity_exceeded);
    CHECK(project_waveform(
              *analyzed.waveform,
              {.begin_frame = 16, .end_frame = 16, .pixel_width = 1, .maximum_columns = 1})
              .status == waveform_projection_status_t::invalid_window);
}

TEST_CASE("waveform analysis cancellation publishes no partial cache entry",
          "[unit][audio][waveform][cancellation]") {
    using namespace wormedit::audio;
    std::vector<float> samples(64 * output_channel_count, 0.5F);
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(samples));
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    std::stop_source cancellation;
    cancellation.request_stop();

    const auto result =
        analyze_waveform(decoder, blocks, waveforms, {"generated:cancellation", "pcm:v1"}, 4, {},
                         cancellation.get_token());
    CHECK(result.status == waveform_analysis_status_t::cancelled);
    CHECK_FALSE(result.waveform.has_value());
    CHECK(waveforms.stats().entries == 0);
    CHECK(blocks.stats().entries == 0);
}

namespace {
using namespace wormedit::audio;

auto detail_pcm() -> std::vector<float> {
    std::vector<float> samples(2048 * output_channel_count, 0.0f);
    for (std::size_t frame = 512; frame < 544; ++frame) {
        samples[frame * 2] = 0.75f;
        samples[frame * 2 + 1] = -0.5f;
    }
    return samples;
}

void check_fine_geometry(const waveform_analysis_result_t& fine) {
    REQUIRE(fine.waveform);
    CHECK(fine.waveform->bins.size() == 8);
    CHECK(fine.waveform->total_frames == 2048);
}

void check_fine_values(const waveform_projection_result_t& projected) {
    REQUIRE(projected);
    CHECK(projected.level_frames_per_bin == 32);
    CHECK(projected.columns.front().minimum[0] == 0.75f);
    CHECK(projected.columns.front().maximum[1] == -0.5f);
}

void check_fine_bounds(const waveform_t& waveform, const waveform_projection_result_t& projected) {
    CHECK(projected.columns[8].maximum[0] == 0.0f);
    CHECK(projected.columns.front().begin_frame == 512);
    CHECK(project_waveform(waveform,
        {.begin_frame = 0, .end_frame = 256, .pixel_width = 64}).status ==
        waveform_projection_status_t::invalid_window);
}

void check_other_range(decoder_t& decoder, decoded_block_cache_t& blocks, waveform_cache_t& cache,
                       const audio_source_identity_t& source) {
    const auto other = analyze_waveform(decoder, blocks, cache, source, 32,
                                       {.begin_frame = 768, .end_frame = 1024});
    REQUIRE(other.waveform);
    CHECK(other.status == waveform_analysis_status_t::generated);
    CHECK(cache.stats().entries == 3);
}

void check_cancelled_range(decoder_t& decoder, decoded_block_cache_t& blocks, waveform_cache_t& cache,
                           const audio_source_identity_t& source) {
    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(analyze_waveform(decoder, blocks, cache, source, 32,
          {.begin_frame = 1024, .end_frame = 1280}, cancelled.get_token()).status ==
          waveform_analysis_status_t::cancelled);
    CHECK(cache.stats().entries == 3);
}

void check_signed_points(const std::vector<waveform_sample_t>& samples) {
    REQUIRE(samples.size() == 2);
    CHECK(samples.front().frame == 1);
    CHECK(samples.front().samples[0] == -0.75f);
    CHECK(samples.front().samples[1] == 0.125f);
}

void check_missing_points(decoded_block_cache_t& blocks, const audio_source_identity_t& source) {
    CHECK_FALSE(project_cached_waveform_samples(blocks, source, 1, 4));
    CHECK_FALSE(project_cached_waveform_samples(blocks, {"generated:samples", "pcm:v2"}, 1, 3));
    CHECK_FALSE(project_cached_waveform_samples(blocks, source, 0, 3, 2));
    blocks.clear();
    CHECK_FALSE(project_cached_waveform_samples(blocks, source, 1, 3));
}

auto detail_timing() -> wormedit::timing_index_t {
    wormedit::timing_index_t timing;
    timing.divisions_per_standard_measure = 960;
    timing.total_rows = 960;
    timing.rows = {{.absolute_row = 0, .before_delays_seconds = {0, 1},
        .event_seconds = {0, 1}, .after_stops_seconds = {0, 1}, .effective_bpm = {120, 1}},
        {.absolute_row = 960, .before_delays_seconds = {2, 1}, .event_seconds = {2, 1},
         .after_stops_seconds = {2, 1}, .effective_bpm = {120, 1}}};
    timing.segments = {{.begin_row = 0, .end_row = 960, .begin_seconds = {0, 1},
        .end_seconds = {2, 1}, .effective_bpm = {120, 1}}};
    return timing;
}

void check_detail_window(const waveform_t& waveform, const wormedit::timing_index_t& timing,
                         aligned_waveform_request_t request) {
    const auto window = aligned_waveform_window(waveform, timing, request);
    REQUIRE(window);
    CHECK(window->end_frame - window->begin_frame <= 5);
    CHECK(window->frames_per_pixel < 1.0);
}

void check_aligned_samples(const aligned_waveform_result_t& projected) {
    REQUIRE(projected);
    REQUIRE_FALSE(projected.rows.empty());
    CHECK(projected.rows.front().sample_point);
    CHECK(projected.rows.front().samples[0] == -0.75f);
}

void check_aligned_sample_bounds(const aligned_waveform_result_t& projected) {
    CHECK(projected.rows.front().samples[1] == 0.25f);
    CHECK(projected.rows.front().y_pixels >= 0.0);
    CHECK(projected.rows.back().y_pixels < 8.0);
}

void check_aligned_fallback(const waveform_t& waveform, const wormedit::timing_index_t& timing,
                            aligned_waveform_request_t request, decoded_block_cache_t& blocks) {
    blocks.clear();
    CHECK_FALSE(project_aligned_waveform_samples(waveform, timing, request, blocks));
    CHECK(project_aligned_waveform(waveform, timing, request));
}
} // namespace

TEST_CASE("visible fine waveform preserves frame coordinates and isolates range cache keys",
          "[unit][audio][waveform][detail]") {
    using namespace wormedit::audio;
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(detail_pcm()));
    decoded_block_cache_t blocks;
    waveform_cache_t cache;
    const audio_source_identity_t source{"generated:detail", "pcm:v1"};
    REQUIRE(analyze_waveform(decoder, blocks, cache, source, 256).waveform);
    const auto fine = analyze_waveform(decoder, blocks, cache, source, 32,
                                      {.begin_frame = 512, .end_frame = 768});
    check_fine_geometry(fine);
    const auto projected = project_waveform(*fine.waveform,
        {.begin_frame = 512, .end_frame = 768, .pixel_width = 64});
    check_fine_values(projected);
    check_fine_bounds(*fine.waveform, projected);
    check_other_range(decoder, blocks, cache, source);
    check_cancelled_range(decoder, blocks, cache, source);
}

TEST_CASE("sample waveform projection reads signed visible PCM and falls back on cache misses",
          "[unit][audio][waveform][detail]") {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    const audio_source_identity_t source{"generated:samples", "pcm:v1"};
    const std::vector<float> pcm{0.25f, -0.5f, -0.75f, 0.125f, 0.5f, -0.25f};
    REQUIRE(blocks.store({source, 0}, 3, pcm) == block_store_status_t::stored);
    const auto samples = project_cached_waveform_samples(blocks, source, 1, 3);
    REQUIRE(samples);
    check_signed_points(*samples);
    check_missing_points(blocks, source);
}

TEST_CASE("aligned high zoom uses fractional musical rows and signed sample points",
          "[unit][audio][waveform][detail]") {
    using namespace wormedit::audio;
    std::vector<float> pcm(64 * 2, 0.0f);
    pcm[2] = -0.75f;
    pcm[3] = 0.25f;
    decoder_t decoder;
    REQUIRE(decoder.open_pcm(pcm));
    decoded_block_cache_t blocks;
    waveform_cache_t cache;
    const auto analysis = analyze_waveform(decoder, blocks, cache,
        {"generated:aligned-detail", "pcm:v1"}, 32);
    REQUIRE(analysis.waveform);
    const auto timing = detail_timing();
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 2,
        .viewport_height_pixels = 8, .pixels_per_standard_measure = 176400};
    check_detail_window(*analysis.waveform, timing, request);
    const auto projected = project_aligned_waveform_samples(*analysis.waveform, timing, request, blocks);
    check_aligned_samples(projected);
    check_aligned_sample_bounds(projected);
    check_aligned_fallback(*analysis.waveform, timing, request, blocks);
}

TEST_CASE("short sources and clipped chart ends retain the actual waveform zoom scale",
          "[unit][audio][waveform][detail]") {
    using namespace wormedit::audio;
    waveform_t waveform;
    waveform.sample_rate = output_sample_rate;
    waveform.total_frames = 64;
    const auto timing = detail_timing();
    const aligned_waveform_request_t coarse{.viewport_height_pixels = 800,
        .pixels_per_standard_measure = 192};
    const auto window = aligned_waveform_window(waveform, timing, coarse);
    REQUIRE(window);
    CHECK(window->end_frame == 64);
    CHECK(window->frames_per_pixel > 400.0);
    decoded_block_cache_t blocks;
    CHECK_FALSE(project_aligned_waveform_samples(waveform, timing, coarse, blocks));
}
