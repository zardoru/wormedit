#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace wormedit;

namespace {

auto semantic_layer_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3},
                                  .notes = {{.id = object_id_t{20},
                                             .position = {measure_id_t{3}, 120},
                                             .lane = lane_id_t{1},
                                             .kind = note_kind_t::tap,
                                             .semantic_layers = {layer_id_t{7}},
                                             .label = "accent"},
                                            {.id = object_id_t{21},
                                             .position = {measure_id_t{3}, 240},
                                             .lane = lane_id_t{1},
                                             .kind = note_kind_t::hold,
                                             .end = local_position_t{measure_id_t{3}, 480},
                                             .label = "sustain"}}}},
                    .lane_layouts = {{.id = object_id_t{30},
                                      .position = {measure_id_t{3}, 0},
                                      .layout = {.profile_id = "single-lane",
                                                 .display_name = "Single lane",
                                                 .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
                    .input_profile = {.profile_id = "single-lane",
                                      .bindings = {{.key = physical_key_t::digit_1,
                                                    .lane = lane_id_t{1}}}},
                    .semantic_layers = {{.id = layer_id_t{7},
                                         .name = "Vocal",
                                         .color = {255, 128, 0, 255},
                                         .visible = true,
                                         .locked = false}},
                    .bpms = {{.id = object_id_t{40},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto bgm_placement_song() -> song_document_t {
    auto song = semantic_layer_song();
    song.audio_assets.push_back({.id = asset_id_t{50}, .source_path = "music.wav"});
    song.resources.push_back({.id = resource_id_t{51}, .asset = asset_id_t{50},
                              .roles = {resource_role_t::bgm}});
    song.charts.front().sound_layer_defaults.push_back(
        {.resource = resource_id_t{51}, .layers = {layer_id_t{7}}});
    REQUIRE(validate_document(song).empty());
    return song;
}

auto semantic_layer_bgm_song() -> song_document_t {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{33}, .name = "Music"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{23},
        .position = {measure_id_t{3}, 360}, .source_lane = object_id_t{33}});
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto note_by_id(chart_document_t& chart, object_id_t id) -> note_t& {
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end()) {
            return *found;
        }
    }
    FAIL("note ID was not found in test fixture");
    return chart.measures.front().notes.front();
}

auto mixed_layer_membership(const chart_document_t& chart, std::vector<layer_id_t> expected)
    -> bool {
    return note_by_id(const_cast<chart_document_t&>(chart), object_id_t{21}).semantic_layers == expected &&
           chart.measures.front().bgm_events.front().semantic_layers == expected;
}


auto check_gameplay_unchanged(const note_t& actual, const note_t& expected) -> void {
    CHECK(std::tie(actual.id, actual.kind, actual.position, actual.lane, actual.end,
                   actual.sound, actual.label) ==
          std::tie(expected.id, expected.kind, expected.position, expected.lane, expected.end,
                   expected.sound, expected.label));
}

auto check_cleared_gameplay(const std::vector<note_t>& cleared_notes,
                            const std::vector<note_t>& original_notes) -> void {
    REQUIRE(cleared_notes.size() == original_notes.size());
    for (std::size_t index = 0; index < cleared_notes.size(); ++index) {
        CHECK(cleared_notes[index].semantic_layers.empty());
        check_gameplay_unchanged(cleared_notes[index], original_notes[index]);
    }
}

auto check_clear_memberships(chart_document_t& chart, std::vector<layer_id_t> tap,
                             std::vector<layer_id_t> hold) -> void {
    CHECK(note_by_id(chart, object_id_t{20}).semantic_layers == tap);
    CHECK(note_by_id(chart, object_id_t{21}).semantic_layers == hold);
}

auto check_pristine_clear_history(const document_history_t& history,
                                  std::uint64_t next_revision) -> void {
    CHECK(history.document.revision == 4);
    CHECK(history.next_revision == next_revision);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
    CHECK_FALSE(is_document_dirty(history));
}

auto check_rejected_clear(const document_command_result_t& result, diagnostic_code_t code) -> void {
    CHECK_FALSE(result);
    CHECK(has_code(result, code));
}

} // namespace

TEST_CASE("semantic-layer palette windows page visible layers and reveal a hidden selection",
          "[unit][core][semantic-layer-editor][palette]") {
    chart_document_t chart;
    for (std::uint64_t index = 0; index < 12; ++index) {
        chart.semantic_layers.push_back({
            .id = layer_id_t{index + 1},
            .name = "Layer " + std::to_string(index + 1),
            .color = {0, 0, 0, 255},
            .visible = index != 10,
        });
    }

    const auto first = semantic_layer_palette_window_t::from(chart, std::nullopt, 0, false, 4);
    REQUIRE(first);
    CHECK(first->page == 0);
    CHECK(first->page_count == 3);
    CHECK(first->total_count == 11);
    CHECK(first->layer_indices == std::vector<std::size_t>{0, 1, 2, 3});

    const auto clamped = semantic_layer_palette_window_t::from(chart, std::nullopt, 99, false, 4);
    REQUIRE(clamped);
    CHECK(clamped->page == 2);
    CHECK(clamped->layer_indices == std::vector<std::size_t>{8, 9, 11});

    const auto revealed = semantic_layer_palette_window_t::from(chart, layer_id_t{11}, 0, true, 4);
    REQUIRE(revealed);
    CHECK(revealed->page == 2);
    CHECK(revealed->total_count == 12);
    CHECK(revealed->layer_indices == std::vector<std::size_t>{8, 9, 10, 11});

    CHECK_FALSE(semantic_layer_palette_window_t::from(chart, std::nullopt, 0, false, 0));
    CHECK_FALSE(semantic_layer_palette_window_t::from(chart, std::nullopt, 0, false, 4, 11));
}

namespace {
void check_scrolling_window(bool condition) { CHECK(condition); }
void require_scrolling_window(bool condition) { REQUIRE(condition); }
} // namespace

TEST_CASE("semantic-layer scrolling windows include hidden layers and clamp bounded requests",
          "[unit][core][semantic-layer-editor][palette]") {
    chart_document_t chart;
    for (std::uint64_t index = 0; index < 30; ++index)
        chart.semantic_layers.push_back({.id = layer_id_t{index + 1},
                                         .name = "Layer " + std::to_string(index + 1),
                                         .visible = index % 2 == 0});
    const auto window = semantic_layer_palette_window_t::from(chart, 9, 4);
    require_scrolling_window(window.has_value());
    check_scrolling_window(window->first_row == 9);
    check_scrolling_window(window->total_count == 30);
    check_scrolling_window(window->layer_indices == std::vector<std::size_t>{9, 10, 11, 12});

    const auto clamped = semantic_layer_palette_window_t::from(
        chart, std::numeric_limits<std::size_t>::max(),
        std::numeric_limits<std::size_t>::max());
    require_scrolling_window(clamped.has_value());
    check_scrolling_window(clamped->first_row == 29);
    check_scrolling_window(clamped->layer_indices == std::vector<std::size_t>{29});
    check_scrolling_window(!semantic_layer_palette_window_t::from(chart, 0, 0));
    check_scrolling_window(!semantic_layer_palette_window_t::from(chart, 0, 4, 29));
    check_scrolling_window(!semantic_layer_palette_window_t::from(chart, 0, 4, 0));

    chart.semantic_layers.clear();
    const auto empty = semantic_layer_palette_window_t::from(chart, 99, 4);
    require_scrolling_window(empty.has_value());
    check_scrolling_window(empty->first_row == 0);
    check_scrolling_window(empty->total_count == 0);
    check_scrolling_window(empty->layer_indices.empty());
}

TEST_CASE("semantic layers insert, update, reorder, and share bounded document history",
          "[unit][core][semantic-layer-editor]") {
    auto history = document_history_t::from(semantic_layer_song());
    const auto inserted = apply_semantic_layer_edit(
        history, wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2},
                     .value = {.name = "Percussion",
                               .color = {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25}}});
    REQUIRE(inserted);
    const auto added = history.document.charts.front().semantic_layers.back();
    CHECK(added.id == layer_id_t{8});
    CHECK(added.name == "Percussion");
    CHECK(added.color == color_rgba_t{0, 128, 255, 64});
    CHECK(added.visible);
    CHECK_FALSE(added.locked);
    CHECK(history.document.revision == 5);
    CHECK(is_document_dirty(history));

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.size() == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.back().id == added.id);

    const auto updated = apply_semantic_layer_edit(
        history, wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = added.id,
                     .value = {.name = "♪ Percussion",
                               .color = {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25},
                               .visible = false,
                               .locked = true}});
    REQUIRE(updated);
    const auto changed = history.document.charts.front().semantic_layers.back();
    CHECK(changed.id == added.id);
    CHECK(changed.name == "♪ Percussion");
    CHECK_FALSE(changed.visible);
    CHECK(changed.locked);

    const auto reorder = apply_semantic_layer_edit(
        history, reorder_semantic_layers_action_t{chart_id_t{2}, {added.id, layer_id_t{7}}});
    REQUIRE(reorder);
    CHECK(history.document.charts.front().semantic_layers.front().id == added.id);
    CHECK(history.document.charts.front().semantic_layers.back().id == layer_id_t{7});
    CHECK(history.document.charts.front().semantic_layers.front().locked);

    const auto no_op = apply_semantic_layer_edit(
        history, reorder_semantic_layers_action_t{chart_id_t{2}, {added.id, layer_id_t{7}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 7);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.front().id == layer_id_t{7});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.front().id == added.id);

    const auto unlocked = apply_semantic_layer_edit(
        history, wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = added.id,
                     .value = {.name = changed.name,
                               .color = {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25},
                               .visible = changed.visible,
                               .locked = false}});
    REQUIRE(unlocked);
    CHECK_FALSE(history.document.charts.front().semantic_layers.front().locked);
    CHECK(is_document_dirty(history));
}

