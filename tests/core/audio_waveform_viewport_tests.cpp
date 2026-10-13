#include <wormedit/audio/waveform_viewport.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cstdint>

namespace {

using namespace wormedit::audio;

constexpr std::uint64_t eighty_minutes = 80ULL * 60ULL * 48'000ULL;

auto synthetic_waveform(std::uint64_t bins, std::uint64_t frames_per_bin) -> waveform_t {
    waveform_t waveform;
    waveform.key = {.source = {"synthetic", "synthetic-v1"},
                    .frames_per_bin = static_cast<std::uint32_t>(frames_per_bin)};
    waveform.total_frames = bins * frames_per_bin;
    waveform.sample_rate = output_sample_rate;
    waveform.bins.assign(bins, waveform_bin_t{.maximum = {0.5F, 0.5F},
                                              .absolute_peak = {0.5F, 0.5F},
                                              .rms = {0.25F, 0.25F},
                                              .frame_count = frames_per_bin});
    for (auto& bin : waveform.bins) bin.minimum = {-0.5F, -0.5F};
    return waveform;
}

} // namespace

TEST_CASE("fit viewport covers the whole source", "[unit][audio][waveform]") {
    const auto fit = fit_waveform_viewport(48'000, 480);
    CHECK(is_waveform_viewport_valid(fit));
    CHECK(fit.begin_frame == 0);
    CHECK(waveform_viewport_end_frame(fit) == 48'000);
    CHECK_FALSE(is_waveform_viewport_valid(fit_waveform_viewport(0, 480)));
    CHECK_FALSE(is_waveform_viewport_valid(fit_waveform_viewport(48'000, 0)));
}

TEST_CASE("zoom keeps the anchored frame under the pointer", "[unit][audio][waveform]") {
    auto view = fit_waveform_viewport(eighty_minutes, 1'000);
    view = zoom_waveform_viewport(view, 500.0, 64.0);
    REQUIRE(is_waveform_viewport_valid(view));
    CHECK(view.frames_per_pixel < eighty_minutes / 1'000.0);
    // Anchor was frame total/2; allow rounding of one pixel's worth of frames.
    const auto anchored = static_cast<double>(view.begin_frame) + 500.0 * view.frames_per_pixel;
    CHECK(anchored == Catch::Approx(eighty_minutes / 2.0).margin(view.frames_per_pixel + 1.0));

    const auto again = zoom_waveform_viewport(view, 250.0, 4.0);
    const auto before = static_cast<double>(view.begin_frame) + 250.0 * view.frames_per_pixel;
    const auto after = static_cast<double>(again.begin_frame) + 250.0 * again.frames_per_pixel;
    CHECK(after == Catch::Approx(before).margin(view.frames_per_pixel + 1.0));
}

TEST_CASE("zoom clamps at the sample level and at the full source", "[unit][audio][waveform]") {
    auto view = fit_waveform_viewport(10'000, 100);
    view = zoom_waveform_viewport(view, 50.0, 1e9);
    CHECK(view.frames_per_pixel == minimum_waveform_frames_per_pixel);
    CHECK(is_waveform_viewport_valid(view));
    view = zoom_waveform_viewport(view, 50.0, 1e-9);
    CHECK(view == fit_waveform_viewport(10'000, 100));
}

TEST_CASE("pan stays inside the source", "[unit][audio][waveform]") {
    auto view = zoom_waveform_viewport(fit_waveform_viewport(100'000, 100), 0.0, 10.0);
    CHECK(pan_waveform_viewport(view, -1e9).begin_frame == 0);
    const auto end = pan_waveform_viewport(view, 1e9);
    CHECK(waveform_viewport_end_frame(end) == 100'000);
    CHECK(is_waveform_viewport_valid(end));
    const auto moved = pan_waveform_viewport(view, 10.0);
    CHECK(moved.begin_frame == 10 * static_cast<std::uint64_t>(view.frames_per_pixel));
}

TEST_CASE("degenerate sources and inputs are handled", "[unit][audio][waveform]") {
    const auto one = fit_waveform_viewport(1, 640);
    CHECK(is_waveform_viewport_valid(one));
    CHECK(waveform_viewport_end_frame(one) == 1);
    const auto bad = zoom_waveform_viewport(one, 0.0, 0.0);
    CHECK(bad == one);
    CHECK(pan_waveform_viewport(waveform_viewport_t{}, 5.0) == waveform_viewport_t{});
}

TEST_CASE("projection uses the viewport window", "[unit][audio][waveform]") {
    const auto waveform = synthetic_waveform(1'000, 480);
    auto view = fit_waveform_viewport(waveform.total_frames, 100);
    const auto full = project_waveform_viewport(waveform, view);
    REQUIRE(full);
    CHECK(full.columns.size() == 100);
    view = zoom_waveform_viewport(view, 0.0, 8.0);
    const auto zoomed = project_waveform_viewport(waveform, view);
    REQUIRE(zoomed);
    CHECK(zoomed.columns.front().begin_frame == 0);
    CHECK(zoomed.columns.back().end_frame <= waveform_viewport_end_frame(view));

    auto wrong = view;
    wrong.total_frames += 1;
    CHECK_FALSE(project_waveform_viewport(waveform, wrong));
}
