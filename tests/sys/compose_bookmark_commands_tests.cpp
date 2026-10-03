#include "sys/compose_bookmark_commands.h"

#include <wormedit/audio/decoder.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
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
