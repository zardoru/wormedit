#include "../support/stale_action_assertions.h"
#include "sys/bookmark_commands.h"
#include "sys/compose_navigation.h"
#include "sys/application_commands.h"
#include "sys/compose_playhead_skip.h"
#include "sys/compose_playhead_cursor.h"

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
    document_history_t history{document_history_t::from(bookmark_song())};
    editor_interaction_state_t state;
    application_command_state_t application;

    explicit fixture_t(local_position_t cursor) {
        constexpr std::array snaps{std::uint64_t{4}};
        auto setup = editor_interaction_state_t::from(history, chart_id_t{2}, lane_id_t{1}, cursor, snaps, 4,
                                             192.0);
        REQUIRE(setup);
        state = std::move(*setup.state);
    }
};

void check_stale_compose_navigation(fixture_t& fixture) {
    const auto navigation = navigate_compose_position(fixture.history, fixture.state,
        fixture.application, {.position = fixture.state.cursor}, fixture.history.document.revision + 1);
    wormedit::test::check_stale_action_message(navigation.message, "Compose navigation");
    CHECK_FALSE(navigation.accepted);
}

} // namespace

TEST_CASE("nearest snap row picks the closest row and ties go earlier",
          "[unit][sys][bookmark-commands]") {
    const auto chart = bookmark_song().charts.front();
    auto index = measure_index_t::from(chart).value();
    // A quarter-note snap of a 960-division measure is 240 rows.
    CHECK(nearest_snap_row(chart, index, 100.0, 4) == std::optional<std::uint64_t>{0});
    CHECK(nearest_snap_row(chart, index, 130.0, 4) == std::optional<std::uint64_t>{240});
    CHECK(nearest_snap_row(chart, index, 120.0, 4) == std::optional<std::uint64_t>{0});
    CHECK(nearest_snap_row(chart, index, 5000.0, 4) == std::optional<std::uint64_t>{1680});
    CHECK_FALSE(nearest_snap_row(chart, index, 10.0, 7));
    index.total_rows = 0;
    CHECK_FALSE(nearest_snap_row(chart, index, 10.0, 4));
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
    const auto next = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                             marker_navigation_direction_t::next);
    REQUIRE(next.accepted);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto beyond = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    CHECK(beyond.accepted);
    CHECK(beyond.message == "No next bookmark.");
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto previous = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state,
                                                 fixture.application,
                                                 marker_navigation_direction_t::previous);
    CHECK(previous.accepted);
    CHECK(previous.message == "No previous bookmark.");
}

TEST_CASE("playing bookmark navigation seeks once instead of moving the cursor",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    std::uint64_t seeks{};
    std::uint64_t frame{};
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 0.0;
    fixture.application.services.transport_seek = [&](std::uint64_t target) {
        ++seeks;
        frame = target;
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    const auto before = fixture.state.cursor;
    const auto result = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    REQUIRE(result.accepted);
    CHECK(seeks == 1);
    CHECK(frame > 0);
    CHECK((fixture.state.cursor == before));
    CHECK(result.source == notification_source_t::input);
}

TEST_CASE("playing bookmark navigation retains producer feedback metadata",
          "[unit][sys][bookmark-commands][notification]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 0.0;
    fixture.application.services.transport_seek = [](std::uint64_t) {
        return compose_action_outcome_t::from(std::string{"Opaque producer feedback."},
                                              notification_severity_t::hint,
                                              notification_source_t::input);
    };
    const auto result = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                               marker_navigation_direction_t::next);
    CHECK(result.accepted);
    CHECK(result.severity == notification_severity_t::hint);
    CHECK(result.source == notification_source_t::input);
    CHECK(result.message == "Opaque producer feedback.");
    fixture.application.services.transport_seek = {};
    const auto rejected = fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                                 marker_navigation_direction_t::next);
    CHECK_FALSE(rejected.accepted);
    CHECK(rejected.severity == notification_severity_t::warning);
}

