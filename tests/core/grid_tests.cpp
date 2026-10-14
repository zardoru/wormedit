#include <wormedit/core/grid.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <source_location>

using namespace wormedit;

namespace {

auto make_chart(std::uint64_t divisions, std::vector<measure_chunk_t> measures)
    -> chart_document_t {
    return {
        .id = chart_id_t{1},
        .divisions_per_standard_measure = divisions,
        .measures = std::move(measures),
    };
}

auto line_at(const editor_grid_result_t& result, std::uint64_t whole_row)
    -> const editor_grid_line_t* {
    const auto found = std::ranges::find_if(result.value.lines, [whole_row](const auto& line) {
        return line.chart_row.whole_rows == whole_row &&
               line.chart_row.fractional_rows.numerator == 0;
    });
    return found == result.value.lines.end() ? nullptr : &*found;
}

void expect_grid(bool condition,
                 std::source_location source = std::source_location::current()) {
    INFO("Grid expectation at " << source.line());
    REQUIRE(condition);
}

} // namespace

TEST_CASE("editor grid lines use exact meter, compound groups, variable bars, and snap overlap",
          "[unit][core][grid]") {
    auto chart = make_chart(960, {
                                     {.id = measure_id_t{1}},
                                     {.id = measure_id_t{2}, .definition = {{3, 4}, meter_t{6, 8}}},
                                 });
    auto request = editor_grid_request_t{
        .visible_begin_row = 0,
        .visible_end_row = 1680,
        .active_snap_denominator = 4,
        .density = {0.0, 0.0, 0.0},
    };

    const auto result = make_editor_grid_lines(chart, request);
    REQUIRE(result.ok());

    const auto* first_group = line_at(result, 480);
    REQUIRE(first_group != nullptr);
    CHECK(first_group->kind == editor_grid_line_kind_t::group);
    CHECK(first_group->emphasis == editor_grid_emphasis_t::strong);
    const auto* second_measure = line_at(result, 960);
    REQUIRE(second_measure != nullptr);
    CHECK(second_measure->kind == editor_grid_line_kind_t::measure);
    CHECK(second_measure->position.measure == measure_id_t{2});

    const auto* compound_beat = line_at(result, 1080);
    REQUIRE(compound_beat != nullptr);
    CHECK(compound_beat->kind == editor_grid_line_kind_t::beat);
    CHECK(compound_beat->position.measure == measure_id_t{2});
    CHECK(compound_beat->position.whole_rows == 120);
    const auto* compound_group = line_at(result, 1320);
    REQUIRE(compound_group != nullptr);
    CHECK(compound_group->kind == editor_grid_line_kind_t::group);
    CHECK(compound_group->position.whole_rows == 360);

    request.authored_groupings = {{.measure = measure_id_t{2}, .beat_counts = {2, 2, 2}}};
    const auto regrouped = make_editor_grid_lines(chart, request);
    REQUIRE(regrouped.ok());
    const auto* authored_group = line_at(regrouped, 1200);
    REQUIRE(authored_group != nullptr);
    CHECK(authored_group->kind == editor_grid_line_kind_t::group);
    CHECK(authored_group->position.whole_rows == 240);
}

TEST_CASE("editor grid preserves fractional beat coordinates below note-row resolution",
          "[unit][core][grid]") {
    auto chart = make_chart(3, {
                                   {.id = measure_id_t{1}, .definition = {{1, 3}, meter_t{2, 6}}},
                               });
    const auto result = make_editor_grid_lines(chart, {.visible_begin_row = 0,
                                                       .visible_end_row = 1,
                                                       .pixels_per_standard_measure = 192.0,
                                                       .density = {0.0, 0.0, 0.0}});
    REQUIRE(result.ok());
    const auto beat = std::ranges::find_if(
        result.value.lines, [](const auto& line) { return line.kind == editor_grid_line_kind_t::beat; });
    REQUIRE(beat != result.value.lines.end());
    CHECK(beat->position.measure == measure_id_t{1});
    CHECK(beat->position.whole_rows == 0);
    CHECK((beat->position.fractional_rows == rational_t{1, 2}));
    CHECK(beat->chart_row.whole_rows == 0);
    CHECK((beat->chart_row.fractional_rows == rational_t{1, 2}));
}