TEST_CASE("semantic layer membership is bounded, atomic, and leaves gameplay data intact",
          "[unit][core][semantic-layer-editor][membership]") {
    auto history = document_history_t::from(semantic_layer_song());
    const auto original_tap = note_by_id(history.document.charts.front(), object_id_t{20});
    const auto original_hold = note_by_id(history.document.charts.front(), object_id_t{21});

    const auto assigned = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}, object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::assign});
    REQUIRE(assigned);
    auto& chart = history.document.charts.front();
    CHECK(note_by_id(chart, object_id_t{20}).semantic_layers == std::vector{layer_id_t{7}});
    CHECK(note_by_id(chart, object_id_t{21}).semantic_layers == std::vector{layer_id_t{7}});
    CHECK(note_by_id(chart, object_id_t{20}).kind == original_tap.kind);
    CHECK(note_by_id(chart, object_id_t{20}).position == original_tap.position);
    CHECK(note_by_id(chart, object_id_t{20}).sound == original_tap.sound);
    CHECK(note_by_id(chart, object_id_t{20}).label == original_tap.label);
    CHECK(note_by_id(chart, object_id_t{21}).kind == original_hold.kind);
    CHECK(note_by_id(chart, object_id_t{21}).position == original_hold.position);
    CHECK(note_by_id(chart, object_id_t{21}).end == original_hold.end);
    CHECK(note_by_id(chart, object_id_t{21}).sound == original_hold.sound);
    CHECK(note_by_id(chart, object_id_t{21}).label == original_hold.label);

    const auto assign_no_op = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}, object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::assign});
    CHECK_FALSE(assign_no_op.applied);
    CHECK(assign_no_op.diagnostics.empty());
    CHECK(history.document.revision == 5);

    const auto removed = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}},
                                         .operation = wormedit::membership_operation_t::remove});
    REQUIRE(removed);
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers.empty());
    CHECK(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers ==
          std::vector{layer_id_t{7}});

    const auto toggled = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}, object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::toggle});
    REQUIRE(toggled);
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers ==
          std::vector{layer_id_t{7}});
    CHECK(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers.empty());
    REQUIRE(undo_document_command(history));
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers.empty());
    CHECK(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers ==
          std::vector{layer_id_t{7}});
    REQUIRE(redo_document_command(history));
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers ==
          std::vector{layer_id_t{7}});
    CHECK(is_document_dirty(history));
}

