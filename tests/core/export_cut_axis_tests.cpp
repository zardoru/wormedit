#include <wormedit/core/export_cut_axis.h>
#include <wormedit/core/export_cut_frames.h>
#include <wormedit/core/document.h>
#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/interaction_queries.h>
#include <memory>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <algorithm>

namespace {
using namespace wormedit;
auto axis_timing() -> timing_index_t {
    const chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{2}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {120, 1}}}};
    auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    return std::move(*timing.value);
}
auto axis_cuts(std::int64_t begin, std::int64_t end) -> export_cut_timeline_t {
    const std::array cuts{export_cut_t{.id = object_id_t{1}, .begin_milliseconds = begin, .end_milliseconds = end}};
    auto result = export_cut_timeline_t::from(cuts);
    REQUIRE(result.ok());
    return std::move(result.value);
}
auto paused_axis_chart() -> chart_document_t {
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{2}, .notes = {{.id = object_id_t{6},
            .position = {measure_id_t{2}, 240}, .lane = lane_id_t{1}}}}},
        .lane_layouts = {{.id = object_id_t{4}, .position = {measure_id_t{2}, 0},
            .layout = {.profile_id = "cut", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {120, 1}}}};
    chart.pauses = {
        {.id = object_id_t{7}, .position = {measure_id_t{2}, 240}, .kind = pause_kind_t::delay,
            .duration = {500, 1}, .source_unit = pause_unit_t::milliseconds},
        {.id = object_id_t{8}, .position = {measure_id_t{2}, 240}, .kind = pause_kind_t::stop,
            .duration = {500, 1}, .source_unit = pause_unit_t::milliseconds}};
    return chart;
}
auto paused_axis(std::int64_t begin, std::int64_t end) -> export_cut_axis_t {
    const auto timing = timing_index_t::from(paused_axis_chart());
    REQUIRE(timing.value);
    const auto axis = export_cut_axis_t::from(*timing.value, axis_cuts(begin, end));
    REQUIRE(axis.ok());
    return axis.value;
}
void check_event_hit(const chart_document_t& chart, const editor_interaction_view_model_t& view,
    const export_cut_axis_t& axis, bool visible, std::uint64_t row) {
    const auto hits = build_editor_hit_test_index(chart, 0);
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(hits));
    const auto hit = hit_test_editor(chart, std::get<editor_hit_test_index_t>(hits), view,
        {.x_pixels = 50, .y_pixels = static_cast<double>(axis.collapsed_row(row)) * 400 / 960,
            .field_width_pixels = 100, .note_hit_radius_pixels = 0, .note_head_height_pixels = 10});
    REQUIRE(hit);
    CHECK(hit->note.has_value() == visible);
}
void check_event_scene(const chart_document_t& chart, const export_cut_axis_t& axis, bool visible, std::uint64_t row) {
    const editor_interaction_view_model_t view{.chart = chart.id, .active_lane = lane_id_t{1},
        .cursor = {measure_id_t{2}, 0}, .active_snap_denominator = 4,
        .supported_snap_denominators = {4}, .pixels_per_standard_measure = 400,
        .viewport_height_pixels = 400, .cut_axis = std::make_shared<export_cut_axis_t>(axis)};
    const auto index = editor_field_scene_index_t::from(chart, 0);
    REQUIRE(index);
    const auto scene = editor_field_scene_view_result_t::from(chart, *index, view,
        {.field_width_pixels = 100});
    REQUIRE(scene.value);
    CHECK(scene.value->note_endpoints.size() == (visible ? 1 : 0));
    check_event_hit(chart, view, axis, visible, row);
}
void check_event_navigation(const chart_document_t& chart, const export_cut_axis_t& axis, std::uint64_t next,
    std::uint64_t start = 0, std::uint64_t denominator = 4) {
    auto history = document_history_t::from({.id = song_id_t{1}, .charts = {chart}});
    const std::array snaps{denominator};
    auto setup = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1}, {measure_id_t{2}, start}, snaps, denominator);
    REQUIRE(setup.state);
    setup.state->cut_axis = std::make_shared<export_cut_axis_t>(axis);
    REQUIRE(dispatch_editor_action(history, *setup.state, move_edit_cursor_action_t{interaction_direction_t::forward}));
    CHECK(setup.state->cursor.row == next);
    REQUIRE(dispatch_editor_action(history, *setup.state, move_edit_cursor_action_t{interaction_direction_t::backward}));
    CHECK(setup.state->cursor.row == start);
}
void check_cut_event_membership(const timing_index_t& timing, const export_cut_timeline_t& timeline,
                                std::uint64_t row, bool hidden) {
    const auto seconds = timing.seconds_at(row);
    REQUIRE(seconds.seconds);
    CHECK(timeline.contains_seconds(*seconds.seconds) == hidden);
}
void check_paused_event_scene(const export_cut_axis_t& axis, bool visible) {
    check_event_scene(paused_axis_chart(), axis, visible, 240);
}
void check_paused_event_navigation(const export_cut_axis_t& axis, std::uint64_t next) {
    check_event_navigation(paused_axis_chart(), axis, next);
}
auto warped_axis_chart() -> chart_document_t {
    auto chart = paused_axis_chart();
    chart.pauses.clear();
    chart.measures.front().notes.front().position.row = 360;
    chart.warps.push_back({.id = object_id_t{9}, .span = {{measure_id_t{2}, 240}, {measure_id_t{2}, 480}}});
    return chart;
}
auto warped_axis(std::int64_t begin, std::int64_t end) -> export_cut_axis_t {
    const auto timing = timing_index_t::from(warped_axis_chart());
    REQUIRE(timing.value);
    const auto axis = export_cut_axis_t::from(*timing.value, axis_cuts(begin, end));
    REQUIRE(axis.ok());
    return axis.value;
}
void check_forward_cursor(const editor_interaction_state_t& state) { CHECK(state.cursor.row == 720); }
void check_kept_rows(const export_cut_axis_t& axis, long double quarter) {
    const auto kept = axis.kept_intervals(0, quarter * 4);
    REQUIRE(kept.size() == 2);
    CHECK(kept[0].end == quarter);
    CHECK(kept[1].begin == quarter * 2);
}
void check_kept_frames(const export_cut_frame_timeline_t& frames) {
    CHECK(frames.source_frame(43) == 43);
    CHECK(frames.kept_frame_count(0, 441) == 397);
    CHECK(frames.source_frame(397) == 441);
}
}
TEST_CASE("Collapsed Compose rows preserve source position and right biased seams", "[core][export-cut][axis]") {
    const auto timing = axis_timing();
    const auto axis = export_cut_axis_t::from(timing, axis_cuts(500, 1000));
    REQUIRE(axis.ok());
    REQUIRE(axis.value.intervals().size() == 1);
    const auto quarter = static_cast<long double>(timing.divisions_per_standard_measure) / 4;
    CHECK(static_cast<double>(axis.value.collapsed_row(quarter * 2)) == Catch::Approx(static_cast<double>(quarter)));
    CHECK(static_cast<double>(axis.value.source_row(quarter)) == Catch::Approx(static_cast<double>(quarter * 2)));
    CHECK(axis.value.contains_row(quarter));
    CHECK_FALSE(axis.value.contains_row(quarter * 2));
    check_kept_rows(axis.value, quarter);
}
TEST_CASE("Sample cut seams round absolute boundaries once at 44.1 kHz", "[core][export-cut][frames]") {
    const auto frames = export_cut_frame_timeline_t::from(axis_cuts(1, 2), 44100, 441);
    REQUIRE(frames.ok());
    REQUIRE(frames.value.intervals().size() == 1);
    CHECK(frames.value.intervals()[0].begin_frame == 44);
    CHECK(frames.value.intervals()[0].end_frame == 88);
    CHECK(frames.value.collapsed_frame(88) == 44);
    CHECK(frames.value.source_frame(44) == 88);
    check_kept_frames(frames.value);
}
TEST_CASE("Sample cuts clip to the physical source endpoint", "[core][export-cut][frames]") {
    const auto frames = export_cut_frame_timeline_t::from(axis_cuts(5, 1000), 48000, 480);
    REQUIRE(frames.ok());
    CHECK(frames.value.removed_frames() == 240);
    CHECK(frames.value.source_frame(240) == 480);
    CHECK(frames.value.kept_frame_count(0, 480) == 240);
    CHECK_FALSE(export_cut_frame_timeline_t::from(axis_cuts(1, 2), 0).ok());
}

