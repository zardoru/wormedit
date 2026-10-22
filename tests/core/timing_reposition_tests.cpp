#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/timing_reposition.h>
#include <wormedit/core/timing.h>
#include <wormedit/formats/osu_mania.h>
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
using namespace wormedit;
namespace {
auto anchored_song() -> song_document_t {
    const std::string text = "osu file format v14\n[General]\nMode:3\nAudioFilename:song.ogg\n"
        "[Difficulty]\nCircleSize:4\n[TimingPoints]\n0,500,4,1,0,100,1,0\n"
        "[HitObjects]\n64,192,500,128,0,1500:0:0:0:0:\n192,192,1000,1,0,0:0:0:0:\n";
    auto parsed = parse_osu_mania_bytes(std::as_bytes(std::span{text.data(), text.size()}));
    REQUIRE(parsed.value);
    auto song = std::move(*parsed.value);
    auto& chart = song.charts.front();
    const auto& hold = chart.measures.front().notes.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{900}, .name = "BGM"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{901}, .position = hold.position,
        .source_lane = object_id_t{900}, .end = hold.end});
    chart.bookmarks.push_back({.id = object_id_t{902}, .anchor = marker_anchor_t::audio_time, .audio_milliseconds = 750});
    chart.sections.push_back({.id = object_id_t{903}, .anchor = marker_anchor_t::audio_time,
        .audio_millisecond_span = {{250, 1250}}});
    chart.sound_channels.push_back({.id = object_id_t{904}, .name = "Retained source",
        .source_clip = {.id = clip_id_t{905}, .source = song.audio_assets.front().id},
        .events = {{.id = object_id_t{906}, .position = hold.position, .end = hold.end,
            .lane = chart.lane_layouts.front().layout.lanes.front().id}}});
    REQUIRE(validate_document(song).empty());
    return song;
}
auto chart_with_head_warp(chart_document_t chart) -> chart_document_t {
    const auto begin = chart.measures.front().notes.front().position;
    chart.warps.push_back({.id = object_id_t{950},
        .span = {begin, {begin.measure, begin.row + chart.divisions_per_standard_measure / 8}}});
    return chart;
}
struct audio_point_t {
    object_id_t id;
    signed_rational_t head;
    std::optional<signed_rational_t> end;
    auto operator==(const audio_point_t&) const -> bool = default;
};
auto audio_time(const timing_index_t& timing, const measure_index_t& measures, local_position_t p) -> signed_rational_t {
    const auto row = measures.row_of(p, measure_chart_end_policy_t::allow);
    REQUIRE(row);
    const auto seconds = timing.seconds_at(*row);
    REQUIRE(seconds.seconds);
    return *seconds.seconds;
}
auto audio_points(const chart_document_t& chart) -> std::vector<audio_point_t> {
    const auto timing = timing_index_t::from(chart);
    const auto measures = measure_index_t::from(chart);
    REQUIRE(timing.value);
    REQUIRE(measures);
    std::vector<audio_point_t> result;
    const auto append = [&](const auto& events) {
        for (const auto& event : events) result.push_back({event.id, audio_time(*timing.value, *measures, event.position),
            event.end ? std::optional{audio_time(*timing.value, *measures, *event.end)} : std::nullopt});
    };
    for (const auto& measure : chart.measures) { append(measure.notes); append(measure.bgm_events); }
    for (const auto& channel : chart.sound_channels) append(channel.events);
    std::ranges::sort(result, {}, &audio_point_t::id);
    return result;
}
auto initial_bpm_edit(const song_document_t& song, signed_rational_t bpm) -> bpm_edit_action_t {
    const auto& chart = song.charts.front();
    return update_edit_t<bpm_edit_value_t>{.chart = chart.id, .id = chart.bpms.front().id,
        .value = bpm_edit_value_t::from(chart.bpms.front().position, {.beats_per_minute = bpm})};
}
void check_musical_preserved(const song_document_t& before, const document_history_t& history) {
    const auto& notes = history.document.charts.front().measures.front().notes;
    const auto& original = before.charts.front().measures.front().notes;
    CHECK(notes.front().position == original.front().position);
    CHECK(notes.front().end == original.front().end);
    CHECK(audio_points(history.document.charts.front()) != audio_points(before.charts.front()));
}
void check_rounding_preview(const timing_lane_pseudonote_outcome_t& stage,
    const document_history_t& history, const std::vector<audio_point_t>& original) {
    CHECK_FALSE(stage.value);
    REQUIRE_FALSE(stage.diagnostics.empty());
    CHECK(stage.diagnostics.front().code == diagnostic_code_t::invalid_resolution);
    CHECK(stage.report()->reposition.rounded_positions > 0);
    CHECK(history.undo.empty());
    CHECK(audio_points(history.document.charts.front()) == original);
}
void check_audio_preserved(const song_document_t& before, const document_history_t& history) {
    const auto& chart = history.document.charts.front();
    CHECK(audio_points(chart) == audio_points(before.charts.front()));
    CHECK(chart.bookmarks.front().audio_milliseconds == 750);
    CHECK(chart.sections.front().audio_millisecond_span == before.charts.front().sections.front().audio_millisecond_span);
}
}
TEST_CASE("BPM edits keep musical positions by default and preserve audio only when requested", "[core][timing-anchoring]") {
    const auto song = anchored_song();
    auto history = document_history_t::from(song);
    timing_anchoring_t options;
    bool audio{};
    SECTION("musical default") {}
    SECTION("audio-time policy") { options.positions = timing_position_policy_t::audio; audio = true; }
    REQUIRE(apply_bpm_edit(history, initial_bpm_edit(history.document, {240, 1}), {}, options).applied);
    CHECK(history.undo.size() == 1);
    if (audio) check_audio_preserved(song, history);
    else check_musical_preserved(song, history);
    REQUIRE(undo_document_command(history));
    CHECK(audio_points(history.document.charts.front()) == audio_points(song.charts.front()));
}
TEST_CASE("Timing preview reports rounding and requires explicit quantization permission", "[core][timing-anchoring]") {
    auto history = document_history_t::from(anchored_song());
    const auto original = audio_points(history.document.charts.front());
    const auto chart = history.document.charts.front().id;
    const auto event = history.document.charts.front().bpms.front().id;
    auto stage = timing_lane_pseudonote_preview_t::propose(history, chart, timing_lane_event_ref_t{timing_lane_kind_t::bpm, event}, false);
    stage = timing_lane_pseudonote_preview_t::revise(std::move(stage), history,
        timing_anchoring_t{.positions = timing_position_policy_t::audio}, false);
    stage = timing_lane_pseudonote_preview_t::revise(std::move(stage), history,
        timing_bpm_value_t{{1234567, 10000}}, false);
    check_rounding_preview(stage, history, original);
    stage = timing_lane_pseudonote_preview_t::revise(std::move(stage), history,
        timing_anchoring_t{.positions = timing_position_policy_t::audio, .allow_quantization = true}, false);
    REQUIRE(stage.value);
    CHECK(apply_proposal(history, stage, false).applied);
}
TEST_CASE("Pause insertion and deletion retain all sound endpoint audio times in one edit each", "[core][timing-anchoring]") {
    const auto song = anchored_song();
    auto history = document_history_t::from(song);
    const auto& chart = history.document.charts.front();
    const auto chart_id = chart.id;
    auto stage = timing_lane_pseudonote_preview_t::propose(history, chart.id,
        timing_lane_pseudonote_state_t{.anchoring = {.positions = timing_position_policy_t::audio},
            .kind = timing_lane_kind_t::stop, .position = {chart.measures.front().id, chart.divisions_per_standard_measure / 8},
            .value = timing_pause_value_t{.duration = {1, 10}}}, false);
    REQUIRE(stage.value);
    REQUIRE(apply_proposal(history, stage, false).applied);
    check_audio_preserved(song, history);
    const auto id = history.document.charts.front().pauses.front().id;
    REQUIRE(delete_existing_timing_lane_event(history, chart_id, timing_lane_kind_t::stop, id, false,
        {.positions = timing_position_policy_t::audio}).applied);
    check_audio_preserved(song, history);
    CHECK(history.undo.size() == 2);
}

