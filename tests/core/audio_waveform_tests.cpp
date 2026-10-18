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

TEST_CASE("waveform expected errors retain generated data when cache publication fails",
          "[unit][audio][waveform][result]") {
    using namespace wormedit::audio;
    decoder_t decoder;
    const std::array<float, 16> samples{};
    REQUIRE(decoder.open_pcm(samples));
    decoded_block_cache_t blocks;
    waveform_cache_t cache{waveform_cache_limits_t{.maximum_entries = 0}};
    auto outcome = waveform_analysis_t::from(decoder, blocks, cache,
        {"generated:uncached-outcome", "pcm:v1"}, 4);
    REQUIRE_FALSE(outcome.has_value());
    CHECK(outcome.error().code == wormedit::failure_code_t::allocation_failed);
    REQUIRE(waveform_analysis_t::find_waveform(outcome));
    const auto expected_waveform = outcome.error().waveform;
    const auto legacy = waveform_analysis_result_t::from(std::move(outcome));
    CHECK(legacy.status == wormedit::failure_code_t::allocation_failed);
    CHECK(legacy.waveform == expected_waveform);
}

TEST_CASE("canonical waveform projection reports invalid geometry",
          "[unit][audio][waveform][result]") {
    using namespace wormedit::audio;
    const auto outcome = waveform_view_t::from({}, waveform_projection_request_t{});
    REQUIRE_FALSE(outcome.has_value());
    CHECK(outcome.error() == wormedit::failure_code_t::invalid_data);
}

TEST_CASE("MP3 project audio produces a complete coarse waveform", "[audio][waveform][fixture]") {
    wormedit::audio::decoder_t decoder;
    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.mp3"), &error));
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    const auto result = wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms,
                                                        fixture_source(), 256);
    CAPTURE(result ? wormedit::operation_state_t{result->status}
                   : wormedit::operation_state_t<wormedit::audio::waveform_analysis_phase_t>{result.error().code});
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(result));
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(result)->total_frames == decoder.info()->output_frame_count);
    CHECK(bin_at_seconds(*wormedit::audio::waveform_analysis_t::find_waveform(result), 0.2).absolute_peak[0] > 0.05F);
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
    const auto result = wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms,
                                                          fixture_source(), frames_per_bin);

    REQUIRE(result.value().status == wormedit::audio::waveform_analysis_phase_t::generated);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(result) != nullptr);
    const auto& waveform = *wormedit::audio::waveform_analysis_t::find_waveform(result);
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

    const auto generated = wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms,
                                                             fixture_source(), frames_per_bin);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(generated) != nullptr);
    const auto expected = *wormedit::audio::waveform_analysis_t::find_waveform(generated);

    wormedit::audio::decode_session_t session;
    const auto token = session.start();
    REQUIRE(token.has_value());
    const wormedit::audio::audio_block_key_t sought{
        .source = fixture_source(),
        .block_index = 12,
    };
    const auto decoded =
        wormedit::audio::decode_and_cache_block(decoder, blocks, session, *token, sought);
    CHECK((decoded.status == wormedit::audio::block_decode_phase_t::decoded ||
           decoded.status == wormedit::audio::block_decode_phase_t::cache_hit));

    const auto cached = wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms,
                                                          fixture_source(), frames_per_bin);
    REQUIRE(cached.value().status == wormedit::audio::waveform_analysis_phase_t::cache_hit);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(cached) != nullptr);
    CHECK(*wormedit::audio::waveform_analysis_t::find_waveform(cached) == expected);
    CHECK(waveforms.stats().hits == 1);

    const auto coarser = wormedit::audio::waveform_level_for_zoom(
        *wormedit::audio::waveform_analysis_t::find_waveform(cached), static_cast<double>(frames_per_bin) * 4.0);
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
        return wormedit::audio::waveform_analysis_t::from(decoder, blocks, fine_cache, fixture_source(),
                                                 wormedit::audio::output_sample_rate / 40U);
    });
    auto coarse_job = std::async(std::launch::async, [&] {
        return wormedit::audio::waveform_analysis_t::from(decoder, blocks, coarse_cache, fixture_source(),
                                                 wormedit::audio::output_sample_rate / 10U);
    });
    const auto fine = fine_job.get();
    const auto coarse = coarse_job.get();
    REQUIRE(fine.value().status == wormedit::audio::waveform_analysis_phase_t::generated);
    REQUIRE(coarse.value().status == wormedit::audio::waveform_analysis_phase_t::generated);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(fine) != nullptr);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(coarse) != nullptr);
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(fine)->total_frames == wormedit::audio::waveform_analysis_t::find_waveform(coarse)->total_frames);
    CHECK(bin_at_seconds(*wormedit::audio::waveform_analysis_t::find_waveform(fine), 0.2).rms[0] > 0.02F);
    CHECK(bin_at_seconds(*wormedit::audio::waveform_analysis_t::find_waveform(fine), 1.0).rms[0] < 0.005F);
    CHECK(bin_at_seconds(*wormedit::audio::waveform_analysis_t::find_waveform(coarse), 2.2).rms[1] > 0.02F);
    CHECK(bin_at_seconds(*wormedit::audio::waveform_analysis_t::find_waveform(coarse), 3.0).rms[1] < 0.005F);
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

    const auto serial = wormedit::audio::waveform_analysis_t::from(
        decoder, blocks, serial_cache, fixture_source(), frames_per_bin,
        {.parallel_minimum_bins = 1'000'000, .maximum_parallel_workers = 4});
    const auto parallel = wormedit::audio::waveform_analysis_t::from(
        decoder, blocks, parallel_cache, fixture_source(), frames_per_bin,
        {.parallel_minimum_bins = 1, .maximum_parallel_workers = 4});

    REQUIRE(serial.value().status == wormedit::audio::waveform_analysis_phase_t::generated);
    REQUIRE(parallel.value().status == wormedit::audio::waveform_analysis_phase_t::generated);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(serial) != nullptr);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(parallel) != nullptr);
    CHECK(*wormedit::audio::waveform_analysis_t::find_waveform(parallel) == *wormedit::audio::waveform_analysis_t::find_waveform(serial));
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(parallel)->bins.back().frame_count < frames_per_bin);
    REQUIRE_FALSE(wormedit::audio::waveform_analysis_t::find_waveform(parallel)->coarser_levels.empty());
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(parallel)->coarser_levels.back().bins.size() == 1);
}