TEST_CASE("grid zoom suppresses dense subdivisions without moving retained lines",
          "[unit][core][grid]") {
    const auto chart = make_chart(960, {{.id = measure_id_t{1}}});
    const auto high_zoom = make_editor_grid_lines(chart, {.visible_begin_row = 0,
                                                          .visible_end_row = 960,
                                                          .active_snap_denominator = 16,
                                                          .pixels_per_standard_measure = 192.0});
    const auto low_zoom = make_editor_grid_lines(chart, {.visible_begin_row = 0,
                                                         .visible_end_row = 960,
                                                         .active_snap_denominator = 16,
                                                         .pixels_per_standard_measure = 24.0});
    REQUIRE(high_zoom.ok());
    REQUIRE(low_zoom.ok());

    CHECK(std::ranges::any_of(high_zoom.value.lines, [](const auto& line) {
        return line.kind == editor_grid_line_kind_t::active_snap;
    }));
    CHECK(std::ranges::none_of(low_zoom.value.lines, [](const auto& line) {
        return line.kind == editor_grid_line_kind_t::active_snap ||
               line.kind == editor_grid_line_kind_t::beat;
    }));
    const auto* high_group = line_at(high_zoom, 480);
    const auto* low_group = line_at(low_zoom, 480);
    REQUIRE(high_group != nullptr);
    REQUIRE(low_group != nullptr);
    CHECK(high_group->kind == low_group->kind);
    CHECK(high_group->position == low_group->position);
    CHECK(high_group->chart_row == low_group->chart_row);
}

TEST_CASE("visible grid intervals are half-open and reject malformed groupings",
          "[unit][core][grid]") {
    const auto chart = make_chart(960, {{.id = measure_id_t{1}}});
    const auto clipped = make_editor_grid_lines(chart, {.visible_begin_row = 100,
                                                        .visible_end_row = 480,
                                                        .pixels_per_standard_measure = 192.0,
                                                        .density = {0.0, 0.0, 0.0}});
    REQUIRE(clipped.ok());
    REQUIRE(clipped.value.lines.size() == 1);
    CHECK(clipped.value.lines.front().chart_row.whole_rows == 240);

    auto metered =
        make_chart(960, {
                            {.id = measure_id_t{2}, .definition = {{3, 4}, meter_t{6, 8}}},
                        });
    const auto invalid_grouping = make_editor_grid_lines(
        metered, {.visible_begin_row = 0,
                  .visible_end_row = 720,
                  .authored_groupings = {{.measure = measure_id_t{2}, .beat_counts = {2, 2}}}});
    CHECK_FALSE(invalid_grouping.ok());
    CHECK(std::ranges::any_of(invalid_grouping.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == diagnostic_code_t::invalid_measure;
    }));
}

TEST_CASE("dense grid thinning retains stable representatives instead of hiding pairs",
          "[unit][core][grid][density]") {
    const auto chart =
        make_chart(960, {
                            {.id = measure_id_t{1}, .definition = {{7, 8}, meter_t{7, 8}}},
                        });
    const auto result = make_editor_grid_lines(
        chart, {.visible_begin_row = 0,
                .visible_end_row = 840,
                .pixels_per_standard_measure = 64.0,
                .density = {9.0, 9.0, 9.0},
                .authored_groupings = {
                    {.measure = measure_id_t{1}, .beat_counts = {1, 1, 1, 1, 1, 1, 1}}}});
    REQUIRE(result.ok());
    const auto groups = std::ranges::count_if(
        result.value.lines, [](const auto& line) { return line.kind == editor_grid_line_kind_t::group; });
    CHECK(groups > 0);
    CHECK(groups < 6);
}

