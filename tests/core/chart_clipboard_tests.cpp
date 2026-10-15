#include "../support/stale_action_assertions.h"
#include <wormedit/core/chart_clipboard.h>
#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <array>

using namespace wormedit;

namespace {
void expect(bool value, const std::source_location at = std::source_location::current()) {
    INFO(at.file_name() << ":" << at.line());
    REQUIRE(value);
}

auto clipboard_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 7;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}},
                      {.id = measure_id_t{3}}};
    lane_layout_segment_t layout;
    layout.id = object_id_t{100};
    layout.position = {measure_id_t{1}, 0};
    layout.layout.profile_id = "clipboard-test";
    layout.layout.lanes = {{.id = lane_id_t{1}, .name = "Key"},
                           {.id = lane_id_t{2}, .name = "Second key"}};
    chart.lane_layouts.push_back(layout);
    note_t hold;
    hold.id = object_id_t{1};
    hold.position = {measure_id_t{1}, 840};
    hold.lane = lane_id_t{1};
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{3}, 240};
    hold.label = "retained label";
    hold.sound = resource_id_t{42};
    chart.measures[0].notes.push_back(hold);
    note_t tap;
    tap.id = object_id_t{2};
    tap.position = {measure_id_t{2}, 120};
    tap.lane = lane_id_t{2};
    chart.measures[1].notes.push_back(tap);
    chart.bgm_source_lanes.push_back({.id = object_id_t{9}, .name = "BGM"});
    chart.measures[1].bgm_events.push_back({.id = object_id_t{1},
        .position = {measure_id_t{2}, 240}, .source_lane = object_id_t{9}});
    song.charts.push_back(std::move(chart));
    return song;
}

auto clipboard_selection() -> editor_interaction_state_t {
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_notes = {object_id_t{2}, object_id_t{1}, object_id_t{1}};
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{1}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{1}}};
    return state;
}
} // namespace

TEST_CASE("Chart clipboard preserves cross-measure holds and typed ID collisions exactly") {
    const auto song = clipboard_song();
    const auto copied = chart_clipboard_t::from(song, clipboard_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.size() == 3);
    const auto& objects = copied->objects;
    expect(objects[0].offset == rational_t{0, 1});
    expect(objects[0].duration == rational_t{11, 8});
    expect(objects[1].offset == rational_t{1, 4});
    expect(objects[2].offset == rational_t{3, 8});
    const auto& hold = std::get<note_t>(objects[0].payload);
    expect(hold.label == "retained label");
    expect(hold.sound == resource_id_t{42});
    expect(song.revision == 7);
    expect(song.charts.front().measures.front().notes.size() == 1);
}

TEST_CASE("Chart clipboard rejects stale empty and mixed unsupported selections atomically") {
    const auto song = clipboard_song();
    auto state = clipboard_selection();
    expect(!chart_clipboard_t::from(song, state, 6));
    test::check_stale_action_message(chart_clipboard_t::from(song, state, 6).error(), "Chart selection copy");
    state.selected_objects.push_back({editor_selection_object_kind_t::bpm_event, object_id_t{1}});
    expect(!chart_clipboard_t::from(song, state, 7));
    state.selected_objects.clear();
    state.selected_notes.clear();
    expect(!chart_clipboard_t::from(song, state, 7));
    state.selected_notes = {object_id_t{999}};
    expect(!chart_clipboard_t::from(song, state, 7));
}

TEST_CASE("Chart clipboard cut deletes the complete merged selection in one undo step") {
    auto song = clipboard_song();
    song.charts.front().measures.front().notes.front().sound.reset();
    auto history = document_history_t::from(std::move(song));
    auto state = clipboard_selection();
    const std::array snaps{std::uint64_t{4}};
    auto interaction = editor_interaction_state_t::from(history, state.chart, lane_id_t{1},
        {measure_id_t{1}, 0}, snaps, 4);
    expect(static_cast<bool>(interaction));
    interaction.state->selected_notes = state.selected_notes;
    interaction.state->selected_objects = state.selected_objects;
    state = std::move(*interaction.state);
    const auto original_revision = history.document.revision;
    const auto cut = create_cut_chart_clipboard(history, state);
    expect(static_cast<bool>(cut));
    expect(cut->objects.size() == 3);
    expect(history.undo.size() == 1);
    expect(history.document.charts.front().measures[0].notes.empty());
    expect(history.document.charts.front().measures[1].notes.empty());
    expect(history.document.charts.front().measures[1].bgm_events.empty());
    expect(undo_document_command(history));
    expect(history.document.charts.front().measures[0].notes.size() == 1);
    expect(history.document.charts.front().measures[1].notes.size() == 1);
    expect(history.document.charts.front().measures[1].bgm_events.size() == 1);
}

