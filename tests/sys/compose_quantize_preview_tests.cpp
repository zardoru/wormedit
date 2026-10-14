#include "sys/compose_quantize_preview.h"
#include "sys/compose_area_actions.h"
#include <wormedit/core/command.h>
#include <wormedit/core/quantize.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <source_location>
#include <string>
#include <limits>
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

auto chart_with_lanes(std::size_t count) -> chart_document_t {
    auto chart = chart_with_notes({});
    auto& lanes = chart.lane_layouts.front().layout.lanes;
    lanes.clear();
    for (std::size_t i = 0; i < count; ++i)
        lanes.push_back({.id = lane_id_t{i + 1}, .name = "Lane"});
    return chart;
}

void check_halfway_row(const quantize_preview_t& preview, std::uint64_t expected,
                       bool head, bool tail) {
    REQUIRE(preview.report_rows.size() == 1);
    const auto& row = preview.report_rows.front();
    CHECK(row.start_after.row == expected);
    CHECK(row.halfway);
    CHECK(row.head_halfway == head);
    CHECK(row.tail_halfway == tail);
}

void check_expectation(bool value,
                       std::source_location source = std::source_location::current());
void require_expectation(bool value,
                         std::source_location source = std::source_location::current());

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
    return document_history_t::from(std::move(song));
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
    require_expectation(row.end_before.has_value());
    require_expectation(row.end_after.has_value());
    require_expectation(row.end_before_absolute_row.has_value());
    require_expectation(row.end_after_absolute_row.has_value());
    check_expectation(row.end_before->row == 250);
    check_expectation(row.end_after->row == 240);
    check_expectation(*row.end_before_absolute_row == 250);
    check_expectation(*row.end_after_absolute_row == 240);
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
    require_expectation(selected != preview.report_rows.end());
    require_expectation(peer != preview.report_rows.end());
    check_expectation(selected->selected);
    check_expectation(selected->moved);
    check_expectation(selected->collision_peer == object_id_t{10});
    check_expectation(!peer->moved);
    check_expectation(!peer->selected);
    check_expectation(peer->collision_peer == object_id_t{11});
}

void check_promoted_cross_measure_row(const quantize_preview_t& preview) {
    check_expectation(preview.drawable);
    check_expectation(preview.source_resolution == 960);
    check_expectation(preview.display_resolution == 6720);
    require_expectation(preview.report_rows.size() == 1);
    const auto& row = preview.report_rows.front();
    check_expectation(row.start_before_absolute_row == 820);
    check_expectation(row.start_after.measure == measure_id_t{2});
    check_expectation(row.start_after.row == 960);
    check_expectation(row.start_after_absolute_row == 6000);
}

void check_expectation(bool value, std::source_location source) {
    INFO("Preview expectation at line " << source.line());
    CHECK(value);
}

void require_expectation(bool value, std::source_location source) {
    INFO("Preview requirement at line " << source.line());
    REQUIRE(value);
}

void check_feedback_count(
    bool value, std::source_location source = std::source_location::current()) {
    check_expectation(value, source);
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

TEST_CASE("quantize preview retains halfway and hold counts without report rows",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({120, 480});
    chart.measures.front().notes[1].end = local_position_t{measure_id_t{1}, 600};
    chart.measures.front().notes[1].kind = note_kind_t::hold;
    quantize_request_t request{.divisions = 4,
                               .tie = quantization_tie_t::reject,
                               .holds = quantization_hold_t::start_only};
    const auto blocked =
        preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, request);
    check_feedback_count(blocked.halfway_notes == 1);
    check_feedback_count(blocked.holds == 1);
    check_feedback_count(blocked.blocked != 0);
    request.holds = quantization_hold_t::both_endpoints;
    const auto tails =
        preview_quantize_selection(chart, {object_id_t{10}, object_id_t{11}}, request);
    check_feedback_count(tails.halfway_notes == 2);
    request.tie = quantization_tie_t::earlier;
    quantize_preview_cache_t cache;
    const auto& cached = cache.get(chart, 1, {object_id_t{11}}, request);
    check_feedback_count(cached.halfway_notes == 1);
    check_feedback_count(cached.holds == 1);
    check_feedback_count(&cache.get(chart, 1, {object_id_t{11}}, request) == &cached);
}

