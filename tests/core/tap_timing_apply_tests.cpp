#include <wormedit/core/tap_timing_apply.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>

using namespace wormedit;

namespace {

auto apply_song(bool has_bpm = false) -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 8,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .beat_zero_audio_offset_seconds = {0, 1},
                    .measures = {{.id = measure_id_t{10}}, {.id = measure_id_t{11}}},
                    .lane_layouts = {{.id = object_id_t{20},
                                      .position = {measure_id_t{10}, 0},
                                      .layout = {.profile_id = "tap-test",
                                                 .display_name = "1K",
                                                 .lanes = {{.id = lane_id_t{30},
                                                            .name = "Key 1"}}}}}}},
    };
    if (has_bpm) {
        song.charts.front().bpms.push_back({.id = object_id_t{21},
            .position = {measure_id_t{10}, 0}, .beats_per_minute = {120, 1}});
    }
    return song;
}

auto stable_estimate(double phase = 0.0, rational_t bpm = {120, 1}) -> tap_tempo_estimate_t {
    return {.bpm = static_cast<double>(bpm.numerator) / bpm.denominator,
            .rational_bpm = bpm,
            .rounded_bpm = std::round(static_cast<double>(bpm.numerator) / bpm.denominator),
            .half_bpm = std::round(static_cast<double>(bpm.numerator) * 2 / bpm.denominator) / 2,
            .phase_frame = phase,
            .tap_count = 16,
            .session_tap_count = 16,
            .confidence = 1.0,
            .stable = true};
}

auto proposal(const song_document_t& song, tap_tempo_estimate_t estimate = stable_estimate(),
              tap_bpm_choice_t choice = tap_bpm_choice_t::raw, std::uint32_t rate = 44'100,
              std::uint16_t meter = 4) -> tap_timing_apply_proposal_result_t {
    return propose_tap_timing_apply(song, chart_id_t{2}, estimate, rate, meter, choice);
}

