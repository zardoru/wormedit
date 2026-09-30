#include <wormedit/audio/time_stretch.h>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
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
    while (!stretch.complete()) {
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
    expect(stretch.complete());
}