TEST_CASE("Cursor navigation skips removed rows on the active musical grid", "[core][export-cut][cursor]") {
    using namespace wormedit;
    chart_document_t chart{.id = chart_id_t{1}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{2}}},
        .lane_layouts = {{.id = object_id_t{4}, .position = {measure_id_t{2}, 0},
            .layout = {.profile_id = "cut", .lanes = {{.id = lane_id_t{1}, .name = "Key"}}}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {120, 1}}}};
    auto history = document_history_t::from({.id = song_id_t{1}, .charts = {chart}});
    const std::array snaps{std::uint64_t{4}};
    auto setup = editor_interaction_state_t::from(history, chart.id, lane_id_t{1}, {measure_id_t{2}, 0}, snaps, 4);
    REQUIRE(setup.state);
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto axis = export_cut_axis_t::from(*timing.value, axis_cuts(500, 1100));
    REQUIRE(axis.ok());
    setup.state->cut_axis = std::make_shared<const export_cut_axis_t>(axis.value);
    REQUIRE(dispatch_editor_action(history, *setup.state, move_edit_cursor_action_t{interaction_direction_t::forward}));
    check_forward_cursor(*setup.state);
    REQUIRE(dispatch_editor_action(history, *setup.state, move_edit_cursor_action_t{interaction_direction_t::backward}));
    CHECK(setup.state->cursor.row == 0);
}

