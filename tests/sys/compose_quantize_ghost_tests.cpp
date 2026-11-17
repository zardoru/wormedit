// BGA stroke admission, quantize ghost geometry and BGA move previews.
#include "frame_timing_fixtures.h"
#include "compose_window_overloads.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cmath>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void prepare_bga_admission_stroke(compose_test_fixture_t& fixture) {
    fixture.project();
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    auto& pending = fixture.application.project.compose_gesture.bga_draw_stroke;
    auto geometry = measure_index_t::from(*fixture.history.read_chart(chart_id_t{1}));
    expect_compose_test(geometry.has_value());
    pending = application_command_state_t::bga_draw_stroke_proposal_t::from(
        chart_stamp_t::from(fixture.history, chart_id_t{1}),
        application_command_state_t::bga_draw_stroke_t{
            .resource = resource_id_t{42}, .geometry = std::move(*geometry)});
}

TEST_CASE("BGA family rejects absent or mismatched admitted payloads without consuming its stroke",
          "[sys][compose][admission][draw]") {
    using namespace compose_input_detail;
    const auto wrong_type = GENERATE(false, true);
    const auto kind = GENERATE(WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
        WORM_UI_COMPOSE_INPUT_NOTE_PAINT_UPDATE, WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE);
    CAPTURE(wrong_type, kind);
    compose_test_fixture_t fixture;
    prepare_bga_admission_stroke(fixture);
    const auto& pending = fixture.application.project.compose_gesture.bga_draw_stroke;
    notification_sink_t sink = notification_sink_t::muted();
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    ui::workspace_action_t action{};
    auto action_window = ui::compose_input_window_of(action);
    const compose_input_inputs_t environment{
        .document = {.history = fixture.history, .workspace = *fixture.binding.value,
            .notifications = sink},
        .commands = fixture.application,
        .controls = {.cache = fixture.cache, .viewport = viewport, .navigation = navigation}};
    compose_input_context_t context{.environment = environment, .ui_action = action_window,
        .state = *fixture.interaction, .input_chart = fixture.history.read_chart(chart_id_t{1})};
    if (wrong_type) context.admitted_selector = admitted_note_paint_t{};
    const auto before = write_native_project(fixture.history.document);
    expect_compose_test(before.value.has_value());
    editor_interaction_result_t result;
    std::string_view accepted;
    const auto routed = dispatch_bga_draw_input(
        {.kind = kind, .resource_role = resource_role_t::bga, .paint_intent = 1},
        context, result, accepted);
    expect_compose_test(routed.has_value());
    expect_compose_test(routed->is_stopping_batch());
    expect_compose_test(routed->outcome.message == "Unsupported Compose input.");
    expect_compose_test(routed->outcome.source == notification_source_t::input);
    expect_compose_test(pending.has_value());
    expect_compose_test(pending->payload.resource == resource_id_t{42});
    expect_compose_test(write_native_project(fixture.history.document).value == before.value);
    expect_compose_test(fixture.history.undo.empty());
    expect_compose_test(!result.document_changed);
    expect_compose_test(accepted.empty());
}

void check_t437_quantize_geometry(const wormedit::ui::compose_view_t& view,
                                  const editor_interaction_state_t& interaction,
                                  std::uint64_t note_id, std::uint64_t expected_after) {
    expect_compose_test(view.quantize_preview_rows.size() == 1);
    expect_compose_test(view.quantize_ghosts_drawable && view.quantize_ghosts.size() == 1);
    const auto& report = view.quantize_preview_rows[0];
    const auto& ghost = view.quantize_ghosts[0];
    expect_compose_test(report.start_after_absolute_row == expected_after);
    expect_compose_test(report.selected && report.head_halfway && ghost.head_halfway && ghost.moved_head);
    expect_compose_test(view.quantize_source_resolution == 960 && view.quantize_geometry_valid);
    expect_compose_test(ghost.object_id.value == note_id);
    const auto expected_before = compose_project_y(report.start_before_absolute_row,
        view.pixels_per_standard_measure / 960.0, interaction.viewport_scroll_pixels,
        view.viewport_height_pixels, view.downscroll);
    const auto expected_after_y = compose_project_y(
        static_cast<long double>(report.start_after_absolute_row) / view.quantize_resolution_scale,
        view.pixels_per_standard_measure / 960.0, interaction.viewport_scroll_pixels,
        view.viewport_height_pixels, view.downscroll);
    expect_compose_test(std::abs(ghost.before_head_anchor_y - expected_before) < 1e-8);
    expect_compose_test(std::abs(ghost.after_head_anchor_y - expected_after_y) < 1e-8);
}

