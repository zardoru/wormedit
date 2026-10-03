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
    -> waveform_analysis_result_t {
    decoder_t decoder;
    if (!decoder.open_pcm(samples)) return {};
    decoded_block_cache_t blocks;
    waveform_cache_t waveforms;
    return analyze_waveform(decoder, blocks, waveforms,
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
    REQUIRE(analyzed.waveform.has_value());
    const auto request = request_for(analyzed.waveform->total_frames);

    const auto first = detect_waveform_onsets(*analyzed.waveform, request);
    const auto second = detect_waveform_onsets(*analyzed.waveform, request);
    REQUIRE(first.status == onset_status_t::detected);
    CHECK((first.frames == std::vector<std::uint64_t>{512, 1536}));
    CHECK(second.frames == first.frames);
}

TEST_CASE("waveform onset detector returns no candidates for silence",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    const auto result = detect_waveform_onsets(*analyzed.waveform, request_for(analyzed.waveform->total_frames));

    CHECK(result.status == onset_status_t::detected);
    CHECK(result.frames.empty());
}

TEST_CASE("waveform onset detector sees a click in one stereo channel",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{512};
    const auto samples = make_click_track(1'024, clicks, 1);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto request = request_for(analyzed.waveform->total_frames);
    request.minimum_separation_frames = 256;
    const auto result = detect_waveform_onsets(*analyzed.waveform, request);

    REQUIRE(result.status == onset_status_t::detected);
    CHECK((result.frames == std::vector<std::uint64_t>{512}));
}

TEST_CASE("waveform onset detector accepts a short source-tail window",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{1'288};
    const auto samples = make_click_track(1'344, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto request = request_for(analyzed.waveform->total_frames);
    request.minimum_separation_frames = 256;
    const auto result = detect_waveform_onsets(*analyzed.waveform, request);

    REQUIRE(result.status == onset_status_t::detected);
    CHECK((result.frames == std::vector<std::uint64_t>{1'280}));
}

TEST_CASE("waveform onset detector honors source-zero windows in a subrange",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 2> clicks{256, 776};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto request = request_for(1'280);
    request.begin_frame = 640;
    request.minimum_separation_frames = 256;
    const auto result = detect_waveform_onsets(*analyzed.waveform, request);

    REQUIRE(result.status == onset_status_t::detected);
    CHECK((result.frames == std::vector<std::uint64_t>{768}));
}

TEST_CASE("waveform onset detector discards earlier results on incomplete coverage",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 2> clicks{256, 1'280};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto waveform = *analyzed.waveform;
    waveform.key.end_frame = 1'024;
    waveform.bins.resize(16);
    const auto result = detect_waveform_onsets(waveform, request_for(analyzed.waveform->total_frames));

    CHECK(result.status == onset_status_t::not_ready);
    CHECK(result.frames.empty());
}

TEST_CASE("waveform onset detector requires cached context for its first baseline",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 1> clicks{768};
    const auto samples = make_click_track(2'048, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto waveform = *analyzed.waveform;
    waveform.key.begin_frame = 512;
    waveform.bins.erase(waveform.bins.begin(), waveform.bins.begin() + 8);
    auto request = request_for(1'280);
    request.begin_frame = 512;

    const auto result = detect_waveform_onsets(waveform, request);
    CHECK(result.status == onset_status_t::not_ready);
    CHECK(result.frames.empty());
}

TEST_CASE("waveform onset detector rejects malformed RMS bins",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto waveform = *analyzed.waveform;
    waveform.bins[0].rms[0] = std::numeric_limits<float>::quiet_NaN();
    const auto result = detect_waveform_onsets(waveform, request_for(analyzed.waveform->total_frames));

    CHECK(result.status == onset_status_t::invalid_waveform);
    CHECK(result.frames.empty());
}

TEST_CASE("waveform onset detector rejects invalid requests and oversized base bins",
    "[audio][onset]") {
    const auto samples = make_click_track(1'024, {});
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    const auto& waveform = *analyzed.waveform;
    auto invalid = request_for(analyzed.waveform->total_frames);
    invalid.window_frames = 0;
    CHECK(detect_waveform_onsets(waveform, invalid).status == onset_status_t::invalid_request);

    auto too_many = request_for(analyzed.waveform->total_frames);
    too_many.maximum_onsets = maximum_onset_frames + 1;
    CHECK(detect_waveform_onsets(waveform, too_many).status == onset_status_t::invalid_request);

    const auto coarse = analyze_pcm(samples, 512);
    REQUIRE(coarse.waveform.has_value());
    CHECK(detect_waveform_onsets(*coarse.waveform, request_for(analyzed.waveform->total_frames)).status ==
          onset_status_t::not_ready);

    auto unaligned = request_for(analyzed.waveform->total_frames);
    unaligned.window_frames = 257;
    CHECK(detect_waveform_onsets(waveform, unaligned).status == onset_status_t::not_ready);
}

TEST_CASE("waveform onset detector reports output capacity overflow without partial frames",
          "[audio][onset]") {
    constexpr std::array<std::uint64_t, 3> clicks{256, 768, 1'280};
    const auto samples = make_click_track(1'536, clicks);
    const auto analyzed = analyze_pcm(samples, 64);
    REQUIRE(analyzed.waveform.has_value());
    auto request = request_for(analyzed.waveform->total_frames);
    request.maximum_onsets = 2;
    const auto result = detect_waveform_onsets(*analyzed.waveform, request);

    CHECK(result.status == onset_status_t::capacity_exceeded);
    CHECK(result.frames.empty());
}

TEST_CASE("waveform onset detector checks complete coverage before the output cap", "[audio][onset]") {
    constexpr std::array<std::uint64_t, 3> clicks{256, 768, 1'280};
    const auto analyzed = analyze_pcm(make_click_track(2'048, clicks), 64);
    REQUIRE(analyzed.waveform);
    auto waveform = *analyzed.waveform;
    waveform.key.end_frame = 1'024;
    waveform.bins.resize(16);
    auto request = request_for(waveform.total_frames);
    request.maximum_onsets = 1;
    const auto result = detect_waveform_onsets(waveform, request);
    CHECK(result.status == onset_status_t::not_ready);
    CHECK(result.frames.empty());
}
