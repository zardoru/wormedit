#include "nuklear_event_harness.h"
#include "ui/nuklear/compose/compose_panel.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>

namespace {
auto text_bounds(const nuklear_event_harness_t& harness, std::string_view text)
    -> wormedit::ui::testing_drawn_text_view_t {
    const auto rows = harness.drawn_text_bounds();
    const auto found = std::ranges::find_if(rows, [&](const auto& row) {
        return std::string_view{row.text} == text;
    });
    REQUIRE(found != rows.end());
    return *found;
}

void check_position(const wormedit::ui::testing_drawn_text_view_t& actual, const wormedit::ui::testing_drawn_text_view_t& expected) {
    CHECK(actual.x == expected.x);
    CHECK(actual.y == expected.y);
    CHECK(actual.width == expected.width);
}

struct playback_status_fixture_t {
    nuklear_event_harness_t harness{1280, 900};
    std::array<wormedit::ui::compose_waveform_overview_row_view_t, 2> rows{{{0, 0.5f}, {1, 0.5f}}};
    playback_status_fixture_t() {
        REQUIRE(harness.ready());
        auto& view = harness.view().compose;
        view.bgm_bar_reserved = true;
        view.has_bgm_events = true;
        view.bgm_bar_rows.assign(rows.begin(), rows.end());

        view.bgm_bar_end_fraction = 1;
        view.waveform_overview_rows.assign(rows.begin(), rows.end());

        view.waveform_overview_total_frames = 96000;
        view.waveform_overview_sample_rate = 48000;
        view.waveform_audio_offset_denominator = 1;
        view.waveform_overview_visible_end_fraction = 1;
    }
};
}

TEST_CASE("Malformed overview publication leaves the editable workspace and status visible",
          "[event-harness][playback-status]") {
    playback_status_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    REQUIRE(worm_ui_nuklear_compose_view_valid(&view));
    REQUIRE(worm_ui_nuklear_compose_overview_valid(&view));
    for (const int invalid : {0, 1, 2, 3}) {
        view.waveform_overview_visible_end_fraction = invalid == 0 ? 1.00001 : 1;
        view.waveform_overview_rows.assign(fixture.rows.begin(), fixture.rows.end());
        view.waveform_overview_rows[0].audio_fraction = invalid == 1 ? -0.1 : 0;
        view.waveform_overview_rows[1].audio_fraction = invalid == 2 ? 1.1 : 1;
        view.waveform_overview_rows[0].absolute_peak = invalid == 3 ? std::numeric_limits<float>::quiet_NaN() : 0.5f;
        CHECK(worm_ui_nuklear_compose_view_valid(&view));
        CHECK_FALSE(worm_ui_nuklear_compose_overview_valid(&view));
        REQUIRE(worm_ui_nuklear_compose_overview_error(&view));
        (void)fixture.harness.frame();
        CHECK(fixture.harness.drawn_text().find("Ready") != std::string::npos);
        CHECK(fixture.harness.drawn_text().find("could not display") == std::string::npos);
    }
}

TEST_CASE("Playback status geometry stays fixed across overview and notification changes",
          "[event-harness][playback-status]") {
    playback_status_fixture_t fixture;
    auto& workspace = fixture.harness.view();
    (void)fixture.harness.frame();
    const auto clock = text_bounds(fixture.harness, "0:00.000");
    const auto details = text_bounds(fixture.harness, "Test artist · Normal");
    for (const bool available : {false, true}) {
        workspace.compose.bgm_bar_rows.assign(fixture.rows.begin(), fixture.rows.begin() + (available ? 2 : 0));
        workspace.compose.waveform_overview_rows.assign(fixture.rows.begin(), fixture.rows.begin() + (available ? 2 : 0));
        workspace.notifications.unread_warnings = available ? 1 : 0;
        workspace.transport_playing = available;
        (void)fixture.harness.frame();
        check_position(text_bounds(fixture.harness, "0:00.000"), clock);
        check_position(text_bounds(fixture.harness, "Test artist · Normal"), details);
    }
}

TEST_CASE("Long UTF-8 status and metadata elide inside their reserved columns",
          "[event-harness][playback-status]") {
    playback_status_fixture_t fixture;
    std::string long_text;
    for (int index = 0; index < 100; ++index) long_text += "Timing 日本語 · ";
    fixture.harness.view().status_text = long_text.c_str();
    fixture.harness.view().artist = long_text.c_str();
    (void)fixture.harness.frame();
    std::size_t elided{};
    for (const auto& row : fixture.harness.drawn_text_bounds()) {
        const std::string_view text{row.text};
        if (!text.starts_with("Timing") || !text.ends_with("…")) continue;
        ++elided;
        CHECK(text.size() < long_text.size());
        CHECK(std::isfinite(row.x));
        CHECK(row.x + row.width <= 1280);
    }
    CHECK(elided == 2);
}

TEST_CASE("Malformed BGM overview arrays are bounded independently of chart geometry",
          "[event-harness][playback-status]") {
    playback_status_fixture_t fixture;
    auto& view = fixture.harness.view().compose;
    std::array<wormedit::ui::compose_waveform_overview_row_view_t, 1> bad_rows{{{0.5, 1.5f}}};
    for (const int invalid : {0, 1, 2}) {
        view.bgm_bar_rows.assign(bad_rows.begin(), bad_rows.end());
        view.bgm_bar_rows[0].audio_fraction = invalid == 0 ? -0.5 : 0.5;
        view.bgm_bar_rows[0].absolute_peak = invalid == 1 ? std::numeric_limits<float>::quiet_NaN() : 1.5f;
        CHECK(worm_ui_nuklear_compose_view_valid(&view));
        CHECK_FALSE(worm_ui_nuklear_compose_overview_valid(&view));
        (void)fixture.harness.frame();
        CHECK(fixture.harness.drawn_text().find("Ready") != std::string::npos);
        CHECK(fixture.harness.drawn_text().find("could not display") == std::string::npos);
    }
}
