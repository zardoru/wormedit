#include "sys/compose_view.h"
#include "sys/timing_settings.h"
#include <wormedit/core/timing.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>

using namespace wormedit;
TEST_CASE("Timing count-in converts measures to first-meter pulses and clips negative audio time",
          "[sys][timing-settings]") {
    chart_document_t chart{.id = {1}, .divisions_per_standard_measure = 960,
        .beat_zero_audio_offset_seconds = {10, 1},
        .measures = {{.id = {1}, .definition = {{7, 8}, meter_t{7, 8}}}},
        .bpms = {{.id = {2}, .position = {{1}, 0}, .beats_per_minute = {120, 1}}}};
    const auto pulses = wormedit::sys::timing_count_in_pulses(chart, 2);
    REQUIRE(pulses == 14);
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto schedule = build_preview_schedule(chart, *timing.value,
        {.sample_rate = 48'000, .count_in_pulses = *pulses});
    REQUIRE(schedule);
    CHECK(std::ranges::count_if(schedule.schedule->metronome,
        [](const auto& click) { return click.pulse < 0; }) == 14);
    CHECK(schedule.schedule->metronome.front().pulse == -14);
    CHECK(schedule.schedule->metronome.front().accent == metronome_accent_t::measure);
    chart.beat_zero_audio_offset_seconds = {0, 1};
    const auto zero_timing = timing_index_t::from(chart);
    REQUIRE(zero_timing.value);
    const auto clipped = build_preview_schedule(chart, *zero_timing.value,
        {.sample_rate = 48'000, .count_in_pulses = *pulses});
    REQUIRE(clipped);
    CHECK(std::ranges::none_of(clipped.schedule->metronome,
        [](const auto& click) { return click.pulse < 0; }));
    CHECK_FALSE(clipped.diagnostics.empty());
    CHECK(wormedit::sys::timing_count_in_pulses(chart, 0) == 0);
    CHECK_FALSE(wormedit::sys::timing_count_in_pulses(chart, 5));
}

TEST_CASE("Timing count-in uses quarter pulses for an unspecified meter and rejects overflow",
          "[sys][timing-settings]") {
    chart_document_t chart{.id = {1}, .divisions_per_standard_measure = 960,
        .measures = {{.id = {1}}}};
    CHECK(wormedit::sys::timing_count_in_pulses(chart, 4) == 16);
    chart.measures.front().definition = {{3, 2}, std::nullopt};
    CHECK(wormedit::sys::timing_count_in_pulses(chart, 2) == 12);
    chart.measures.clear();
    CHECK_FALSE(wormedit::sys::timing_count_in_pulses(chart, 1));
}

TEST_CASE("Timing count-in accepts the final partial beat of an unmetered measure",
          "[sys][timing-settings][partial-measure]") {
    chart_document_t chart{.id = {1}, .divisions_per_standard_measure = 960,
        .measures = {{.id = {1}, .definition = {{721, 960}, std::nullopt}}}};
    CHECK(wormedit::sys::timing_count_in_pulses(chart, 1) == 4);
    chart.measures.front().definition.length = {1, 960};
    CHECK(wormedit::sys::timing_count_in_pulses(chart, 2) == 2);
}

TEST_CASE("Timing settings commands use the metronome owner and unavailable defaults", "[sys][timing-settings][metronome]") {
    wormedit::sys::application_command_state_t state;
    CHECK_FALSE(wormedit::sys::toggle_transport_metronome(state));
    wormedit::sys::metronome_t metronome;
    metronome.settings.available = true;
    state.metronome = &metronome;
    wormedit::ui::timing_settings_action_t action{};
    action.change_enable = action.change_volume = action.change_count_in = true;
    action.enabled = true;
    action.volume = 25;
    action.count_in = 2;
    wormedit::sys::apply_timing_settings_action(state, action);
    CHECK(state.metronome_settings() == wormedit::sys::metronome_settings_t{true, true, 25, 2});
    CHECK(wormedit::sys::toggle_transport_metronome(state));
    CHECK_FALSE(state.metronome_settings().enabled);
}
