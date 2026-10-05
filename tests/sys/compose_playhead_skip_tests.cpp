#include "sys/compose_playhead_skip.h"

#include <wormedit/audio/decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <string>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

// Two measures of 960 rows, so snap 4 is a 240-row step.
auto two_measures() -> measure_index_t {
    measure_index_t index;
    index.entries.push_back({.id = measure_id_t{1}, .absolute_begin_row = 0, .row_capacity = 960});
    index.entries.push_back(
        {.id = measure_id_t{2}, .absolute_begin_row = 960, .row_capacity = 960});
    index.total_rows = 1920;
    return index;
}

auto skip(double row, playhead_skip_unit_t unit, double amount) -> std::optional<std::uint64_t> {
    return playhead_skip_target_row(two_measures(), row, unit, amount, 960, 4);
}

} // namespace

TEST_CASE("snap skips go strictly past the playhead", "[sys][playhead-skip]") {
    CHECK(skip(100.0, playhead_skip_unit_t::snap, 1) == 240);
    CHECK(skip(100.0, playhead_skip_unit_t::snap, -1) == 0);
    CHECK(skip(240.0, playhead_skip_unit_t::snap, 1) == 480);
    CHECK(skip(240.0, playhead_skip_unit_t::snap, -1) == 0);
    CHECK(skip(300.0, playhead_skip_unit_t::snap, -1) == 240);
}

TEST_CASE("playhead skips clamp to the chart", "[sys][playhead-skip]") {
    CHECK(skip(1900.0, playhead_skip_unit_t::snap, 1) == 1919);
    CHECK(skip(10.0, playhead_skip_unit_t::rows, -50) == 0);
}

TEST_CASE("measure skips keep the playhead offset", "[sys][playhead-skip]") {
    CHECK(skip(100.0, playhead_skip_unit_t::measure, 1) == 1060);
    CHECK(skip(1060.0, playhead_skip_unit_t::measure, -1) == 100);
    CHECK(skip(1060.0, playhead_skip_unit_t::measure, 1) == 1060);
}

TEST_CASE("invalid snaps and rows are rejected", "[sys][playhead-skip]") {
    CHECK_FALSE(playhead_skip_target_row(two_measures(), 0.0, playhead_skip_unit_t::snap, 1, 960, 7));
    CHECK_FALSE(playhead_skip_target_row(two_measures(), std::nan(""), playhead_skip_unit_t::rows, 1,
                                         960, 4));
}

namespace {

auto skip_chart() -> chart_document_t {
    return {.id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}},
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{1}, 0},
                      .beats_per_minute = {240, 1}}}};
}

} // namespace

TEST_CASE("repeated playing skips within 300 ms continue from the last target",
          "[sys][playhead-skip]") {
    const auto chart = skip_chart();
    application_command_state_t application;
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds{5};
    application.bookmark_clock = [&] { return now; };
    std::vector<std::uint64_t> seeks;
    application.transport_seek = [&](std::uint64_t frame) {
        seeks.push_back(frame);
        return std::string{};
    };
    constexpr auto quarter = audio::output_sample_rate / 4;
    const auto notch = [&](double amount) {
        // The audio clock has not caught up: the live playhead stays at row 10.
        application.preview_playhead_absolute_row = 10.0;
        REQUIRE(skip_playing_playhead(application, chart, 4, playhead_skip_unit_t::snap, amount)
                    .accepted);
    };
    for (int count = 0; count < 3; ++count) {
        notch(1.0);
        now += std::chrono::milliseconds{50};
    }
    CHECK(seeks == std::vector<std::uint64_t>{quarter, 2 * quarter, 3 * quarter});

    // A direction reversal starts from the live playhead.
    notch(-1.0);
    CHECK(seeks.back() == 0);

    // After the window the live playhead is the origin again.
    seeks.clear();
    notch(1.0);
    now += std::chrono::milliseconds{400};
    notch(1.0);
    CHECK(seeks == std::vector<std::uint64_t>{quarter, quarter});
}