TEST_CASE("quantize preview counts authored and promoted grids exactly",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({60});
    quantize_request_t request{.divisions = 8};
    check_feedback_count(
        preview_quantize_selection(chart, {object_id_t{10}}, request).halfway_notes == 1);
    request.divisions = 16;
    check_feedback_count(
        preview_quantize_selection(chart, {object_id_t{10}}, request).halfway_notes == 0);
    chart.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    chart.measures.front().notes.front().position.row = 45;
    request.divisions = 8;
    request.space = quantization_space_t::authored_measure;
    check_feedback_count(
        preview_quantize_selection(chart, {object_id_t{10}}, request).halfway_notes == 1);
    request.space = quantization_space_t::standard_measure;
    check_feedback_count(
        preview_quantize_selection(chart, {object_id_t{10}}, request).halfway_notes == 0);
    request.divisions = 0;
    check_feedback_count(
        preview_quantize_selection(chart, {object_id_t{10}}, request).halfway_notes == 0);
    request.divisions = 7;
    request.allow_resolution_promotion = true;
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_feedback_count(preview.halfway_notes == 0);
}

TEST_CASE("quantize report identifies a moved note", "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({0, 250});
    const auto preview = preview_quantize_selection(chart, {object_id_t{11}}, 4);
    check_moved_note_report(preview);
}

TEST_CASE("quantize report tracks the selected head halfway endpoint and tie direction",
          "[sys][quantize-preview]") {
    const auto chart = chart_with_notes({120});
    quantize_request_t request{.divisions = 4, .tie = quantization_tie_t::earlier};
    const auto earlier = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(earlier.drawable);
    check_halfway_row(earlier, 0, true, false);
    request.tie = quantization_tie_t::later;
    const auto later = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(later.drawable);
    check_halfway_row(later, 240, true, false);
}

TEST_CASE("quantize report marks snapped and stationary halfway hold tails",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({0});
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 120};
    quantize_request_t request{.divisions = 4, .tie = quantization_tie_t::later,
                               .holds = quantization_hold_t::start_only};
    const auto stationary = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(stationary.drawable);
    check_expectation(stationary.halfway_notes == 0);
    require_expectation(stationary.report_rows.size() == 1);
    check_expectation(stationary.report_rows.front().end_after->row == 120);
    check_expectation(stationary.report_rows.front().tail_halfway);
    check_expectation(stationary.report_rows.front().stationary_tail_annotation);

    request.holds = quantization_hold_t::both_endpoints;
    const auto snapped = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(snapped.drawable);
    check_expectation(snapped.halfway_notes == 1);
    require_expectation(snapped.report_rows.size() == 1);
    check_expectation(snapped.report_rows.front().end_after->row == 240);
    check_expectation(snapped.report_rows.front().tail_halfway);
    check_expectation(!snapped.report_rows.front().stationary_tail_annotation);
}

TEST_CASE("quantize collision rejection retains drawable rows while tie rejection does not",
          "[sys][quantize-preview]") {
    const auto collision_chart = chart_with_notes({0, 100});
    const auto rejected = preview_quantize_selection(
        collision_chart, {object_id_t{10}, object_id_t{11}}, 4);
    check_expectation(rejected.blocked > 0);
    check_expectation(rejected.geometry_valid);
    check_expectation(rejected.drawable);
    check_expectation(rejected.colliding_objects == 2);

    auto allow = quantize_request_t{.divisions = 4,
                                    .collisions = quantization_collision_t::allow};
    const auto allowed = preview_quantize_selection(
        collision_chart, {object_id_t{10}, object_id_t{11}}, allow);
    check_expectation(allowed.blocked == 0);
    check_expectation(allowed.drawable);
    check_expectation(allowed.colliding_objects == 2);

    const auto halfway_chart = chart_with_notes({120, 250});
    const auto blocked = preview_quantize_selection(
        halfway_chart, {object_id_t{10}, object_id_t{11}},
        quantize_request_t{.divisions = 4, .tie = quantization_tie_t::reject});
    check_expectation(blocked.blocked > 0);
    check_expectation(!blocked.geometry_valid);
    check_expectation(!blocked.drawable);

    auto mixed = chart_with_notes({0, 100});
    mixed.measures.front().notes.front().kind = note_kind_t::hold;
    mixed.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 120};
    const auto mixed_block = preview_quantize_selection(
        mixed, {object_id_t{10}, object_id_t{11}},
        quantize_request_t{.divisions = 4, .tie = quantization_tie_t::reject,
                           .holds = quantization_hold_t::both_endpoints});
    check_expectation(mixed_block.halfway_notes == 1);
    check_expectation(mixed_block.blocked > 0);
    check_expectation(!mixed_block.drawable);
}