TEST_CASE("Chart clipboard respects shortened measures without rounding offsets") {
    auto song = clipboard_song();
    song.charts.front().measures[1].definition.length = {1, 2};
    song.charts.front().measures[1].definition.meter.reset();
    const auto copied = chart_clipboard_t::from(song, clipboard_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.front().duration == rational_t{7, 8});
    expect(copied->objects[1].offset == rational_t{1, 4});
}

namespace {
auto paste_fixture() -> std::pair<song_document_t, chart_clipboard_t> {
    auto song = clipboard_song();
    song.charts.front().measures.front().notes.front().sound.reset();
    song.charts.front().measures[1].bgm_events.front().id = object_id_t{3};
    auto state = clipboard_selection();
    state.selected_objects.back().id = object_id_t{3};
    auto copied = chart_clipboard_t::from(song, state, 7);
    expect(static_cast<bool>(copied));
    auto target = song.charts.front();
    target.id = chart_id_t{2};
    target.divisions_per_standard_measure = 1920;
    target.lane_layouts.front().layout.lanes.front().id = lane_id_t{21};
    for (auto& measure : target.measures) {
        measure.notes.clear();
        measure.bgm_events.clear();
    }
    song.charts.push_back(std::move(target));
    return {std::move(song), std::move(*copied)};
}
}

TEST_CASE("Chart clipboard paste remaps exact resolution and lanes with one undo command") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    const auto result = paste_chart_clipboard(history, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120});
    INFO((result.diagnostics.empty() ? "No paste diagnostic" : result.diagnostics.front().message));
    expect(static_cast<bool>(result));
    const auto& target = history.document.charts.back();
    expect(target.measures.front().notes.size() == 2);
    const auto& hold = target.measures.front().notes.front();
    expect(hold.position == local_position_t{measure_id_t{1}, 120});
    expect(hold.end == local_position_t{measure_id_t{2}, 840});
    expect(hold.lane == lane_id_t{21});
    expect(target.measures.front().notes.back().position.row == 600);
    expect(target.measures.front().bgm_events.front().position.row == 840);
    expect(history.undo.size() == 1);
    expect(undo_document_command(history));
    expect(history.document.charts.back().measures.front().notes.empty());
}

TEST_CASE("Chart clipboard paste collision retains ghosts and rejects commit atomically") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    const auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{1}, clipboard,
        {measure_id_t{1}, 840}, history.document.revision);
    REQUIRE_FALSE(proposal.value);
    const auto preview = chart_paste_preview_view_t::from(proposal);
    REQUIRE(preview.report);
    expect(preview.report->objects.size() == clipboard.objects.size());
    expect(!paste_chart_clipboard(history, chart_id_t{1}, clipboard,
        {measure_id_t{1}, 840}));
    expect(history.document.revision == 7);
    expect(history.undo.empty());
}

TEST_CASE("Chart clipboard paste rejects fractional rows stale revisions and foreign projects") {
    auto [song, clipboard] = paste_fixture();
    song.charts.back().divisions_per_standard_measure = 4;
    expect(!chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7).value.has_value());
    expect(!chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 6).value.has_value());
    clipboard.source_song = song_id_t{999};
    test::check_stale_action_message(chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard, {measure_id_t{1}, 0}, 6).diagnostics.front().message, "Chart paste proposal");
    expect(!chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7).value.has_value());
}

TEST_CASE("Chart clipboard paste reports every unavailable target lane") {
    auto [song, clipboard] = paste_fixture();
    song.charts.back().lane_layouts.front().layout.lanes.clear();
    song.charts.back().bgm_source_lanes.clear();
    const auto proposal = chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7);
    expect(!proposal.value.has_value());
    REQUIRE(proposal.diagnostics.size() == 1);
    expect(proposal.diagnostics.front().report.missing_lane_count == 3);
}