TEST_CASE("Audio-time rounding rejects newly colliding note heads atomically", "[core][timing-anchoring]") {
    auto song = anchored_song();
    auto& chart = song.charts.front();
    auto& notes = chart.measures.front().notes;
    notes.front().kind = note_kind_t::tap;
    notes.front().end.reset();
    notes.back().lane = notes.front().lane;
    auto history = document_history_t::from(song);
    const auto result = apply_bpm_edit(history, initial_bpm_edit(history.document, {1, 100}), {},
        {.positions = timing_position_policy_t::audio, .allow_quantization = true});
    CHECK_FALSE(result.applied);
    CHECK(std::ranges::any_of(result.diagnostics, [](const auto& d) { return d.code == diagnostic_code_t::collision; }));
    CHECK(history.undo.empty());
    CHECK(audio_points(history.document.charts.front()) == audio_points(song.charts.front()));
}

TEST_CASE("Audio anchoring uses the audible exit of a collapsed warp plateau", "[core][timing-anchoring][warp-anchoring]") {
    const auto before = anchored_song().charts.front();
    auto after = chart_with_head_warp(before);
    const auto result = preserve_audio_positions(before, after, {.positions = timing_position_policy_t::audio});
    REQUIRE(result.ok());
    CHECK(audio_points(after) == audio_points(before));
    const auto timing = timing_index_t::from(after);
    const auto measures = measure_index_t::from(after);
    REQUIRE(timing.value);
    REQUIRE(measures);
    CHECK_FALSE(timing.value->seconds_at(*measures, after.measures.front().notes.front().position).inside_warp);
}
TEST_CASE("Audio anchoring rejects sound objects with no playable time inside a source warp", "[core][timing-anchoring][warp-anchoring]") {
    auto song = anchored_song();
    song.charts.front() = chart_with_head_warp(std::move(song.charts.front()));
    auto history = document_history_t::from(std::move(song));
    const auto result = apply_bpm_edit(history, initial_bpm_edit(history.document, {240, 1}), {},
        {.positions = timing_position_policy_t::audio});
    CHECK_FALSE(result.applied);
    REQUIRE_FALSE(result.diagnostics.empty());
    CHECK(result.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
}