TEST_CASE("quantize preview hides malformed and layout-boundary collision geometry",
          "[sys][quantize-preview]") {
    auto layout_chart = chart_with_notes({250});
    layout_chart.lane_layouts.front().position.row = 280;
    layout_chart.lane_layouts.push_back({
        .id = object_id_t{2}, .position = {measure_id_t{1}, 300},
        .layout = {.profile_id = "test",
                   .lanes = {{.id = lane_id_t{1}, .name = "Lane 1"}}}});
    const std::vector<object_id_t> boundary_ids{object_id_t{1}, object_id_t{2}};
    const auto core_layout = quantization_plan_t::from(
        layout_chart, quantize_request_t{.divisions = 4, .objects = boundary_ids});
    check_expectation(!core_layout.value.chart.has_value());
    check_expectation(std::ranges::any_of(core_layout.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::collision &&
            diagnostic.object == object_id_t{2};
    }));
    const auto layout = preview_quantize_selection(layout_chart, boundary_ids, 4);
    check_expectation(layout.blocked > 0);
    check_expectation(!layout.drawable);

    auto malformed = chart_with_notes({100});
    malformed.measures.front().notes.front().kind = note_kind_t::hold;
    malformed.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 100};
    const auto collapsed = preview_quantize_selection(malformed, {object_id_t{10}}, 4);
    check_expectation(collapsed.blocked > 0);
    check_expectation(!collapsed.drawable);

    const auto missing = preview_quantize_selection(chart_with_notes({0}), {object_id_t{99}}, 4);
    check_expectation(missing.blocked > 0);
    check_expectation(!missing.drawable);
}

TEST_CASE("quantize promotion reports native before and promoted after coordinates",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({60});
    chart.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    quantize_request_t request{.divisions = 7, .space = quantization_space_t::authored_measure,
                               .tie = quantization_tie_t::later,
                               .allow_resolution_promotion = true};
    const auto preview = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(preview.drawable);
    check_expectation(preview.source_resolution == 960);
    check_expectation(preview.display_resolution == 6720);
    check_expectation(preview.resolution_scale == 7);
    require_expectation(preview.report_rows.size() == 1);
    check_expectation(preview.report_rows.front().start_before.row == 60);
    check_expectation(preview.report_rows.front().start_before_absolute_row == 60);
    check_expectation(preview.report_rows.front().start_after.row == 720);
    check_expectation(preview.report_rows.front().start_after_absolute_row == 720);

    auto held = chart_with_notes({50});
    held.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    held.measures.front().notes.front().kind = note_kind_t::hold;
    held.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 150};
    request.tie = quantization_tie_t::earlier;
    request.holds = quantization_hold_t::both_endpoints;
    const auto promoted_hold = preview_quantize_selection(held, {object_id_t{10}}, request);
    check_expectation(promoted_hold.drawable);
    require_expectation(promoted_hold.report_rows.size() == 1);
    check_expectation(promoted_hold.report_rows.front().end_before->row == 150);
    check_expectation(promoted_hold.report_rows.front().end_before_absolute_row == 150);
    check_expectation(promoted_hold.report_rows.front().end_after->row == 720);
    check_expectation(promoted_hold.report_rows.front().end_after_absolute_row == 720);

    chart.divisions_per_standard_measure = std::numeric_limits<std::uint64_t>::max();
    chart.measures.front().notes.front().position.row = 1;
    request.divisions = 2;
    request.space = quantization_space_t::standard_measure;
    const auto overflow = preview_quantize_selection(chart, {object_id_t{10}}, request);
    check_expectation(overflow.blocked > 0);
    check_expectation(!overflow.drawable);
}