TEST_CASE("add bookmark uses the cursor or the snapped playhead and refuses duplicates",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 240}};
    const auto added = fixture.application.bookmarks.add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    REQUIRE(added.accepted);
    CHECK(added.mutated);
    CHECK(fixture.history.document.charts.front().bookmarks.size() == 2);

    const auto duplicate = fixture.application.bookmarks.add_beat_bookmark_here(fixture.history, fixture.state,
                                                  fixture.application);
    CHECK_FALSE(duplicate.accepted);
    CHECK(duplicate.message == "A bookmark already exists on this row.");

    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().bookmarks.size() == 1);

    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 1000.0;
    const auto playing = fixture.application.bookmarks.add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    REQUIRE(playing.accepted);
    const auto& bookmarks = fixture.history.document.charts.front().bookmarks;
    REQUIRE(bookmarks.size() == 2);
    // Absolute row 1000 snaps to 960, which is the first row of the second measure.
    CHECK((bookmarks.back().position == local_position_t{measure_id_t{4}, 0}));
}

TEST_CASE("bookmark frames clamp to the preview end", "[unit][sys][bookmark-commands]") {
    const auto song = bookmark_song();
    const auto& chart = song.charts.front();
    const auto measures = measure_index_t::from(chart);
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    REQUIRE(measures.has_value());
    const auto& index = measures.value();
    const auto frame = bookmark_preview_frame(*timing.value, index, {measure_id_t{4}, 480},
                                              std::nullopt);
    REQUIRE(frame);
    CHECK(*frame > 0);
    CHECK(bookmark_preview_frame(*timing.value, index, {measure_id_t{4}, 480}, 10) ==
          std::optional<std::uint64_t>{10});
    CHECK(bookmark_preview_frame(*timing.value, index, {measure_id_t{3}, 0}, std::nullopt) ==
          std::optional<std::uint64_t>{0});
}