TEST_CASE("semantic layer membership edits notes and BGM events in one undo step",
          "[unit][core][semantic-layer-editor][bgm]") {
    auto history = document_history_t::from(semantic_layer_bgm_song());
    const auto assigned = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{21}},
                                         .bgm_events = {object_id_t{23}},
                                         .operation = wormedit::membership_operation_t::assign});
    REQUIRE(assigned);
    REQUIRE(history.undo.size() == 1);
    CHECK(mixed_layer_membership(history.document.charts.front(), {layer_id_t{7}}));
    REQUIRE(undo_document_command(history));
    CHECK(mixed_layer_membership(history.document.charts.front(), {}));
    REQUIRE(redo_document_command(history));
}

TEST_CASE("locked BGM layer rejects a mixed clear atomically",
          "[unit][core][semantic-layer-editor][bgm][locked]") {
    auto locked_song = semantic_layer_bgm_song();
    locked_song.charts.front().semantic_layers.front().locked = true;
    locked_song.charts.front().measures.front().bgm_events.front().semantic_layers = {layer_id_t{7}};
    auto locked_history = document_history_t::from(std::move(locked_song));
    const auto before_revision = locked_history.document.revision;
    const auto rejected = apply_semantic_layer_edit(
        locked_history, clear_semantic_layer_membership_action_t{.chart = chart_id_t{2},
                                                                 .notes = {object_id_t{21}},
                                                                 .bgm_events = {object_id_t{23}}});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected, diagnostic_code_t::unsupported_feature));
    CHECK(locked_history.document.revision == before_revision);
    CHECK(note_by_id(locked_history.document.charts.front(), object_id_t{21}).semantic_layers.empty());
    CHECK(locked_history.document.charts.front().measures.front().bgm_events.front().semantic_layers ==
          std::vector{layer_id_t{7}});
    CHECK(locked_history.undo.empty());
}

TEST_CASE("deleting a semantic layer with unlink removes BGM membership",
          "[unit][core][semantic-layer-editor][bgm]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{33}, .name = "Music"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{23},
        .position = {measure_id_t{3}, 360}, .source_lane = object_id_t{33},
        .semantic_layers = {layer_id_t{7}}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto removed = apply_semantic_layer_edit(
        history,
        wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .id = layer_id_t{7}, .options = {.remove_from_notes = true}});
    REQUIRE(removed);
    CHECK(history.document.charts.front().measures.front().bgm_events.front().semantic_layers.empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().bgm_events.front().semantic_layers ==
          std::vector{layer_id_t{7}});
}

