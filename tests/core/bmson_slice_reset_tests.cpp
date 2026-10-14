#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <span>
#include <string>

namespace {

struct reset_fixture_t {
    wormedit::document_history_t history;
    wormedit::chart_id_t chart;
    wormedit::object_id_t target;
    wormedit::object_id_t playable;
};

auto make_reset_fixture() -> reset_fixture_t {
    const std::string json = R"({"version":"1.0.0","info":{"title":"Reset regression",
      "artist":"Wormedit","genre":"test","chart_name":"reset","level":1,
      "init_bpm":120,"judge_rank":100,"total":100,"resolution":240,"mode_hint":"beat-7k"},
      "lines":[{"y":0},{"y":960},{"y":1920}],"bpm_events":[],"stop_events":[],
      "sound_channels":[{"name":"reset.wav","notes":[
        {"x":0,"y":0,"l":0,"c":false},
        {"x":0,"y":240,"l":0,"c":true},
        {"x":1,"y":240,"l":0,"c":true},
        {"x":0,"y":480,"l":0,"c":true},
        {"x":0,"y":720,"l":0,"c":false},
        {"x":0,"y":960,"l":0,"c":true}]}]})";
    const auto parsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{json.data(), json.size()}));
    REQUIRE(parsed.song);
    reset_fixture_t fixture{wormedit::document_history_t::from(*parsed.song)};
    fixture.chart = fixture.history.document.charts.front().id;
    fixture.history.document.audio_assets.front().stream = {48'000, 2, 200'000, true};
    const auto& events = fixture.history.document.charts.front().sound_channels.front().events;
    const auto target = std::ranges::find_if(events, [](const auto& event) {
        return event.position.row == 240 && !event.playable;
    });
    REQUIRE(target != events.end());
    fixture.target = target->id;
    const auto playable = std::ranges::find_if(events, [](const auto& event) {
        return event.position.row == 240 && event.playable;
    });
    REQUIRE(playable != events.end());
    fixture.playable = playable->id;
    return fixture;
}

auto export_json(const wormedit::document_history_t& history) -> std::string {
    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json);
    return *written.json;
}

void require_set_restart(reset_fixture_t& fixture, bool restart) {
    REQUIRE(wormedit::set_bmson_sound_channel_restart(
        fixture.history, fixture.chart, fixture.target, restart));
}

void check_target_pulse(const reset_fixture_t& fixture, bool restart) {
    const auto& events = fixture.history.document.charts.front().sound_channels.front().events;
    for (const auto& event : events) {
        if (event.position.row == 240) CHECK(event.continuation == !restart);
    }
}

void check_history_unchanged(const wormedit::document_history_t& history,
                             const wormedit::document_history_t& before) {
    CHECK(history.document.revision == before.document.revision);
    CHECK(history.undo.size() == before.undo.size());
    CHECK(history.redo.size() == before.redo.size());
}

void check_document_unchanged(const wormedit::document_history_t& history,
                              const wormedit::document_history_t& before) {
    CHECK(history.next_revision == before.next_revision);
    CHECK(export_json(history) == export_json(before));
}

void check_noop(reset_fixture_t& fixture, bool restart) {
    const auto before = fixture.history;
    const auto result = wormedit::set_bmson_sound_channel_restart(
        fixture.history, fixture.chart, fixture.target, restart);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    check_history_unchanged(fixture.history, before);
    check_document_unchanged(fixture.history, before);
}

void check_rejection(reset_fixture_t& fixture,
                     wormedit::object_id_t event, wormedit::chart_id_t chart) {
    const auto before = fixture.history;
    const auto result = wormedit::set_bmson_sound_channel_restart(
        fixture.history, chart, event, true);
    CHECK_FALSE(result.applied);
    CHECK_FALSE(result.diagnostics.empty());
    check_history_unchanged(fixture.history, before);
    check_document_unchanged(fixture.history, before);
}

void check_slice_offsets(const reset_fixture_t& fixture,
                          const std::array<std::uint64_t, 5>& expected) {
    const auto& chart = fixture.history.document.charts.front();
    const auto derived = wormedit::derive_sound_channel_slices(
        chart, chart.sound_channels.front(), 48'000, 200'000);
    REQUIRE(derived);
    REQUIRE(derived.slices.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        CHECK(derived.slices[index].source_begin_frame == expected[index]);
}

void check_slice_ends(const reset_fixture_t& fixture,
                       const std::array<std::uint64_t, 5>& expected,
                       std::uint64_t decoded_frames = 200'000) {
    const auto& chart = fixture.history.document.charts.front();
    const auto derived = wormedit::derive_sound_channel_slices(
        chart, chart.sound_channels.front(), 48'000, decoded_frames);
    REQUIRE(derived);
    REQUIRE(derived.slices.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
        CHECK(derived.slices[index].source_end_frame == expected[index]);
}

void check_exported_restart(const reset_fixture_t& fixture, bool restart) {
    const auto json = export_json(fixture.history);
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{json.data(), json.size()}));
    REQUIRE(reparsed.song);
    const auto& events = reparsed.song->charts.front().sound_channels.front().events;
    for (const auto& event : events) {
        if (event.position.row == 240) CHECK(event.continuation == !restart);
    }
}

void require_undo(reset_fixture_t& fixture) {
    REQUIRE(wormedit::undo_document_command(fixture.history));
}

} // namespace

