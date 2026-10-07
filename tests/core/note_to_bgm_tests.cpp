#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/formats/bmson.h>
#include "sys/compose_note_to_bgm.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>

using namespace wormedit;

namespace {

auto plain_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 3;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.lane_layouts.push_back({.id = object_id_t{100}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "t", .lanes = {{.id = lane_id_t{1}, .name = "A"}, {.id = lane_id_t{2}, .name = "B"}}}});
    chart.bgm_source_lanes = {{.id = object_id_t{9}, .name = "BGM 1", .source_order = 0},
                              {.id = object_id_t{10}, .name = "BGM 2", .source_order = 1}};
    chart.measures[0].notes = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}},
                               {.id = object_id_t{2}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{2}}};
    chart.measures[1].notes = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .lane = lane_id_t{1}}};
    song.charts.push_back(std::move(chart));
    return song;
}

} // namespace

TEST_CASE("notes moved to the BGM area take the preferred lane and shift over when it is taken",
          "[unit][core][note-to-bgm]") {
    auto history = make_document_history(plain_song());
    const std::array both{object_id_t{1}, object_id_t{2}};
    const auto moved = move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, both, object_id_t{9});
    for (const auto& d : moved.diagnostics) WARN(d.message);
    REQUIRE(moved.applied);
    const auto& chart = history.document.charts.front();
    CHECK(chart.measures[0].notes.empty());
    REQUIRE(chart.measures[0].bgm_events.size() == 2);
    // Same row: the first takes BGM 1, the second shifts to BGM 2.
    CHECK(chart.measures[0].bgm_events[0].source_lane == object_id_t{9});
    CHECK(chart.measures[0].bgm_events[1].source_lane == object_id_t{10});
    CHECK(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].notes.size() == 2);
}

TEST_CASE("a third note at the same row makes a new BGM lane; unknown notes reject atomically",
          "[unit][core][note-to-bgm]") {
    auto song = plain_song();
    song.charts.front().measures[0].notes.push_back(
        {.id = object_id_t{4}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{1}});
    auto history = make_document_history(song);
    const std::array three{object_id_t{1}, object_id_t{2}, object_id_t{4}};
    REQUIRE(move_notes_to_bgm(history, history.document.revision, chart_id_t{1}, three, std::nullopt).applied);
    CHECK(history.document.charts.front().bgm_source_lanes.size() == 3);

    auto other = make_document_history(plain_song());
    const std::array bad{object_id_t{1}, object_id_t{999}};
    const auto revision = other.document.revision;
    CHECK_FALSE(move_notes_to_bgm(other, revision, chart_id_t{1}, bad, std::nullopt).applied);
    CHECK(other.document.revision == revision);
    CHECK(other.document.charts.front().measures[0].notes.size() == 2);
    CHECK_FALSE(move_notes_to_bgm(other, revision + 5, chart_id_t{1}, std::array{object_id_t{1}}, std::nullopt).applied);
}

TEST_CASE("a BMSON stem tap goes back to its stem's BGM lane and keeps its slice point",
          "[unit][core][note-to-bgm][bmson]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = make_document_history(std::move(song));
    const auto chart_id = history.document.charts.front().id;
    const auto& chart = history.document.charts.front();
    const auto measure_it = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(measure_it != chart.measures.end());
    const auto measure_index = static_cast<std::size_t>(measure_it - chart.measures.begin());
    const auto bgm = measure_it->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    REQUIRE(assign_bmson_sound_event(history, history.document.revision, chart_id, bgm.id, lane).applied);
    const auto undo_before = history.undo.size();

    const std::array note{bgm.id};
    const auto back = move_notes_to_bgm(history, history.document.revision, chart_id, note, std::nullopt);
    REQUIRE(back.applied);
    const auto& restored = history.document.charts.front().measures[measure_index];
    REQUIRE(restored.bgm_events.size() == 1);
    CHECK(restored.bgm_events.front().sound_channel_event == bgm.sound_channel_event);
    CHECK(history.undo.size() == undo_before + 1);
}

#include <wormedit/core/sound_slice.h>

