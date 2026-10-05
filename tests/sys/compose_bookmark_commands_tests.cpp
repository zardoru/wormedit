#include "sys/compose_bookmark_commands.h"
#include "sys/application_commands.h"

#include <wormedit/audio/decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <string>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto bookmark_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{.id = measure_id_t{3}}, {.id = measure_id_t{4}}},
            .lane_layouts =
                {{.id = object_id_t{21},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "one-key",
                             .lanes = {{.id = lane_id_t{1}, .name = "Key", .display_order = 0}}}}},
            .input_profile = {.profile_id = "one-key",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
            .bpms = {{.id = object_id_t{22},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {240, 1}}},
            .bookmarks = {{.id = object_id_t{30},
                           .anchor = marker_anchor_t::beat,
                           .position = local_position_t{measure_id_t{4}, 480}}},
            .sections = {{.id = object_id_t{31},
                          .label = "section",
                          .anchor = marker_anchor_t::beat,
                          .beat_span = position_span_t{{measure_id_t{3}, 240},
                                                       {measure_id_t{3}, 480}}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

struct fixture_t {
    document_history_t history{make_document_history(bookmark_song())};
    editor_interaction_state_t state;
    application_command_state_t application;

    explicit fixture_t(local_position_t cursor) {
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = make_editor_interaction(history, chart_id_t{2}, lane_id_t{1}, cursor, snaps, 4,
                                             192.0);
        REQUIRE(setup);
        state = std::move(*setup.state);
    }
};

} // namespace

TEST_CASE("nearest snap row picks the closest row and ties go earlier",
          "[unit][sys][bookmark-commands]") {
    // A quarter-note snap of a 960-division measure is 240 rows.
    CHECK(nearest_snap_row(100.0, 960, 4, 1920) == std::optional<std::uint64_t>{0});
    CHECK(nearest_snap_row(130.0, 960, 4, 1920) == std::optional<std::uint64_t>{240});
    CHECK(nearest_snap_row(120.0, 960, 4, 1920) == std::optional<std::uint64_t>{0});
    CHECK(nearest_snap_row(5000.0, 960, 4, 1920) == std::optional<std::uint64_t>{1680});
    CHECK_FALSE(nearest_snap_row(10.0, 960, 7, 1920));
    CHECK_FALSE(nearest_snap_row(10.0, 960, 4, 0));
}

TEST_CASE("Go To Bookmark shortcut maps to its application command",
          "[unit][sys][bookmark-commands]") {
    const auto parsed = parse_application_command("navigation.go_to_bookmark");
    REQUIRE(parsed);
    CHECK(parsed->kind == application_command_kind_t::navigation_go_to_bookmark);
}

TEST_CASE("bookmark navigation moves the cursor over bookmarks only",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    const auto next = navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                             marker_navigation_direction_t::next);
    REQUIRE(next.accepted);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto beyond = navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    CHECK(beyond.accepted);
    CHECK(beyond.status == "No next bookmark.");
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto previous = navigate_beat_bookmark(fixture.history, fixture.state,
                                                 fixture.application,
                                                 marker_navigation_direction_t::previous);
    CHECK(previous.accepted);
    CHECK(previous.status == "No previous bookmark.");
}

TEST_CASE("playing bookmark navigation seeks once instead of moving the cursor",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    std::uint64_t seeks{};
    std::uint64_t frame{};
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 0.0;
    fixture.application.transport_seek = [&](std::uint64_t target) {
        ++seeks;
        frame = target;
        return std::string{};
    };
    const auto before = fixture.state.cursor;
    const auto result = navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    REQUIRE(result.accepted);
    CHECK(seeks == 1);
    CHECK(frame > 0);
    CHECK((fixture.state.cursor == before));
    CHECK_FALSE(result.feedback_posted);
}

TEST_CASE("playing bookmark navigation retains producer publication ownership", "[unit][sys][bookmark-commands][notification]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 0.0;
    fixture.application.transport_seek_posts_feedback = true;
    fixture.application.transport_seek = [](std::uint64_t) { return std::string{"Opaque producer feedback."}; };
    const auto result = navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    CHECK(result.accepted);
    CHECK(result.feedback_posted);
    CHECK(result.status == "Opaque producer feedback.");
    fixture.application.transport_seek = {};
    const auto rejected = navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                                 marker_navigation_direction_t::next);
    CHECK_FALSE(rejected.accepted);
    CHECK_FALSE(rejected.feedback_posted);
}