TEST_CASE("bookmark chooser selection jumps to a stable beat ID and rejects stale rows",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    const auto selected = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision, chart_id_t{2}, object_id_t{30});
    REQUIRE(selected.accepted);
    CHECK((fixture.state.cursor == local_position_t{measure_id_t{4}, 480}));

    const auto cursor = fixture.state.cursor;
    check_stale_compose_navigation(fixture);
    const auto stale = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state,
        fixture.application, fixture.history.document.revision + 1, chart_id_t{2}, object_id_t{30});
    CHECK_FALSE(stale.accepted);
    wormedit::test::check_stale_action_message(stale.message, "Bookmark chooser");
    CHECK(fixture.state.cursor == cursor);
    const auto missing = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state,
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
    fixture.application.services.transport_preview_playing = [] { return true; };
    std::uint64_t frame{};
    std::int64_t audio_ms{};
    fixture.application.services.transport_preview_end_frame = [] { return std::optional<std::uint64_t>{}; };
    fixture.application.services.transport_seek = [&](std::uint64_t target) {
        frame = target;
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    fixture.application.services.transport_seek_audio_milliseconds = [&](std::int64_t target) {
        audio_ms = target;
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    const auto before = fixture.state.cursor;
    const auto beat = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state, fixture.application,
        fixture.history.document.revision, chart_id_t{2}, object_id_t{30});
    REQUIRE(beat.accepted);
    CHECK(frame > 0);
    CHECK(fixture.state.cursor == before);
    const auto audio = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state, fixture.application,
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
    const auto selected = fixture.application.bookmarks.navigate_to_bookmark(fixture.history, fixture.state,
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
    fixture.application.services.transport_preview_playing = [] { return true; };
    std::vector<std::uint64_t> seeks;
    fixture.application.services.transport_seek = [&](std::uint64_t frame) {
        seeks.push_back(frame);
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    const auto cursor = fixture.state.cursor;
    fixture.application.project.transport.preview_playhead_absolute_row = 0.0;
    REQUIRE(fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                   marker_navigation_direction_t::next).accepted);
    fixture.application.project.transport.preview_playhead_absolute_row = 240.0;
    REQUIRE(fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
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
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 240.25;
    std::uint64_t sought{};
    fixture.application.services.transport_seek = [&](std::uint64_t frame) {
        sought = frame;
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    REQUIRE(fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
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
    fixture.application.services.transport_preview_playing = [] { return true; };
    auto now = std::chrono::steady_clock::time_point{} + std::chrono::seconds{10};
    fixture.application.bookmarks.fixed_clock = &now;
    std::vector<std::uint64_t> seeks;
    fixture.application.services.transport_seek = [&](std::uint64_t frame) {
        seeks.push_back(frame);
        return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    const auto previous = [&] {
        return fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                      marker_navigation_direction_t::previous);
    };
    constexpr auto quarter = audio::output_sample_rate / 4;
    // The live playhead sits past bookmark B (row 1440 -> frame 6 * quarter) and does not
    // advance between presses, as when the audio clock lags the seek.
    fixture.application.project.transport.preview_playhead_absolute_row = 1500.0;
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

TEST_CASE("adding a bookmark past the last measure explains it in plain words and changes nothing",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    fixture.state.virtual_bms_cursor_row = 5000;
    const auto revision = fixture.history.document.revision;
    const auto outcome = fixture.application.bookmarks.add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    CHECK_FALSE(outcome.accepted);
    CHECK(outcome.message == "Bookmarks can only go inside existing measures. "
                             "Add measures up to the cursor first.");
    CHECK(outcome.message.find("materialized") == std::string::npos);
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.document.charts.front().bookmarks.size() == 1);
}

namespace {

struct mixed_navigation_fixture_t {
    fixture_t fixture{{measure_id_t{3}, 0}};
    std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::time_point{} + std::chrono::seconds{10};
    std::vector<std::uint64_t> seeks;

    mixed_navigation_fixture_t() {
        fixture.application.services.transport_preview_playing = [] { return true; };
        fixture.application.bookmarks.fixed_clock = &now;
        fixture.application.services.transport_seek = [this](std::uint64_t frame) {
            seeks.push_back(frame);
            return compose_action_outcome_t::from(std::string{}, notification_severity_t::hint,
                                                  notification_source_t::input);
        };
    }
    auto skip(double live_row, double amount) -> bool {
        fixture.application.project.transport.preview_playhead_absolute_row = live_row;
        return skip_playing_playhead(fixture.history, fixture.application,
                                     fixture.history.document.charts.front(), 4,
                                     playhead_skip_unit_t::snap, amount).accepted;
    }
    auto previous_bookmark(double live_row) -> bool {
        fixture.application.project.transport.preview_playhead_absolute_row = live_row;
        return fixture.application.bookmarks.navigate_beat_bookmark(fixture.history, fixture.state, fixture.application,
                                      marker_navigation_direction_t::previous).accepted;
    }
};

constexpr auto quarter_frames = audio::output_sample_rate / 4;

} // namespace

TEST_CASE("a bookmark chooser jump ends a wheel skip repeat sequence",
          "[unit][sys][bookmark-commands]") {
    mixed_navigation_fixture_t mixed;
    REQUIRE(mixed.skip(10.0, 1.0));                       // Notch to row 240.
    CHECK(mixed.seeks.back() == quarter_frames);
    const auto jumped = mixed.fixture.application.bookmarks.navigate_to_bookmark(mixed.fixture.history, mixed.fixture.state,
        mixed.fixture.application, mixed.fixture.history.document.revision, chart_id_t{2},
        object_id_t{30});                                 // Bookmark at row 1440.
    REQUIRE(jumped.accepted);
    CHECK(mixed.seeks.back() == 6 * quarter_frames);
    mixed.now += std::chrono::milliseconds{50};
    REQUIRE(mixed.skip(1440.0, 1.0));                     // Next notch advances from 1440.
    CHECK(mixed.seeks.back() == 7 * quarter_frames);
}

TEST_CASE("a manual seek, stop or restart ends the repeat sequences",
          "[unit][sys][bookmark-commands]") {
    mixed_navigation_fixture_t mixed;
    mixed.fixture.history.document.charts.front().bookmarks.push_back(
        {.id = object_id_t{32}, .anchor = marker_anchor_t::beat,
         .position = local_position_t{measure_id_t{3}, 240}});
    REQUIRE(mixed.previous_bookmark(1500.0));             // Bookmark at 1440 recorded.
    CHECK(mixed.seeks.back() == 6 * quarter_frames);
    // The transport callbacks (stop, play, restart) and manual seeks call this.
    mixed.fixture.application.invalidate_playing_seek_records();
    mixed.now += std::chrono::milliseconds{50};
    REQUIRE(mixed.previous_bookmark(1500.0));             // Starts again from the live playhead.
    CHECK(mixed.seeks.back() == 6 * quarter_frames);
    REQUIRE(mixed.skip(10.0, 1.0));
    mixed.fixture.application.invalidate_playing_seek_records();
    mixed.now += std::chrono::milliseconds{50};
    REQUIRE(mixed.skip(10.0, 1.0));
    CHECK(mixed.seeks.back() == quarter_frames);
}

TEST_CASE("bookmark steps and wheel skips do not continue each other's sequences",
          "[unit][sys][bookmark-commands]") {
    mixed_navigation_fixture_t mixed;
    mixed.fixture.history.document.charts.front().bookmarks.push_back(
        {.id = object_id_t{32}, .anchor = marker_anchor_t::beat,
         .position = local_position_t{measure_id_t{3}, 240}});
    REQUIRE(mixed.previous_bookmark(1500.0));             // Bookmark record: row 1440.
    mixed.now += std::chrono::milliseconds{50};
    REQUIRE(mixed.skip(1500.0, -1.0));                    // Wheel notch back from the live row.
    CHECK(mixed.seeks.back() == 6 * quarter_frames);      // Row 1440: strictly before 1500.
    mixed.now += std::chrono::milliseconds{50};
    REQUIRE(mixed.previous_bookmark(1500.0));             // Not stepping past the old record.
    CHECK(mixed.seeks.back() == 6 * quarter_frames);
}

TEST_CASE("Playing bookmarks and following cursor use the current measure grid",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition.length = {1, 3};
    chart.measures.front().definition.meter.reset();
    chart.sections.clear();
    const auto index = measure_index_t::from(chart).value();
    CHECK(nearest_snap_row(chart, index, 300, 4) == 320);
    CHECK(nearest_snap_row(chart, index, 440, 4) == 320);
    CHECK(nearest_snap_row(chart, index, 441, 4) == 560);
    fixture.application.services.transport_preview_playing = [] { return true; };
    fixture.application.project.transport.preview_playhead_absolute_row = 441;
    const auto added = fixture.application.bookmarks.add_beat_bookmark_here(fixture.history, fixture.state, fixture.application);
    REQUIRE(added.accepted);
    CHECK(fixture.history.document.charts.front().bookmarks.back().position ==
          local_position_t{measure_id_t{4}, 240});
}

TEST_CASE("Following playhead cursor floors on the current measure grid",
          "[unit][sys][bookmark-commands]") {
    fixture_t fixture{{measure_id_t{3}, 0}};
    auto& chart = fixture.history.document.charts.front();
    chart.measures.front().definition.length = {1, 3};
    chart.measures.front().definition.meter.reset();
    chart.sections.clear();
    REQUIRE(follow_compose_playhead(fixture.history, fixture.state, 550, true));
    CHECK(fixture.state.cursor == local_position_t{measure_id_t{4}, 0});
    REQUIRE(follow_compose_playhead(fixture.history, fixture.state, 560, true));
    CHECK(fixture.state.cursor == local_position_t{measure_id_t{4}, 240});
}