TEST_CASE("quantize promotion scales absolute measure starts for retained rows",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({100});
    chart.measures.front().definition = {{3, 4}, meter_t{3, 4}};
    chart.measures.push_back({.id = measure_id_t{2},
                              .definition = {{1, 1}, meter_t{4, 4}}});
    chart.measures.front().notes.front().position = {measure_id_t{2}, 100};
    quantize_request_t request{.divisions = 7,
                               .space = quantization_space_t::authored_measure,
                               .tie = quantization_tie_t::later,
                               .allow_resolution_promotion = true};
    check_promoted_cross_measure_row(
        preview_quantize_selection(chart, {object_id_t{10}}, request));
}

TEST_CASE("quantize report caps rows after deterministic source-order retention",
          "[sys][quantize-preview]") {
    auto chart = chart_with_lanes(257);
    std::vector<object_id_t> selected;
    for (std::uint64_t i = 0; i < 257; ++i) {
        const auto id = object_id_t{10 + i};
        selected.push_back(id);
        chart.measures.front().notes.push_back({.id = id,
            .position = {measure_id_t{1}, 100}, .lane = lane_id_t{i + 1},
            .kind = note_kind_t::tap});
    }
    const auto preview = preview_quantize_selection(chart, selected, 4);
    check_expectation(preview.drawable);
    check_expectation(preview.report_total_rows == 257);
    check_expectation(preview.report_rows.size() == quantize_report_row_capacity);
    check_expectation(preview.report_truncated);
    check_expectation(preview.report_rows.front().object == object_id_t{10});
    check_expectation(preview.report_rows.back().object == object_id_t{265});
}

TEST_CASE("quantize report keeps all selected collision rows ahead of 256 earlier occupants",
          "[sys][quantize-preview][t437]") {
    auto chart = chart_with_lanes(256);
    std::vector<object_id_t> selected;
    selected.reserve(256);
    for (std::uint64_t index = 0; index < 256; ++index) {
        const auto lane = lane_id_t{index + 1};
        chart.measures.front().notes.push_back({.id = object_id_t{10 + index},
            .position = {measure_id_t{1}, 0}, .lane = lane, .kind = note_kind_t::tap});
    }
    for (std::uint64_t index = 0; index < 256; ++index) {
        const auto lane = lane_id_t{index + 1};
        const auto id = object_id_t{266 + index};
        chart.measures.front().notes.push_back({.id = id,
            .position = {measure_id_t{1}, 120}, .lane = lane, .kind = note_kind_t::tap});
        selected.push_back(id);
    }
    const auto preview = preview_quantize_selection(chart, selected, 4);
    check_expectation(preview.drawable && preview.blocked != 0);
    check_expectation(preview.colliding_objects == 512);
    check_expectation(preview.report_total_rows == 512);
    check_expectation(preview.report_rows.size() == quantize_report_row_capacity);
    check_expectation(preview.report_truncated);
    check_expectation(std::ranges::all_of(preview.report_rows,
        [](const auto& row) { return row.selected; }));
    check_expectation(std::ranges::all_of(selected, [&](const auto id) {
        return std::ranges::find(preview.report_rows, id, &quantize_report_row_t::object) !=
               preview.report_rows.end();
    }));
}

TEST_CASE("quantize report deterministically fills spare rows after selected collision rows",
          "[sys][quantize-preview][t437]") {
    auto chart = chart_with_lanes(255);
    for (std::uint64_t index = 0; index < 255; ++index) {
        const auto lane = lane_id_t{index + 1};
        chart.measures.front().notes.push_back({.id = object_id_t{10 + index},
            .position = {measure_id_t{1}, 0}, .lane = lane, .kind = note_kind_t::tap});
    }
    std::vector<object_id_t> selected;
    selected.reserve(255);
    for (std::uint64_t index = 0; index < 255; ++index) {
        const auto id = object_id_t{265 + index};
        chart.measures.front().notes.push_back({.id = id,
            .position = {measure_id_t{1}, 120}, .lane = lane_id_t{index + 1}, .kind = note_kind_t::tap});
        selected.push_back(id);
    }
    const auto first = preview_quantize_selection(chart, selected, 4);
    const auto second = preview_quantize_selection(chart, selected, 4);
    check_expectation(first.drawable && first.blocked != 0);
    check_expectation(first.colliding_objects == 510);
    check_expectation(first.report_total_rows == 510);
    check_expectation(first.report_rows.size() == quantize_report_row_capacity);
    check_expectation(first.report_truncated);
    check_expectation(std::ranges::count_if(first.report_rows,
        [](const auto& row) { return row.selected; }) == 255);
    check_expectation(std::ranges::count_if(first.report_rows,
        [](const auto& row) { return !row.selected; }) == 1);
    check_expectation(std::ranges::equal(first.report_rows, second.report_rows,
        {}, &quantize_report_row_t::object, &quantize_report_row_t::object));
}