TEST_CASE("Cuts made from rows preserve their exclusive end after millisecond conversion",
          "[core][export-cut][cursor][rounded-cut-boundary]") {
    using namespace wormedit;
    auto chart = paused_axis_chart();
    chart.pauses.clear();
    chart.bpms.front().beats_per_minute = {381024000, 2880079};
    chart.beat_zero_audio_offset_seconds = {5539, 44100};
    chart.measures.front().definition = {.length = {128, 1}};
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const auto cut = export_cut_interval_t::from_rows(*timing.value, 57600, 61440);
    REQUIRE(cut);
    // This earlier boundary rounds upward with nearest-millisecond conversion.
    CHECK(export_cut_interval_t::milliseconds_at(*timing.value, 38400) == 72689);
    const auto timeline = axis_cuts(cut->begin_milliseconds, cut->end_milliseconds);
    check_cut_event_membership(*timing.value, timeline, 57600, true);
    check_cut_event_membership(*timing.value, timeline, 61440, false);
    const auto axis = export_cut_axis_t::from(*timing.value, timeline);
    REQUIRE(axis.ok());
    CHECK_FALSE(axis.value.contains_row(61440));
    check_event_navigation(chart, axis.value, 61440, 57540, 16);
}

TEST_CASE("Overview fractions use the physical seam and preserve its right side", "[core][export-cut][frames]") {
    const auto frames = export_cut_frame_timeline_t::from(axis_cuts(1, 2), 44100, 441);
    REQUIRE(frames.ok());
    CHECK(frames.value.source_frame_at_fraction(44.0 / 396) == 88);
    CHECK(frames.value.collapsed_fraction(88) == Catch::Approx(44.0 / 397));
    CHECK(frames.value.source_frame_at_fraction(1) == 440);
    CHECK_FALSE(frames.value.source_frame_at_fraction(-1));
}

TEST_CASE("Cuts spanning a delay and stop hide the event without removing row width", "[core][export-cut][axis][pause]") {
    const auto axis = paused_axis(750, 1250);
    CHECK(axis.intervals().empty());
    CHECK(axis.contains_row(240));
    CHECK(axis.collapsed_row(240) == 240);
    CHECK(axis.source_row(240) > 240);
    CHECK(axis.source_boundary_row(240) == 240);
    check_paused_event_scene(axis, false);
    check_paused_event_navigation(axis, 480);
}
TEST_CASE("A cut entirely inside a stop preserves the row event", "[core][export-cut][axis][pause]") {
    const auto axis = paused_axis(1100, 1300);
    CHECK(axis.is_empty());
    CHECK_FALSE(axis.contains_row(240));
    CHECK(axis.source_row(240) == 240);
    check_paused_event_scene(axis, true);
    check_paused_event_navigation(axis, 240);
}
TEST_CASE("Cuts beginning inside a stop retain its event at the left seam", "[core][export-cut][axis][pause]") {
    const auto axis = paused_axis(1100, 1750);
    CHECK_FALSE(axis.contains_row(240));
    CHECK(axis.contains_row(300));
    CHECK(axis.source_row_before_seam(240) == 240);
    const auto kept = axis.kept_intervals(0, 960);
    CHECK(std::ranges::any_of(kept, [](const auto& span) { return span.begin == 240 && span.end == 240; }));
    check_paused_event_scene(axis, true);
    check_paused_event_navigation(axis, 240);
}
TEST_CASE("Cuts ending inside a stop hide its event at the right seam", "[core][export-cut][axis][pause]") {
    const auto axis = paused_axis(250, 1250);
    CHECK(axis.contains_row(240));
    CHECK(axis.source_row(axis.collapsed_row(240)) > 240);
    check_paused_event_scene(axis, false);
    check_paused_event_navigation(axis, 480);
}

TEST_CASE("Cuts ending at a warp retain its interior at the excluded source time", "[core][export-cut][axis][warp]") {
    const auto chart = warped_axis_chart();
    const auto axis = warped_axis(250, 500);
    REQUIRE(axis.intervals().size() == 1);
    CHECK(axis.intervals().front().end == 240);
    CHECK_FALSE(axis.contains_row(360));
    check_event_scene(chart, axis, true, 360);
    check_event_navigation(chart, axis, 360, 240, 8);
}
TEST_CASE("Cuts beginning at a warp hide its interior at the included source time", "[core][export-cut][axis][warp]") {
    const auto chart = warped_axis_chart();
    const auto axis = warped_axis(500, 750);
    REQUIRE(axis.intervals().size() == 1);
    CHECK(axis.intervals().front().begin == 240);
    CHECK(axis.contains_row(360));
    check_event_scene(chart, axis, false, 360);
    check_event_navigation(chart, axis, 600, 120, 8);
}