TEST_CASE("dragging a stem tap moves its slice point and leaves the audio where it is",
          "[unit][core][note-to-bgm][bmson][stem-move]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 400'000, true};
    auto history = make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto tap = std::ranges::find_if(chart.measures.front().notes, [](const auto& note) {
        return note.position.row == 840 && note.sound_channel_event.has_value();
    });
    REQUIRE(tap != chart.measures.front().notes.end());
    const auto note_id = tap->id;
    const auto event_id = *tap->sound_channel_event;
    const auto lane = tap->lane;
    const auto channel_before = chart.sound_channels.front();
    const auto slices_before = derive_sound_channel_slices(chart, channel_before, 48'000, 400'000);
    REQUIRE(slices_before);
    const auto before_at = std::ranges::find(slices_before.slices, 840U, &derived_sound_slice_t::absolute_row);
    REQUIRE(before_at != slices_before.slices.end());

    constexpr std::array snaps{std::uint64_t{4}, std::uint64_t{8}};
    auto setup = make_editor_interaction(history, chart.id, lane, {measure_id_t{chart.measures.front().id.value}, 840}, snaps, 4);
    REQUIRE(setup);
    auto state = std::move(*setup.state);
    REQUIRE(dispatch_editor_action(history, state, select_editor_notes_action_t{{note_id}}));
    REQUIRE(dispatch_editor_action(history, state, begin_note_move_gesture_action_t{note_id}));
    const auto next_measure = history.document.charts.front().measures[1].id;
    const auto released = dispatch_editor_action(
        history, state, release_note_move_gesture_action_t{{next_measure, 0}, lane});
    REQUIRE(released);
    REQUIRE(released.document_changed);

    const auto& moved_chart = history.document.charts.front();
    const auto& channel = moved_chart.sound_channels.front();
    const auto event = std::ranges::find(channel.events, event_id, &sound_channel_event_t::id);
    REQUIRE(event != channel.events.end());
    CHECK(event->position == (local_position_t{next_measure, 0}));
    const auto slices = derive_sound_channel_slices(moved_chart, channel, 48'000, 400'000);
    REQUIRE(slices);
    const auto after_at = std::ranges::find(slices.slices, 960U, &derived_sound_slice_t::absolute_row);
    REQUIRE(after_at != slices.slices.end());
    // The cut moved later, so the slice starts further into the same audio: nothing was re-anchored.
    CHECK(after_at->source_begin_frame > before_at->source_begin_frame);
    REQUIRE(undo_document_command(history));
    const auto restored = std::ranges::find(history.document.charts.front().sound_channels.front().events,
                                            event_id, &sound_channel_event_t::id);
    CHECK(restored->position.row == 840);
}

TEST_CASE("BGM events move into a playfield lane keeping their row",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.measures[1].bgm_events = {{.id = object_id_t{50}, .position = {measure_id_t{2}, 480}, .source_lane = object_id_t{9}}};
    auto history = make_document_history(song);
    const std::array event{object_id_t{50}};
    const auto moved = move_bgm_events_to_lane(history, history.document.revision, chart_id_t{1}, event, lane_id_t{2});
    REQUIRE(moved.applied);
    const auto& after = history.document.charts.front().measures[1];
    CHECK(after.bgm_events.empty());
    REQUIRE(after.notes.size() == 2);
    const auto note = std::ranges::find(after.notes, object_id_t{50}, &note_t::id);
    REQUIRE(note != after.notes.end());
    CHECK(note->lane == lane_id_t{2});
    CHECK(note->position == (local_position_t{measure_id_t{2}, 480}));
    CHECK(history.undo.size() == 1);

    // A lane that already holds a note at the row rejects without changing anything.
    auto other = plain_song();
    other.charts.front().measures[0].bgm_events = {{.id = object_id_t{51}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto blocked = make_document_history(other);
    const auto revision = blocked.document.revision;
    const std::array clash{object_id_t{51}};
    CHECK_FALSE(move_bgm_events_to_lane(blocked, revision, chart_id_t{1}, clash, lane_id_t{1}).applied);
    CHECK(blocked.document.revision == revision);
    CHECK_FALSE(move_bgm_events_to_lane(blocked, revision, chart_id_t{1}, clash, lane_id_t{99}).applied);
}

TEST_CASE("Shift lane assignment moves mixed selected objects one at a time in document order",
          "[unit][core][note-to-bgm][bgm-to-lane][successive]") {
    auto song = plain_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back({.id = lane_id_t{3}, .name = "C"});
    chart.measures[0].bgm_events = {
        {.id = object_id_t{50}, .position = {measure_id_t{1}, 300}, .source_lane = object_id_t{9}}};
    auto history = make_document_history(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_notes = {object_id_t{1}, object_id_t{3}};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{50}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{51}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{52}},
                              {editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::note, object_id_t{3}}};
    const auto move_next = [&] {
        return sys::move_selected_bgm_to_lane(history, state, lane_id_t{3}, std::nullopt, true);
    };

    auto moved = move_next();
    REQUIRE(moved.command.applied);
    CHECK(state.selected_notes == std::vector<object_id_t>{object_id_t{3}});
    CHECK(std::ranges::none_of(state.selected_objects, [](const auto& object) {
        return object.id == object_id_t{1};
    }));
    REQUIRE(move_next().command.applied);
    CHECK(std::ranges::none_of(state.selected_objects, [](const auto& object) {
        return object.kind == editor_selection_object_kind_t::bgm_event &&
               object.id == object_id_t{50};
    }));
    REQUIRE(move_next().command.applied);
    CHECK(state.selected_notes.empty());
    REQUIRE(history.undo.size() == 3);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].bgm_events.empty());

    auto collision_song = plain_song();
    collision_song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{60}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto collision_history = make_document_history(std::move(collision_song));
    editor_interaction_state_t collision_state;
    collision_state.chart = chart_id_t{1};
    collision_state.selected_objects = {
        {editor_selection_object_kind_t::bgm_event, object_id_t{60}}};
    const auto rejected = sys::move_selected_bgm_to_lane(
        collision_history, collision_state, lane_id_t{1}, std::nullopt, true);
    CHECK_FALSE(rejected.command.applied);
    CHECK(collision_state.selected_objects.size() == 1);
    CHECK(collision_history.undo.empty());
}