TEST_CASE("quantize stationary tail annotations share the bounded report capacity",
          "[sys][quantize-preview]") {
    auto chart = chart_with_lanes(257);
    std::vector<object_id_t> selected;
    for (std::uint64_t i = 0; i < 257; ++i) {
        const auto id = object_id_t{10 + i};
        selected.push_back(id);
        chart.measures.front().notes.push_back({.id = id,
            .position = {measure_id_t{1}, 0}, .lane = lane_id_t{i + 1},
            .kind = note_kind_t::hold,
            .end = local_position_t{measure_id_t{1}, 120}});
    }
    const auto preview = preview_quantize_selection(
        chart, selected, quantize_request_t{.divisions = 4,
                                            .holds = quantization_hold_t::start_only});
    check_expectation(preview.drawable);
    check_expectation(preview.moved == 0);
    check_expectation(preview.holds == 257);
    check_expectation(preview.halfway_notes == 0);
    check_expectation(preview.report_total_rows == 257);
    check_expectation(preview.report_rows.size() == quantize_report_row_capacity);
    check_expectation(preview.report_truncated);
    check_expectation(preview.report_rows.front().object == object_id_t{10});
    check_expectation(preview.report_rows.back().object == object_id_t{265});
    check_expectation(preview.report_rows.front().stationary_tail_annotation);
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
    require_expectation(complete_holds.report_rows.size() == 1);
    require_expectation(complete_holds.report_rows.front().end_after.has_value());
    check_expectation(complete_holds.report_rows.front().end_after->row == 240);
    request.holds = quantization_hold_t::start_only;
    const auto& start_only = cache.get(chart, 1, selection, request);
    check_expectation(start_only.moved == 0);
    require_expectation(start_only.report_rows.size() == 1);
    check_expectation(start_only.report_rows.front().stationary_tail_annotation);
    const auto& warm = cache.get(chart, 1, selection, request);
    check_expectation(&warm == &start_only);
    check_expectation(warm.report_rows.front().start_after == start_only.report_rows.front().start_after);
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

TEST_CASE("area quantize rejects invalid documents with duplicate timing identities",
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
    CHECK(history.document.revision == revision);
    CHECK(history.undo.empty());
    CHECK(history.document.charts.front().measures.front().notes.front().position.row == 250);
    CHECK(history.document.charts.front().bpms.front().position.row == 0);
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

namespace {
void t477_check(bool value) { CHECK(value); }
void t477_require(bool value) { REQUIRE(value); }

auto fractional_authored_chart() -> chart_document_t {
    auto chart = chart_with_notes({});
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{2}, .name = "Lane 2"});
    chart.measures.front().notes.push_back({
        .id = object_id_t{21}, .position = {measure_id_t{1}, 850},
        .lane = lane_id_t{2}, .kind = note_kind_t::hold,
        .end = local_position_t{measure_id_t{2}, 500}});
    chart.measures.push_back({.id = measure_id_t{2},
        .definition = {{3, 4}, meter_t{3, 4}},
        .notes = {
            {.id = object_id_t{20}, .position = {measure_id_t{2}, 250},
             .lane = lane_id_t{1}, .kind = note_kind_t::tap},
        }});
    return chart;
}

auto capped_report_chart(std::size_t count, std::vector<object_id_t>& selected)
    -> chart_document_t {
    auto chart = chart_with_notes({});
    selected.clear();
    selected.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const auto row = static_cast<std::uint64_t>(i + 1);
        const object_id_t id{static_cast<std::uint64_t>(count + 9 - i)};
        selected.push_back(id);
        chart.measures.front().notes.push_back({.id = id,
            .position = {measure_id_t{1}, row}, .lane = lane_id_t{1}, .kind = note_kind_t::tap});
    }
    return chart;
}

