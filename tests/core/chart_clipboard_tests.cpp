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
    const auto copied = copy_chart_selection(song, clipboard_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied.clipboard->objects.size() == 3);
    const auto& objects = copied.clipboard->objects;
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
    expect(!copy_chart_selection(song, state, 6));
    state.selected_objects.push_back({editor_selection_object_kind_t::bpm_event, object_id_t{1}});
    expect(!copy_chart_selection(song, state, 7));
    state.selected_objects.clear();
    state.selected_notes.clear();
    expect(!copy_chart_selection(song, state, 7));
    state.selected_notes = {object_id_t{999}};
    expect(!copy_chart_selection(song, state, 7));
}

TEST_CASE("Chart clipboard cut deletes the complete merged selection in one undo step") {
    auto song = clipboard_song();
    song.charts.front().measures.front().notes.front().sound.reset();
    auto history = make_document_history(std::move(song));
    auto state = clipboard_selection();
    const std::array snaps{std::uint64_t{4}};
    auto interaction = make_editor_interaction(history, state.chart, lane_id_t{1},
        {measure_id_t{1}, 0}, snaps, 4);
    expect(static_cast<bool>(interaction));
    interaction.state->selected_notes = state.selected_notes;
    interaction.state->selected_objects = state.selected_objects;
    state = std::move(*interaction.state);
    const auto original_revision = history.document.revision;
    const auto cut = cut_chart_selection(history, state, original_revision);
    expect(static_cast<bool>(cut));
    expect(cut.clipboard->objects.size() == 3);
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
    const auto copied = copy_chart_selection(song, clipboard_selection(), 7);
    expect(static_cast<bool>(copied));
    expect(copied.clipboard->objects.front().duration == rational_t{7, 8});
    expect(copied.clipboard->objects[1].offset == rational_t{1, 4});
}

namespace {
auto paste_fixture() -> std::pair<song_document_t, chart_clipboard_t> {
    auto song = clipboard_song();
    song.charts.front().measures.front().notes.front().sound.reset();
    song.charts.front().measures[1].bgm_events.front().id = object_id_t{3};
    auto state = clipboard_selection();
    state.selected_objects.back().id = object_id_t{3};
    auto copied = copy_chart_selection(song, state, 7);
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
    return {std::move(song), std::move(*copied.clipboard)};
}
}

TEST_CASE("Chart clipboard paste remaps exact resolution and lanes with one undo command") {
    auto [song, clipboard] = paste_fixture();
    auto history = make_document_history(std::move(song));
    const auto result = paste_chart_clipboard(history, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 120}, history.document.revision);
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
    auto history = make_document_history(std::move(song));
    const auto proposal = propose_chart_paste(history.document, chart_id_t{1}, clipboard,
        {measure_id_t{1}, 840}, history.document.revision);
    expect(!proposal.valid);
    expect(proposal.objects.size() == clipboard.objects.size());
    expect(!paste_chart_clipboard(history, chart_id_t{1}, clipboard,
        {measure_id_t{1}, 840}, history.document.revision));
    expect(history.document.revision == 7);
    expect(history.undo.empty());
}

TEST_CASE("Chart clipboard paste rejects fractional rows stale revisions and foreign projects") {
    auto [song, clipboard] = paste_fixture();
    song.charts.back().divisions_per_standard_measure = 4;
    expect(!propose_chart_paste(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7).valid);
    expect(!propose_chart_paste(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 6).valid);
    clipboard.source_song = song_id_t{999};
    expect(!propose_chart_paste(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7).valid);
}

TEST_CASE("Chart clipboard paste reports every unavailable target lane") {
    auto [song, clipboard] = paste_fixture();
    song.charts.back().lane_layouts.front().layout.lanes.clear();
    song.charts.back().bgm_source_lanes.clear();
    const auto proposal = propose_chart_paste(song, chart_id_t{2}, clipboard,
        {measure_id_t{1}, 0}, 7);
    expect(!proposal.valid);
    expect(proposal.missing_lane_count == 3);
}
