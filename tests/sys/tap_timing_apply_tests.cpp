#include "sys/tap_timing_runtime.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <wormedit/core/timing.h>

using namespace wormedit;
using namespace wormedit::sys;
namespace {
auto apply_song() -> song_document_t {
    return {.id = {1}, .revision = 2,
        .charts = {{.id = {1}, .measures = {{.id = {1},
            .notes = {{.id = {5}, .position = {{1}, 120}, .lane = {3}}}}},
            .lane_layouts = {{.id = {4}, .position = {{1}, 0},
                .layout = {.profile_id = "test", .display_name = "1K",
                    .lanes = {{.id = {3}, .name = "Key"}}}}},
            .bpms = {{.id = {2}, .position = {{1}, 0}, .beats_per_minute = {110, 1}},
                     {.id = {6}, .position = {{1}, 480}, .beats_per_minute = {135, 1}}}}}};
}
auto record_apply_taps(tap_timing_runtime_t& runtime) -> tap_timing_clock_t {
    runtime.set_visible(true);
    tap_timing_clock_t clock{.chart = 1, .revision = 2, .asset = 3, .epoch = 4,
        .epoch_begin_ns = 1'000'000'000, .sample_rate = 48'000, .playing = true};
    for (std::uint64_t i = 0; i < 10; ++i) {
        clock.observed_ns = 2'000'000'000 + i * 500'000'000;
        clock.source_frame = 48'000 + i * 24'000;
        REQUIRE(runtime.tap(clock.observed_ns, clock) == tap_timing_result_t::accepted);
    }
    return clock;
}
}

TEST_CASE("Tap Apply binds a stable proposal and records both values once", "[sys][tap-apply]") {
    auto history = document_history_t::from(apply_song());
    tap_timing_runtime_t runtime;
    const auto clock = record_apply_taps(runtime);
    runtime.prepare_apply(history.document, false);
    REQUIRE(runtime.apply_proposal());
    const auto proposal = *runtime.apply_proposal();
    const auto preview = build_tap_timing_preview(history.document, proposal);
    REQUIRE(preview);
    const auto original = build_timing_index(history.document.charts.front());
    const auto proposed = build_timing_index(*preview);
    REQUIRE(original);
    REQUIRE(proposed);
    const auto ghost = proposed_timing_y_on_fixed_waveform(*original.index, *proposed.index,
                                                         {120, {}}, 0, 1);
    REQUIRE(ghost);
    CHECK(*ghost != Catch::Approx(120.0));
    REQUIRE(runtime.apply(history, clock, 2, false));
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute == proposal.beats_per_minute);
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == proposal.beat_zero_audio_offset_seconds);
    CHECK(history.document.charts.front().bpms.back().position == local_position_t{{1}, 480});
    CHECK(history.document.charts.front().bpms.back().beats_per_minute == signed_rational_t{135, 1});
    CHECK(history.document.charts.front().measures.front().notes.front().position == local_position_t{{1}, 120});
    CHECK_FALSE(runtime.apply_proposal());
    CHECK_FALSE(runtime.state().estimate);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute == signed_rational_t{110, 1});
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{0, 1});
}

TEST_CASE("Tap Apply rejects changed source session revision lock and expired estimate", "[sys][tap-apply]") {
    for (int failure = 0; failure < 7; ++failure) {
        auto history = document_history_t::from(apply_song());
        tap_timing_runtime_t runtime;
        auto clock = record_apply_taps(runtime);
        runtime.prepare_apply(history.document, false);
        REQUIRE(runtime.apply_proposal());
        if (failure == 0) ++clock.epoch;
        if (failure == 1) ++clock.asset;
        if (failure == 2) ++clock.revision;
        if (failure == 3) clock.playing = false;
        if (failure == 4) clock.source_frame.reset();
        if (failure == 5) {
            clock.observed_ns += 3'000'000'000;
            *clock.source_frame += 144'000;
        }
        const auto result = runtime.apply(history, clock, 2, failure == 6);
        CHECK_FALSE(result);
        CHECK(history.undo.empty());
        CHECK(history.redo.empty());
        CHECK(history.document.revision == 2);
        CHECK(history.next_revision == 3);
        CHECK(history.document.charts.front().bpms.front().beats_per_minute == signed_rational_t{110, 1});
    }
}

TEST_CASE("Tap Apply choice and hidden panel invalidate the prepared proposal", "[sys][tap-apply]") {
    auto history = document_history_t::from(apply_song());
    tap_timing_runtime_t runtime;
    const auto clock = record_apply_taps(runtime);
    runtime.prepare_apply(history.document, false);
    REQUIRE(runtime.apply_proposal());
    runtime.set_bpm_choice(tap_bpm_choice_t::nearest_half);
    CHECK_FALSE(runtime.apply_proposal());
    runtime.prepare_apply(history.document, false);
    REQUIRE(runtime.apply_proposal());
    runtime.set_visible(false);
    CHECK_FALSE(runtime.apply(history, clock, 2, false));
    CHECK(history.undo.empty());
}

TEST_CASE("Tap Apply cannot enable a structurally invalid preview", "[sys][tap-apply]") {
    auto history = document_history_t::from(apply_song());
    tap_timing_runtime_t runtime;
    const auto clock = record_apply_taps(runtime);
    history.document.charts.front().measures.clear();
    runtime.prepare_apply(history.document, false);
    CHECK_FALSE(runtime.apply_proposal());
    CHECK_FALSE(runtime.apply(history, clock, 2, false));
    CHECK(history.undo.empty());
}
