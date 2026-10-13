#include <wormedit/audio/time_stretch.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace wormedit;
using namespace wormedit::audio;

namespace {
void expect(bool value) { REQUIRE(value); }

auto sine(std::size_t frames) -> std::vector<float> {
    std::vector<float> samples(frames * 2);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto value = static_cast<float>(std::sin(frame * 2.0 * std::numbers::pi * 440.0 / 44100.0));
        samples[frame * 2] = value;
        samples[frame * 2 + 1] = value;
    }
    return samples;
}

auto render(std::span<const float> input, rational_t rate, std::size_t block_frames = 512)
    -> std::vector<float> {
    time_stretch_t stretch;
    expect(stretch.reset(rate));
    std::vector<float> block(block_frames * 2), output;
    std::size_t offset{};
    while (offset < input.size() / 2) {
        const auto count = std::min(stretch.input_frames_needed(block_frames), input.size() / 2 - offset);
        const auto result = stretch.process(input.subspan(offset * 2, count * 2), block);
        expect(result.consumed_frames != 0 || result.produced_frames != 0);
        offset += result.consumed_frames;
        output.insert(output.end(), block.begin(), block.begin() + result.produced_frames * 2);
    }
    stretch.finish();
    while (!stretch.is_complete()) {
        const auto result = stretch.process({}, block);
        expect(result.produced_frames != 0);
        output.insert(output.end(), block.begin(), block.begin() + result.produced_frames * 2);
    }
    expect(stretch.buffered_source_frames() == 0);
    return output;
}

auto frequency(std::span<const float> samples) -> double {
    const auto first = std::size_t{4410};
    const auto last = samples.size() / 2 - first;
    std::size_t crossings{};
    for (auto frame = first + 1; frame < last; ++frame)
        crossings += samples[(frame - 1) * 2] <= 0.0F && samples[frame * 2] > 0.0F;
    return crossings * 44100.0 / static_cast<double>(last - first);
}
}

TEST_CASE("audio.time_stretch preserves sine pitch and exact duration", "[audio][time-stretch]") {
    const auto source = sine(44100);
    for (const auto rate : {rational_t{1, 1}, rational_t{1, 2}, rational_t{1, 10}}) {
        const auto output = render(source, rate);
        expect(output.size() / 2 == output_frames_for(44100, rate));
        expect(std::abs(frequency(output) - 440.0) <= 2.0);
        expect(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }));
    }
}

TEST_CASE("audio.time_stretch silence and partial windows drain deterministically", "[audio][time-stretch]") {
    const std::vector<float> silence(137 * 2);
    const auto output = render(silence, {1, 2}, 31);
    expect(output.size() == 137 * 4);
    expect(std::all_of(output.begin(), output.end(), [](float value) { return value == 0.0F; }));
    expect(render({}, {1, 2}).empty());
    const auto source = sine(22050);
    expect(render(source, {1, 2}) == render(source, {1, 2}));
    expect(render(source, {1, 2}, 127) == render(source, {1, 2}, 1024));
}

TEST_CASE("audio.time_stretch reset clears pending audio and rejects invalid spans", "[audio][time-stretch]") {
    time_stretch_t stretch;
    const auto source = sine(2000);
    std::array<float, 100> output{};
    expect(stretch.process(source, output).produced_frames == 50);
    expect(!stretch.reset({2, 1}));
    expect(stretch.reset({1, 2}));
    expect(stretch.buffered_source_frames() == 0);
    expect(stretch.process(std::span(source).first(3), output).consumed_frames == 0);
    stretch.finish();
    expect(stretch.is_complete());
}

