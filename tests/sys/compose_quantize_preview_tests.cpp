#include "sys/compose_quantize_preview.h"
#include "sys/compose_area_actions.h"
#include <wormedit/core/command.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <string>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {

auto chart_with_notes(std::initializer_list<std::uint64_t> rows) -> chart_document_t {
    chart_document_t chart{
        .id = chart_id_t{1},
        .measures = {{.id = measure_id_t{1}}},
        .lane_layouts = {{.id = object_id_t{1},
                          .position = {measure_id_t{1}, 0},
                          .layout = {.profile_id = "test",
                                     .lanes = {{.id = lane_id_t{1}, .name = "Lane 1"}}}}},
    };
    std::uint64_t id = 10;
    for (const auto row : rows)
        chart.measures[0].notes.push_back({.id = object_id_t{id++},
                                           .position = {measure_id_t{1}, row},
                                           .lane = lane_id_t{1},
                                           .kind = note_kind_t::tap});
    return chart;
}

auto area_quantize_action(const document_history_t& history, std::uint64_t divisions,
                          quantization_space_t space,
                          quantization_tie_t tie, quantization_hold_t holds)
    -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE;
    action.compose_input_chart_id = history.document.charts.front().id.value;
    action.compose_input_document_revision = history.document.revision;
    action.compose_quantize_divisions = divisions;
    action.compose_quantize_space = static_cast<std::uint8_t>(space);
    action.compose_quantize_tie = static_cast<std::uint8_t>(tie);
    action.compose_quantize_holds = static_cast<std::uint8_t>(holds);
    return action;
}

auto history_with_chart(chart_document_t chart) -> document_history_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.charts.push_back(std::move(chart));
    return make_document_history(std::move(song));
}

void check_moved_note_report(const quantize_preview_t& preview) {
    REQUIRE(preview.report_rows.size() == 1);
    const auto& row = preview.report_rows.front();
    CHECK(row.object == object_id_t{11});
    CHECK(row.start_before.row == 250);
    CHECK(row.start_after.row == 240);
    CHECK(row.moved);
}

void check_collision_report(const quantize_preview_t& preview) {
    REQUIRE(preview.report_rows.size() == 2);
    CHECK(preview.report_rows[0].collision);
    CHECK(preview.report_rows[1].collision);
    CHECK(preview.report_rows[0].collision_peer == preview.report_rows[1].object);
    CHECK(preview.report_rows[1].collision_peer == preview.report_rows[0].object);
    CHECK_FALSE(preview.message.empty());
}

void check_hold_tail_values(const quantize_report_row_t& row) {
    REQUIRE(row.end_before.has_value());
    REQUIRE(row.end_after.has_value());
    CHECK(row.end_before->row == 250);
    CHECK(row.end_after->row == 240);
}

void check_hold_tail_report(const quantize_preview_t& preview) {
    REQUIRE(preview.report_rows.size() == 1);
    const auto& row = preview.report_rows.front();
    CHECK(row.object == object_id_t{10});
    CHECK(row.moved);
    check_hold_tail_values(row);
}

void check_selected_and_unselected_collision_rows(const quantize_preview_t& preview) {
    const auto selected = std::ranges::find(preview.report_rows, object_id_t{11},
                                            &quantize_report_row_t::object);
    const auto peer = std::ranges::find(preview.report_rows, object_id_t{10},
                                        &quantize_report_row_t::object);
    REQUIRE(selected != preview.report_rows.end());
    REQUIRE(peer != preview.report_rows.end());
    CHECK(selected->moved);
    CHECK(selected->collision_peer == object_id_t{10});
    CHECK_FALSE(peer->moved);
    CHECK(peer->collision_peer == object_id_t{11});
}

} // namespace

TEST_CASE("quantize preview counts moved and unchanged notes", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, 4);
    CHECK(preview.selected == 2);
    CHECK(preview.moved == 1);
    CHECK(preview.unchanged == 1);
    CHECK(preview.blocked == 0);
}

TEST_CASE("quantize report identifies a moved note", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    const auto preview = preview_quantize_selection(chart, {object_id_t{11}}, 4);
    check_moved_note_report(preview);
}

TEST_CASE("quantize preview blocks on collisions", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 100});
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, 4);
    CHECK(preview.blocked > 0);
}

TEST_CASE("quantize report pairs colliding notes", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 100});
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, 4);
    CHECK(preview.colliding_objects == 2);
    check_collision_report(preview);
}

TEST_CASE("quantize report groups held-end movement into one note row", "[sys][quantize-preview]") {
    auto chart = chart_with_notes({0});
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 250};
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}}, 4);
    check_hold_tail_report(preview);
}

TEST_CASE("quantize report includes the unselected note colliding with the selection",
          "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 100});
    const auto preview = preview_quantize_selection(chart, {object_id_t{11}}, 4);
    CHECK(preview.blocked > 0);
    CHECK(preview.colliding_objects == 2);
    check_selected_and_unselected_collision_rows(preview);
}

TEST_CASE("quantize preview of an empty selection reports a hint", "[sys][quantize-preview]") {
    const auto preview = preview_quantize_selection(chart_with_notes({0}), {}, 4);
    CHECK(preview.selected == 0);
    CHECK_FALSE(preview.message.empty());
}

