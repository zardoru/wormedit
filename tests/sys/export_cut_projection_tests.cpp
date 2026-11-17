// Export cut drags and projection against song cuts.
#include "frame_timing_fixtures.h"
#include "../support/stale_action_assertions.h"
#include <catch2/catch_test_macros.hpp>
#include <source_location>
#include <catch2/catch_approx.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
auto export_cut_drag_song() -> song_document_t {
    auto song = compose_test_song();
    song.export_cuts = {{.id = object_id_t{9001}, .label = "Cut",
        .begin_milliseconds = 501, .end_milliseconds = 1501}};
    return song;
}

void project_cut_timing(compose_test_fixture_t& fixture, bool snapped = true) {
    fixture.application.project.compose_view.compose_timing_focus = true;
    fixture.application.project.compose_view.edit_snap_enabled = snapped;
    project_with_eighth_snap(fixture);
}

auto export_cut_drag_action(compose_test_fixture_t& fixture, std::uint8_t edge,
    std::uint64_t from_row, std::uint64_t to_row) -> wormedit::ui::workspace_action_t {
    return kiai_input(fixture, WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT, 9001, edge,
        kiai_screen_y(fixture, to_row), kiai_screen_y(fixture, from_row));
}

void check_recipe_span_after_cut(const ui::compose_view_t& view) {
    REQUIRE(view.recipe_areas.size() == 1);
    const auto& area = view.recipe_areas.front();
    CHECK(area.recipe_id == recipe_id_t{7001});
    CHECK(area.bounds.top >= 0);
    CHECK(area.bounds.bottom <= view.viewport_height_pixels);
    CHECK(area.graph_count >= 2);
}
} // namespace

TEST_CASE("Recipes and timing graphs remain visible after preceding export cuts",
          "[sys][export-cut][recipe-areas]") {
    const bool downscroll = GENERATE(false, true);
    auto song = dense_song(12, 0);
    song.export_cuts = {{.id = object_id_t{9001}, .label = "Shorten intro",
        .begin_milliseconds = 0, .end_milliseconds = 16000}};
    auto& chart = song.charts.front();
    const local_position_t begin{chart.measures[9].id, 0};
    chart.sv_recipes = {{.id = recipe_id_t{7001}, .name = "After cut",
        .range = {begin, {begin.measure, 480}}, .curve = sv_recipe_curve_t::linear,
        .parameters = {.value_from = {1, 1}, .value_to = {3, 1}}}};
    chart.scrolls = {{.id = object_id_t{7002}, .position = begin, .multiplier = {2, 1}}};
    chart.speeds = {{.id = object_id_t{7003}, .position = begin, .multiplier = {3, 1},
        .transition_beats = {1, 1}}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.preferences.editor_preferences.scroll_directions[
        static_cast<std::size_t>(editor_mode_family_t::other)] = downscroll ? 2 : 1;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    REQUIRE(view.cut_axis);
    check_recipe_span_after_cut(view);
    CHECK(view.sv_graph_maximum == 2);
    CHECK(fixture.flattened.compose.timing_geometry.speed_graph.maximum == 3);
}

namespace {
void check_retained_timing_marker_positions(const ui::compose_view_t& view) {
    expect_compose_test(view.cut_axis != nullptr);
    expect_compose_test(view.static_scene != nullptr);
    const auto& source = *view.static_scene;
    const auto origin = view.downscroll ? -view.area_scroll_pixels - view.viewport_height_pixels
                                       : view.area_scroll_pixels;
    std::size_t checked{};
    for (const auto& marker : view.timing_markers) {
        if (marker.object_id.value < 7001) continue;
        CAPTURE(view.downscroll, view.area_scroll_pixels, marker.lane);
        const auto retained = std::ranges::find(source.timing_markers, marker.object_id,
            &ui::compose_timing_marker_view_t::object_id);
        REQUIRE(retained != source.timing_markers.end());
        CHECK(retained->marker_y_pixels + view.static_scene_world_origin_pixels - origin ==
            Catch::Approx(marker.marker_y_pixels));
        ++checked;
    }
    // Kiai uses merged spans rather than point markers.
    CHECK(checked == timing_lane_kind_count - 1);
}
} // namespace

void check_cut_bookmarks(bool value, std::source_location where = std::source_location::current()) {
    INFO("check at line " << where.line()); CHECK(value);
}
void require_cut_bookmarks(bool value, std::source_location where = std::source_location::current()) {
    INFO("check at line " << where.line()); REQUIRE(value);
}

TEST_CASE("Bookmarks inside an export cut are hidden and the one on its end stays",
          "[sys][export-cut][bookmarks]") {
    // 120 BPM: a measure is two seconds, so 0..16000 ms hides measures 1..8 and ends on measure 9.
    auto song = dense_song(12, 0);
    song.export_cuts = {{.id = object_id_t{9001}, .label = "Shorten intro",
        .begin_milliseconds = 0, .end_milliseconds = 16000}};
    auto& chart = song.charts.front();
    for (const auto measure : {0, 4, 8, 9})
        chart.bookmarks.push_back({.id = object_id_t{9100 + static_cast<std::uint64_t>(measure)},
            .label = "M" + std::to_string(measure + 1),
            .position = local_position_t{chart.measures[static_cast<std::size_t>(measure)].id, 0}});
    chart.bookmarks.push_back({.id = object_id_t{9200}, .label = "Audio inside",
        .anchor = marker_anchor_t::audio_time, .audio_milliseconds = 8000});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    require_cut_bookmarks(static_cast<bool>(view.cut_axis));
    require_cut_bookmarks(view.beat_bookmarks.size() == 4);
    for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
        const auto& bookmark = view.beat_bookmarks[index];
        CAPTURE(index, bookmark.chart_row);
        // The cut's end row is visible; every row inside [begin, end) is not.
        check_cut_bookmarks(bookmark.projection_visible == !view.cut_axis->contains_row(bookmark.chart_row));
    }
    check_cut_bookmarks(!(view.beat_bookmarks[0].projection_visible));
    check_cut_bookmarks(!(view.beat_bookmarks[1].projection_visible));
    check_cut_bookmarks(view.beat_bookmarks[2].projection_visible);
    require_cut_bookmarks(view.audio_bookmarks.size() == 1);
    check_cut_bookmarks(!(view.audio_bookmarks[0].projection_visible));
}