TEST_CASE("add bookmark uses the cursor or the snapped playhead and refuses duplicates",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 240}};
    const auto added = add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    REQUIRE(added.accepted);
    CHECK(added.document_changed);
    CHECK(fixture.history.document.charts.front().bookmarks.size() == 2);

    const auto duplicate = add_beat_bookmark_here(fixture.history, fixture.state,
                                                  fixture.application);
    CHECK_FALSE(duplicate.accepted);
    CHECK(duplicate.status == "A bookmark already exists on this row.");

    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().bookmarks.size() == 1);

    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 1000.0;
    const auto playing = add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    REQUIRE(playing.accepted);
    const auto& bookmarks = fixture.history.document.charts.front().bookmarks;
    REQUIRE(bookmarks.size() == 2);
    // Absolute row 1000 snaps to 960, which is the first row of the second measure.
    CHECK((bookmarks.back().position == local_position_t{measure_id_t{4}, 0}));
}

TEST_CASE("bookmark frames clamp to the preview end", "[unit][sys][bookmark-commands]") {
    const auto song = bookmark_song();
    const auto& chart = song.charts.front();
    const auto measures = build_measure_index(chart);
    const auto timing = build_timing_index(chart);
    REQUIRE(timing.index);
    REQUIRE(std::holds_alternative<measure_index_t>(measures));
    const auto& index = std::get<measure_index_t>(measures);
    const auto frame = bookmark_preview_frame(*timing.index, index, {measure_id_t{4}, 480},
                                              std::nullopt);
    REQUIRE(frame);
    CHECK(*frame > 0);
    CHECK(bookmark_preview_frame(*timing.index, index, {measure_id_t{4}, 480}, 10) ==
          std::optional<std::uint64_t>{10});
    CHECK(bookmark_preview_frame(*timing.index, index, {measure_id_t{3}, 0}, std::nullopt) ==
          std::optional<std::uint64_t>{0});
}

TEST_CASE("bookmark chooser selection jumps to a stable beat ID and rejects stale rows",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    const auto selected = navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision, chart_id_t{2}, object_id_t{30});
    REQUIRE(selected.accepted);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto cursor = fixture.state.cursor;
    const auto stale = navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision + 1, chart_id_t{2}, object_id_t{30});
    CHECK_FALSE(stale.accepted);
    CHECK(fixture.state.cursor == cursor);
    const auto missing = navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision, chart_id_t{2}, object_id_t{999});
    CHECK_FALSE(missing.accepted);
    CHECK(fixture.state.cursor == cursor);
}

TEST_CASE("bookmark chooser selection preserves playback for beat and audio anchors",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.history.document.charts.front().bookmarks.push_back({
        .id = object_id_t{32}, .label = "audio", .anchor = marker_anchor_t::audio_time,
        .audio_milliseconds = 500});
    fixture.application.transport_preview_playing = [] { return true; };
    std::uint64_t frame{};
    std::int64_t audio_ms{};
    fixture.application.transport_preview_end_frame = [] { return std::optional<std::uint64_t>{}; };
    fixture.application.transport_seek = [&](std::uint64_t target) {
        frame = target;
        return std::string{};
    };
    fixture.application.transport_seek_audio_milliseconds = [&](std::int64_t target) {
        audio_ms = target;
        return std::string{};
    };
    const auto before = fixture.state.cursor;
    const auto beat = navigate_to_bookmark(fixture.history, fixture.state, fixture.application,
        fixture.history.document.revision, chart_id_t{2}, object_id_t{30});
    REQUIRE(beat.accepted);
    CHECK(frame > 0);
    CHECK(fixture.state.cursor == before);
    const auto audio = navigate_to_bookmark(fixture.history, fixture.state, fixture.application,
        fixture.history.document.revision, chart_id_t{2}, object_id_t{32});
    REQUIRE(audio.accepted);
    CHECK(audio_ms == 500);
    CHECK(fixture.state.cursor == before);
}

