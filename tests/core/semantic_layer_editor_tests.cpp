#include <wormedit/core/semantic_layer_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <ranges>
#include <string>
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

    const auto first = make_semantic_layer_palette_window(chart, std::nullopt, 0, false, 4);
    REQUIRE(first);
    CHECK(first->page == 0);
    CHECK(first->page_count == 3);
    CHECK(first->total_count == 11);
    CHECK(first->layer_indices == std::vector<std::size_t>{0, 1, 2, 3});

    const auto clamped = make_semantic_layer_palette_window(chart, std::nullopt, 99, false, 4);
    REQUIRE(clamped);
    CHECK(clamped->page == 2);
    CHECK(clamped->layer_indices == std::vector<std::size_t>{8, 9, 11});

    const auto revealed = make_semantic_layer_palette_window(chart, layer_id_t{11}, 0, true, 4);
    REQUIRE(revealed);
    CHECK(revealed->page == 2);
    CHECK(revealed->total_count == 12);
    CHECK(revealed->layer_indices == std::vector<std::size_t>{8, 9, 10, 11});

    CHECK_FALSE(make_semantic_layer_palette_window(chart, std::nullopt, 0, false, 0));
    CHECK_FALSE(make_semantic_layer_palette_window(chart, std::nullopt, 0, false, 4, 11));
}

TEST_CASE("semantic layers insert, update, reorder, and share bounded document history",
          "[unit][core][semantic-layer-editor]") {
    auto history = make_document_history(semantic_layer_song());
    const auto inserted = apply_semantic_layer_edit(
        history, 4,
        insert_semantic_layer_action_t{
            chart_id_t{2}, "Percussion", {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25}});
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
        history, 5,
        update_semantic_layer_action_t{chart_id_t{2},
                                       added.id,
                                       "♪ Percussion",
                                       {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25},
                                       false,
                                       true});
    REQUIRE(updated);
    const auto changed = history.document.charts.front().semantic_layers.back();
    CHECK(changed.id == added.id);
    CHECK(changed.name == "♪ Percussion");
    CHECK_FALSE(changed.visible);
    CHECK(changed.locked);

    const auto reorder = apply_semantic_layer_edit(
        history, 6, reorder_semantic_layers_action_t{chart_id_t{2}, {added.id, layer_id_t{7}}});
    REQUIRE(reorder);
    CHECK(history.document.charts.front().semantic_layers.front().id == added.id);
    CHECK(history.document.charts.front().semantic_layers.back().id == layer_id_t{7});
    CHECK(history.document.charts.front().semantic_layers.front().locked);

    const auto no_op = apply_semantic_layer_edit(
        history, 7, reorder_semantic_layers_action_t{chart_id_t{2}, {added.id, layer_id_t{7}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 7);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.front().id == layer_id_t{7});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().semantic_layers.front().id == added.id);

    const auto unlocked = apply_semantic_layer_edit(
        history, 7,
        update_semantic_layer_action_t{chart_id_t{2},
                                       added.id,
                                       changed.name,
                                       {.red = 0.0, .green = 0.5, .blue = 1.0, .alpha = 0.25},
                                       changed.visible,
                                       false});
    REQUIRE(unlocked);
    CHECK_FALSE(history.document.charts.front().semantic_layers.front().locked);
    CHECK(is_document_dirty(history));
}

TEST_CASE("semantic layer membership is bounded, atomic, and leaves gameplay data intact",
          "[unit][core][semantic-layer-editor][membership]") {
    auto history = make_document_history(semantic_layer_song());
    const auto original_tap = note_by_id(history.document.charts.front(), object_id_t{20});
    const auto original_hold = note_by_id(history.document.charts.front(), object_id_t{21});

    const auto assigned = apply_semantic_layer_edit(
        history, 4,
        assign_notes_to_semantic_layer_action_t{
            chart_id_t{2}, layer_id_t{7}, {object_id_t{20}, object_id_t{21}}});
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
        history, 5,
        assign_notes_to_semantic_layer_action_t{
            chart_id_t{2}, layer_id_t{7}, {object_id_t{20}, object_id_t{21}}});
    CHECK_FALSE(assign_no_op.applied);
    CHECK(assign_no_op.diagnostics.empty());
    CHECK(history.document.revision == 5);

    const auto removed = apply_semantic_layer_edit(
        history, 5,
        remove_notes_from_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, {object_id_t{20}}});
    REQUIRE(removed);
    CHECK(note_by_id(history.document.charts.front(), object_id_t{20}).semantic_layers.empty());
    CHECK(note_by_id(history.document.charts.front(), object_id_t{21}).semantic_layers ==
          std::vector{layer_id_t{7}});

    const auto toggled = apply_semantic_layer_edit(
        history, 6,
        toggle_semantic_layer_membership_action_t{
            chart_id_t{2}, layer_id_t{7}, {object_id_t{20}, object_id_t{21}}});
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

