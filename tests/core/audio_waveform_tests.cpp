#include <wormedit/audio/waveform.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <future>
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
