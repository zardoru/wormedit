#include <wormedit/audio/decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct duration_case_t {
    const char* name;
    double seconds;
};

constexpr std::array duration_cases{
    duration_case_t{"keysound-c4", 0.22},
    duration_case_t{"keysound-e4", 0.22},
    duration_case_t{"keysound-g4", 0.22},
    duration_case_t{"polyphonic-chord", 0.50},
    duration_case_t{"sliced-seek-source", 12.0},
    duration_case_t{"silence", 1.0},
};

constexpr std::array<std::pair<double, double>, 6> seek_regions{{
    {0.20, 220.0},
    {2.20, 277.0},
    {4.20, 330.0},
    {6.20, 392.0},
    {8.20, 440.0},
    {10.20, 523.0},
}};

constexpr std::array<double, 6> silence_centers{
    1.0, 3.0, 5.0, 7.0, 9.0, 11.0,
};

auto fixture_audio_path(std::string_view name, std::string_view extension)
    -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "audio" /
           (std::string(name) + "." + std::string(extension));
}

auto open_fixture(wormedit::audio::decoder_t& decoder,
                  const std::filesystem::path& path) -> bool {
    std::string error;
    const auto opened = decoder.open(path, &error);
    INFO("decoder open error: " << error);
    return opened;
}

auto window_rms(std::span<const float> samples) -> double {
    if (samples.empty()) {
        return 0.0;
    }
    double square_sum{};
    for (const auto sample : samples) {
        square_sum += static_cast<double>(sample) * sample;
    }
    return std::sqrt(square_sum / static_cast<double>(samples.size()));
}

auto goertzel_power(std::span<const float> interleaved_stereo,
                    std::size_t frames,
                    double frequency) -> double {
    const auto omega = 2.0 * std::numbers::pi * frequency /
                       wormedit::audio::output_sample_rate;
    const auto coefficient = 2.0 * std::cos(omega);
    double previous{};
    double previous_previous{};
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto mono = (static_cast<double>(interleaved_stereo[frame * 2]) +
                           interleaved_stereo[frame * 2 + 1]) * 0.5;
        const auto current = mono + coefficient * previous - previous_previous;
        previous_previous = previous;
        previous = current;
    }
    return std::max(0.0, previous * previous + previous_previous * previous_previous -
                             coefficient * previous * previous_previous);
}

auto estimate_frequency(std::span<const float> interleaved_stereo,
                        std::size_t frames,
                        double expected_frequency) -> double {
    auto best_frequency = expected_frequency;
    auto best_power = -1.0;
    for (int step = -20; step <= 20; ++step) {
        const auto frequency = expected_frequency + step * 0.5;
        const auto power = goertzel_power(interleaved_stereo, frames, frequency);
        if (power > best_power) {
            best_power = power;
            best_frequency = frequency;
        }
    }
    return best_frequency;
}

