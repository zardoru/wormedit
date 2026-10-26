#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/editor_scene.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

auto chart_with_two_measures(wormedit::format_family_t family) -> wormedit::chart_document_t {
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{1};
    chart.source_family = family;
    chart.divisions_per_standard_measure = 960;
    chart.measures = {
        {.id = wormedit::measure_id_t{10}, .definition = {{1, 1}, wormedit::meter_t{4, 4}}},
        {.id = wormedit::measure_id_t{11}, .definition = {{1, 2}, wormedit::meter_t{2, 4}}}};
    return chart;
}

} // namespace

TEST_CASE("BMS virtual canvas addresses #000 through #999 without adding measure buckets",
          "[unit][core][virtual-canvas]") {
    const auto chart = chart_with_two_measures(wormedit::format_family_t::bms);
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    const auto& canvas = std::get<wormedit::virtual_canvas_t>(built);
    CHECK(chart.measures.size() == 2);
    CHECK(canvas.authored_index.entries.size() == 2);
    CHECK(canvas.measure_count == 1'000);
    CHECK(canvas.total_rows == 959'520);
    REQUIRE(canvas.measure_at(0));
    CHECK(canvas.measure_at(0)->authored);
    CHECK(canvas.measure_at(0)->begin_row == 0);
    REQUIRE(canvas.measure_at(1));
    CHECK(canvas.measure_at(1)->begin_row == 960);
    CHECK(canvas.measure_at(1)->row_capacity == 480);
    REQUIRE(canvas.measure_at(2));
    CHECK_FALSE(canvas.measure_at(2)->authored);
    CHECK(canvas.measure_at(2)->begin_row == 1'440);
    REQUIRE(canvas.measure_at(999));
    CHECK(canvas.measure_at(999)->begin_row == 958'560);
    CHECK_FALSE(canvas.measure_at(1'000));
    REQUIRE(canvas.measure_containing(1'439));
    CHECK(canvas.measure_containing(1'439)->number == 1);
    REQUIRE(canvas.measure_containing(1'440));
    CHECK(canvas.measure_containing(1'440)->number == 2);
    REQUIRE(canvas.measure_containing(canvas.total_rows - 1));
    CHECK(canvas.measure_containing(canvas.total_rows - 1)->number == 999);
    CHECK_FALSE(canvas.measure_containing(canvas.total_rows));
    CHECK(chart.measures.size() == 2);
}

TEST_CASE("non-BMS virtual canvas stops at authored measure geometry",
          "[unit][core][virtual-canvas]") {
    const auto chart = chart_with_two_measures(wormedit::format_family_t::native_project);
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    const auto& canvas = std::get<wormedit::virtual_canvas_t>(built);
    CHECK(canvas.measure_count == 2);
    CHECK(canvas.total_rows == 1'440);
    CHECK_FALSE(canvas.measure_at(2));
    CHECK_FALSE(canvas.measure_containing(1'440));
}

namespace {
void check_unanchored_canvas(const wormedit::chart_document_t& chart, const wormedit::virtual_canvas_t& canvas) {
    CHECK(canvas.measure_count == wormedit::native_unanchored_virtual_measure_count);
    CHECK(canvas.authored_index.entries.size() == 2);
    CHECK_FALSE(canvas.measure_at(2)->authored);
    CHECK(chart.measures.size() == 2);

}
} // namespace

TEST_CASE("non-BMS chart without timed BGM exposes a virtual tail that BGM removes",
          "[unit][core][virtual-canvas]") {
    wormedit::song_document_t document;
    const auto family = GENERATE(wormedit::format_family_t::native_project,
        wormedit::format_family_t::osu_mania, wormedit::format_family_t::stepmania,
        wormedit::format_family_t::bmson, wormedit::format_family_t::quaver);
    document.charts.push_back(chart_with_two_measures(family));
    wormedit::sync_virtual_tails(document);
    const auto& chart = document.charts.front();
    CHECK(wormedit::has_chart_virtual_tail(chart));
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    const auto& canvas = std::get<wormedit::virtual_canvas_t>(built);
    check_unanchored_canvas(chart, canvas);

    document.audio_assets.push_back({.id = wormedit::asset_id_t{1},
                                     .stream = {.sample_rate = 48'000, .channels = 2,
                                                .frame_count = 48'000 * 60}});
    document.resources.push_back({.id = wormedit::resource_id_t{1}, .asset = wormedit::asset_id_t{1},
                                  .roles = {wormedit::resource_role_t::bgm}});
    wormedit::sync_virtual_tails(document);
    CHECK_FALSE(wormedit::has_chart_virtual_tail(document.charts.front()));
}

TEST_CASE("BGM without a known length does not anchor a native chart",
          "[unit][core][virtual-canvas]") {
    wormedit::song_document_t document;
    document.charts.push_back(chart_with_two_measures(wormedit::format_family_t::native_project));
    document.audio_assets.push_back({.id = wormedit::asset_id_t{1}});
    document.resources.push_back({.id = wormedit::resource_id_t{1}, .asset = wormedit::asset_id_t{1},
                                  .roles = {wormedit::resource_role_t::music}});
    wormedit::sync_virtual_tails(document);
    CHECK(wormedit::has_chart_virtual_tail(document.charts.front()));
}

TEST_CASE("virtual BMS tail rejects row overflow without changing the chart",
          "[unit][core][virtual-canvas]") {
    auto chart = chart_with_two_measures(wormedit::format_family_t::bms);
    chart.measures.resize(1);
    chart.divisions_per_standard_measure = std::numeric_limits<std::uint64_t>::max() / 999 + 1;
    const auto built = wormedit::build_virtual_canvas(chart);
    REQUIRE(std::holds_alternative<wormedit::diagnostic_list_t>(built));
    CHECK_FALSE(std::get<wormedit::diagnostic_list_t>(built).empty());
    CHECK(chart.measures.size() == 1);
}

namespace {

struct virtual_scene_boundary_fixture_t {
    wormedit::chart_document_t chart;
    wormedit::virtual_canvas_t canvas;
    wormedit::editor_field_scene_index_t index;
    wormedit::editor_interaction_view_model_t view;
    wormedit::editor_field_scene_request_t request;
};

auto make_virtual_scene_boundary_fixture() -> virtual_scene_boundary_fixture_t {
    virtual_scene_boundary_fixture_t fixture;
    fixture.chart = chart_with_two_measures(wormedit::format_family_t::bms);
    fixture.chart.lane_layouts = {{
        .id = wormedit::object_id_t{20},
        .position = {wormedit::measure_id_t{10}, 0},
        .layout = {.profile_id = "virtual-tail",
                   .display_name = "Virtual tail",
                   .lanes = {{.id = wormedit::lane_id_t{1},
                              .name = "Key 1",
                              .role = wormedit::lane_role_t::playable,
                              .glyph = wormedit::lane_glyph_t::bar}}}}};
    const auto built = wormedit::build_virtual_canvas(fixture.chart);
    REQUIRE(std::holds_alternative<wormedit::virtual_canvas_t>(built));
    fixture.canvas = std::get<wormedit::virtual_canvas_t>(built);
    const auto indexed = wormedit::build_editor_field_scene_index(fixture.chart, 7);
    REQUIRE(std::holds_alternative<wormedit::editor_field_scene_index_t>(indexed));
    fixture.index = std::get<wormedit::editor_field_scene_index_t>(indexed);
    fixture.view = {.chart = fixture.chart.id,
                    .active_lane = wormedit::lane_id_t{1},
                    .cursor = {wormedit::measure_id_t{10}, 0},
                    .active_snap_denominator = 4,
                    .supported_snap_denominators = {4},
                    .document_revision = 7,
                    .pixels_per_standard_measure = 96.0,
                    .viewport_height_pixels = 480.0};
    const auto content_height = static_cast<double>(fixture.canvas.total_rows) *
                                fixture.view.pixels_per_standard_measure /
                                static_cast<double>(fixture.chart.divisions_per_standard_measure);
    fixture.view.viewport_scroll_pixels = wormedit::editor_maximum_viewport_scroll(
        content_height, fixture.view.viewport_height_pixels);
    fixture.request = {
        .field_left_pixels = 10.0, .field_width_pixels = 200.0, .include_virtual_bms_tail = true};
    return fixture;
}

void require_virtual_scene_boundary_visible(const virtual_scene_boundary_fixture_t& fixture) {
    const auto projected = wormedit::editor_field_scene_view_t::from(fixture.chart, fixture.index,
                                                                     fixture.view, fixture.request);
    REQUIRE(projected);
    const auto last_measure = fixture.canvas.measure_at(999);
    REQUIRE(last_measure);
    CHECK(std::ranges::any_of(projected.scene->grid_lines, [&](const auto& line) {
        return line.kind == wormedit::editor_grid_line_kind_t::measure &&
               line.chart_row.whole_rows == last_measure->begin_row && line.y_pixels >= 0.0 &&
               line.y_pixels <= fixture.view.viewport_height_pixels;
    }));
}

void check_virtual_scene_boundary_rejected(const virtual_scene_boundary_fixture_t& fixture) {
    auto view = fixture.view;
    view.viewport_scroll_pixels = std::nextafter(
        view.viewport_scroll_pixels, std::numeric_limits<double>::infinity());
    const auto rejected = wormedit::editor_field_scene_view_t::from(fixture.chart, fixture.index,
                                                                    view, fixture.request);
    CHECK_FALSE(rejected);
    CHECK(std::ranges::any_of(rejected.diagnostics, [](const auto& diagnostic) {
        return diagnostic.code == wormedit::diagnostic_code_t::row_out_of_range;
    }));
}

void check_virtual_scene_authored_buckets(const virtual_scene_boundary_fixture_t& fixture) {
    CHECK(fixture.chart.measures.size() == 2);
    CHECK(fixture.index.measures.entries.size() == 2);
}

} // namespace

TEST_CASE("virtual BMS scene accepts the controller scroll limit and rejects its next value",
          "[unit][core][virtual-canvas][editor-scene]") {
    const auto fixture = make_virtual_scene_boundary_fixture();
    require_virtual_scene_boundary_visible(fixture);
    check_virtual_scene_authored_buckets(fixture);
    check_virtual_scene_boundary_rejected(fixture);
    check_virtual_scene_authored_buckets(fixture);
}