namespace {
auto transient_stereo(std::size_t frames) -> std::vector<float> {
    std::vector<float> samples(frames * 2U);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto time = static_cast<double>(frame) / 44100.0;
        samples[frame * 2U] = static_cast<float>(0.55 * std::sin(2.0 * std::numbers::pi * 330.0 * time));
        samples[frame * 2U + 1U] = static_cast<float>(0.4 * std::cos(2.0 * std::numbers::pi * 523.25 * time));
    }
    for (const auto frame : {std::size_t{1237}, std::size_t{22003}, frames - 1U}) {
        if (frame >= frames) continue;
        samples[frame * 2U] = 0.9F;
        samples[frame * 2U + 1U] = -0.85F;
    }
    return samples;
}

auto finite_bounded_stereo(std::span<const float> samples) -> bool {
    return std::ranges::all_of(samples, [](float sample) {
        return std::isfinite(sample) && std::abs(sample) <= 1.0F;
    });
}

auto byte_identical(std::span<const float> left, std::span<const float> right) -> bool {
    return left.size() == right.size() &&
        (left.empty() || std::memcmp(left.data(), right.data(), left.size_bytes()) == 0);
}

auto one_rate_is_chunk_deterministic(std::span<const float> source, rational_t rate) -> bool {
    const auto expected_frames = output_frames_for(source.size() / 2U, rate);
    if (!expected_frames) return false;
    const auto standard = render(source, rate, 512);
    const auto repeated = render(source, rate, 512);
    const auto partitioned = render(source, rate, 127);
    return byte_identical(standard, repeated) && byte_identical(standard, partitioned) &&
        standard.size() / 2U == *expected_frames && finite_bounded_stereo(standard);
}

auto all_supported_rates_are_chunk_deterministic() -> bool {
    constexpr std::size_t non_hop_aligned_frames = 44'137;
    const auto source = transient_stereo(non_hop_aligned_frames);
    return std::ranges::all_of(playback_rates, [&](rational_t rate) {
        return one_rate_is_chunk_deterministic(source, rate);
    });
}

auto non_silent_partial_eof_drains_exactly() -> bool {
    constexpr std::size_t frames = 12'347;
    const auto source = transient_stereo(frames);
    const auto rate = rational_t{1, 3};
    const auto output = render(source, rate, 127);
    const auto expected = output_frames_for(frames, rate);
    if (!expected || output.size() / 2U != *expected || !finite_bounded_stereo(output))
        return false;
    // EOF zero-padding may silence part of the final WSOLA window.
    constexpr std::size_t final_window_samples = (44'100 / 50) * 2;
    return std::ranges::any_of(std::span(output).last(final_window_samples), [](float value) {
        return std::abs(value) > 0.01F;
    });
}
}

TEST_CASE("audio.time_stretch all supported rates are deterministic for partial stereo transients",
          "[audio][time-stretch][supported-rates]") {
    expect(all_supported_rates_are_chunk_deterministic());
}

TEST_CASE("audio.time_stretch non-silent partial EOF drains exact rational duration",
          "[audio][time-stretch][partial-eof]") {
    expect(non_silent_partial_eof_drains_exactly());
}

namespace {
auto quarter_phase_sparse_source() -> std::vector<float> {
    std::vector<float> source(12'347 * 2U);
    for (std::size_t frame = 2; frame < source.size() / 2U; frame += 4) {
        source[frame * 2U] = 0.7F;
        source[frame * 2U + 1U] = 0.5F;
    }
    return source;
}

auto sparse_phase_remains_audible_and_deterministic(rational_t rate) -> bool {
    const auto source = quarter_phase_sparse_source();
    const auto output = render(source, rate, 127);
    double energy{};
    for (const auto value : output) energy += value * value;
    return finite_bounded_stereo(output) &&
        output.size() / 2U == output_frames_for(source.size() / 2U, rate) &&
        std::sqrt(energy / output.size()) > 0.2 &&
        byte_identical(output, render(source, rate, 512));
}
}

TEST_CASE("audio.time_stretch quarter-phase sparse input retains energy and deterministic alignment",
          "[audio][time-stretch][sparse-phase]") {
    expect(sparse_phase_remains_audible_and_deterministic({1, 2}));
    expect(sparse_phase_remains_audible_and_deterministic({1, 10}));
}