TEST_CASE("BGM placement receives the sound's semantic layer defaults",
          "[unit][core][semantic-layer-editor][bgm]") {
    auto history = document_history_t::from(bgm_placement_song());
    const auto placed = apply_resource_placement(
        history, {.chart = chart_id_t{2}, .resource = resource_id_t{51},
                     .position = {measure_id_t{3}, 600},
                     .kind = resource_placement_kind_t::bgm_event});
    REQUIRE(placed);
    const auto& events = history.document.charts.front().measures.front().bgm_events;
    REQUIRE(events.size() == 1);
    CHECK(events.front().semantic_layers == std::vector{layer_id_t{7}});
}

TEST_CASE("document validation rejects unknown and multiple BGM semantic layers",
          "[unit][core][semantic-layer-editor][bgm][validation]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes.push_back({.id = object_id_t{33}, .name = "Music"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{23},
        .position = {measure_id_t{3}, 360}, .source_lane = object_id_t{33}});
    REQUIRE(validate_document(song).empty());
    auto& event = chart.measures.front().bgm_events.front();
    event.semantic_layers = {layer_id_t{99}};
    CHECK_FALSE(validate_document(song).empty());
    event.semantic_layers = {layer_id_t{7}, layer_id_t{7}};
    CHECK_FALSE(validate_document(song).empty());
}

TEST_CASE("clearing mixed semantic layers is one undoable revision and preserves gameplay",
          "[unit][core][semantic-layer-editor][clear]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = layer_id_t{8}, .name = "Percussion"});
    note_by_id(chart, object_id_t{21}).semantic_layers = {layer_id_t{8}};
    const auto original_notes = chart.measures.front().notes;
    auto history = document_history_t::from(std::move(song));

    REQUIRE(
        apply_semantic_layer_edit(history, clear_semantic_layer_membership_action_t{
                                               chart_id_t{2}, {object_id_t{20}, object_id_t{21}}}));
    CHECK(history.document.revision == 5);
    REQUIRE(history.undo.size() == 1);
    const auto& cleared_notes = history.document.charts.front().measures.front().notes;
    check_cleared_gameplay(cleared_notes, original_notes);
    REQUIRE(undo_document_command(history));
    CHECK(history.undo.empty());
    check_clear_memberships(history.document.charts.front(), {layer_id_t{7}}, {layer_id_t{8}});
    REQUIRE(redo_document_command(history));
    check_clear_memberships(history.document.charts.front(), {}, {});
}

TEST_CASE("clearing memberships rejects a locked member without modifying history",
          "[unit][core][semantic-layer-editor][clear][locked]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = layer_id_t{8}, .name = "Locked", .locked = true});
    note_by_id(chart, object_id_t{21}).semantic_layers = {layer_id_t{8}};
    auto history = document_history_t::from(std::move(song));
    const auto next_revision = history.next_revision;

    const auto rejected =
        apply_semantic_layer_edit(history, clear_semantic_layer_membership_action_t{
                                               chart_id_t{2}, {object_id_t{20}, object_id_t{21}}});
    CHECK_FALSE(rejected);
    CHECK(has_code(rejected, diagnostic_code_t::unsupported_feature));
    check_pristine_clear_history(history, next_revision);
    check_clear_memberships(history.document.charts.front(), {layer_id_t{7}}, {layer_id_t{8}});
}