TEST_CASE("waveform analysis rejects invalid or unbounded requests deterministically",
          "[unit][audio][waveform]") {
    wormedit::audio::decoder_t decoder;
    wormedit::audio::decoded_block_cache_t blocks;
    wormedit::audio::waveform_cache_t waveforms;
    CHECK(
        wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms, fixture_source(), 0).error().code ==
        wormedit::failure_code_t::invalid_request);
    CHECK(
        wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms, {"", "source"}, 128).error().code ==
        wormedit::failure_code_t::invalid_request);
    CHECK(wormedit::audio::waveform_analysis_t::from(decoder, blocks, waveforms, fixture_source(), 128).error().code == wormedit::failure_code_t::unavailable);

    std::string error;
    REQUIRE(decoder.open(fixture_path("sliced-seek-source.ogg"), &error));
    INFO("decoder open error: " << error);
    wormedit::audio::waveform_cache_t one_bin_only({
        .maximum_entries = 1,
        .maximum_bytes = sizeof(wormedit::audio::waveform_bin_t),
        .maximum_bins_per_entry = 1,
    });
    CHECK(wormedit::audio::waveform_analysis_t::from(decoder, blocks, one_bin_only, fixture_source(), 1).error().code == wormedit::failure_code_t::capacity_exceeded);
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
        waveform_analysis_t::from(decoder, blocks, waveforms, {"generated:projection", "pcm:v1"}, 1);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(analyzed) != nullptr);

    const auto projected = waveform_view_t::from(
        *wormedit::audio::waveform_analysis_t::find_waveform(analyzed),
        {.begin_frame = 0, .end_frame = 16, .pixel_width = 4, .maximum_columns = 4});
    REQUIRE(projected);
    CHECK(projected->level_frames_per_bin == 4);
    REQUIRE(projected->columns.size() == 4);
    for (std::size_t index = 0; index < projected->columns.size(); ++index) {
        const auto& column = projected->columns[index];
        CHECK(column.begin_frame == index * 4);
        CHECK(column.end_frame == (index + 1) * 4);
        CHECK(column.minimum[0] == static_cast<float>(index * 4));
        CHECK(column.maximum[0] == static_cast<float>(index * 4 + 3));
        CHECK(column.minimum[1] == -static_cast<float>(index * 4 + 3));
        CHECK(column.maximum[1] == -static_cast<float>(index * 4));
    }

    const auto sample_columns = waveform_view_t::from(
        *wormedit::audio::waveform_analysis_t::find_waveform(analyzed),
        {.begin_frame = 4, .end_frame = 7, .pixel_width = 12, .maximum_columns = 12});
    REQUIRE(sample_columns);
    CHECK(sample_columns->columns.size() == 3);
    CHECK(sample_columns->level_frames_per_bin == 1);
    CHECK(sample_columns->columns.front().begin_frame == 4);
    CHECK(sample_columns->columns.back().end_frame == 7);

    CHECK(waveform_view_t::from(
              *wormedit::audio::waveform_analysis_t::find_waveform(analyzed),
              {.begin_frame = 0, .end_frame = 16, .pixel_width = 5, .maximum_columns = 4}).error() == wormedit::failure_code_t::capacity_exceeded);
    CHECK(waveform_view_t::from(
              *wormedit::audio::waveform_analysis_t::find_waveform(analyzed),
              {.begin_frame = 16, .end_frame = 16, .pixel_width = 1, .maximum_columns = 1}).error() == wormedit::failure_code_t::invalid_request);
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
        waveform_analysis_t::from(decoder, blocks, waveforms, {"generated:cancellation", "pcm:v1"}, 4, {},
                         cancellation.get_token());
    CHECK(result.error().code == wormedit::failure_code_t::cancelled);
    CHECK_FALSE(wormedit::audio::waveform_analysis_t::find_waveform(result) != nullptr);
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