TEST_CASE("stopped audio bookmark chooser selection resolves the musical row",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.history.document.charts.front().bookmarks.push_back({
        .id = object_id_t{32}, .label = "audio", .anchor = marker_anchor_t::audio_time,
        .audio_milliseconds = 500});
    const auto selected = navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision, chart_id_t{2}, object_id_t{32});
    REQUIRE(selected.accepted);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{3}, 480}));
}

TEST_CASE("playing bookmark jumps advance from the live playhead and preserve the edit cursor",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    auto& bookmarks = fixture.history.document.charts.front().bookmarks;
    bookmarks.push_back({.id = object_id_t{32}, .anchor = marker_anchor_t::beat,
                          .position = local_position_t{measure_id_t{3}, 240}});
    fixture.application.transport_preview_playing = [] { return true; };
    std::vector<std::uint64_t> seeks;
    fixture.application.transport_seek = [&](std::uint64_t frame) {
        seeks.push_back(frame);
        return std::string{};
    };
    const auto cursor = fixture.state.cursor;
    fixture.application.preview_playhead_absolute_row = 0.0;
    REQUIRE(navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                   marker_navigation_direction_t::next).accepted);
    fixture.application.preview_playhead_absolute_row = 240.0;
    REQUIRE(navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                   marker_navigation_direction_t::next).accepted);
    REQUIRE(seeks.size() == 2);
    CHECK(seeks[1] > seeks[0]);
    CHECK(fixture.state.cursor == cursor);
}

TEST_CASE("previous playing bookmark includes the row below a fractional playhead",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.history.document.charts.front().bookmarks.push_back(
        {.id = object_id_t{32}, .anchor = marker_anchor_t::beat,
         .position = local_position_t{measure_id_t{3}, 240}});
    fixture.application.transport_preview_playing = [] { return true; };
    fixture.application.preview_playhead_absolute_row = 240.25;
    std::uint64_t sought{};
    fixture.application.transport_seek = [&](std::uint64_t frame) {
        sought = frame;
        return std::string{};
    };
    REQUIRE(navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                   marker_navigation_direction_t::previous).accepted);
    CHECK(sought == audio::output_sample_rate / 4);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{3}, 0}));
}

TEST_CASE("repeated previous playing bookmark within 300 ms steps past the bookmark just reached",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    auto& bookmarks = fixture.history.document.charts.front().bookmarks;
    bookmarks.push_back({.id = object_id_t{32}, .anchor = marker_anchor_t::beat,
                          .position = local_position_t{measure_id_t{3}, 240}});
    bookmarks.push_back({.id = object_id_t{33}, .anchor = marker_anchor_t::beat,
                          .position = local_position_t{measure_id_t{3}, 0}});
    fixture.application.transport_preview_playing = [] { return true; };
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds{10};
    fixture.application.bookmark_clock = [&] { return now; };
    std::vector<std::uint64_t> seeks;
    fixture.application.transport_seek = [&](std::uint64_t frame) {
        seeks.push_back(frame);
        return std::string{};
    };
    const auto previous = [&] {
        return navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                      marker_navigation_direction_t::previous);
    };
    constexpr auto quarter = audio::output_sample_rate / 4;
    // The live playhead sits past bookmark B (row 1440 -> frame 6 * quarter) and does not
    // advance between presses, as when the audio clock lags the seek.
    fixture.application.preview_playhead_absolute_row = 1500.0;
    REQUIRE(previous().accepted);
    CHECK(seeks.back() == 6 * quarter);
    now += std::chrono::milliseconds{100};
    REQUIRE(previous().accepted);
    CHECK(seeks.back() == quarter);
    now += std::chrono::milliseconds{100};
    REQUIRE(previous().accepted);
    CHECK(seeks.back() == 0);
    // Past the window the live playhead is the origin again.
    now += std::chrono::milliseconds{400};
    REQUIRE(previous().accepted);
    CHECK(seeks.back() == 6 * quarter);
}