TEST_CASE("three selected BGM markers move on successive presses with one undo each",
          "[unit][core][note-to-bgm][bgm-to-lane][successive]") {
    auto song = plain_song();
    song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{70}, .position = {measure_id_t{1}, 40}, .source_lane = object_id_t{9}},
        {.id = object_id_t{71}, .position = {measure_id_t{1}, 80}, .source_lane = object_id_t{9}},
        {.id = object_id_t{72}, .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{9}}};
    auto history = make_document_history(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{70}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{71}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{72}}};
    const std::array expected_ids{object_id_t{70}, object_id_t{71}, object_id_t{72}};
    for (std::size_t index = 0; index < expected_ids.size(); ++index) {
        const auto moved = sys::move_selected_bgm_to_lane(
            history, state, lane_id_t{2}, std::nullopt, true);
        REQUIRE(moved.command.applied);
        CHECK(std::ranges::none_of(state.selected_objects, [&](const auto& object) {
            return object.id == expected_ids[index];
        }));
        CHECK(history.undo.size() == index + 1);
    }
    CHECK(state.selected_objects.empty());
    REQUIRE(history.undo.size() == 3);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures[0].bgm_events.size() == 1);
    CHECK(history.document.charts.front().measures[0].bgm_events.front().id == expected_ids.back());
}

TEST_CASE("plain lane assignment moves a mixed selection in one undo and rejects atomically",
          "[unit][core][note-to-bgm][bgm-to-lane]") {
    auto song = plain_song();
    song.charts.front().lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "C"});
    song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{80}, .position = {measure_id_t{1}, 300}, .source_lane = object_id_t{9}}};
    auto history = make_document_history(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.cursor = {measure_id_t{1}, 0};
    state.active_lane = lane_id_t{1};
    state.active_snap_denominator = 4;
    state.supported_snap_denominators = {4};
    state.selected_notes = {object_id_t{1}};
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{80}}};
    const std::array notes{object_id_t{1}};
    const std::array events{object_id_t{80}};
    const auto moved = move_selected_objects_to_lane(
        history, history.document.revision, state, notes, events, lane_id_t{3});
    REQUIRE(moved.applied);
    CHECK(history.undo.size() == 1);
    const auto& chart = history.document.charts.front();
    CHECK(std::ranges::find(chart.measures[0].notes, object_id_t{1}, &note_t::id)->lane == lane_id_t{3});
    CHECK(std::ranges::find(chart.measures[0].notes, object_id_t{80}, &note_t::id)->lane == lane_id_t{3});

    auto colliding_song = plain_song();
    colliding_song.charts.front().lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{3}, .name = "C"});
    colliding_song.charts.front().measures[0].bgm_events = {
        {.id = object_id_t{81}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{9}}};
    auto blocked = make_document_history(colliding_song);
    const auto before = blocked.document;
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{81}}};
    const std::array collision_events{object_id_t{81}};
    const auto rejected = move_selected_objects_to_lane(
        blocked, blocked.document.revision, state, notes, collision_events, lane_id_t{3});
    CHECK_FALSE(rejected.applied);
    CHECK(blocked.document.revision == before.revision);
    CHECK(blocked.document.charts.front().measures[0].notes[0].lane ==
          before.charts.front().measures[0].notes[0].lane);
    CHECK(blocked.document.charts.front().measures[0].bgm_events.size() == 1);
    CHECK(blocked.undo.empty());
}

TEST_CASE("a BMSON stem marker moves to a playfield lane and back without losing its slice point",
          "[unit][core][note-to-bgm][bgm-to-lane][bmson]") {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / "published-vox-slicing.bmson",
                        std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    const auto parsed = parse_bmson_bytes(std::as_bytes(std::span{bytes.data(), bytes.size()}));
    REQUIRE(parsed.song.has_value());
    auto song = *parsed.song;
    song.audio_assets.front().stream = {48'000, 2, 200'000, true};
    auto history = make_document_history(std::move(song));
    const auto& chart = history.document.charts.front();
    const auto measure = std::ranges::find_if(chart.measures, [](const auto& m) { return !m.bgm_events.empty(); });
    REQUIRE(measure != chart.measures.end());
    const auto marker = measure->bgm_events.front();
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    const std::array events{marker.id};
    REQUIRE(move_bgm_events_to_lane(history, history.document.revision, chart.id, events, lane).applied);
    const auto& playable = history.document.charts.front();
    const auto note = std::ranges::find_if(playable.measures, [&](const auto& m) {
        return std::ranges::any_of(m.notes, [&](const auto& n) { return n.id == marker.id; });
    });
    REQUIRE(note != playable.measures.end());
    const auto tap = std::ranges::find(note->notes, marker.id, &note_t::id);
    CHECK(tap->sound_channel_event == marker.sound_channel_event);
    CHECK(tap->lane == lane);
}