TEST_CASE("locked layers block membership and deletion until unlocked",
          "[unit][core][semantic-layer-editor][locked]") {
    auto song = semantic_layer_song();
    song.charts.front().semantic_layers.front().locked = true;
    auto history = make_document_history(std::move(song));

    const auto assign_locked = apply_semantic_layer_edit(
        history, 4,
        assign_notes_to_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, {object_id_t{21}}});
    CHECK_FALSE(assign_locked);
    CHECK(has_code(assign_locked, diagnostic_code_t::unsupported_feature));
    const auto delete_locked = apply_semantic_layer_edit(
        history, 4, delete_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, true});
    CHECK_FALSE(delete_locked);
    CHECK(has_code(delete_locked, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.revision == 4);

    REQUIRE(apply_semantic_layer_edit(
        history, 4,
        update_semantic_layer_action_t{
            chart_id_t{2},
            layer_id_t{7},
            "Vocal",
            {.red = 1.0, .green = 128.0 / 255.0, .blue = 0.0, .alpha = 1.0},
            true,
            false}));
    REQUIRE(apply_semantic_layer_edit(
        history, 5,
        assign_notes_to_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, {object_id_t{21}}}));
    const auto in_use = apply_semantic_layer_edit(
        history, 6, delete_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}});
    CHECK_FALSE(in_use);
    CHECK(has_code(in_use, diagnostic_code_t::unsupported_feature));

    const auto removal_capped = apply_semantic_layer_edit(
        history, 6, delete_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, true},
        {.maximum_note_selection = 1});
    CHECK_FALSE(removal_capped);
    CHECK(has_code(removal_capped, diagnostic_code_t::invalid_resolution));
    const auto deleted = apply_semantic_layer_edit(
        history, 6, delete_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, true});
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
    auto history = make_document_history(semantic_layer_song());
    const auto revision = history.document.revision;
    const auto valid_color =
        semantic_layer_rgba_t{.red = 1.0, .green = 0.0, .blue = 0.0, .alpha = 1.0};

    const auto empty_name = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "", valid_color});
    CHECK_FALSE(empty_name);
    CHECK(has_code(empty_name, diagnostic_code_t::unsupported_feature));
    const auto empty_update = apply_semantic_layer_edit(
        history, revision,
        update_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, "", valid_color});
    CHECK_FALSE(empty_update);
    CHECK(has_code(empty_update, diagnostic_code_t::unsupported_feature));
    const auto zero_name_cap = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "Valid", valid_color},
        {.maximum_name_bytes = 0});
    CHECK_FALSE(zero_name_cap);
    CHECK(has_code(zero_name_cap, diagnostic_code_t::invalid_resolution));
    const auto zero_total_name_cap = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "Valid", valid_color},
        {.maximum_total_name_bytes = 0});
    CHECK_FALSE(zero_total_name_cap);
    CHECK(has_code(zero_total_name_cap, diagnostic_code_t::invalid_resolution));

    for (const auto& name : {std::string{"bad\0name", 8}, std::string{"\xC0\xAF", 2}}) {
        const auto invalid = apply_semantic_layer_edit(
            history, revision, insert_semantic_layer_action_t{chart_id_t{2}, name, valid_color});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::unsupported_feature));
    }
    const auto long_name = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "Long name", valid_color},
        {.maximum_name_bytes = 4});
    CHECK_FALSE(long_name);
    CHECK(has_code(long_name, diagnostic_code_t::unsupported_feature));
    const auto total_name_cap = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "x", valid_color},
        {.maximum_total_name_bytes = 5});
    CHECK_FALSE(total_name_cap);
    CHECK(has_code(total_name_cap, diagnostic_code_t::invalid_resolution));

    for (const auto color :
         {semantic_layer_rgba_t{.red = -0.1}, semantic_layer_rgba_t{.green = 1.1},
          semantic_layer_rgba_t{.blue = std::numeric_limits<double>::infinity()},
          semantic_layer_rgba_t{.alpha = std::numeric_limits<double>::quiet_NaN()}}) {
        const auto invalid = apply_semantic_layer_edit(
            history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "Valid", color});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::invalid_resolution));
    }

    const auto layer_capped = apply_semantic_layer_edit(
        history, revision, insert_semantic_layer_action_t{chart_id_t{2}, "Extra", valid_color},
        {.maximum_layers = 1});
    CHECK_FALSE(layer_capped);
    CHECK(has_code(layer_capped, diagnostic_code_t::invalid_resolution));

    const auto duplicate_selection = apply_semantic_layer_edit(
        history, revision,
        assign_notes_to_semantic_layer_action_t{
            chart_id_t{2}, layer_id_t{7}, {object_id_t{20}, object_id_t{20}}});
    CHECK_FALSE(duplicate_selection);
    CHECK(has_code(duplicate_selection, diagnostic_code_t::duplicate_id));
    const auto unknown_note = apply_semantic_layer_edit(
        history, revision,
        assign_notes_to_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, {object_id_t{99}}});
    CHECK_FALSE(unknown_note);
    CHECK(has_code(unknown_note, diagnostic_code_t::invalid_id));
    const auto unknown_layer = apply_semantic_layer_edit(
        history, revision,
        assign_notes_to_semantic_layer_action_t{chart_id_t{2}, layer_id_t{99}, {object_id_t{20}}});
    CHECK_FALSE(unknown_layer);
    CHECK(has_code(unknown_layer, diagnostic_code_t::invalid_id));
    const auto note_capped = apply_semantic_layer_edit(
        history, revision,
        assign_notes_to_semantic_layer_action_t{
            chart_id_t{2}, layer_id_t{7}, {object_id_t{20}, object_id_t{21}}},
        {.maximum_note_selection = 1});
    CHECK_FALSE(note_capped);
    CHECK(has_code(note_capped, diagnostic_code_t::invalid_resolution));

    const auto bad_order = apply_semantic_layer_edit(
        history, revision,
        reorder_semantic_layers_action_t{chart_id_t{2}, {layer_id_t{7}, layer_id_t{7}}});
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
    auto history = make_document_history(std::move(song));

    const auto started = std::chrono::steady_clock::now();
    const auto applied = apply_semantic_layer_edit(
        history, 4,
        assign_notes_to_semantic_layer_action_t{chart_id_t{2}, layer_id_t{7}, std::move(selected)});
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