TEST_CASE("quantize preview cache recomputes when inputs change", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    quantize_preview_cache_t cache;
    const std::vector<object_id_t> selection{object_id_t{11}};
    CHECK(cache.get(chart, 1, selection, 4).moved == 1);
    CHECK(cache.get(chart, 1, selection, 4).moved == 1);
    CHECK(cache.get(chart, 1, selection, 1920).moved == 0);
}

TEST_CASE("quantize preview cache keys hold scope",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({0});
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 250};
    quantize_preview_cache_t cache;
    const std::vector<object_id_t> selection{object_id_t{10}};
    auto request = quantize_request_t{.divisions = 4};
    const auto complete_holds = cache.get(chart, 1, selection, request);
    REQUIRE(complete_holds.report_rows.size() == 1);
    REQUIRE(complete_holds.report_rows.front().end_after.has_value());
    CHECK(complete_holds.report_rows.front().end_after->row == 240);
    request.holds = quantization_hold_t::start_only;
    const auto start_only = cache.get(chart, 1, selection, request);
    CHECK(start_only.moved == 0);
    CHECK(start_only.report_rows.empty());
}

TEST_CASE("area quantize applies a selected grid and can undo it", "[sys][quantize-preview]") {
    auto chart = chart_with_notes({250});
    auto history = history_with_chart(std::move(chart));
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{10}};
    auto action = area_quantize_action(history, 6, quantization_space_t::standard_measure,
                                       quantization_tie_t::earlier,
                                       quantization_hold_t::both_endpoints);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    CHECK(history.undo.size() == 1);
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 320);
    CHECK(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 250);
}

TEST_CASE("area quantize applies the configured tie policy", "[sys][quantize-preview]") {
    auto history = history_with_chart(chart_with_notes({240}));
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{10}};
    auto action = area_quantize_action(history, 6, quantization_space_t::standard_measure,
                                       quantization_tie_t::later,
                                       quantization_hold_t::both_endpoints);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 320);
}

TEST_CASE("area quantize applies the configured hold scope", "[sys][quantize-preview]") {
    auto chart = chart_with_notes({0});
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 250};
    auto history = history_with_chart(std::move(chart));
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{10}};
    auto action = area_quantize_action(history, 4, quantization_space_t::standard_measure,
                                       quantization_tie_t::earlier,
                                       quantization_hold_t::start_only);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    CHECK(history.document.charts.front().measures.front().notes.front().end->row == 250);
}

TEST_CASE("area quantize can move both hold endpoints", "[sys][quantize-preview]") {
    auto chart = chart_with_notes({250});
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 500};
    auto history = history_with_chart(std::move(chart));
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{10}};
    auto action = area_quantize_action(history, 4, quantization_space_t::standard_measure,
                                       quantization_tie_t::earlier,
                                       quantization_hold_t::both_endpoints);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    const auto& hold = history.document.charts.front().measures.front().notes.front();
    CHECK(hold.position.row == 240);
    REQUIRE(hold.end.has_value());
    CHECK(hold.end->row == 480);
}

TEST_CASE("area quantize collision rejection preserves history", "[sys][quantize-preview]") {
    auto history = history_with_chart(chart_with_notes({0, 100}));
    const auto revision = history.document.revision;
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{11}};
    auto action = area_quantize_action(history, 4, quantization_space_t::standard_measure,
                                       quantization_tie_t::earlier,
                                       quantization_hold_t::both_endpoints);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK(history.document.charts.front().measures.front().notes.back().position.row == 100);
}

TEST_CASE("area quantize rejects selection IDs shared with timing objects",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({250});
    chart.bpms.push_back({.id = object_id_t{10}, .position = {measure_id_t{1}, 0},
                          .beats_per_minute = {120, 1}});
    auto history = history_with_chart(std::move(chart));
    const auto revision = history.document.revision;
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.active_snap_denominator = 4;
    state.selected_notes = {object_id_t{10}};
    auto action = area_quantize_action(history, 6, quantization_space_t::standard_measure,
                                       quantization_tie_t::later,
                                       quantization_hold_t::both_endpoints);
    const auto result = dispatch_compose_area_action(action, history, state);
    REQUIRE(result.has_value());
    CHECK(result->find("identities overlap") != std::string::npos);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 250);
}

TEST_CASE("quantize report caps rows while retaining full counts", "[sys][quantize-preview]") {
    auto chart = chart_with_notes({});
    std::vector<object_id_t> selected;
    for (std::uint64_t row = 0; row < 300; ++row) {
        const object_id_t id{row + 10};
        selected.push_back(id);
        chart.measures.front().notes.push_back({
            .id = id,
            .position = {measure_id_t{1}, row},
            .lane = lane_id_t{1},
            .kind = note_kind_t::tap,
        });
    }
    const auto preview = preview_quantize_selection(chart, selected, 4);
    CHECK(preview.report_rows.size() == quantize_report_row_capacity);
    CHECK(preview.report_total_rows == 300);
    CHECK(preview.report_truncated);
    CHECK(preview.colliding_objects == 300);
}
