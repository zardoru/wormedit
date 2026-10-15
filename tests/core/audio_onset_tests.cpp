#include <wormedit/audio/onset.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace {

using namespace wormedit::audio;

auto make_click_track(std::uint64_t total_frames, std::span<const std::uint64_t> starts,
                      std::size_t channel = 0) -> std::vector<float> {
    std::vector<float> samples(static_cast<std::size_t>(total_frames) * output_channel_count);
    for (const auto start : starts) {
        for (std::uint64_t offset = 0; offset < 32 && start + offset < total_frames; ++offset) {
            const float polarity = (offset & 1U) == 0 ? 0.8F : -0.8F;
            const auto index = static_cast<std::size_t>(start + offset) * output_channel_count + channel;
            samples[index] = polarity;
        }
    }
    return samples;
}

auto analyze_pcm(std::span<const float> samples, std::uint32_t frames_per_bin)
    -> waveform_analysis_outcome_t {
    decoder_t decoder;
    if (!decoder.open_pcm(samples)) return std::unexpected(waveform_analysis_error_t{});
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    return waveform_analysis_t::from(decoder, blocks, waveforms,
                            {"generated:onset-tests", "pcm:v1"}, frames_per_bin);
}

auto request_for(std::uint64_t end_frame) -> onset_request_t {
    return {.begin_frame = 0,
            .end_frame = end_frame,
            .window_frames = 256,
            .minimum_rms = 0.08,
            .minimum_rms_rise = 0.05,
            .minimum_separation_frames = 512,
            .maximum_onsets = 32};
}

} // namespace

TEST_CASE("waveform onset detector finds generated clicks deterministically",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 2> clicks{512, 1'536};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    const auto request = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);

    const auto first = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);
    const auto second = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);
    REQUIRE(first);
    REQUIRE(second);
    CHECK((first->frames == std::vector<std::uint64_t>{512, 1536}));
    CHECK(second->frames == first->frames);
}

TEST_CASE("waveform onset detector returns no candidates for silence",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    const auto result = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames));

    REQUIRE(result);
    CHECK(result->frames.empty());
}

TEST_CASE("waveform onset detector sees a click in one stereo channel",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{512};
    const auto samples = make_click_track(1'024, clicks, 1);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto request = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    request.minimum_separation_frames = 256;
    const auto result = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);

    REQUIRE(result);
    CHECK((result->frames == std::vector<std::uint64_t>{512}));
}

TEST_CASE("waveform onset detector accepts a short source-tail window",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{1'288};
    const auto samples = make_click_track(1'344, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto request = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    request.minimum_separation_frames = 256;
    const auto result = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);

    REQUIRE(result);
    CHECK((result->frames == std::vector<std::uint64_t>{1'280}));
}

TEST_CASE("waveform onset detector honors source-zero windows in a subrange",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 2> clicks{256, 776};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto request = request_for(1'280);
    request.begin_frame = 640;
    request.minimum_separation_frames = 256;
    const auto result = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);

    REQUIRE(result);
    CHECK((result->frames == std::vector<std::uint64_t>{768}));
}

TEST_CASE("waveform onset detector discards earlier results on incomplete coverage",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 2> clicks{256, 1'280};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto waveform = *waveform_analysis_t::find_waveform(analyzed);
    waveform.key.end_frame = 1'024;
    waveform.bins.resize(16);
    const auto result = waveform_onset_index_t::from(waveform, request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames));

    REQUIRE_FALSE(result);
    CHECK(result.error() == onset_status_t::not_ready);
}

TEST_CASE("waveform onset detector requires cached context for its first baseline",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{768};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto waveform = *waveform_analysis_t::find_waveform(analyzed);
    waveform.key.begin_frame = 512;
    waveform.bins.erase(waveform.bins.begin(), waveform.bins.begin() + 8);
    auto request = request_for(1'280);
    request.begin_frame = 512;

    const auto result = waveform_onset_index_t::from(waveform, request);
    REQUIRE_FALSE(result);
    CHECK(result.error() == onset_status_t::not_ready);
}

TEST_CASE("waveform onset detector rejects malformed RMS bins",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto waveform = *waveform_analysis_t::find_waveform(analyzed);
    waveform.bins[0].rms[0] = std::numeric_limits<float>::quiet_NaN();
    const auto result = waveform_onset_index_t::from(waveform, request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames));

    REQUIRE_FALSE(result);
    CHECK(result.error() == onset_status_t::invalid_waveform);
}

TEST_CASE("waveform onset detector rejects invalid requests and oversized base bins",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    const auto& waveform = *waveform_analysis_t::find_waveform(analyzed);
    auto invalid = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    invalid.window_frames = 0;
    CHECK(waveform_onset_index_t::from(waveform, invalid).error() == onset_status_t::invalid_request);

    auto too_many = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    too_many.maximum_onsets = maximum_onset_frames + 1;
    CHECK(waveform_onset_index_t::from(waveform, too_many).error() == onset_status_t::invalid_request);

    const auto coarse = analyze_pcm(samples, 512);
    REQUIRE(waveform_analysis_t::find_waveform(coarse) != nullptr);
    CHECK(waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(coarse), request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames)).error() ==
          onset_status_t::not_ready);

    auto unaligned = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    unaligned.window_frames = 257;
    CHECK(waveform_onset_index_t::from(waveform, unaligned).error() == onset_status_t::not_ready);
}

TEST_CASE("waveform onset detector reports output capacity overflow without partial frames",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 3> clicks{256, 768, 1'280};
    const auto samples = make_click_track(1'536, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed) != nullptr);
    auto request = request_for(waveform_analysis_t::find_waveform(analyzed)->total_frames);
    request.maximum_onsets = 2;
    const auto result = waveform_onset_index_t::from(*waveform_analysis_t::find_waveform(analyzed), request);

    REQUIRE_FALSE(result);
    CHECK(result.error() == onset_status_t::capacity_exceeded);
}

TEST_CASE("waveform onset detector checks complete coverage before the output cap", "[audio][onset]") {
    constexpr std::array<std::uint64_t, 3> clicks{256, 768, 1'280};
    const auto analyzed = analyze_pcm(make_click_track(2'048, clicks), 64);
    REQUIRE(waveform_analysis_t::find_waveform(analyzed));
    auto waveform = *waveform_analysis_t::find_waveform(analyzed);
    waveform.key.end_frame = 1'024;
    waveform.bins.resize(16);
    auto request = request_for(waveform.total_frames);
    request.maximum_onsets = 1;
    const auto result = waveform_onset_index_t::from(waveform, request);
    REQUIRE_FALSE(result);
    CHECK(result.error() == onset_status_t::not_ready);
}

TEST_CASE("canonical onset construction reports the legacy invalid request code",
          "[unit][audio][onset][result]") {
    waveform_t waveform;
    const auto detected = waveform_onset_index_t::from(waveform, {});
    REQUIRE_FALSE(detected.has_value());
    CHECK(detected.error() == onset_status_t::invalid_request);
}