TEST_CASE("Retained timing markers agree with live positions after export cuts while scrolling",
          "[sys][export-cut][timing-geometry][static-source]") {
    const bool downscroll = GENERATE(false, true);
    auto song = dense_song(12, 0);
    song.export_cuts = {{.id = object_id_t{9001}, .label = "Shorten intro",
        .begin_milliseconds = 0, .end_milliseconds = 16000}};
    auto& chart = song.charts.front();
    const local_position_t position{chart.measures[9].id, 240};
    chart.bpms.push_back({.id = object_id_t{7001}, .position = position,
        .beats_per_minute = {120, 1}});
    chart.scrolls = {{.id = object_id_t{7002}, .position = position, .multiplier = {2, 1}}};
    chart.speeds = {{.id = object_id_t{7003}, .position = position, .multiplier = {3, 1}}};
    chart.pauses = {{.id = object_id_t{7004}, .position = position, .duration = {1, 4}},
        {.id = object_id_t{7005}, .position = position, .kind = pause_kind_t::delay, .duration = {1, 4}}};
    chart.warps = {{.id = object_id_t{7006}, .span = {position, {position.measure, 480}}}};
    chart.timing_anchors = {{.id = object_id_t{7008}, .position = position}};
    chart.volumes = {{.id = object_id_t{7009}, .position = position, .value = 75}};
    chart.sample_sets = {{.id = object_id_t{7010}, .position = position, .value = 2}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_view.compose_timing_lane_visibility_mask = all_timing_lane_mask;
    fixture.application.preferences.editor_preferences.scroll_directions[
        static_cast<std::size_t>(editor_mode_family_t::other)] = downscroll ? 2 : 1;
    fixture.project();
    for (const double scroll : {0.0, 48.5, 96.0}) {
        fixture.interaction->viewport_scroll_pixels = scroll;
        fixture.project();
        check_retained_timing_marker_positions(fixture.flattened.view.compose);
    }
}

TEST_CASE("Timing-focus cut begin drags respect snap and retain the untouched millisecond endpoint",
          "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    const auto snapped = GENERATE(false, true);
    project_cut_timing(fixture, snapped);
    const auto original = fixture.history.document.export_cuts;
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_BEGIN, 240, 470);
    expect_compose_test(dispatch_paste_action(fixture, action) == "Export cut edited.");
    const auto& cut = fixture.history.document.export_cuts.front();
    expect_compose_test(cut.begin_milliseconds == (snapped ? 1000 : 979));
    expect_compose_test(cut.end_milliseconds == 1501);
    expect_compose_test(cut.id == object_id_t{9001} && cut.label == "Cut");
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(undo_document_command(fixture.history));
    expect_compose_test(fixture.history.document.export_cuts == original);
}