TEST_CASE("BMSON Restart and Continue set the selected pulse without repeated history edits",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    const auto original = export_json(fixture.history);
    require_set_restart(fixture, true);
    check_target_pulse(fixture, true);
    check_exported_restart(fixture, true);
    check_noop(fixture, true);
    require_set_restart(fixture, false);
    check_target_pulse(fixture, false);
    check_exported_restart(fixture, false);
    check_noop(fixture, false);
    REQUIRE(export_json(fixture.history) == original);
    require_undo(fixture);
    check_target_pulse(fixture, true);
    check_noop(fixture, true);
    require_undo(fixture);
    CHECK(export_json(fixture.history) == original);
}

TEST_CASE("BMSON explicit restart normalizes a mixed pulse without toggling its existing restart",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    auto& events = fixture.history.document.charts.front().sound_channels.front().events;
    const auto playable = std::ranges::find(events, fixture.playable,
                                            &wormedit::sound_channel_event_t::id);
    REQUIRE(playable != events.end());
    playable->continuation = false;
    require_set_restart(fixture, true);
    check_target_pulse(fixture, true);
    check_noop(fixture, true);
}

TEST_CASE("BMSON explicit restart rejects missing selections without changing the chart",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    check_rejection(fixture, {}, fixture.chart);
    check_rejection(fixture, wormedit::object_id_t{999'999}, fixture.chart);
    check_rejection(fixture, fixture.playable, fixture.chart);
    check_rejection(fixture, fixture.target, {});
}

TEST_CASE("BMSON reset offsets continue from the last restart until the next restart pulse",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    check_slice_offsets(fixture, {0, 24'000, 48'000, 0, 24'000});
    check_slice_ends(fixture, {24'000, 48'000, 72'000, 24'000, 200'000});
    require_set_restart(fixture, true);
    check_slice_offsets(fixture, {0, 0, 24'000, 0, 24'000});
    check_slice_ends(fixture, {24'000, 24'000, 48'000, 24'000, 200'000});
    require_set_restart(fixture, false);
    check_slice_offsets(fixture, {0, 24'000, 48'000, 0, 24'000});
}

TEST_CASE("BMSON reset keeps a nonzero source clip origin and its final frame boundary",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    auto& clip = fixture.history.document.charts.front().sound_channels.front().source_clip;
    clip.begin_frame = 1'000;
    clip.end_frame = 120'000;
    require_set_restart(fixture, true);
    check_slice_offsets(fixture, {1'000, 1'000, 25'000, 1'000, 25'000});
    check_slice_ends(fixture, {25'000, 25'000, 49'000, 25'000, 120'000});
}

TEST_CASE("BMSON restart clips the previous source range before rewinding an exhausted source",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    auto& channel = fixture.history.document.charts.front().sound_channels.front();
    channel.source_clip.begin_frame = 1'000;
    channel.source_clip.end_frame = 10'000;
    for (auto& event : channel.events) event.continuation = false;
    check_slice_offsets(fixture, {1'000, 1'000, 1'000, 1'000, 1'000});
    check_slice_ends(fixture, {10'000, 10'000, 10'000, 10'000, 10'000});
    check_slice_ends(fixture, {8'000, 8'000, 8'000, 8'000, 8'000}, 8'000);
}

TEST_CASE("continuation points after the end of the sample derive as empty slices",
          "[unit][core][bmson][slice-reset]") {
    auto fixture = make_reset_fixture();
    const auto& chart = fixture.history.document.charts.front();
    // 24,000 frames at 48 kHz is half a second: the point at beat 2 (one second in) is past the end.
    const auto derived = wormedit::derive_sound_channel_slices(chart, chart.sound_channels.front(), 48'000, 30'000);
    REQUIRE(derived);
    REQUIRE(derived.slices.size() == 5);
    CHECK(derived.slices[0].source_begin_frame == 0);
    CHECK(derived.slices[1].source_begin_frame == 24'000);
    // The continuation at row 480 starts at 48,000 > 30,000: empty, at the end of the source.
    CHECK(derived.slices[2].source_begin_frame == 30'000);
    CHECK(derived.slices[2].source_end_frame == 30'000);
    // A restart still plays from the beginning.
    CHECK(derived.slices[3].source_begin_frame == 0);
    CHECK(derived.slices[3].restart);
}