TEST_CASE("Quantize preview publishes selected endpoint geometry on the current field frame",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.resize(1);
    notes.front().position.row = 120;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto note_id = fixture.history.document.charts.front().measures.front().notes.front().id;
    fixture.interaction->selected_notes = {note_id};
    fixture.interaction->viewport_scroll_pixels = -20.0;
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
                                    quantization_hold_t::both_endpoints);
    const auto revision = fixture.history.document.revision;
    const auto source_note = fixture.history.document.charts.front().measures.front().notes.front();
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    check_t437_quantize_geometry(view, *fixture.interaction, note_id.value, 0);
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::later,
                                    quantization_hold_t::both_endpoints);
    fixture.project();
    check_t437_quantize_geometry(fixture.flattened.view.compose, *fixture.interaction, note_id.value, 240);
    expect_compose_test(fixture.history.document.revision == revision);
    expect_compose_test(fixture.history.undo.empty());
    expect_compose_test(fixture.history.document.charts.front().measures.front().notes.front().position ==
                        source_note.position);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{note_id});
    fixture.interaction->selected_notes.clear();
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.empty() &&
                        fixture.flattened.view.compose.quantize_ghosts.size() == 0);
    fixture.interaction->selected_notes = {note_id};
    fixture.history.document.charts.front().measures.front().notes.front().position.row = 360;
    ++fixture.history.document.revision;
    fixture.project();
    const auto& revised = fixture.flattened.view.compose;
    expect_compose_test(revised.quantize_ghosts.size() == 1 &&
                        revised.quantize_preview_rows[0].start_before_absolute_row == 360);
    fixture.application.project.quantize.quantize_editor->request.divisions = 0;
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.quantize_ghosts_drawable &&
                        fixture.flattened.view.compose.quantize_ghosts.size() == 0);
}

TEST_CASE("Quantize dispatcher refreshes authored grid geometry without editing the chart",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    chart.measures.front().notes.resize(1);
    chart.measures.front().notes.front().position.row = 120;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto note_id = fixture.history.document.charts.front().measures.front().notes.front().id;
    fixture.interaction->selected_notes = {note_id};
    const auto original_note = fixture.history.document.charts.front().measures.front().notes.front();
    const auto original_revision = fixture.history.document.revision;
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
        quantization_hold_t::both_endpoints, 4, quantization_space_t::authored_measure);
    fixture.project();
    const auto& authored_fourths = fixture.flattened.view.compose;
    expect_compose_test(authored_fourths.quantize_space == quantization_space_t::authored_measure);
    expect_compose_test(authored_fourths.quantize_display_resolution == 960 &&
                        authored_fourths.quantize_resolution_scale == 1);
    expect_compose_test(authored_fourths.quantize_ghosts.size() == 1 &&
                        authored_fourths.quantize_preview_rows[0].start_after_absolute_row == 180);
    const auto first_ghost = authored_fourths.quantize_ghosts[0];

    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
        quantization_hold_t::both_endpoints, 8, quantization_space_t::authored_measure);
    fixture.project();
    const auto& authored_eighths = fixture.flattened.view.compose;
    expect_compose_test(authored_eighths.quantize_ghosts.size() == 1 &&
                        authored_eighths.quantize_preview_rows[0].start_after_absolute_row == 90);
    const auto second_ghost = authored_eighths.quantize_ghosts[0];
    fixture.project();
    const auto& repeated = fixture.flattened.view.compose.quantize_ghosts[0];
    expect_compose_test(repeated.object_id.value == second_ghost.object_id.value &&
                        repeated.before_head_anchor_y == second_ghost.before_head_anchor_y &&
                        repeated.after_head_anchor_y == second_ghost.after_head_anchor_y &&
                        repeated.after_head.left == second_ghost.after_head.left &&
                        repeated.after_head.top == second_ghost.after_head.top);
    expect_compose_test(first_ghost.after_head_anchor_y != second_ghost.after_head_anchor_y);
    expect_compose_test(fixture.history.document.revision == original_revision &&
                        fixture.history.undo.empty());
    const auto& current_note =
        fixture.history.document.charts.front().measures.front().notes.front();
    expect_compose_test(current_note.id == original_note.id &&
                        current_note.position == original_note.position &&
                        current_note.lane == original_note.lane &&
                        current_note.kind == original_note.kind &&
                        current_note.end == original_note.end);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{note_id});
}