auto report_rows_sorted(const quantize_preview_t& preview) -> bool {
    return std::ranges::is_sorted(preview.report_rows, [](const auto& left, const auto& right) {
        if (left.start_after_absolute_row != right.start_after_absolute_row)
            return left.start_after_absolute_row < right.start_after_absolute_row;
        if (left.lane != right.lane) return left.lane.value < right.lane.value;
        return left.object.value < right.object.value;
    });
}

auto fractional_report_matches_golden() -> bool {
    const auto chart = fractional_authored_chart();
    const auto preview = preview_quantize_selection(chart,
        {object_id_t{20}, object_id_t{21}}, quantize_request_t{
            .divisions = 3, .space = quantization_space_t::authored_measure});
    return preview.selected == 2 && preview.moved == 2 && preview.unchanged == 0 &&
        preview.blocked == 0 && preview.report_rows.size() == 2 &&
        preview.report_rows[0].object == object_id_t{21} &&
        preview.report_rows[0].start_before_absolute_row == 850 &&
        preview.report_rows[0].start_after_absolute_row == 960 &&
        preview.report_rows[0].start_after == local_position_t{measure_id_t{2}, 0} &&
        preview.report_rows[0].end_before == local_position_t{measure_id_t{2}, 500} &&
        preview.report_rows[1].object == object_id_t{20} &&
        preview.report_rows[1].start_before_absolute_row == 1'210 &&
        preview.report_rows[1].start_after_absolute_row == 1'200 &&
        preview.report_rows[1].start_after == local_position_t{measure_id_t{2}, 240} &&
        preview.report_rows[0].end_after == local_position_t{measure_id_t{2}, 480};
}

auto invalid_selection_reports_are_preserved() -> bool {
    const auto chart = chart_with_notes({0, 250});
    const auto duplicate = preview_quantize_selection(chart,
        {object_id_t{11}, object_id_t{11}}, 4);
    const auto missing = preview_quantize_selection(chart, {object_id_t{99}}, 4);
    return duplicate.selected == 2 && duplicate.moved == 0 && duplicate.unchanged == 2 &&
        duplicate.blocked > 0 && duplicate.report_rows.empty() &&
        duplicate.message.find("nonzero and unique") != std::string::npos &&
        missing.selected == 1 && missing.moved == 0 && missing.unchanged == 1 &&
        missing.blocked > 0 && missing.report_rows.empty() &&
        missing.message.find("missing or non-positional") != std::string::npos;
}

auto cache_recomputes_for_equal_identity_replacement() -> bool {
    auto first = chart_with_notes({0, 250});
    auto replacement = chart_with_notes({0, 240});
    quantize_preview_cache_t cache;
    const std::vector<object_id_t> selected{object_id_t{11}};
    const auto moved = cache.get(first, 7, selected, 4);
    const auto unchanged = cache.get(replacement, 7, selected, 4);
    return first.id == replacement.id && moved.moved == 1 &&
        replacement.measures.front().notes.back().position.row == 240 &&
        unchanged.moved == 0 && unchanged.unchanged == 1 && unchanged.report_rows.empty();
}

auto capped_report_boundary_is_source_ordered(std::size_t count) -> bool {
    std::vector<object_id_t> selected;
    const auto chart = capped_report_chart(count, selected);
    const auto preview = preview_quantize_selection(chart, selected, 4);
    if (preview.selected != count || preview.moved != count - 1 || preview.unchanged != 1 ||
        preview.blocked == 0 || preview.colliding_objects != count ||
        preview.report_rows.size() != std::min(count, quantize_report_row_capacity) ||
        preview.report_total_rows != count || !report_rows_sorted(preview)) return false;
    if (count == quantize_report_row_capacity)
        return !preview.report_truncated &&
            std::ranges::find(preview.report_rows, object_id_t{10},
                              &quantize_report_row_t::object) != preview.report_rows.end();
    return preview.report_truncated &&
        std::ranges::find(preview.report_rows, object_id_t{10},
                          &quantize_report_row_t::object) == preview.report_rows.end();
}