TEST_CASE("clearing unassigned notes is a no-op and mixed assigned notes still clear",
          "[unit][core][semantic-layer-editor][clear][noop]") {
    auto history = document_history_t::from(semantic_layer_song());
    const auto next_revision = history.next_revision;
    const auto no_op = apply_semantic_layer_edit(
        history, clear_semantic_layer_membership_action_t{chart_id_t{2}, {object_id_t{21}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    check_pristine_clear_history(history, next_revision);
    REQUIRE(
        apply_semantic_layer_edit(history, clear_semantic_layer_membership_action_t{
                                               chart_id_t{2}, {object_id_t{20}, object_id_t{21}}}));
    CHECK(history.document.revision == 5);
    CHECK(history.undo.size() == 1);
    check_clear_memberships(history.document.charts.front(), {}, {});
}

TEST_CASE("clearing semantic layers validates charts notes and selection caps",
          "[unit][core][semantic-layer-editor][clear][negative]") {
    auto history = document_history_t::from(semantic_layer_song());
    const auto unknown_chart = apply_semantic_layer_edit(
        history, clear_semantic_layer_membership_action_t{chart_id_t{99}, {object_id_t{20}}});
    check_rejected_clear(unknown_chart, diagnostic_code_t::invalid_id);
    for (const auto invalid_note : {object_id_t{}, object_id_t{99}, object_id_t{30}}) {
        const auto unknown =
            apply_semantic_layer_edit(history, clear_semantic_layer_membership_action_t{
                                                   chart_id_t{2}, {object_id_t{20}, invalid_note}});
        check_rejected_clear(unknown, diagnostic_code_t::invalid_id);
    }
    const auto duplicate =
        apply_semantic_layer_edit(history, clear_semantic_layer_membership_action_t{
                                               chart_id_t{2}, {object_id_t{20}, object_id_t{20}}});
    check_rejected_clear(duplicate, diagnostic_code_t::duplicate_id);
    const auto capped = apply_semantic_layer_edit(
        history,
        clear_semantic_layer_membership_action_t{chart_id_t{2}, {object_id_t{20}, object_id_t{21}}},
        {.maximum_note_selection = 1});
    check_rejected_clear(capped, diagnostic_code_t::invalid_resolution);
    check_pristine_clear_history(history, 5);
    check_clear_memberships(history.document.charts.front(), {layer_id_t{7}}, {});
}

TEST_CASE("locked layers block membership and deletion until unlocked",
          "[unit][core][semantic-layer-editor][locked]") {
    auto song = semantic_layer_song();
    song.charts.front().semantic_layers.front().locked = true;
    auto history = document_history_t::from(std::move(song));

    const auto assign_locked = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::assign});
    CHECK_FALSE(assign_locked);
    CHECK(has_code(assign_locked, diagnostic_code_t::unsupported_feature));
    const auto delete_locked = apply_semantic_layer_edit(
        history,
        wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .id = layer_id_t{7}, .options = {.remove_from_notes = true}});
    CHECK_FALSE(delete_locked);
    CHECK(has_code(delete_locked, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 4);

    REQUIRE(apply_semantic_layer_edit(
        history,
        wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2},
            .id = layer_id_t{7},
            .value = {.name = "Vocal",
                      .color = {.red = 1.0, .green = 128.0 / 255.0, .blue = 0.0, .alpha = 1.0},
                      .visible = true,
                      .locked = false}}));
    REQUIRE(apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::assign}));
    const auto in_use = apply_semantic_layer_edit(
        history, wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2}, .id = layer_id_t{7}});
    CHECK_FALSE(in_use);
    CHECK(has_code(in_use, diagnostic_code_t::unsupported_feature));

    const auto removal_capped = apply_semantic_layer_edit(
        history,
        wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .id = layer_id_t{7}, .options = {.remove_from_notes = true}},
        {.maximum_note_selection = 1});
    CHECK_FALSE(removal_capped);
    CHECK(has_code(removal_capped, diagnostic_code_t::invalid_resolution));
    const auto deleted = apply_semantic_layer_edit(
        history,
        wormedit::delete_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .id = layer_id_t{7}, .options = {.remove_from_notes = true}});
    REQUIRE(deleted);
    CHECK(history.document.charts.front().semantic_layers.empty());
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers.empty());
    CHECK(validate_document(history.document).empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.front().id == layer_id_t{7});
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers ==
          std::vector{layer_id_t{7}});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.empty());
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers.empty());
}