TEST_CASE("Timing-focus cut end drags preserve an off-grid source-song start", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    const auto snapped = GENERATE(false, true);
    project_cut_timing(fixture, snapped);
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_END, 720, 850);
    expect_compose_test(dispatch_paste_action(fixture, action) == "Export cut edited.");
    expect_compose_test(fixture.history.document.export_cuts.front().begin_milliseconds == 501);
    expect_compose_test(fixture.history.document.export_cuts.front().end_milliseconds == (snapped ? 1750 : 1770));
    expect_compose_test(fixture.history.undo.size() == 1);
}

TEST_CASE("Cut body drags translate both exact endpoints by one source-time delta", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    project_cut_timing(fixture);
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_BODY, 240, 360);
    expect_compose_test(dispatch_paste_action(fixture, action) == "Export cut edited.");
    expect_compose_test(fixture.history.document.export_cuts.front().begin_milliseconds == 751);
    expect_compose_test(fixture.history.document.export_cuts.front().end_milliseconds == 1751);
    expect_compose_test(fixture.history.undo.size() == 1);
}

TEST_CASE("Stale cut edge input rejects before mutating song cuts", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    project_cut_timing(fixture);
    const auto original = fixture.history.document.export_cuts;
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_END, 720, 850);
    ++action.input.compose_inputs.front().timing_document_revision;
    test::check_stale_action_message(dispatch_paste_action(fixture, action), "Export cut edit");
    expect_compose_test(fixture.history.document.export_cuts == original);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Ordinary Compose refuses direct export-cut edge mutation", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    fixture.project();
    const auto original = fixture.history.document.export_cuts;
    auto action = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_EXPORT_CUT_SPAN_EDIT, 9001);
    expect_compose_test(dispatch_paste_action(fixture, action) == "Edit export cut edges in Timing focus.");
    expect_compose_test(fixture.history.document.export_cuts == original);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Cut edge drags reject inverted intervals and unknown handles", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    project_cut_timing(fixture);
    const auto original = fixture.history.document.export_cuts;
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_BEGIN, 240, 850);
    SECTION("begin cannot cross the retained end") {
        expect_compose_test(dispatch_paste_action(fixture, action) ==
            "Keep the export cut start nonnegative and before its end.");
    }
    SECTION("unknown handle") {
        action.input.compose_inputs.front().span_edge = WORM_UI_SPAN_HANDLE_CREATE;
        expect_compose_test(dispatch_paste_action(fixture, action) == "The export cut handle is stale.");
    }
    expect_compose_test(fixture.history.document.export_cuts == original);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Missing export cuts and invalid pointer clocks reject without publication", "[sys][export-cut][cut-drag]") {
    compose_test_fixture_t fixture{export_cut_drag_song()};
    project_cut_timing(fixture);
    const auto original = fixture.history.document.export_cuts;
    auto action = export_cut_drag_action(fixture, WORM_UI_SPAN_HANDLE_END, 720, 850);
    SECTION("missing cut") {
        action.input.compose_inputs.front().object_id = object_id_t{9999};
        expect_compose_test(dispatch_paste_action(fixture, action) == "The export cut or its chart timing is no longer available.");
    }
    SECTION("invalid pointer") {
        action.input.compose_inputs.front().y_pixels = std::numeric_limits<double>::quiet_NaN();
        expect_compose_test(dispatch_paste_action(fixture, action) == "The export cut destination is outside the source-song timeline.");
    }
    expect_compose_test(fixture.history.document.export_cuts == original);
    expect_compose_test(fixture.history.undo.empty());
}