auto cache_settings_and_revision_change_results() -> bool {
    auto chart = chart_with_notes({120});
    quantize_preview_cache_t cache;
    const std::vector<object_id_t> selected{object_id_t{10}};
    const auto early = cache.get(chart, 1, selected, quantize_request_t{.divisions = 4});
    auto later_request = quantize_request_t{.divisions = 4, .tie = quantization_tie_t::later};
    const auto later = cache.get(chart, 1, selected, later_request);
    chart.measures.front().notes.front().position.row = 240;
    const auto revised = cache.get(chart, 2, selected, later_request);
    const auto fractional = fractional_authored_chart();
    quantize_preview_cache_t space_cache;
    const auto standard_space = space_cache.get(fractional, 3,
        {object_id_t{20}}, quantize_request_t{.divisions = 4});
    const auto authored_space = space_cache.get(fractional, 3,
        {object_id_t{20}}, quantize_request_t{
            .divisions = 4, .space = quantization_space_t::authored_measure});
    auto collision_chart = chart_with_notes({0, 100});
    quantize_preview_cache_t collision_cache;
    const std::vector<object_id_t> collision_selection{object_id_t{11}};
    const auto rejected = collision_cache.get(collision_chart, 1, collision_selection,
        quantize_request_t{.divisions = 4});
    const auto allowed = collision_cache.get(collision_chart, 1, collision_selection,
        quantize_request_t{.divisions = 4, .collisions = quantization_collision_t::allow});
    return early.moved == 1 && early.report_rows.front().start_after.row == 0 &&
        later.moved == 1 && later.report_rows.front().start_after.row == 240 &&
        revised.moved == 0 && revised.unchanged == 1 && revised.report_rows.empty() &&
        standard_space.report_rows.front().start_after.row == 240 &&
        authored_space.report_rows.front().start_after.row == 180 &&
        rejected.blocked > 0 && allowed.blocked == 0;
}

auto empty_and_unchanged_report_semantics() -> bool {
    const auto empty = preview_quantize_selection(chart_with_notes({0}), {}, 4);
    const auto unchanged = preview_quantize_selection(chart_with_notes({0}), {object_id_t{10}}, 4);
    const auto invalid = preview_quantize_selection(chart_with_notes({0}), {object_id_t{10}},
        quantize_request_t{.divisions = 0});
    return empty.selected == 0 && empty.moved == 0 && empty.unchanged == 0 &&
        empty.blocked == 0 && empty.report_rows.empty() && !empty.message.empty() &&
        unchanged.selected == 1 && unchanged.moved == 0 && unchanged.unchanged == 1 &&
        unchanged.blocked == 0 && unchanged.report_rows.empty() && unchanged.message.empty() &&
        invalid.selected == 1 && invalid.moved == 0 && invalid.unchanged == 1 &&
        invalid.blocked > 0 && invalid.report_rows.empty() && !invalid.message.empty();
}
} // namespace

TEST_CASE("quantize report maps fractional authored measures and hold ends",
          "[sys][quantize-preview]") {
    t477_check(fractional_report_matches_golden());
}

TEST_CASE("quantize preview preserves duplicate and missing selection diagnostics",
          "[sys][quantize-preview]") {
    t477_check(invalid_selection_reports_are_preserved());
}

TEST_CASE("quantize preview cache invalidates for a new chart object with matching IDs",
          "[sys][quantize-preview]") {
    t477_check(cache_recomputes_for_equal_identity_replacement());
}

TEST_CASE("quantize report cap transitions at 256 and retains source-order candidates",
          "[sys][quantize-preview]") {
    t477_require(capped_report_boundary_is_source_ordered(quantize_report_row_capacity));
    t477_check(capped_report_boundary_is_source_ordered(quantize_report_row_capacity + 1));
}

TEST_CASE("quantize preview cache observes settings and revision changes",
          "[sys][quantize-preview]") {
    t477_check(cache_settings_and_revision_change_results());
}

TEST_CASE("quantize preview keeps empty, unchanged, and invalid no-row report semantics",
          "[sys][quantize-preview]") {
    t477_check(empty_and_unchanged_report_semantics());
}