TEST_CASE("Chart clipboard copies and pastes timing objects at a new anchor") {
    auto song = clipboard_song();
    auto& chart = song.charts.front();
    chart.measures[0].notes[0].sound.reset();
    chart.measures[1].bgm_events.clear();
    chart.bpms.push_back({.id = object_id_t{50}, .position = {measure_id_t{1}, 0},
                          .beats_per_minute = {120, 1}});
    chart.pauses.push_back({.id = object_id_t{51}, .position = {measure_id_t{1}, 120},
                            .kind = pause_kind_t::stop, .duration = {1, 2}});
    chart.warps.push_back({.id = object_id_t{52},
                           .span = {{measure_id_t{1}, 240}, {measure_id_t{1}, 360}}});
    chart.speeds.push_back({.id = object_id_t{53}, .position = {measure_id_t{1}, 480},
                            .multiplier = {2, 1}});
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {
        {editor_selection_object_kind_t::bpm_event, object_id_t{50}},
        {editor_selection_object_kind_t::stop_event, object_id_t{51}},
        {editor_selection_object_kind_t::warp_event, object_id_t{52}},
        {editor_selection_object_kind_t::speed_event, object_id_t{53}}};
    const auto copied = chart_clipboard_t::from(song, state, 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.size() == 4);
    expect(copied->objects[2].duration == rational_t{1, 8});

    auto history = document_history_t::from(song);
    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                              {measure_id_t{2}, 0});
    const std::string reason = pasted.diagnostics.empty() ? "" : pasted.diagnostics.front().message;
    INFO(reason);
    expect(pasted.diagnostics.empty());
    const auto& after = history.document.charts.front();
    expect(after.bpms.size() == 2 && after.pauses.size() == 2 && after.warps.size() == 2 &&
           after.speeds.size() == 2);
    expect(after.bpms.back().position == local_position_t{measure_id_t{2}, 0});
    expect(after.pauses.back().duration == signed_rational_t{1, 2});
    expect(after.warps.back().span.begin == local_position_t{measure_id_t{2}, 240});
    expect(after.warps.back().span.end == local_position_t{measure_id_t{2}, 360});
    expect(after.speeds.back().position == local_position_t{measure_id_t{2}, 480});
    expect(after.bpms.back().id != after.bpms.front().id);
}

namespace {
auto stem_song() -> song_document_t {
    auto song = clipboard_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.measures[0].notes[0].sound.reset();
    chart.measures[1].bgm_events[0].id = object_id_t{3};
    song.audio_assets.push_back({.id = asset_id_t{5}});
    song.resources.push_back({.id = resource_id_t{80}, .name = "stem", .asset = asset_id_t{5},
                              .roles = {resource_role_t::bgm}});
    chart.sound_channels.push_back({.id = object_id_t{50}, .name = "stem",
        .source_clip = {.source = asset_id_t{5}},
        .events = {{.id = object_id_t{60}, .position = {measure_id_t{1}, 0}, .playable = false},
                   {.id = object_id_t{61}, .position = {measure_id_t{1}, 480},
                    .continuation = true, .playable = false}}});
    chart.measures[0].bgm_events.push_back({.id = object_id_t{70}, .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{9}, .sound = resource_id_t{80},
        .sound_channel_event = object_id_t{60}});
    chart.measures[0].bgm_events.push_back({.id = object_id_t{71}, .position = {measure_id_t{1}, 480},
        .source_lane = object_id_t{9}, .sound = resource_id_t{80},
        .sound_channel_event = object_id_t{61}});
    return song;
}

auto stem_selection() -> editor_interaction_state_t {
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{70}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{71}}};
    return state;
}
} // namespace