TEST_CASE("Quantize ghost geometry projects promoted absolute rows into native layout bands",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.resize(2);
    auto note = chart.measures.front().notes.front();
    chart.measures[0].definition = {{3, 4}, meter_t{3, 4}};
    chart.measures[0].notes.clear();
    chart.measures[1].id = measure_id_t{2};
    chart.measures[1].definition = {{2, 1}, meter_t{8, 4}};
    note.position = {measure_id_t{2}, 1000};
    auto hold = note;
    hold.id = object_id_t{900001};
    hold.position.row = 0;
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{2}, 960};
    chart.measures[1].notes = {note, hold};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto& projected_notes = fixture.history.document.charts.front().measures[1].notes;
    const auto id = projected_notes.front().id;
    const auto hold_id = projected_notes.back().id;
    fixture.interaction->selected_notes = {id, hold_id};
    fixture.interaction->viewport_scroll_pixels = -20.0;
    fixture.application.project.quantize.quantize_editor = application_command_state_t::quantize_editor_t{
        .stamp = chart_stamp_t::from(fixture.history.document, fixture.history.document.charts.front().id),
        .request = {.divisions = 7, .space = quantization_space_t::authored_measure,
                    .tie = quantization_tie_t::later, .holds = quantization_hold_t::start_only,
                    .allow_resolution_promotion = true}, .preview_enabled = true};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto row = std::ranges::find(std::span(view.quantize_preview_rows),
        id, &wormedit::ui::compose_quantize_report_row_view_t::object_id);
    const auto ghost = std::ranges::find(std::span(view.quantize_ghosts),
        id, &wormedit::ui::compose_quantize_ghost_view_t::object_id);
    const auto hold_ghost = std::ranges::find(std::span(view.quantize_ghosts),
        hold_id, &wormedit::ui::compose_quantize_ghost_view_t::object_id);
    expect_compose_test(view.quantize_ghosts_drawable && view.quantize_ghosts.size() == 2);
    expect_compose_test(view.quantize_source_resolution == 960 &&
                        view.quantize_display_resolution == 6720 && view.quantize_resolution_scale == 7);
    expect_compose_test(row != std::span(view.quantize_preview_rows).end());
    expect_compose_test(row->start_before_absolute_row == 1720 && row->start_after_absolute_row == 12720);
    const auto expected_after = compose_project_y(12720.0L / 7.0L,
        view.pixels_per_standard_measure / 960.0, fixture.interaction->viewport_scroll_pixels,
        view.viewport_height_pixels, view.downscroll);
    expect_compose_test(ghost != std::span(view.quantize_ghosts).end());
    expect_compose_test(std::abs(ghost->after_head_anchor_y - expected_after) < 1e-8);
    expect_compose_test(hold_ghost != std::span(view.quantize_ghosts).end());
    expect_compose_test(!hold_ghost->moved_head && !hold_ghost->moved_tail &&
                        hold_ghost->stationary_tail_annotation);
}

TEST_CASE("Quantize ghost lane geometry follows sparse split-stage physical ranks",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.resize(1);
    auto& note = chart.measures.front().notes.front();
    note.position.row = 120;
    note.lane = lane_id_t{72};
    auto& layout = chart.lane_layouts.front().layout;
    layout.split_stage = true;
    layout.split_after_column = 1;
    layout.lanes = {
        {.id = lane_id_t{99}, .name = "right late", .stage = stage_group_t::right,
         .display_order = 100, .glyph = lane_glyph_t::arrow_left},
        {.id = lane_id_t{31}, .name = "left late", .stage = stage_group_t::left,
         .display_order = 20, .glyph = lane_glyph_t::square},
        {.id = lane_id_t{4}, .name = "left early", .stage = stage_group_t::left,
         .display_order = 2, .glyph = lane_glyph_t::arrow_up},
        {.id = lane_id_t{72}, .name = "right early", .stage = stage_group_t::right,
         .display_order = 7, .glyph = lane_glyph_t::bar}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto id = fixture.history.document.charts.front().measures.front().notes.front().id;
    fixture.interaction->selected_notes = {id};
    fixture.interaction->viewport_scroll_pixels = -20;
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
                                    quantization_hold_t::both_endpoints);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto lane = std::ranges::find(std::span(view.lanes), lane_id_t{72},
                                        &wormedit::ui::compose_lane_view_t::lane_id);
    expect_compose_test(view.quantize_ghosts.size() == 1 && lane != std::span(view.lanes).end());
    expect_compose_test(view.quantize_ghosts[0].before_head.left == lane->bounds.left &&
                        view.quantize_ghosts[0].after_head.right == lane->bounds.right);
    expect_compose_test(view.quantize_ghosts[0].before_head_glyph == lane->glyph &&
                        view.quantize_ghosts[0].after_head_glyph == lane->glyph);
}