TEST_CASE("quantize no-move report preserves core validation and resolution promotion",
          "[sys][quantize-preview]") {
    auto chart = chart_with_notes({0});
    const auto promoted = preview_quantize_selection(chart, {object_id_t{10}},
        quantize_request_t{.divisions = 7, .allow_resolution_promotion = true});
    t477_check(promoted.selected == 1 && promoted.moved == 0 && promoted.unchanged == 1);
    t477_check(promoted.blocked == 0 && promoted.report_rows.empty());
    chart.measures.front().notes.front().kind = note_kind_t::hold;
    chart.measures.front().notes.front().end = local_position_t{measure_id_t{1}, 0};
    const auto invalid_hold = preview_quantize_selection(chart, {object_id_t{10}}, 4);
    t477_check(invalid_hold.moved == 0 && invalid_hold.unchanged == 1);
    t477_check(invalid_hold.blocked > 0 && invalid_hold.report_rows.empty());
    t477_check(invalid_hold.message.find("reverse or collapse") != std::string::npos);
    t477_check(chart.divisions_per_standard_measure == 960);
}

TEST_CASE("Mixed quantize preview and Apply keep note and BGM tails distinct",
          "[sys][quantize-preview][mixed-quantize]") {
    auto chart = chart_with_notes({250});
    chart.bgm_source_lanes.push_back({.id = object_id_t{30}});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{11},
        .position = {measure_id_t{1}, 130}, .source_lane = object_id_t{30},
        .end = local_position_t{measure_id_t{1}, 610}});
    auto history = history_with_chart(chart);
    editor_interaction_state_t state;
    state.chart = chart.id;
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{10}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{11}}};
    const quantize_request_t request{.divisions = 4, .selection = editor_quantize_selection(state)};
    quantize_preview_cache_t cache;
    const auto& preview = cache.get(chart, 0, {}, request);
    require_expectation(preview.selected == 2 && preview.moved == 2 && preview.holds == 1);
    require_expectation(preview.blocked == 0 && preview.drawable && preview.report_rows.size() == 2);
    check_expectation(std::ranges::count(preview.report_rows, true, &quantize_report_row_t::bgm) == 1);
    const auto action = area_quantize_action(history, 4, quantization_space_t::standard_measure,
        quantization_tie_t::earlier, quantization_hold_t::both_endpoints);
    const auto response = dispatch_compose_area_action(action, history, state);
    require_expectation(response.has_value() && history.undo.size() == 1);
    check_expectation(history.document.charts.front().measures.front().notes.front().position.row == 240);
    check_expectation(history.document.charts.front().measures.front().bgm_events.front().end->row == 720);
    require_expectation(undo_document_command(history));
    check_expectation(history.document.charts.front().measures.front().bgm_events.front().end->row == 610);
    state.selected_objects.erase(state.selected_objects.begin());
    const quantize_request_t bgm_request{.divisions = 4, .selection = editor_quantize_selection(state)};
    const auto& bgm = cache.get(chart, 0, {}, bgm_request);
    check_expectation(bgm.selected == 1 && bgm.moved == 1 && bgm.report_rows.size() == 1);
    check_expectation(bgm.report_rows.front().bgm);
}

TEST_CASE("A BGM collision rejects the whole mixed quantize without changing selection",
          "[sys][quantize-preview][mixed-quantize]") {
    auto chart = chart_with_notes({250});
    chart.bgm_source_lanes.push_back({.id = object_id_t{30}});
    chart.measures.front().bgm_events = {
        {.id = object_id_t{11}, .position = {measure_id_t{1}, 130}, .source_lane = object_id_t{30}},
        {.id = object_id_t{12}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{30}}};
    auto history = history_with_chart(chart);
    editor_interaction_state_t state;
    state.chart = chart.id;
    state.selected_notes = {object_id_t{10}};
    state.selected_objects = {{editor_selection_object_kind_t::note, object_id_t{10}},
        {editor_selection_object_kind_t::bgm_event, object_id_t{11}}};
    const auto selection = state.selected_objects;
    const auto preview = preview_quantize_selection(chart, state.selected_notes,
        {.divisions = 4, .selection = editor_quantize_selection(state)});
    check_expectation(preview.blocked != 0 && preview.colliding_objects == 2);
    const auto action = area_quantize_action(history, 4, quantization_space_t::standard_measure,
        quantization_tie_t::earlier, quantization_hold_t::both_endpoints);
    require_expectation(dispatch_compose_area_action(action, history, state).has_value());
    check_expectation(history.undo.empty() && state.selected_objects == selection);
    check_expectation(history.document.charts.front().measures.front().notes.front().position.row == 250);
    check_expectation(history.document.charts.front().measures.front().bgm_events.front().position.row == 130);
}