TEST_CASE(
    "semantic layer edits reject malformed names colors IDs and bounded selections atomically",
    "[unit][core][semantic-layer-editor][negative]") {
    auto history = document_history_t::from(semantic_layer_song());
    const auto revision = history.document.revision;
    const auto valid_color =
        semantic_layer_rgba_t{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0};

    const auto empty_name = apply_semantic_layer_edit(
        history, wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2}, .value = {.name = "", .color = valid_color}});
    CHECK_FALSE(empty_name);
    CHECK(has_code(empty_name, diagnostic_code_t::unsupported_feature));
    const auto empty_update = apply_semantic_layer_edit(
        history, wormedit::update_edit_t<wormedit::semantic_layer_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = layer_id_t{7},
                     .value = {.name = "", .color = valid_color}});
    CHECK_FALSE(empty_update);
    CHECK(has_code(empty_update, diagnostic_code_t::unsupported_feature));
    const auto zero_name_cap = apply_semantic_layer_edit(
        history,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .value = {.name = "Valid", .color = valid_color}},
        {.maximum_name_bytes = 0});
    CHECK_FALSE(zero_name_cap);
    CHECK(has_code(zero_name_cap, diagnostic_code_t::invalid_resolution));
    const auto zero_total_name_cap = apply_semantic_layer_edit(
        history,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .value = {.name = "Valid", .color = valid_color}},
        {.maximum_total_name_bytes = 0});
    CHECK_FALSE(zero_total_name_cap);
    CHECK(has_code(zero_total_name_cap, diagnostic_code_t::invalid_resolution));

    for (const auto& name : {std::string{"bad\0name", 8}, std::string{"\xC0\xAF", 2}}) {
        const auto invalid = apply_semantic_layer_edit(
            history, wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
                         .chart = chart_id_t{2}, .value = {.name = name, .color = valid_color}});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::unsupported_feature));
    }
    const auto long_name = apply_semantic_layer_edit(
        history,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .value = {.name = "Long name", .color = valid_color}},
        {.maximum_name_bytes = 4});
    CHECK_FALSE(long_name);
    CHECK(has_code(long_name, diagnostic_code_t::unsupported_feature));
    const auto total_name_cap = apply_semantic_layer_edit(
        history,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .value = {.name = "x", .color = valid_color}},
        {.maximum_total_name_bytes = 5});
    CHECK_FALSE(total_name_cap);
    CHECK(has_code(total_name_cap, diagnostic_code_t::invalid_resolution));

    for (const auto color :
         {semantic_layer_rgba_t{.red = -0.1}, semantic_layer_rgba_t{.green = 1.1},
          semantic_layer_rgba_t{.blue = std::numeric_limits<double>::infinity()},
          semantic_layer_rgba_t{.alpha = std::numeric_limits<double>::quiet_NaN()}}) {
        const auto invalid = apply_semantic_layer_edit(
            history, wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
                         .chart = chart_id_t{2}, .value = {.name = "Valid", .color = color}});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::invalid_resolution));
    }

    const auto layer_capped = apply_semantic_layer_edit(
        history,
        wormedit::insert_edit_t<wormedit::semantic_layer_edit_value_t>{
            .chart = chart_id_t{2}, .value = {.name = "Extra", .color = valid_color}},
        {.maximum_layers = 1});
    CHECK_FALSE(layer_capped);
    CHECK(has_code(layer_capped, diagnostic_code_t::invalid_resolution));

    const auto duplicate_selection = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}, object_id_t{20}},
                                         .operation = wormedit::membership_operation_t::assign});
    CHECK_FALSE(duplicate_selection);
    CHECK(has_code(duplicate_selection, diagnostic_code_t::duplicate_id));
    const auto unknown_note = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{99}},
                                         .operation = wormedit::membership_operation_t::assign});
    CHECK_FALSE(unknown_note);
    CHECK(has_code(unknown_note, diagnostic_code_t::invalid_id));
    const auto unknown_layer = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{99},
                                         .notes = {object_id_t{20}},
                                         .operation = wormedit::membership_operation_t::assign});
    CHECK_FALSE(unknown_layer);
    CHECK(has_code(unknown_layer, diagnostic_code_t::invalid_id));
    const auto note_capped = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = {object_id_t{20}, object_id_t{21}},
                                         .operation = wormedit::membership_operation_t::assign},
        {.maximum_note_selection = 1});
    CHECK_FALSE(note_capped);
    CHECK(has_code(note_capped, diagnostic_code_t::invalid_resolution));

    const auto bad_order = apply_semantic_layer_edit(
        history, reorder_semantic_layers_action_t{chart_id_t{2}, {layer_id_t{7}, layer_id_t{7}}});
    CHECK_FALSE(bad_order);
    CHECK(has_code(bad_order, diagnostic_code_t::invalid_resolution));

    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().semantic_layers.size() == 1);
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers ==
          std::vector{layer_id_t{7}});
    CHECK(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers.empty());
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("semantic layer assignment indexes a fifty-thousand-note chart once",
          "[unit][core][semantic-layer-editor][stress]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    auto& notes = chart.measures.front().notes;
    notes.clear();
    notes.reserve(50'000);
    std::vector<object_id_t> selected;
    selected.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        const auto id = object_id_t{1'000 + index};
        notes.push_back({.id = id, .position = {measure_id_t{3}, index * 2}, .lane = lane_id_t{1}});
        selected.push_back(id);
    }
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));

    const auto started = std::chrono::steady_clock::now();
    const auto applied = apply_semantic_layer_edit(
        history,
        semantic_layer_membership_edit_t{.chart = chart_id_t{2},
                                         .layer = layer_id_t{7},
                                         .notes = std::move(selected),
                                         .operation = wormedit::membership_operation_t::assign});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(applied);
    CHECK(history.document.charts.front().measures.front().notes.size() == 50'000);
    CHECK(history.document.charts.front().measures.front().notes.back().semantic_layers ==
          std::vector{layer_id_t{7}});
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    INFO("50k semantic layer assignment took " << milliseconds << " ms");
    if (milliseconds > 125) {
        WARN("50k semantic layer assignment exceeded the 125 ms interaction target on this host");
    }
    CHECK(milliseconds < 500);
}

namespace {
void check_membership(bool condition) { CHECK(condition); }
void require_membership(bool condition) { REQUIRE(condition); }
}

TEST_CASE("replacement layer assignments keep one membership and restore originals on undo",
          "[unit][core][semantic-layer][replacement]") {
    auto song = semantic_layer_song();
    song.charts.front().semantic_layers.push_back({.id = layer_id_t{8}, .name = "Target", .color = {32, 64, 128, 255}});
    auto history = document_history_t::from(song);
    require_membership(static_cast<bool>(apply_semantic_layer_edit(
        history, replace_notes_semantic_layer_action_t{
                     chart_id_t{2}, layer_id_t{8}, {object_id_t{20}, object_id_t{21}}})));
    check_membership(static_cast<bool>(history.document.revision == 5));
    check_membership(static_cast<bool>(history.undo.size() == 1));
    for (const auto& note : history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers == std::vector{layer_id_t{8}}));
    require_membership(static_cast<bool>(undo_document_command(history)));
    check_membership(static_cast<bool>(write_native_project(history.document).value == write_native_project(song).value));
}