TEST_CASE("Quantize hold endpoints use their own layout glyph bands and downscroll anchors",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.resize(1);
    auto& note = chart.measures.front().notes.front();
    note.position.row = 120;
    note.kind = note_kind_t::hold;
    note.end = local_position_t{measure_id_t{1}, 360};
    chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::bar;
    auto later_layout = chart.lane_layouts.front();
    later_layout.id = object_id_t{900002};
    later_layout.position.row = 240;
    later_layout.layout.lanes.front().glyph = lane_glyph_t::square;
    chart.lane_layouts.push_back(std::move(later_layout));
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.preferences.editor_preferences.scroll_directions[0] = 2;
    fixture.project();
    const auto id = fixture.history.document.charts.front().measures.front().notes.front().id;
    fixture.interaction->selected_notes = {id};
    fixture.interaction->viewport_scroll_pixels = -20.0;
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::later,
                                    quantization_hold_t::start_only);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.downscroll && view.quantize_ghosts.size() == 1);
    const auto& ghost = view.quantize_ghosts[0];
    expect_compose_test(ghost.before_head_glyph == lane_glyph_t::bar);
    expect_compose_test(ghost.after_head_glyph == lane_glyph_t::square);
    expect_compose_test(ghost.before_tail_glyph == lane_glyph_t::square);
    expect_compose_test(ghost.before_head_anchor_y == ghost.before_head.bottom &&
                        ghost.after_head_anchor_y == ghost.after_head.bottom &&
                        ghost.before_tail_anchor_y == ghost.before_tail.bottom);
    expect_compose_test(ghost.before_head.top == ghost.before_head_anchor_y - 16.0 &&
                        ghost.before_tail.top == ghost.before_tail_anchor_y - 16.0);
}

TEST_CASE("Quantize ghost tails reflect Leave in place and snapped hold settings",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.resize(1);
    notes.front().position.row = 120;
    notes.front().kind = note_kind_t::hold;
    notes.front().end = local_position_t{measure_id_t{1}, 360};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto note_id = fixture.history.document.charts.front().measures.front().notes.front().id;
    fixture.interaction->selected_notes = {note_id};
    fixture.interaction->viewport_scroll_pixels = -20.0;
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::later,
                                    quantization_hold_t::start_only);
    fixture.project();
    const auto& stationary = fixture.flattened.view.compose;
    expect_compose_test(stationary.quantize_ghosts.size() == 1);
    expect_compose_test(stationary.quantize_ghosts[0].has_tail);
    expect_compose_test(!stationary.quantize_ghosts[0].moved_tail &&
                        stationary.quantize_ghosts[0].stationary_tail_annotation);
    expect_compose_test(stationary.quantize_preview_rows[0].end_after_absolute_row == 360);
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::later,
                                    quantization_hold_t::both_endpoints);
    fixture.project();
    const auto& snapped = fixture.flattened.view.compose;
    expect_compose_test(snapped.quantize_ghosts.size() == 1 && snapped.quantize_ghosts[0].has_tail);
    expect_compose_test(snapped.quantize_ghosts[0].moved_tail);
    expect_compose_test(snapped.quantize_preview_rows[0].end_after_absolute_row == 480);
}

TEST_CASE("Quantize ghosts retain collision visibility and hide blocked halfway settings",
          "[sys][frame-timing][t437]") {
    auto song = compose_test_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.resize(2);
    notes[0].position.row = 120;
    notes[1].lane = notes[0].lane;
    notes[1].position.row = 0;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto selected = fixture.history.document.charts.front().measures.front().notes[0].id;
    fixture.interaction->selected_notes = {selected};
    fixture.interaction->viewport_scroll_pixels = -20.0;
    fixture.application.project.quantize.quantize_editor = application_command_state_t::quantize_editor_t{
        .stamp = chart_stamp_t::from(fixture.history.document, fixture.history.document.charts.front().id),
        .request = {.divisions = 4, .tie = quantization_tie_t::earlier,
                    .collisions = quantization_collision_t::reject}, .preview_enabled = true};
    fixture.project();
    const auto& collision = fixture.flattened.view.compose;
    expect_compose_test(collision.quantize_preview_blocked != 0);
    expect_compose_test(collision.quantize_preview_drawable && collision.quantize_ghosts.size() == 1);
    expect_compose_test(collision.quantize_ghosts[0].collision);
    fixture.application.project.quantize.quantize_editor->request.tie = quantization_tie_t::reject;
    fixture.project();
    const auto& halfway_rejected = fixture.flattened.view.compose;
    expect_compose_test(!halfway_rejected.quantize_preview_drawable);
    expect_compose_test(halfway_rejected.quantize_ghosts.size() == 0);
}
} // namespace