TEST_CASE("Chart clipboard carries BMSON stem points with their restart and continue flags") {
    auto history = document_history_t::from(stem_song());
    const auto copied = chart_clipboard_t::from(history.document, stem_selection(), history.document.revision);
    expect(static_cast<bool>(copied));
    expect(copied->objects.size() == 2);
    for (const auto& object : copied->objects) expect(object.stem.has_value());
    expect(copied->objects[0].stem->channel == object_id_t{50});
    expect(!copied->objects[0].stem->continuation);
    expect(copied->objects[1].stem->continuation);

    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                              {measure_id_t{2}, 600});
    if (!pasted.diagnostics.empty()) WARN(pasted.diagnostics.front().message);
    expect(pasted.applied);
    const auto& chart = history.document.charts.front();
    expect(chart.sound_channels.front().events.size() == 4);
    const auto& added = chart.sound_channels.front().events;
    expect(!added[2].continuation && added[3].continuation);
    expect(added[2].position == (local_position_t{measure_id_t{2}, 600}));
    expect(added[3].position == (local_position_t{measure_id_t{2}, 1080 - 960}) ||
           added[3].position.measure == measure_id_t{3});
    // Each pasted marker links to its own new retained event, never the original.
    const auto& bgm = chart.measures[1].bgm_events;
    const auto pasted_marker = std::ranges::find_if(bgm, [&](const auto& event) {
        return event.sound_channel_event == added[2].id;
    });
    expect(pasted_marker != bgm.end());
    expect(history.undo.size() == 1);
    expect(undo_document_command(history));
    expect(history.document.charts.front().sound_channels.front().events.size() == 2);
}

TEST_CASE("Chart clipboard rejects stem points that have no channel or collide with a pulse") {
    auto history = document_history_t::from(stem_song());
    const auto copied = chart_clipboard_t::from(history.document, stem_selection(), history.document.revision);
    expect(static_cast<bool>(copied));
    const auto revision = history.document.revision;

    // Onto the pulses the channel already has: the same anchor as the source.
    const auto collision = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                                 {measure_id_t{1}, 0});
    expect(!collision.applied);

    // A target chart without that channel changes nothing.
    auto other = stem_song();
    other.charts.front().sound_channels.clear();
    for (auto& measure : other.charts.front().measures) measure.bgm_events.clear();
    auto other_history = document_history_t::from(other);
    const auto missing = paste_chart_clipboard(other_history, chart_id_t{1}, *copied,
                                               {measure_id_t{2}, 600});
    expect(!missing.applied);
    expect(other_history.undo.empty());
    expect(history.document.revision == revision);
}

namespace {
// 120 BPM at 960 rows per measure: a measure lasts two seconds. The 4 s sample sounds for the
// first two measures; the points are a restart at measure 1 and a continuation half a measure on.
auto timed_stem_song() -> song_document_t {
    auto song = stem_song();
    auto& chart = song.charts.front();
    for (std::uint64_t id = 4; id <= 8; ++id) chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms.push_back({.id = object_id_t{95}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}});
    song.audio_assets.front().stream = {48'000, 2, 48'000 * 4, true};
    return song;
}

auto only_point(std::uint64_t id) -> editor_interaction_state_t {
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{id}}};
    return state;
}
} // namespace

TEST_CASE("stem points pasted inside the ongoing audio stay continuations",
          "[unit][core][clipboard][stem-run]") {
    auto history = document_history_t::from(timed_stem_song());
    const auto copied = chart_clipboard_t::from(history.document, only_point(71), history.document.revision);
    expect(static_cast<bool>(copied));
    // Measure 1, row 720 is 1.5 s in: the 4 s sample is still playing.
    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                              {measure_id_t{1}, 720});
    expect(pasted.applied);
    const auto& events = history.document.charts.front().sound_channels.front().events;
    expect(events.size() == 3);
    expect(events.back().continuation);
}

TEST_CASE("a continuation pasted after the stem ended becomes a reset",
          "[unit][core][clipboard][stem-run]") {
    auto history = document_history_t::from(timed_stem_song());
    const auto copied = chart_clipboard_t::from(history.document, only_point(71), history.document.revision);
    expect(static_cast<bool>(copied));
    // Measure 4 is six seconds in: the sample ended at four, so the point has to restart it.
    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                              {measure_id_t{4}, 0});
    expect(pasted.applied);
    const auto& events = history.document.charts.front().sound_channels.front().events;
    expect(events.size() == 3);
    expect(!events.back().continuation);
}