TEST_CASE("replacement layer assignments reject locked source layers atomically",
          "[unit][core][semantic-layer][replacement]") {
    auto song = semantic_layer_song();
    song.charts.front().semantic_layers.front().locked = true;
    song.charts.front().semantic_layers.push_back({.id = layer_id_t{8}, .name = "Target", .color = {32, 64, 128, 255}});
    auto history = document_history_t::from(song);
    const auto result = apply_semantic_layer_edit(
        history, replace_notes_semantic_layer_action_t{
                     chart_id_t{2}, layer_id_t{8}, {object_id_t{21}, object_id_t{20}}});
    check_membership(static_cast<bool>(!(result)));
    check_membership(static_cast<bool>(has_code(result, diagnostic_code_t::unsupported_feature)));
    check_membership(static_cast<bool>(write_native_project(history.document).value == write_native_project(song).value));
    check_membership(static_cast<bool>(history.undo.empty()));
}

TEST_CASE("replacement layer paint toggles only the target and replaces other memberships",
          "[unit][core][semantic-layer][replacement]") {
    auto song = semantic_layer_song();
    song.charts.front().semantic_layers.push_back({.id = layer_id_t{8}, .name = "Target", .color = {32, 64, 128, 255}});
    song.charts.front().measures.front().notes.back().semantic_layers = {layer_id_t{8}};
    auto history = document_history_t::from(song);
    require_membership(static_cast<bool>(apply_semantic_layer_edit(
        history, replace_notes_semantic_layer_action_t{
                     chart_id_t{2}, layer_id_t{8}, {object_id_t{20}, object_id_t{21}}, true})));
    check_membership(static_cast<bool>(history.document.charts.front().measures.front().notes.front().semantic_layers == std::vector{layer_id_t{8}}));
    check_membership(static_cast<bool>(history.document.charts.front().measures.front().notes.back().semantic_layers.empty()));
    require_membership(static_cast<bool>(undo_document_command(history)));
    check_membership(static_cast<bool>(write_native_project(history.document).value == write_native_project(song).value));
}

TEST_CASE("replacement membership batch checks a shared layer catalog once",
          "[unit][core][semantic-layer][replacement][batch]") {
    auto song = semantic_layer_song();
    auto& chart = song.charts.front();
    for (std::uint64_t index = 0; index < 1000; ++index)
        chart.semantic_layers.push_back({.id = layer_id_t{100 + index}, .name = "Layer " + std::to_string(index), .color = {32, 64, 128, 255}});
    chart.measures.front().notes.clear();
    std::vector<object_id_t> ids;
    for (std::uint64_t index = 0; index < 900; ++index) {
        ids.push_back(object_id_t{10000 + index});
        chart.measures.front().notes.push_back({.id = ids.back(), .position = {measure_id_t{3}, index},
            .lane = lane_id_t{1}, .semantic_layers = {layer_id_t{100 + index}}});
    }
    auto history = document_history_t::from(song);
    require_membership(static_cast<bool>(apply_semantic_layer_edit(
        history, replace_notes_semantic_layer_action_t{chart_id_t{2}, layer_id_t{1099}, ids})));
    check_membership(static_cast<bool>(history.undo.size() == 1));
    for (const auto& note : history.document.charts.front().measures.front().notes)
        check_membership(static_cast<bool>(note.semantic_layers == std::vector{layer_id_t{1099}}));
}

namespace {
auto selected_sound_layer_song() -> song_document_t {
    auto song = semantic_layer_bgm_song();
    song.audio_assets.push_back({.id = asset_id_t{50}, .source_path = "sample.wav"});
    song.resources.push_back({.id = resource_id_t{51}, .asset = asset_id_t{50},
                              .roles = {resource_role_t::sample, resource_role_t::bgm}});
    song.resources.push_back({.id = resource_id_t{52}, .asset = asset_id_t{50},
                              .roles = {resource_role_t::sample}});
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = layer_id_t{8}, .name = "Percussion"});
    chart.sound_layer_defaults.push_back({resource_id_t{51}, {layer_id_t{7}}});
    auto& measure = chart.measures.front();
    for (auto& note : measure.notes) note.sound = resource_id_t{51};
    measure.bgm_events.front().sound = resource_id_t{51};
    measure.bgm_events.push_back({.id = object_id_t{24}, .position = {measure.id, 600},
        .source_lane = object_id_t{33}, .sound = resource_id_t{51}, .semantic_layers = {layer_id_t{7}}});
    measure.notes.push_back({.id = object_id_t{25}, .position = {measure.id, 720},
                             .lane = lane_id_t{1}, .sound = resource_id_t{52}});
    measure.notes.push_back({.id = object_id_t{26}, .position = {measure.id, 840},
                             .lane = lane_id_t{1}});
    REQUIRE(validate_document(song).empty());
    return song;
}
void check_selected_sounds(bool value) { CHECK(value); }
}