void check_fine_geometry(const waveform_analysis_outcome_t& fine) {
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(fine));
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(fine)->bins.size() == 8);
    CHECK(wormedit::audio::waveform_analysis_t::find_waveform(fine)->total_frames == 2048);
}

void check_fine_values(const waveform_view_outcome_t& projected) {
    REQUIRE(projected);
    CHECK(projected->level_frames_per_bin == 32);
    CHECK(projected->columns.front().minimum[0] == 0.75f);
    CHECK(projected->columns.front().maximum[1] == -0.5f);
}

void check_fine_bounds(const waveform_t& waveform, const waveform_view_outcome_t& projected) {
    CHECK(projected->columns[8].maximum[0] == 0.0f);
    CHECK(projected->columns.front().begin_frame == 512);
    CHECK(waveform_view_t::from(waveform,
        {.begin_frame = 0, .end_frame = 256, .pixel_width = 64}).error() ==
        wormedit::failure_code_t::invalid_request);
}

void check_other_range(decoder_t& decoder, decoded_block_cache_t& blocks, waveform_cache_t& cache,
                       const audio_source_identity_t& source) {
    const auto other = waveform_analysis_t::from(decoder, blocks, cache, source, 32,
                                       {.begin_frame = 768, .end_frame = 1024});
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(other));
    CHECK(other.value().status == waveform_analysis_phase_t::generated);
    CHECK(cache.stats().entries == 3);
}

void check_cancelled_range(decoder_t& decoder, decoded_block_cache_t& blocks, waveform_cache_t& cache,
                           const audio_source_identity_t& source) {
    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK(waveform_analysis_t::from(decoder, blocks, cache, source, 32,
          {.begin_frame = 1024, .end_frame = 1280}, cancelled.get_token()).error().code ==
          wormedit::failure_code_t::cancelled);
    CHECK(cache.stats().entries == 3);
}

void check_signed_points(const std::vector<waveform_sample_t>& samples) {
    REQUIRE(samples.size() == 2);
    CHECK(samples.front().frame == 1);
    CHECK(samples.front().samples[0] == -0.75f);
    CHECK(samples.front().samples[1] == 0.125f);
}