TEST_CASE("BMS grid projects the final virtual measure without materializing it",
          "[unit][core][grid][virtual-canvas]") {
    auto chart = make_chart(960, {{.id = measure_id_t{1}}});
    chart.source_family = format_family_t::bms;
    const auto request = editor_grid_request_t{.visible_begin_row = 959'000,
                                               .visible_end_row = 959'520,
                                               .active_snap_denominator = 4,
                                               .include_virtual_bms_tail = true,
                                               .density = {0.0, 0.0, 0.0}};
    const auto projected = make_editor_grid_lines(chart, request);
    REQUIRE(projected.ok());
    const auto* final_measure = line_at(projected, 959'040);
    REQUIRE(final_measure);
    CHECK(final_measure->kind == editor_grid_line_kind_t::measure);
    CHECK(final_measure->position.measure == measure_id_t{});
    const auto* beat = line_at(projected, 959'280);
    REQUIRE(beat);
    CHECK(beat->kind == editor_grid_line_kind_t::beat);
    CHECK(beat->position.measure == measure_id_t{});
    CHECK(std::ranges::none_of(
        projected.value.lines, [](const auto& line) { return line.chart_row.whole_rows >= 959'520; }));
    CHECK(projected.value.lines.size() < 24);
    CHECK(chart.measures.size() == 1);
    auto authored_only = request;
    authored_only.include_virtual_bms_tail = false;
    CHECK_FALSE(make_editor_grid_lines(chart, authored_only).ok());
}

TEST_CASE("virtual BMS beats retain exact fractions when note rows are coarse",
          "[unit][core][grid][virtual-canvas]") {
    auto chart = make_chart(3, {{.id = measure_id_t{1}}});
    chart.source_family = format_family_t::bms;
    const auto grid = make_editor_grid_lines(chart, {.visible_begin_row = 3,
                                                     .visible_end_row = 6,
                                                     .include_virtual_bms_tail = true,
                                                     .density = {0.0, 0.0, 0.0}});
    REQUIRE(grid.ok());
    const auto beat = std::ranges::find_if(grid.value.lines, [](const auto& line) {
        return line.kind == editor_grid_line_kind_t::beat && line.chart_row.whole_rows == 3 &&
               line.chart_row.fractional_rows == rational_t{3, 4};
    });
    REQUIRE(beat != grid.value.lines.end());
    CHECK(beat->position.measure == measure_id_t{});
    CHECK(beat->position.whole_rows == 0);
    CHECK(beat->position.fractional_rows == rational_t{3, 4});
    CHECK(chart.measures.size() == 1);
}

TEST_CASE("snap subdivisions restart at every short authored measure head",
          "[unit][core][grid][measure-local-grid]") {
    auto chart = make_chart(960, {
        {.id = measure_id_t{1}, .definition = {{1, 16}, std::nullopt}},
        {.id = measure_id_t{2}, .definition = {{1, 8}, std::nullopt}},
        {.id = measure_id_t{3}},
    });
    const auto result = make_editor_grid_lines(chart, {
        .visible_begin_row = 180, .visible_end_row = 1140,
        .active_snap_denominator = 8, .density = {0.0, 0.0, 0.0}});
    expect_grid(result.ok());
    const auto* snap = line_at(result, 300);
    expect_grid(snap);
    expect_grid(snap->kind == editor_grid_line_kind_t::active_snap);
    expect_grid(snap->position.measure == measure_id_t{3});
    expect_grid(snap->position.whole_rows == 120);
    expect_grid(!line_at(result, 240));
    const auto* beat = line_at(result, 420);
    expect_grid(beat);
    expect_grid(beat->kind == editor_grid_line_kind_t::beat);
    const auto* group = line_at(result, 660);
    expect_grid(group);
    expect_grid(group->kind == editor_grid_line_kind_t::group);
    expect_grid(group->emphasis == editor_grid_emphasis_t::strong);
}

TEST_CASE("offset measure grids retain authored grouping and clipped snap phase",
          "[unit][core][grid][measure-local-grid]") {
    auto chart = make_chart(960, {
        {.id = measure_id_t{1}, .definition = {{1, 16}, std::nullopt}},
        {.id = measure_id_t{2}, .definition = {{3, 4}, meter_t{6, 8}}},
    });
    const auto result = make_editor_grid_lines(chart, {
        .visible_begin_row = 121, .visible_end_row = 421,
        .active_snap_denominator = 16, .density = {0.0, 0.0, 0.0},
        .authored_groupings = {{.measure = measure_id_t{2}, .beat_counts = {2, 2, 2}}}});
    expect_grid(result.ok());
    const auto* snap = line_at(result, 240);
    expect_grid(snap);
    expect_grid(snap->position.whole_rows == 180);
    expect_grid(snap->kind == editor_grid_line_kind_t::active_snap);
    const auto* group = line_at(result, 300);
    expect_grid(group);
    expect_grid(group->kind == editor_grid_line_kind_t::group);
    expect_grid(!line_at(result, 120));
}

TEST_CASE("virtual BMS snap phase follows the final authored measure and stays bounded",
          "[unit][core][grid][virtual-canvas][measure-local-grid]") {
    auto chart = make_chart(960, {{.id = measure_id_t{1},
        .definition = {{1, 16}, std::nullopt}}});
    chart.source_family = format_family_t::bms;
    const auto result = make_editor_grid_lines(chart, {
        .visible_begin_row = 958'141, .visible_end_row = 958'501,
        .active_snap_denominator = 8, .include_virtual_bms_tail = true,
        .density = {0.0, 0.0, 0.0}, .maximum_line_count = 32});
    expect_grid(result.ok());
    const auto* snap = line_at(result, 958'260);
    expect_grid(snap);
    expect_grid(snap->position.measure == measure_id_t{});
    expect_grid(snap->position.whole_rows == 120);
    expect_grid(result.value.lines.size() < 16);
    expect_grid(!make_editor_grid_lines(chart, {
        .visible_begin_row = 60, .visible_end_row = 1020,
        .active_snap_denominator = 960, .include_virtual_bms_tail = true,
        .density = {0.0, 0.0, 0.0}, .maximum_line_count = 16}).ok());
}
