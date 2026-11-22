#include "sys/audio/compose_audio_analysis.h"

#include "ui/frontend/compose.h"
#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/source_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace {
using wormedit::audio::decoded_source_registry_t;
using wormedit::audio::output_sample_rate;
using wormedit::sys::compose_audio_analysis_t;
using wormedit::ui::compose_audio_analysis_mode_t;

auto stereo_tone(double frequency, std::size_t frames) -> std::vector<float> {
    std::vector<float> samples;
    samples.reserve(frames * 2);
    for (std::size_t index = 0; index < frames; ++index) {
        const auto angle = 2 * std::numbers::pi * frequency * index / output_sample_rate;
        const auto left = static_cast<float>(std::sin(angle));
        samples.push_back(left);
        samples.push_back(-left);
    }
    return samples;
}
auto analyze_tone(double frequency) -> std::shared_ptr<const compose_audio_analysis_t> {
    decoded_source_registry_t sources;
    const auto source = sources.create_generated_source({.source = {"tone", "v1"},
        .interleaved_stereo = stereo_tone(frequency, wormedit::audio::maximum_read_frames + output_sample_rate)});
    REQUIRE(source);
    std::atomic<unsigned> progress{};
    auto analysis = compose_audio_analysis_t::from(sources, *source, {}, &progress);
    REQUIRE(analysis);
    CHECK(progress.load(std::memory_order_relaxed) == 100);
    return analysis;
}

auto tone_bin(double frequency) -> wormedit::sys::compose_audio_analysis_bin_t {
    const auto analysis = analyze_tone(frequency);
    const auto bin = analysis->query(.5, .6);
    REQUIRE(bin);
    return *bin;
}
}

TEST_CASE("Compose spectrum separates opposite-polarity stereo tones by octave",
          "[sys][compose-audio-analysis]") {
    const auto low = tone_bin(110);
    const auto high = tone_bin(7200);
    CHECK(std::distance(low.spectrum.begin(), std::ranges::max_element(low.spectrum)) == 1);
    CHECK(std::distance(high.spectrum.begin(), std::ranges::max_element(high.spectrum)) == 7);
}

TEST_CASE("Compose filtered waveform retains stereo band amplitudes",
          "[sys][compose-audio-analysis]") {
    const auto low = tone_bin(110);
    const auto high = tone_bin(7200);
    CHECK(low.rms[0][0] > low.rms[2][0] * 10);
    CHECK(high.rms[2][0] > high.rms[0][0] * 10);
    CHECK(low.rms[0][0] == low.rms[0][1]);
}

TEST_CASE("Compose audio analysis rejects out-of-range queries and cancellation",
          "[sys][compose-audio-analysis]") {
    const auto analysis = analyze_tone(110);
    CHECK_FALSE(analysis->query(-.2, -.1));
    CHECK_FALSE(analysis->query(10, 11));
    decoded_source_registry_t sources;
    const auto source = sources.create_generated_source({.source = {"tone", "v1"},
        .interleaved_stereo = stereo_tone(110, output_sample_rate)});
    REQUIRE(source);
    std::stop_source cancelled;
    cancelled.request_stop();
    CHECK_FALSE(compose_audio_analysis_t::from(sources, *source, cancelled.get_token()));
}

TEST_CASE("Compose audio analysis uses the existing waveform clock and downscroll",
          "[sys][compose-audio-analysis]") {
    const auto analysis = analyze_tone(110);
    wormedit::audio::display_row_table_t table;
    static_cast<void>(table.viewport.get_or_build(std::tuple{100., 40., 40.},
        [](auto& viewport) {
            viewport.rows = {{.world_pixel = 112, .begin_seconds = .25, .end_seconds = .3, .valid = true},
                             {.world_pixel = 113, .begin_seconds = 2.2, .end_seconds = 2.3, .valid = true}};
        }));
    wormedit::ui::compose_view_t view;
    view.viewport_height_pixels = 40;
    view.downscroll = true;
    analysis->populate_view(view, table, compose_audio_analysis_mode_t::spectrum);
    analysis->populate_view(view, table, compose_audio_analysis_mode_t::lows);
    REQUIRE(view.spectrum_rows.size() == 1);
    REQUIRE(view.filtered_waveform_rows.size() == 1);
    CHECK(view.spectrum_rows.front().y_pixels == 27.5);
    CHECK(view.filtered_waveform_rows.front().y_pixels == view.spectrum_rows.front().y_pixels);
    CHECK(view.waveform_rows.empty());
}