auto expected_tone_is_dominant(std::span<const float> interleaved_stereo,
                               std::size_t frames,
                               double expected_frequency) -> bool {
    const auto expected_power = goertzel_power(interleaved_stereo, frames,
                                               expected_frequency);
    for (const auto other_frequency : {220.0, 277.0, 330.0, 392.0, 440.0, 523.0}) {
        if (std::abs(other_frequency - expected_frequency) < 0.5) {
            continue;
        }
        if (goertzel_power(interleaved_stereo, frames, other_frequency) > expected_power) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("MP3 and Ogg fixtures decode completely to normalized stereo PCM",
          "[unit][audio][decoder]") {
    for (const auto extension : {"mp3", "ogg"}) {
        for (const auto& fixture : duration_cases) {
            CAPTURE(extension, fixture.name, fixture.seconds);
            wormedit::audio::decoder_t decoder;
            REQUIRE(open_fixture(decoder, fixture_audio_path(fixture.name, extension)));

            const auto stream_info = decoder.info();
            REQUIRE(stream_info.has_value());
            CHECK(stream_info->source_sample_rate > 0);
            CHECK(stream_info->source_channel_count > 0);
            CHECK(stream_info->source_frame_count > 0);
            CHECK(stream_info->seekable);
            CHECK(stream_info->output_sample_rate == 44'100);
            CHECK(stream_info->output_channel_count == 2);
            CHECK(stream_info->output_frame_count > 0);

            std::vector<float> block(
                wormedit::audio::maximum_read_frames *
                wormedit::audio::output_channel_count);
            std::uint64_t frames_decoded{};
            for (std::size_t iteration = 0; iteration < 128; ++iteration) {
                const auto read = decoder.read(block,
                                               wormedit::audio::maximum_read_frames);
                CHECK(read.frames_read <= wormedit::audio::maximum_read_frames);
                frames_decoded += read.frames_read;
                if (read.status == wormedit::audio::read_status_t::end_of_stream) {
                    break;
                }
                REQUIRE(read.status == wormedit::audio::read_status_t::success);
                REQUIRE(iteration < 127);
            }
            CHECK(frames_decoded == stream_info->output_frame_count);
            const auto decoded_seconds = static_cast<double>(frames_decoded) /
                                         stream_info->output_sample_rate;
            CHECK(std::abs(decoded_seconds - fixture.seconds) <= 0.060);
        }
    }
}

TEST_CASE("decoder enforces the 16384-frame request and stereo output buffer bounds",
          "[unit][audio][decoder]") {
    wormedit::audio::decoder_t decoder;
    REQUIRE(open_fixture(decoder, fixture_audio_path("sliced-seek-source", "ogg")));

    std::vector<float> buffer(
        wormedit::audio::maximum_read_frames * wormedit::audio::output_channel_count);
    CHECK(decoder.read(buffer, wormedit::audio::maximum_read_frames + 1).status ==
          wormedit::audio::read_status_t::request_too_large);
    CHECK(decoder.read(std::span<float>(buffer.data(), buffer.size() - 1),
                       wormedit::audio::maximum_read_frames)
              .status == wormedit::audio::read_status_t::insufficient_output_space);
}

TEST_CASE("MP3 and Ogg seek to the authored tone and silence regions",
          "[unit][audio][decoder][seek]") {
    constexpr auto window_frames = wormedit::audio::output_sample_rate / 4;
    std::vector<float> window(window_frames * wormedit::audio::output_channel_count);

    for (const auto extension : {"mp3", "ogg"}) {
        CAPTURE(extension);
        wormedit::audio::decoder_t decoder;
        REQUIRE(open_fixture(decoder,
                             fixture_audio_path("sliced-seek-source", extension)));

        for (const auto& [center_seconds, expected_frequency] : seek_regions) {
            CAPTURE(center_seconds, expected_frequency);
            const auto center_frame = static_cast<std::uint64_t>(std::llround(
                center_seconds * wormedit::audio::output_sample_rate));
            const auto first_frame = center_frame - window_frames / 2;
            REQUIRE(decoder.seek(first_frame));
            const auto read = decoder.read(window, window_frames);
            REQUIRE(read.status == wormedit::audio::read_status_t::success);
            REQUIRE(read.frames_read == window_frames);

            const auto rms = window_rms(window);
            const auto dbfs = 20.0 * std::log10(std::max(rms, 1.0e-12));
            CHECK(dbfs >= -24.0);
            CHECK(dbfs <= -12.0);
            CHECK(expected_tone_is_dominant(window, window_frames,
                                            expected_frequency));
            CHECK(std::abs(estimate_frequency(window, window_frames,
                                              expected_frequency) -
                           expected_frequency) <= 5.0);
        }

        for (const auto center_seconds : silence_centers) {
            CAPTURE(center_seconds);
            const auto center_frame = static_cast<std::uint64_t>(std::llround(
                center_seconds * wormedit::audio::output_sample_rate));
            const auto first_frame = center_frame - window_frames / 2;
            REQUIRE(decoder.seek(first_frame));
            const auto read = decoder.read(window, window_frames);
            REQUIRE(read.status == wormedit::audio::read_status_t::success);
            REQUIRE(read.frames_read == window_frames);
            const auto rms = window_rms(window);
            const auto dbfs = 20.0 * std::log10(std::max(rms, 1.0e-12));
            CHECK(dbfs < -45.0);
        }
    }
}