void check_missing_points(decoded_block_cache_t& blocks, const audio_source_identity_t& source) {
    CHECK_FALSE(build_cached_waveform_samples_view(blocks, source, 1, 4));
    CHECK_FALSE(build_cached_waveform_samples_view(blocks, {"generated:samples", "pcm:v2"}, 1, 3));
    CHECK_FALSE(build_cached_waveform_samples_view(blocks, source, 0, 3, 2));
    blocks.clear();
    CHECK_FALSE(build_cached_waveform_samples_view(blocks, source, 1, 3));
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
    // Prefetch covers the outward-rounded row interval, clipped to this short source.
    CHECK(window->begin_frame == 0);
    CHECK(window->end_frame == waveform.total_frames);
    CHECK(window->frames_per_pixel < 1.0);
}

void check_aligned_samples(const aligned_waveform_view_result_t& projected) {
    REQUIRE(!projected.status.has_failure());
    REQUIRE_FALSE(projected.rows.empty());
    CHECK(projected.rows.front().sample_point);
    CHECK(projected.rows.front().samples[0] == -0.75f);
}

void check_aligned_sample_bounds(const aligned_waveform_view_result_t& projected) {
    CHECK(projected.rows.front().samples[1] == 0.25f);
    CHECK(projected.rows.front().y_pixels >= 0.0);
    CHECK(projected.rows.back().y_pixels < 8.0);
}

void check_aligned_fallback(const waveform_t& waveform, const wormedit::timing_index_t& timing,
                            aligned_waveform_request_t request, decoded_block_cache_t& blocks) {
    blocks.clear();
    CHECK_FALSE(!(aligned_waveform_view_result_t::from(waveform, timing, request, blocks)).status.has_failure());
    CHECK(!(aligned_waveform_view_result_t::from(waveform, timing, request)).status.has_failure());
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
    REQUIRE(waveform_analysis_t::find_waveform(
        waveform_analysis_t::from(decoder, blocks, cache, source, 256)));
    const auto fine = waveform_analysis_t::from(decoder, blocks, cache, source, 32,
                                      {.begin_frame = 512, .end_frame = 768});
    check_fine_geometry(fine);
    const auto projected = waveform_view_t::from(*wormedit::audio::waveform_analysis_t::find_waveform(fine),
        {.begin_frame = 512, .end_frame = 768, .pixel_width = 64});
    check_fine_values(projected);
    check_fine_bounds(*wormedit::audio::waveform_analysis_t::find_waveform(fine), projected);
    check_other_range(decoder, blocks, cache, source);
    check_cancelled_range(decoder, blocks, cache, source);
}

TEST_CASE("sample waveform projection reads signed visible PCM and falls back on cache misses",
          "[unit][audio][waveform][detail]") {
    using namespace wormedit::audio;
    decoded_block_cache_t blocks;
    const audio_source_identity_t source{"generated:samples", "pcm:v1"};
    const std::vector<float> pcm{0.25f, -0.5f, -0.75f, 0.125f, 0.5f, -0.25f};
    REQUIRE(blocks.store({source, 0}, 3, pcm).has_value());
    const auto samples = build_cached_waveform_samples_view(blocks, source, 1, 3);
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
    const auto analysis = waveform_analysis_t::from(decoder, blocks, cache,
        {"generated:aligned-detail", "pcm:v1"}, 32);
    REQUIRE(wormedit::audio::waveform_analysis_t::find_waveform(analysis));
    const auto timing = detail_timing();
    const aligned_waveform_request_t request{.viewport_scroll_pixels = 2,
        .viewport_height_pixels = 8, .pixels_per_standard_measure = 176400};
    check_detail_window(*wormedit::audio::waveform_analysis_t::find_waveform(analysis), timing, request);
    const auto projected = aligned_waveform_view_result_t::from(*wormedit::audio::waveform_analysis_t::find_waveform(analysis), timing, request, blocks);
    check_aligned_samples(projected);
    check_aligned_sample_bounds(projected);
    check_aligned_fallback(*wormedit::audio::waveform_analysis_t::find_waveform(analysis), timing, request, blocks);
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
    CHECK_FALSE(!(aligned_waveform_view_result_t::from(waveform, timing, coarse, blocks)).status.has_failure());
}