TEST_CASE("Tap timing proposal computes raw and rounded BPM and signed exact phase offset",
          "[core][tap-timing-apply]") {
    auto song = apply_song(true);
    song.charts.front().beat_zero_audio_offset_seconds = {-1, 2};
    const auto estimate = stable_estimate(-22'050.5, {247, 2});
    const auto raw = proposal(song, estimate);
    const auto one = proposal(song, estimate, tap_bpm_choice_t::nearest_one);
    const auto half = proposal(song, estimate, tap_bpm_choice_t::nearest_half);
    REQUIRE(raw);
    REQUIRE(one);
    REQUIRE(half);
    CHECK(raw.proposal->beats_per_minute == signed_rational_t{247, 2});
    CHECK(one.proposal->beats_per_minute == signed_rational_t{124, 1});
    CHECK(half.proposal->beats_per_minute == signed_rational_t{247, 2});
    CHECK(raw.proposal->beat_zero_audio_offset_seconds == signed_rational_t{-1, 2});

    song.charts.front().beat_zero_audio_offset_seconds = {0, 1};
    const auto even = proposal(song, stable_estimate(-2.5));
    REQUIRE(even);
    CHECK(even.proposal->beat_zero_audio_offset_seconds == signed_rational_t{-1, 22'050});
}

TEST_CASE("Tap timing proposal chooses the nearest measure downbeat and earlier on a tie",
          "[core][tap-timing-apply]") {
    auto song = apply_song(true);
    // 120 BPM in four beats gives an 88,200-frame measure. The existing start at
    // 45,100 frames is equidistant from phase 1,000 and the following measure.
    song.charts.front().beat_zero_audio_offset_seconds = {45'100, 44'100};
    const auto result = proposal(song, stable_estimate(1'000.0));
    REQUIRE(result);
    CHECK(result.proposal->beat_zero_audio_offset_seconds ==
          signed_rational_t{10, 441});
}

TEST_CASE("Tap timing commit inserts row-zero BPM and undo restores both values atomically",
          "[core][tap-timing-apply]") {
    auto history = make_document_history(apply_song());
    const auto proposed = proposal(history.document, stable_estimate(22'050.0));
    REQUIRE(proposed);
    const auto preview = build_tap_timing_preview(history.document, *proposed.proposal);
    REQUIRE(preview);
    CHECK(preview->bpms.size() == 1);
    CHECK(preview->bpms.front().position == local_position_t{measure_id_t{10}, 0});
    CHECK(preview->beat_zero_audio_offset_seconds == signed_rational_t{1, 2});

    const auto result = apply_tap_timing_proposal(history, 8, *proposed.proposal);
    REQUIRE(result);
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().bpms.size() == 1);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute == signed_rational_t{120, 1});
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{1, 2});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.empty());
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{0, 1});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute == signed_rational_t{120, 1});
    CHECK(history.document.charts.front().beat_zero_audio_offset_seconds == signed_rational_t{1, 2});
}

TEST_CASE("Tap timing commit changes only the final row-zero BPM and preserves later events and notes",
          "[core][tap-timing-apply]") {
    auto song = apply_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.push_back({.id = object_id_t{40},
        .position = {measure_id_t{10}, 120}, .lane = lane_id_t{30}});
    chart.bpms = {
        {.id = object_id_t{22}, .position = {measure_id_t{10}, 0},
         .beats_per_minute = {100, 1}, .provenance = {.source_order = 3}},
        {.id = object_id_t{23}, .position = {measure_id_t{10}, 0},
         .beats_per_minute = {110, 1}, .provenance = {.source_order = 8}},
        {.id = object_id_t{24}, .position = {measure_id_t{11}, 240},
         .beats_per_minute = {140, 1}, .provenance = {.source_order = 1}},
    };
    auto history = make_document_history(song);
    const auto before_note = history.document.charts.front().measures.front().notes.front();
    const auto later = history.document.charts.front().bpms.back();
    const auto before_note_position = before_note.position;
    const auto proposed = proposal(history.document, stable_estimate(), tap_bpm_choice_t::nearest_half);
    REQUIRE(proposed);
    REQUIRE(apply_tap_timing_proposal(history, 8, *proposed.proposal));
    const auto& changed = history.document.charts.front();
    CHECK(changed.bpms[0].beats_per_minute == signed_rational_t{100, 1});
    CHECK(changed.bpms[1].beats_per_minute == signed_rational_t{120, 1});
    CHECK(changed.bpms[2].id == later.id);
    CHECK(changed.bpms[2].position == later.position);
    CHECK(changed.bpms[2].beats_per_minute == later.beats_per_minute);
    CHECK(changed.bpms[2].provenance.source_order == later.provenance.source_order);
    CHECK(changed.measures.front().notes.front().id == before_note.id);
    CHECK(changed.measures.front().notes.front().position == before_note_position);
    CHECK(changed.measures.front().notes.front().lane == before_note.lane);
}

TEST_CASE("Tap timing lock, stale, unstable, invalid and overflowing proposals reject unchanged",
          "[core][tap-timing-apply]") {
    auto history = make_document_history(apply_song(true));
    const auto valid = proposal(history.document);
    REQUIRE(valid);
    const auto before_document = history.document;
    const auto before_next = history.next_revision;
    const auto before_undo = history.undo.size();
    const auto before_redo = history.redo.size();
    const auto unchanged = [&] {
        CHECK(history.document.revision == before_document.revision);
        CHECK(history.document.charts.front().bpms.size() == before_document.charts.front().bpms.size());
        CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
              before_document.charts.front().bpms.front().beats_per_minute);
        CHECK(history.document.charts.front().beat_zero_audio_offset_seconds ==
              before_document.charts.front().beat_zero_audio_offset_seconds);
        CHECK(history.next_revision == before_next);
        CHECK(history.undo.size() == before_undo);
        CHECK(history.redo.size() == before_redo);
    };
    CHECK_FALSE(apply_tap_timing_proposal(history, 8, *valid.proposal, true));
    unchanged();
    CHECK_FALSE(apply_tap_timing_proposal(history, 7, *valid.proposal));
    unchanged();
    auto unstable = stable_estimate();
    unstable.stable = false;
    CHECK_FALSE(proposal(history.document, unstable));
    CHECK_FALSE(proposal(history.document, stable_estimate(), tap_bpm_choice_t::raw, 0));
    CHECK_FALSE(proposal(history.document, stable_estimate(), tap_bpm_choice_t::raw, 44'100, 0));
    CHECK_FALSE(propose_tap_timing_apply(history.document, chart_id_t{99}, stable_estimate(),
                                          44'100, 4, tap_bpm_choice_t::raw));
    auto nan = stable_estimate(std::numeric_limits<double>::quiet_NaN());
    CHECK_FALSE(proposal(history.document, nan));
    auto overflow = stable_estimate(static_cast<double>(std::numeric_limits<std::int64_t>::max()));
    CHECK_FALSE(proposal(history.document, overflow));
    auto invalid_bpm = stable_estimate();
    invalid_bpm.rational_bpm = {0, 1};
    CHECK_FALSE(proposal(history.document, invalid_bpm));
    unchanged();
}

TEST_CASE("Tap timing commit rejects a chart made invalid after proposal without history mutation",
          "[core][tap-timing-apply]") {
    auto history = make_document_history(apply_song());
    const auto proposed = proposal(history.document);
    REQUIRE(proposed);
    history.document.charts.front().measures.clear();
    const auto revision = history.document.revision;
    const auto next = history.next_revision;
    const auto result = apply_tap_timing_proposal(history, revision, *proposed.proposal);
    CHECK_FALSE(result);
    CHECK(history.document.revision == revision);
    CHECK(history.next_revision == next);
    CHECK(history.undo.empty());
}

TEST_CASE("Tap timing signed frame limits reject positive exclusive bound and allow negative bound",
          "[core][tap-timing-apply]") {
    auto song = apply_song(true);
    const auto upper = std::ldexp(1.0, 63);
    CHECK_FALSE(proposal(song, stable_estimate(upper), tap_bpm_choice_t::raw, 1));
    CHECK_FALSE(proposal(song, stable_estimate(-std::nextafter(upper,
        std::numeric_limits<double>::infinity())), tap_bpm_choice_t::raw, 1));
    song.charts.front().beat_zero_audio_offset_seconds = {std::numeric_limits<std::int64_t>::min(), 1};
    const auto minimum = proposal(song, stable_estimate(-upper), tap_bpm_choice_t::raw, 1);
    REQUIRE(minimum);
    CHECK(minimum.proposal->beat_zero_audio_offset_seconds ==
          signed_rational_t{std::numeric_limits<std::int64_t>::min(), 1});
}

TEST_CASE("Tap Apply chooses chart-start bars from exact measure length including eighth meters",
          "[core][tap-timing-apply]") {
    auto song = apply_song(true);
    auto& chart = song.charts.front();
    chart.beat_zero_audio_offset_seconds = {2, 3}; // frame 80 at 120 Hz
    chart.measures.front().definition = {{3, 8}, meter_t{3, 8}};
    const auto eighth = proposal(song, stable_estimate(), tap_bpm_choice_t::raw, 120, 3);
    REQUIRE(eighth);
    CHECK(eighth.proposal->beat_zero_audio_offset_seconds == signed_rational_t{3, 4});
    chart.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    const auto quarter = proposal(song, stable_estimate(), tap_bpm_choice_t::raw, 120, 3);
    REQUIRE(quarter);
    CHECK(quarter.proposal->beat_zero_audio_offset_seconds == signed_rational_t{0, 1});
    chart.measures.front().definition = {{5, 4}, std::nullopt};
    chart.beat_zero_audio_offset_seconds = {7, 6}; // frame 140, nearest 300-frame bar is zero
    const auto unusual = proposal(song, stable_estimate(), tap_bpm_choice_t::raw, 120, 4);
    REQUIRE(unusual);
    CHECK(unusual.proposal->beat_zero_audio_offset_seconds == signed_rational_t{0, 1});
}

} // namespace