TEST_CASE("stem points pasted over part of an existing run are rejected",
          "[unit][core][clipboard][stem-run]") {
    auto song = timed_stem_song();
    auto& chart = song.charts.front();
    // A later continuation, silent now, that the pasted run would reach.
    chart.sound_channels.front().events.push_back(
        {.id = object_id_t{62}, .position = {measure_id_t{6}, 0}, .continuation = true, .playable = false});
    chart.measures[5].bgm_events.push_back({.id = object_id_t{72}, .position = {measure_id_t{6}, 0},
        .source_lane = object_id_t{9}, .sound = resource_id_t{80}, .sound_channel_event = object_id_t{62}});
    auto history = document_history_t::from(song);
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{71}},
                              {editor_selection_object_kind_t::bgm_event, object_id_t{72}}};
    const auto copied = chart_clipboard_t::from(history.document, state, history.document.revision);
    expect(static_cast<bool>(copied));
    const auto revision = history.document.revision;
    // The run starts at measure 4 (silent) and its last point lands past the existing one at 6.
    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied,
                                              {measure_id_t{4}, 0});
    expect(!pasted.applied);
    expect(history.document.revision == revision);
    const auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{1}, *copied,
                                              {measure_id_t{4}, 0}, revision);
    expect(proposal.diagnostics.front().message.find("overlap") != std::string::npos);
}

namespace {
// Gives the clipboard fixture's BGM event a native hold tail ending at `end` in measure two.
auto bgm_hold_song(std::optional<local_position_t> end) -> song_document_t {
    auto song = clipboard_song();
    song.charts.front().measures.front().notes.front().sound.reset();
    auto& event = song.charts.front().measures[1].bgm_events.front();
    event.id = object_id_t{3};
    event.end = end;
    return song;
}

auto bgm_only_selection() -> editor_interaction_state_t {
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{3}}};
    return state;
}
} // namespace

TEST_CASE("Chart clipboard pastes a BGM hold with its tail moved by the same offset") {
    const auto song = bgm_hold_song(local_position_t{measure_id_t{2}, 720});
    const auto copied = chart_clipboard_t::from(song, bgm_only_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.front().duration == rational_t{1, 2});
    const auto proposal = chart_paste_proposal_t::propose(song, chart_id_t{1}, *copied,
        {measure_id_t{3}, 0}, 7);
    expect(proposal.value.has_value());
    const auto& pasted = std::get<bgm_event_t>(proposal.value->payload.report.objects.front());
    expect(pasted.position == local_position_t{measure_id_t{3}, 0});
    expect(pasted.end == local_position_t{measure_id_t{3}, 480});
}

TEST_CASE("Chart clipboard moves a BGM hold tail into a shortened target measure") {
    auto song = bgm_hold_song(local_position_t{measure_id_t{2}, 360});
    song.charts.front().measures[2].definition.length = {1, 2};
    song.charts.front().measures[2].definition.meter.reset();
    const auto copied = chart_clipboard_t::from(song, bgm_only_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.front().duration == rational_t{1, 8});
    const auto proposal = chart_paste_proposal_t::propose(song, chart_id_t{1}, *copied,
        {measure_id_t{3}, 0}, 7);
    expect(proposal.value.has_value());
    const auto& pasted = std::get<bgm_event_t>(proposal.value->payload.report.objects.front());
    expect(pasted.end == local_position_t{measure_id_t{3}, 120});
}

TEST_CASE("Chart clipboard rejects a BGM hold whose tail overflows the chart atomically") {
    auto song = bgm_hold_song(local_position_t{measure_id_t{2}, 720});
    song.charts.front().measures[2].definition.length = {1, 2};
    song.charts.front().measures[2].definition.meter.reset();
    const auto copied = chart_clipboard_t::from(song, bgm_only_selection(), 7);
    expect(static_cast<bool>(copied));
    auto history = document_history_t::from(std::move(song));
    const auto result = paste_chart_clipboard(history, chart_id_t{1}, *copied,
        {measure_id_t{3}, 0});
    expect(!static_cast<bool>(result));
    expect(history.document.revision == 7);
    expect(history.undo.empty());
    expect(history.document.charts.front().measures[2].bgm_events.empty());
}