TEST_CASE("Selected sound layers propagate chart-wide while preserving explicit memberships",
          "[unit][core][semantic-layer][selected-sound-layers]") {
    auto history = document_history_t::from(selected_sound_layer_song());
    const auto original = write_native_project(history.document);
    REQUIRE(original.value);
    const auto applied = add_selected_sounds_to_semantic_layer(
        history,
        {chart_id_t{2}, layer_id_t{8}, {object_id_t{20}, object_id_t{26}}, {object_id_t{23}}});
    REQUIRE(applied);
    auto& chart = history.document.charts.front();
    check_selected_sounds(chart.sound_layer_defaults.size() == 1);
    check_selected_sounds(chart.sound_layer_defaults.front().layers == std::vector{layer_id_t{7}, layer_id_t{8}});
    check_selected_sounds(note_by_id(chart, object_id_t{20}).semantic_layers == std::vector{layer_id_t{7}});
    check_selected_sounds(note_by_id(chart, object_id_t{21}).semantic_layers == std::vector{layer_id_t{7}, layer_id_t{8}});
    check_selected_sounds(chart.measures.front().bgm_events[0].semantic_layers == std::vector{layer_id_t{8}});
    check_selected_sounds(chart.measures.front().bgm_events[1].semantic_layers == std::vector{layer_id_t{7}});
    check_selected_sounds(note_by_id(chart, object_id_t{25}).semantic_layers.empty());
    check_selected_sounds(note_by_id(chart, object_id_t{26}).semantic_layers.empty());
    check_selected_sounds(sound_default_layers_for_placement(chart, resource_id_t{51}) ==
                          std::vector{layer_id_t{7}, layer_id_t{8}});
    check_selected_sounds(validate_document(history.document).empty());
    check_selected_sounds(history.undo.size() == 1);
    const auto written = write_native_project(history.document);
    REQUIRE(written.value);
    const auto round_trip = parse_native_project(*written.value);
    REQUIRE(round_trip.value);
    check_selected_sounds(round_trip.value->charts.front().sound_layer_defaults.front().layers ==
                          std::vector{layer_id_t{7}, layer_id_t{8}});
    REQUIRE(undo_document_command(history));
    const auto undone = write_native_project(history.document);
    REQUIRE(undone.value);
    // Undo changes the document revision; compare the restored content directly.
    check_selected_sounds(history.document.charts.front().sound_layer_defaults.front().layers ==
                          std::vector{layer_id_t{7}});
    check_selected_sounds(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers.empty());
    check_selected_sounds(history.document.charts.front().measures.front().bgm_events.front().semantic_layers.empty());
}

TEST_CASE("Selected sound layer edits reject invalid locked and oversized selections atomically",
          "[unit][core][semantic-layer][selected-sound-layers]") {
    auto history = document_history_t::from(selected_sound_layer_song());
    const add_selected_sounds_to_semantic_layer_action_t action{
        chart_id_t{2}, layer_id_t{8}, {object_id_t{20}}, {}};
    auto invalid = action;
    invalid.notes = {object_id_t{999}};
    check_selected_sounds(!add_selected_sounds_to_semantic_layer(history, invalid));
    invalid.notes = {object_id_t{23}}; // A BGM ID is not a note ID.
    check_selected_sounds(!add_selected_sounds_to_semantic_layer(history, invalid));
    invalid.notes = {object_id_t{20}, object_id_t{20}};
    check_selected_sounds(!add_selected_sounds_to_semantic_layer(history, invalid));
    semantic_layer_edit_limits_t limits;
    limits.maximum_note_selection = 0;
    check_selected_sounds(!add_selected_sounds_to_semantic_layer(history, action, limits));
    history.document.charts.front().semantic_layers.back().locked = true;
    check_selected_sounds(!add_selected_sounds_to_semantic_layer(history, action));
    history.document.charts.front().semantic_layers.back().locked = false;
    note_by_id(history.document.charts.front(), object_id_t{20}).sound = resource_id_t{999};
    const auto missing_sound = add_selected_sounds_to_semantic_layer(history, action);
    check_selected_sounds(!missing_sound && has_code(missing_sound, diagnostic_code_t::invalid_id));
    check_selected_sounds(history.document.revision == 4);
    check_selected_sounds(history.undo.empty());
    check_selected_sounds(history.document.charts.front().sound_layer_defaults.front().layers ==
                          std::vector{layer_id_t{7}});
}

TEST_CASE("Selected sound layers deduplicate resources support BGM-only and ignore silent objects",
          "[unit][core][semantic-layer][selected-sound-layers]") {
    auto history = document_history_t::from(selected_sound_layer_song());
    const auto silent = add_selected_sounds_to_semantic_layer(
        history, {chart_id_t{2}, layer_id_t{8}, {object_id_t{26}}, {}});
    check_selected_sounds(!silent.applied && silent.diagnostics.empty());
    check_selected_sounds(history.undo.empty());
    const auto bgm = add_selected_sounds_to_semantic_layer(
        history, {chart_id_t{2}, layer_id_t{8}, {}, {object_id_t{23}, object_id_t{24}}});
    REQUIRE(bgm);
    check_selected_sounds(history.document.charts.front().sound_layer_defaults.size() == 1);
    const auto revision = history.document.revision;
    const auto repeated = add_selected_sounds_to_semantic_layer(
        history,
        {chart_id_t{2}, layer_id_t{8}, {object_id_t{20}, object_id_t{21}}, {object_id_t{23}}});
    check_selected_sounds(!repeated.applied && repeated.diagnostics.empty());
    check_selected_sounds(history.document.revision == revision);
    check_selected_sounds(history.undo.size() == 1);
}
