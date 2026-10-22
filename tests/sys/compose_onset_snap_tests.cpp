#include <wormedit/audio/waveform.h>
#include "sys/compose/compose_onset_snap.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace wormedit::audio;
using namespace wormedit::sys;

auto click_track(std::uint64_t frame_count, std::span<const std::uint64_t> starts,
                 std::size_t channel = 0) -> std::vector<float> {
    std::vector<float> samples(static_cast<std::size_t>(frame_count) * output_channel_count);
    for (const auto start : starts) {
        for (std::uint64_t offset = 0; offset < 32 && start + offset < frame_count; ++offset) {
            const float sample = (offset & 1U) == 0 ? 0.8F : -0.8F;
            samples[static_cast<std::size_t>(start + offset) * output_channel_count + channel] = sample;
        }
    }
    return samples;
}

auto analyzed(std::span<const float> samples, std::uint32_t frames_per_bin = 64)
    -> waveform_analysis_outcome_t {
    decoder_t decoder;
    if (!decoder.open_pcm(samples)) return std::unexpected(waveform_analysis_error_t{});
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    return waveform_analysis_t::from(decoder, blocks, waveforms, {"generated:compose-onset", "pcm:v1"},
                            frames_per_bin);
}

} // namespace

TEST_CASE("compose onset capture returns exact cached source frames in its visible range",
          "[sys][compose-onset]") {
    constexpr std::uint64_t bin_frames = 64;
    const auto sample_rate = static_cast<std::uint64_t>(output_sample_rate);
    const auto minimum_window = std::max(bin_frames, sample_rate / 100);
    const auto window_frames = ((minimum_window + bin_frames - 1) / bin_frames) * bin_frames;
    const auto expected_frame = window_frames * 2;
    const std::array<std::uint64_t, 1> clicks{expected_frame};
    const auto samples = click_track(window_frames * 6, clicks, 1);
    const auto waveform = analyzed(samples);
    REQUIRE(waveform_analysis_t::find_waveform(waveform));

    const auto captured = capture_compose_onsets(*waveform_analysis_t::find_waveform(waveform), window_frames, window_frames * 3);
    CHECK((captured.frames == std::vector<std::uint64_t>{expected_frame}));
}

TEST_CASE("compose onset capture returns empty for silence, invalid ranges, and unfinished coverage",
          "[sys][compose-onset]") {
    const auto samples = click_track(2'048, {});
    const auto waveform = analyzed(samples);
    REQUIRE(waveform_analysis_t::find_waveform(waveform));
    CHECK(capture_compose_onsets(*waveform_analysis_t::find_waveform(waveform), 0, 2'048).frames.empty());
    CHECK(capture_compose_onsets(*waveform_analysis_t::find_waveform(waveform), 512, 512).frames.empty());

    auto unfinished = *waveform_analysis_t::find_waveform(waveform);
    unfinished.key.end_frame = 512;
    unfinished.bins.resize(8);
    CHECK(capture_compose_onsets(unfinished, 0, 2'048).frames.empty());
}

TEST_CASE("nearest compose onset accepts the 30 ms edge and breaks ties earlier",
          "[sys][compose-onset]") {
    const compose_onset_snap_t captured{{100'000, 103'000}};
    CHECK(nearest_compose_onset(captured, 101'500, 100'000) == 100'000);
    CHECK(nearest_compose_onset(captured, 103'000, 100'000) == 103'000);
    CHECK(nearest_compose_onset(captured, 106'000, 100'000) == 103'000);
    CHECK_FALSE(nearest_compose_onset(captured, 106'001, 100'000));
}

TEST_CASE("nearest compose onset handles wide unsigned distances and a zero sample rate",
          "[sys][compose-onset]") {
    const compose_onset_snap_t captured{{std::numeric_limits<std::uint64_t>::max()}};
    CHECK(nearest_compose_onset(captured, std::numeric_limits<std::uint64_t>::max(), 48'000) ==
          std::numeric_limits<std::uint64_t>::max());
    CHECK_FALSE(nearest_compose_onset(captured, 10, 0));
    CHECK_FALSE(nearest_compose_onset(captured, 10, 48'000));
}