TEST_CASE("Chart clipboard pastes plain BGM events without a tail and undoes exactly") {
    const auto song = bgm_hold_song(std::nullopt);
    const auto copied = chart_clipboard_t::from(song, bgm_only_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied->objects.front().duration == rational_t{0, 1});
    auto history = document_history_t::from(song);
    const auto result = paste_chart_clipboard(history, chart_id_t{1}, *copied,
        {measure_id_t{3}, 240});
    expect(static_cast<bool>(result));
    const auto& pasted = history.document.charts.front().measures[2].bgm_events.back();
    expect(pasted.position == local_position_t{measure_id_t{3}, 240});
    expect(!pasted.end);
    expect(undo_document_command(history));
    expect(history.document.charts.front().measures[2].bgm_events.empty());
    expect(!history.document.charts.front().measures[1].bgm_events.front().end);
}

TEST_CASE("Captured paste batches apply exact proposal IDs with one Undo record",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
    expect(proposal.value.has_value());
    expect(proposal.value->stamp == chart_stamp_t::from(history.document, chart_id_t{2}));
    const auto expected_id = std::get<note_t>(proposal.value->payload.report.objects.front()).id;
    const auto result = apply_proposal(history, std::move(*proposal.value));
    expect(result.applied);
    expect(history.document.charts.back().measures.front().notes.front().id == expected_id);
    expect(history.undo.size() == 1);
    expect(undo_document_command(history));
    expect(history.document.charts.back().measures.front().notes.empty());
}

TEST_CASE("Captured paste expires by revision before publishing any object",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
    expect(proposal.value.has_value());
    ++history.document.revision;
    const auto result = apply_proposal(history, std::move(*proposal.value));
    expect(!result.applied);
    expect(result.diagnostics.size() == 1);
    test::check_stale_action_message(result.diagnostics, "Chart paste proposal");
    expect(history.undo.empty());
    expect(history.document.charts.back().measures.front().notes.empty());
}

TEST_CASE("Captured paste retains its separate clipboard-project provenance rejection",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
    expect(proposal.value.has_value());
    history.document.id = song_id_t{99};
    const auto result = apply_proposal(history, std::move(*proposal.value));
    expect(!result.applied);
    expect(result.diagnostics.size() == 1);
    expect(result.diagnostics.front().code == diagnostic_code_t::collision);
    expect(result.diagnostics.front().message == "Clipboard resources belong to another project.");
    expect(history.undo.empty());
}

TEST_CASE("Failed paste proposals own diagnostics without an applicable batch",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    const chart_paste_outcome_t empty;
    expect(!empty.value);
    clipboard.source_song = song_id_t{99};
    const auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
    expect(!proposal.value);
    REQUIRE(proposal.diagnostics.size() == 1);
    const auto result = paste_chart_clipboard(history, chart_id_t{2}, clipboard, {measure_id_t{1}, 120});
    REQUIRE(result.diagnostics.size() == 1);
    expect(result.diagnostics.front().code == diagnostic_code_t::collision);
    expect(result.diagnostics.front().message == proposal.diagnostics.front().message);
    expect(history.undo.empty());
}

TEST_CASE("Paste revision rebuilds ghosts and preserves the captured revision",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    const auto first = chart_paste_proposal_t::propose(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, song.revision);
    REQUIRE(first.value);
    const auto revised = first.value->revise(song, clipboard,
        local_position_t{measure_id_t{1}, 120});
    REQUIRE(revised.value);
    expect(revised.value->stamp == first.value->stamp);
    expect(std::get<note_t>(revised.value->payload.report.objects.front()).position.row == 120);
    ++song.revision;
    const auto stale = first.value->revise(song, clipboard,
        local_position_t{measure_id_t{1}, 120});
    REQUIRE_FALSE(stale.value);
    REQUIRE(stale.diagnostics.size() == 1);
    test::check_stale_action_message(stale.diagnostics.front().message, "Chart paste proposal");
}

TEST_CASE("Empty captured paste batches return before document publication",
          "[core][paste-proposal-stamp]") {
    auto [song, clipboard] = paste_fixture();
    auto history = document_history_t::from(std::move(song));
    auto proposal = chart_paste_proposal_t::propose(history.document, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
    REQUIRE(proposal.value);
    proposal.value->payload.report.objects.clear();
    const auto result = apply_proposal(history, std::move(*proposal.value));
    expect(!result.applied);
    expect(result.diagnostics.empty());
    expect(history.undo.empty());
    expect(history.document.revision == 7);
}
