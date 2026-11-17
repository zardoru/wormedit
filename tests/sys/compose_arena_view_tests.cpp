// BGM, semantic, waveform and marker views over frame arenas, plus pinch dispatch.
#include "frame_timing_fixtures.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <source_location>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
void compare_bgm_arena_views(const wormedit::ui::compose_view_t& a, const wormedit::ui::compose_view_t& b) {
    expect_compose_test(a.bgm_lanes.size() == b.bgm_lanes.size() && a.bgm_events.size() == b.bgm_events.size());
    for (std::size_t i = 0; i < a.bgm_lanes.size(); ++i) {
        compare_geometry_rect(a.bgm_lanes[i].bounds, b.bgm_lanes[i].bounds);
        expect_compose_test(a.bgm_lanes[i].source_lane_id == b.bgm_lanes[i].source_lane_id);
        expect_compose_test(a.bgm_lanes[i].source_order == b.bgm_lanes[i].source_order);
    }
    for (std::size_t i = 0; i < a.bgm_events.size(); ++i) {
        const auto& x = a.bgm_events[i]; const auto& y = b.bgm_events[i];
        compare_geometry_rect(x.bounds, y.bounds);
        expect_compose_test(std::tie(x.event_id,x.source_lane_id,x.sound_id,x.has_sound,x.bmson_slice,
            x.bmson_restart,x.selected) == std::tie(y.event_id,y.source_lane_id,y.sound_id,y.has_sound,
            y.bmson_slice,y.bmson_restart,y.selected));
        expect_compose_test(x.sound_label == y.sound_label);
        expect_compose_test(x.sound_detail == y.sound_detail);
    }
}

void check_bgm_arena_family(format_family_t family) {
        const auto song = bgm_arena_song(family);
        compose_test_fixture_t reused(song), fresh(song);
        wormedit::frame_arena_t a({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
        wormedit::frame_arena_t b({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
        project_geometry_arena(reused); project_geometry_arena(fresh);
        REQUIRE(reused.flattened.view.compose.bgm_lanes.size() > 0);
        REQUIRE(reused.flattened.view.compose.bgm_events.size() > 0);
        compare_bgm_arena_views(reused.flattened.view.compose,fresh.flattened.view.compose);
        const auto& initial = reused.flattened.view.compose.bgm_events[0];
        expect_compose_test(initial.bmson_slice == (family == format_family_t::bmson));
        if (family == format_family_t::bmson) expect_compose_test(initial.bmson_restart);
        expect_compose_test(initial.bounds.bottom > initial.bounds.top);
        const auto storage = reused.cache.scene.static_source.value().storage;
        REQUIRE(storage);
        for (const double scroll : {0.5,24.0,0.0}) {
            a.reset(); b.reset(); fresh.cache={};
            reused.interaction->selected_objects={{editor_selection_object_kind_t::bgm_event,object_id_t{70001}}};
            fresh.interaction->selected_objects=reused.interaction->selected_objects;
            reused.interaction->viewport_scroll_pixels=scroll;
            fresh.interaction->viewport_scroll_pixels=scroll;
            project_geometry_arena(reused); project_geometry_arena(fresh);
            compare_bgm_arena_views(reused.flattened.view.compose,fresh.flattened.view.compose);
            if (reused.flattened.view.compose.bgm_events.size())
                expect_compose_test(reused.flattened.view.compose.bgm_events[0].selected);
            expect_compose_test(reused.flattened.view.compose.bgm_lanes.capacity() >= reused.flattened.view.compose.bgm_lanes.size());
            expect_compose_test(reused.flattened.view.compose.bgm_events.capacity() >= reused.flattened.view.compose.bgm_events.size());
        }
        expect_compose_test(storage->view.compose.bgm_events.size() > 0);
        expect_compose_test(storage->view.compose.bgm_events[0].sound_label == "01");
        expect_compose_test(storage->view.compose.bgm_events[0].sound_detail ==
            "Independent BGM resource label");
}
} // namespace

TEST_CASE("Compose BGM arena rows preserve fresh and retained projection labels and clipping",
          "[sys][compose-bgm-arena]") {
    check_bgm_arena_family(format_family_t::bms);
    check_bgm_arena_family(format_family_t::bmson);
}

TEST_CASE("Compose BGM arena storage remains stable across 1000 reused frames",
          "[sys][compose-bgm-arena]") {
    compose_test_fixture_t fixture(bgm_arena_song(format_family_t::bms));
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    project_geometry_arena(fixture); arena.reset(); project_geometry_arena(fixture);
    const auto used=arena.used_bytes(), committed=arena.committed_bytes();
    bool stable=true;
    for (int i=0;i<1000;++i) {
        arena.reset(); project_geometry_arena(fixture);
        stable=stable && arena.used_bytes()==used && arena.committed_bytes()==committed &&
            fixture.flattened.view.compose.bgm_lanes.size()>0 && fixture.flattened.view.compose.bgm_events.size()>0;
    }
    expect_compose_test(stable);
}

TEST_CASE("Compose owned BGM geometry survives exhausted unrelated frame storage",
          "[sys][compose-bgm-arena]") {
    compose_test_fixture_t fixture(bgm_arena_song(format_family_t::bms));
    wormedit::frame_arena_t arena({.chunk_bytes=64,.maximum_bytes=64});
    project_geometry_arena(fixture);
    const auto& view=fixture.flattened.view.compose;
    expect_compose_test(!view.bgm_lanes.empty() && !view.bgm_events.empty());
}

TEST_CASE("Compose BGM primitive budget rejects oversized source projection",
          "[sys][compose-bgm-arena]") {
    compose_test_fixture_t fixture(bgm_arena_song(format_family_t::bms));
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=16*1024*1024});
    project_geometry_arena(fixture,2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes.empty());
    expect_compose_test(fixture.flattened.view.compose.bgm_events.empty());
}

namespace {
void check_semantic_first(const wormedit::ui::compose_view_t& view) {
    expect_compose_test(view.semantic_layers.size()>0);
    const auto& row=view.semantic_layers[0];
    expect_compose_test(row.layer_id.value==101 && row.name.size()==81);
    expect_compose_test(row.red==99 && row.green==120 && row.blue==140 && row.alpha==255);
    expect_compose_test(row.assigned_note_count==1 && row.visible && row.locked);
}
} // namespace

TEST_CASE("Compose semantic arena preserves palette flags and independent text lifetime",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t fixture(semantic_arena_song());
    fixture.application.project.semantic_layer.semantic_layer_filter_chart=chart_id_t{1};
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{101};
    fixture.application.project.semantic_layer.semantic_layer_solo_active=true;
    fixture.application.project.semantic_layer.required_semantic_layer_filters={layer_id_t{101}};
    fixture.application.project.semantic_layer.excluded_semantic_layer_filters={layer_id_t{102}};
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    project_geometry_arena(fixture);
    const auto& view=fixture.flattened.view.compose;
    REQUIRE(view.scene_available);
    check_semantic_first(view);
    expect_compose_test(view.semantic_layers.size() == 2 && view.semantic_layer_total_count==2);
    expect_compose_test(view.semantic_layers[0].selected && !view.semantic_layers[1].selected);
    expect_compose_test(view.semantic_layer_target_locked && view.semantic_layer_target_visible);
    expect_compose_test(view.semantic_layer_target_required_filter && !view.semantic_layer_target_excluded_filter);
    expect_compose_test(view.semantic_layer_solo_active);
    arena.reset();
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{102};
    fixture.application.project.compose_gesture.semantic_layer_paint_mode_active=true;
    project_geometry_arena(fixture);
    expect_compose_test(fixture.flattened.view.compose.semantic_layer_target_excluded_filter);
    expect_compose_test(!fixture.flattened.view.compose.semantic_layer_target_locked);
    expect_compose_test(fixture.flattened.view.compose.semantic_layer_paint_mode_active);
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{101};
    arena.reset();
    project_geometry_arena(fixture);
    fixture.history.document.charts.front().semantic_layers[0].name="changed source";
    auto moved=std::move(fixture.flattened);

    check_semantic_first(moved.view.compose);
    arena.reset();
    project_geometry_arena(fixture);
    expect_compose_test(std::strcmp(fixture.flattened.view.compose.semantic_layers[0].name.c_str(),"changed source")==0);
}

TEST_CASE("Compose semantic arena preserves bounded scrolling and empty state",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t empty(semantic_arena_song(0)); empty.project();
    expect_compose_test(empty.flattened.view.compose.semantic_layers.size() == 0);
    expect_compose_test(empty.flattened.view.compose.semantic_layer_total_count==0);

    compose_test_fixture_t dense(semantic_arena_song(300));
    dense.application.project.semantic_layer.semantic_layer_row_count=1000;
    dense.application.project.semantic_layer.semantic_layer_target=layer_id_t{101};
    dense.project();
    const auto& view=dense.flattened.view.compose;
    expect_compose_test(view.semantic_layers.size() == wormedit::ui::semantic_layer_window_limit);
    expect_compose_test(view.semantic_layer_total_count==300 && view.semantic_layer_first_row==0);

    dense.application.project.semantic_layer.semantic_layer_first_row=280;
    dense.application.project.semantic_layer.semantic_layer_row_count=18;
    dense.history.document.charts.front().semantic_layers[285].visible=false;
    dense.project();
    expect_compose_test(view.semantic_layer_first_row==280 && view.semantic_layers.size() == 18);
    expect_compose_test(view.semantic_layers[0].layer_id.value==381 && view.semantic_layers[5].layer_id.value==386);
    expect_compose_test(!view.semantic_layers[5].visible);
    expect_compose_test(view.semantic_layer_target.layer_id.value==101 && view.semantic_layer_target.locked);
}

TEST_CASE("Compose note menu projects a bounded independent catalog and retires its capture",
          "[sys][compose-semantic-arena][note-menu]") {
    compose_test_fixture_t fixture(semantic_arena_song(30)); fixture.project();
    auto& app=fixture.application;
    const auto& view=fixture.flattened.view.compose;
    expect_compose_test(view.note_menu_layers.size() == 0 && view.note_menu_layers.empty());
    app.project.semantic_layer.semantic_layer_first_row=20;
    app.project.semantic_layer.note_layer_menu_chart=chart_id_t{1};
    app.project.semantic_layer.note_layer_menu_revision=fixture.history.document.revision;
    app.project.semantic_layer.note_layer_menu_row_count=1000;
    fixture.project();
    expect_compose_test(view.note_menu_layers.size() == wormedit::ui::note_layer_window_limit);
    expect_compose_test(view.note_menu_layer_total_count==30 && view.note_menu_layer_first_row==0);
    expect_compose_test(view.note_menu_layers[0].layer_id.value==101 && view.note_menu_layers[0].locked);
    expect_compose_test(view.semantic_layer_first_row==20 && view.semantic_layers[0].layer_id.value==121);
    app.project.semantic_layer.note_layer_menu_first_row=16;
    fixture.project();
    expect_compose_test(view.note_menu_layer_first_row==16 && view.note_menu_layers.size() == 14);
    expect_compose_test(view.note_menu_layers[13].layer_id.value==130);
    expect_compose_test(view.semantic_layer_first_row==20 && view.semantic_layers[0].layer_id.value==121);
    app.project.semantic_layer.note_layer_menu_row_count=0;
    fixture.project();
    expect_compose_test(view.note_menu_layers.size() == 0 && view.note_menu_layers.empty());
    app.project.semantic_layer.note_layer_menu_row_count=18;
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(view.note_menu_layers.size() == 0 && app.project.semantic_layer.note_layer_menu_row_count==0);
    app.project.semantic_layer.note_layer_menu_revision=fixture.history.document.revision;
    app.project.semantic_layer.note_layer_menu_chart=chart_id_t{2};
    app.project.semantic_layer.note_layer_menu_row_count=18;
    fixture.project();
    expect_compose_test(view.note_menu_layers.size() == 0 && app.project.semantic_layer.note_layer_menu_row_count==0);
}

TEST_CASE("Compose semantic views survive exhausted unrelated frame storage",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t fixture(semantic_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 64, .maximum_bytes = 64});
    project_geometry_arena(fixture);
    expect_compose_test(fixture.flattened.view.compose.semantic_layers.size() == 2);
    check_semantic_first(fixture.flattened.view.compose);
}

TEST_CASE("Compose semantic arena text remains stable across 1000 warmed frames",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t fixture(semantic_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    project_geometry_arena(fixture);arena.reset();project_geometry_arena(fixture);
    const auto used=arena.used_bytes(),committed=arena.committed_bytes();bool stable=true;
    std::uint64_t allocations{};
    for(int i=0;i<1000;++i) {
        arena.reset();
        const auto before=frame_heap_allocation_snapshot();
        project_geometry_arena(fixture);
        allocations=frame_heap_allocation_snapshot().count-before.count;
        stable=stable && arena.used_bytes()==used && arena.committed_bytes()==committed &&
            fixture.flattened.view.compose.semantic_layers.size() == 2;
    }
    INFO("Semantic narrow fixture remaining projection allocations: " << allocations);
    expect_compose_test(stable);
}

namespace {
auto pinch_dispatch_action(compose_test_fixture_t& fixture, double factor,
                           double anchor) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.input.shortcuts.window_focused = true;
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    action.input.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.input.compose_viewport_inputs.resize(1);
    action.input.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
        .cursor_x_pixels = 100, .cursor_y_pixels = anchor, .zoom_factor = factor};
    return action;
}

void start_pinch_test_motion(viewport_input_controller_t& controller, bool autoscroll) {
    if (autoscroll) {
        controller.middle_click(viewport_pane_t::chart, 20, 30);
        controller.update_autoscroll_pointer(20, 130);
    } else {
        controller.begin_momentum(viewport_pane_t::chart, 0, 200);
    }
}
} // namespace

TEST_CASE("Compose pinch dispatch preserves anchor and cancels prior viewport motion",
          "[sys][compose-pinch]") {
    for (const bool autoscroll : {false, true}) {
        compose_test_fixture_t fixture(dense_song(8, 2));
        fixture.project();
        fixture.interaction->viewport_scroll_pixels = 800;
        const auto before = *fixture.interaction;
        const auto revision = fixture.history.document.revision;
        constexpr double anchor = 123;
        auto action = pinch_dispatch_action(fixture, 1.25, anchor);
        viewport_input_controller_t viewport;
        editor_navigation_input_controller_t navigation;
        start_pinch_test_motion(viewport, autoscroll);
        expect_compose_test(viewport.is_momentum_active() || viewport.is_autoscroll_active());
        const auto status = dispatch_compose_input_action(action, fixture.history,
            *fixture.binding.value, *fixture.interaction, viewport, navigation,
            fixture.application, fixture.cache, 0.016);
        INFO(status);
        const auto& after = *fixture.interaction;
        expect_compose_test(std::abs(after.pixels_per_standard_measure -
            before.pixels_per_standard_measure * 1.25) < 1e-9);
        expect_compose_test(std::abs((after.viewport_scroll_pixels + anchor) /
            after.pixels_per_standard_measure - (before.viewport_scroll_pixels + anchor) /
            before.pixels_per_standard_measure) < 1e-9);
        expect_compose_test(after.fixed_cursor_screen_y_pixels == before.fixed_cursor_screen_y_pixels);
        const auto index = measure_index_t::from(fixture.history.document.charts.front()).value();
        const auto cursor_row = static_cast<double>(*index.row_of(after.cursor)) + after.cursor_subrow;
        const auto source_anchor = after.scroll_direction == editor_field_scroll_direction_t::down
            ? after.viewport_height_pixels - *after.fixed_cursor_screen_y_pixels : *after.fixed_cursor_screen_y_pixels;
        expect_compose_test(std::abs(cursor_row * after.pixels_per_standard_measure / 960 -
            after.viewport_scroll_pixels - source_anchor) < 1e-9);
        expect_compose_test(after.selected_objects == before.selected_objects);
        expect_compose_test(after.selected_notes == before.selected_notes);
        expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
        expect_compose_test(!viewport.is_momentum_active() && !viewport.is_autoscroll_active());
        expect_compose_test(viewport.advance(0.016).is_empty());
    }
}

TEST_CASE("Compose pinch rejects invalid ratios and anchors before motion cancellation",
          "[sys][compose-pinch]") {
    const std::array<std::pair<double, double>, 7> inputs{{
        {0, 123}, {-1, 123}, {std::numeric_limits<double>::infinity(), 123},
        {std::numeric_limits<double>::quiet_NaN(), 123}, {1.25, -1},
        {1.25, std::numeric_limits<double>::quiet_NaN()}, {1.25, 1e9}}};
    for (const auto& [factor, anchor] : inputs) {
        for (const bool autoscroll : {false, true}) {
            compose_test_fixture_t fixture(dense_song(8, 2));
            fixture.project();
            fixture.interaction->viewport_scroll_pixels = 800;
            const auto before = *fixture.interaction;
            const auto revision = fixture.history.document.revision;
            auto action = pinch_dispatch_action(fixture, factor, anchor);
            viewport_input_controller_t viewport;
            editor_navigation_input_controller_t navigation;
            start_pinch_test_motion(viewport, autoscroll);
            const auto status = dispatch_compose_input_action(action, fixture.history,
                *fixture.binding.value, *fixture.interaction, viewport, navigation,
                fixture.application, fixture.cache, 0);
            expect_compose_test(status.find("invalid scale or anchor") != std::string::npos);
            expect_compose_test(fixture.interaction->viewport_scroll_pixels == before.viewport_scroll_pixels);
            expect_compose_test(fixture.interaction->pixels_per_standard_measure == before.pixels_per_standard_measure);
            expect_compose_test(fixture.interaction->cursor == before.cursor);
            expect_compose_test(fixture.interaction->selected_objects == before.selected_objects);
            expect_compose_test(fixture.interaction->selected_notes == before.selected_notes);
            expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
            expect_compose_test(viewport.is_autoscroll_active() == autoscroll);
            expect_compose_test(viewport.is_momentum_active() == !autoscroll);
        }
    }
}

namespace {
void expect_waveform_arena(bool value, const std::source_location location = std::source_location::current()) {
    INFO("Waveform arena expectation at " << location.file_name() << ':' << location.line());
    REQUIRE(value);
}

void borrow_waveform_frame(envelope_projection_fixture_t& fixture) {
    auto view = std::move(fixture.flattened.view);
    fixture.flattened = frontend_workspace_view_t{};
    fixture.flattened.view = std::move(view);
}

void check_empty_frame_waveform(const wormedit::ui::compose_view_t& view) {
    expect_waveform_arena(view.waveform_rows.empty());
    expect_waveform_arena(view.waveform_overview_rows.empty());
    expect_waveform_arena(view.waveform_overview_generation == 0);
    expect_waveform_arena(view.waveform_overview_total_frames == 0 && view.waveform_overview_sample_rate == 0);
}
} // namespace

TEST_CASE("Compose waveform arena output preserves persistent source caches across 1000 resets",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    borrow_waveform_frame(fixture);
    expect_waveform_arena(project_waveform_frame(fixture));
    compare_cached_envelope_source(fixture);
    const auto* persistent_overview = fixture.cache.waveform.overview.value().rows.data();
    const auto* persistent_envelope = fixture.cache.waveform.waveform_envelope_ring.value().rows.data();
    const auto generation = fixture.flattened.view.compose.waveform_overview_generation;
    const auto projected = fixture.cache.waveform.waveform_envelope_ring.value().projected_rows;
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    bool stable = true;
    std::size_t minimum_allocations = std::numeric_limits<std::size_t>::max(), maximum_allocations{};
    for (int index=0; index<1000; ++index) {
        arena.reset();
        borrow_waveform_frame(fixture);
        allocations = 0; count_allocations = true;
        const bool available = project_waveform_frame(fixture);
        count_allocations = false;
        minimum_allocations = std::min(minimum_allocations, allocations);
        maximum_allocations = std::max(maximum_allocations, allocations);
        stable = stable && available && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            fixture.flattened.view.compose.waveform_overview_generation == generation;
    }
    INFO("Remaining core timing factor allocations: " << minimum_allocations << ".." << maximum_allocations);
    // Exact baseline0ba3ccb fresh-frame producer allocated12 times; two output
    // allocations are removed. The remaining10 are seconds_at factor vectors,
    // outside this storage unit. A later core optimization may lower this bound.
    expect_waveform_arena(stable);
    expect_waveform_arena(maximum_allocations <= 10);
    expect_waveform_arena(projected > 0);
    expect_waveform_arena(fixture.cache.waveform.overview.value().rows.data() == persistent_overview);
    expect_waveform_arena(static_cast<const void*>(fixture.flattened.view.compose.waveform_overview_rows.data()) !=
        static_cast<const void*>(persistent_overview));
    expect_waveform_arena(fixture.cache.waveform.waveform_envelope_ring.value().rows.data() == persistent_envelope);
    expect_waveform_arena(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    expect_waveform_arena(static_cast<const void*>(fixture.flattened.view.compose.waveform_rows.data()) !=
        static_cast<const void*>(persistent_envelope));
    compare_cached_envelope_source(fixture);
}

TEST_CASE("Compose owned waveform ignores unrelated arena exhaustion and clears unavailable publication",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    frame_arena_t arena({.chunk_bytes=64,.maximum_bytes=64});
    borrow_waveform_frame(fixture);
    expect_waveform_arena(project_waveform_frame(fixture));
    const auto row_capacity = fixture.flattened.view.compose.waveform_rows.capacity();
    const auto overview_capacity = fixture.flattened.view.compose.waveform_overview_rows.capacity();
    expect_waveform_arena(row_capacity > 0 && overview_capacity > 0);
    expect_waveform_arena(!populate_compose_waveform_projection(fixture.flattened, fixture.cache,
        &fixture.analysis, false, false, &fixture.interaction));
    check_empty_frame_waveform(fixture.flattened.view.compose);
    expect_waveform_arena(fixture.flattened.view.compose.waveform_rows.capacity() == row_capacity);
    expect_waveform_arena(fixture.flattened.view.compose.waveform_overview_rows.capacity() == overview_capacity);
}

TEST_CASE("Compose standalone waveform storage survives fixture destruction and move",
          "[sys][waveform-arena]") {
    frontend_workspace_view_t retained;
    double expected_y{}; float expected_peak{};
    std::size_t expected_rows{}, expected_overview{};
    {
        envelope_projection_fixture_t fixture; fixture.project();
        expected_y=fixture.flattened.view.compose.waveform_rows[0].y_pixels;
        expected_peak=fixture.flattened.view.compose.waveform_rows[0].absolute_peak[0];
        expected_rows=fixture.flattened.view.compose.waveform_rows.size();
        expected_overview=fixture.flattened.view.compose.waveform_overview_rows.size();
        retained=fixture.flattened;
    }
    auto moved=std::move(retained);
    expect_waveform_arena(moved.view.compose.waveform_rows.size() == expected_rows && expected_rows>0);
    expect_waveform_arena(moved.view.compose.waveform_overview_rows.size() == expected_overview && expected_overview>0);
    expect_waveform_arena(moved.view.compose.waveform_rows[0].y_pixels == expected_y);
    expect_waveform_arena(moved.view.compose.waveform_rows[0].absolute_peak[0] == expected_peak);
    expect_waveform_arena(retained.view.compose.waveform_rows.empty());
    expect_waveform_arena(retained.view.compose.waveform_overview_rows.empty());
}

TEST_CASE("Compose waveform arena preserves every row at the maximum output capacity",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    fixture.analysis = overview_analysis(.25f);
    fixture.analysis.waveform->key.frames_per_bin = 1024;
    fixture.analysis.waveform->total_frames = fixture.analysis.waveform->bins.size() * 1024;
    for (auto& bin : fixture.analysis.waveform->bins) {
        bin.frame_count = 1024;
        bin.rms = {.125f, .125f};
    }
    fixture.flattened.view.compose.viewport_height_pixels = audio::maximum_aligned_waveform_rows;
    frame_arena_t arena;
    borrow_waveform_frame(fixture); fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto reference = audio::aligned_waveform_view_result_t::from(*fixture.analysis.waveform,
        *fixture.cache.timing.marker_timing_index, {.viewport_scroll_pixels=fixture.interaction.viewport_scroll_pixels,
            .viewport_height_pixels=view.viewport_height_pixels,
            .pixels_per_standard_measure=view.pixels_per_standard_measure,
            .maximum_rows=audio::maximum_aligned_waveform_rows});
    expect_waveform_arena(static_cast<bool>((!reference.status.has_failure())));
    expect_waveform_arena(reference.rows.size() == audio::maximum_aligned_waveform_rows);
    expect_waveform_arena(view.waveform_rows.size() == reference.rows.size());
    for (std::size_t index=0; index<reference.rows.size(); ++index) {
        const auto& row = view.waveform_rows[index];
        const auto& expected = reference.rows[index];
        expect_waveform_arena(row.y_pixels == expected.y_pixels);
        expect_waveform_arena(std::ranges::equal(row.absolute_peak, expected.absolute_peak));
        expect_waveform_arena(std::ranges::equal(row.rms, expected.rms));
        expect_waveform_arena(row.sample_point == expected.sample_point);
        expect_waveform_arena(std::ranges::equal(row.samples, expected.samples));
    }
}

namespace {
void check_marker_frame_spans(const frontend_workspace_view_t& frame) {
    const auto& view = frame.view.compose;
    expect_compose_test(view.beat_bookmarks.size() == view.audio_bookmarks.size());
    expect_compose_test(view.beat_sections.size() == view.audio_sections.size());
}

void check_marker_prefix(const frontend_workspace_view_t& frame, const chart_document_t& chart) {
    check_marker_frame_spans(frame);
    for (std::size_t index = 0; index < frame.view.compose.beat_bookmarks.size(); ++index) {
        const auto& row = frame.view.compose.beat_bookmarks[index]; const auto& source = chart.bookmarks[index * 2];
        expect_compose_test(row.bookmark_id.value == source.id.value && row.measure_number == 1 && row.row == source.position->row);
        expect_compose_test(row.chart_row == source.position->row && std::string_view(row.label) == source.label);
    }
    for (std::size_t index = 0; index < frame.view.compose.audio_bookmarks.size(); ++index) {
        const auto& row = frame.view.compose.audio_bookmarks[index]; const auto& source = chart.bookmarks[index * 2 + 1];
        expect_compose_test(row.bookmark_id.value == source.id.value && row.audio_milliseconds == *source.audio_milliseconds);
        expect_compose_test(std::string_view(row.label) == source.label);
    }
    for (std::size_t index = 0; index < frame.view.compose.beat_sections.size(); ++index) {
        const auto& row = frame.view.compose.beat_sections[index]; const auto& source = chart.sections[index * 2];
        expect_compose_test(row.section_id.value == source.id.value && std::string_view(row.label) == source.label);
        expect_compose_test(row.begin_measure_number == 1 && row.end_measure_number == 1 && row.begin_row == 96 && row.end_row == 480);
        expect_compose_test(row.begin_chart_row == 96 && row.end_chart_row == 480);
    }
    for (std::size_t index = 0; index < frame.view.compose.audio_sections.size(); ++index) {
        const auto& row = frame.view.compose.audio_sections[index]; const auto& source = chart.sections[index * 2 + 1];
        expect_compose_test(row.section_id.value == source.id.value && std::string_view(row.label) == source.label);
        expect_compose_test(row.begin_audio_milliseconds == 200 && row.end_audio_milliseconds == 1600);
    }
}

void check_marker_visible_geometry(const wormedit::ui::compose_view_t& view, bool downscroll, double scroll) {
    const auto y = [&](double up) { return downscroll ? view.viewport_height_pixels - (up - scroll) : up - scroll; };
    expect_compose_test(view.beat_bookmarks[0].projection_visible && view.audio_bookmarks[0].projection_visible);
    expect_compose_test(view.beat_bookmarks[0].projected_y_pixels == Catch::Approx(y(19.2)));
    expect_compose_test(view.audio_bookmarks[0].projected_y_pixels == Catch::Approx(y(48.0)));
    const auto& beat = view.beat_sections[0]; const auto& audio = view.audio_sections[0];
    expect_compose_test(beat.begin_projection_visible && beat.end_projection_visible && beat.span_projection_visible);
    expect_compose_test(audio.begin_projection_visible && audio.end_projection_visible && audio.span_projection_visible);
    expect_compose_test(beat.projected_begin_y_pixels == Catch::Approx(y(19.2)) && beat.projected_end_y_pixels == Catch::Approx(y(96.0)));
    expect_compose_test(audio.projected_begin_y_pixels == Catch::Approx(y(19.2)) && audio.projected_end_y_pixels == Catch::Approx(y(153.6)));
    expect_compose_test(beat.projected_span_top_y_pixels == Catch::Approx(std::min(y(19.2), y(96.0))));
    expect_compose_test(beat.projected_span_bottom_y_pixels == Catch::Approx(std::max(y(19.2), y(96.0))));
    expect_compose_test(!beat.proposed_begin_projection_visible && !beat.proposed_end_projection_visible && !beat.proposed_span_projection_visible);
}
} // namespace

TEST_CASE("Compose marker arenas preserve anchors labels source order and projected geometry", "[sys][frame-arena][compose-markers]") {
    for (const auto direction : {1U, 2U}) {
        compose_test_fixture_t fixture(marker_arena_song());
        fixture.application.preferences.editor_preferences.scroll_directions.fill(direction);
        wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
        project_geometry_arena(fixture);
        REQUIRE(fixture.flattened.view.compose.beat_bookmarks.size() == 2);
        REQUIRE(fixture.flattened.view.compose.audio_bookmarks.size() == 2);
        REQUIRE(fixture.flattened.view.compose.beat_sections.size() == 2);
        REQUIRE(fixture.flattened.view.compose.audio_sections.size() == 2);
        check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
        check_marker_visible_geometry(fixture.flattened.view.compose, direction == 2, fixture.interaction->viewport_scroll_pixels);
    }
}

TEST_CASE("Compose marker arrays and strings belong to the supplied arena through move", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song());
    const auto chart = fixture.history.document.charts.front();
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto begin = arena.allocate(1, 1);
    project_geometry_arena(fixture);
    const auto end = arena.allocate(1, 1);
    REQUIRE((begin && end));
    fixture.history.document.charts.front().bookmarks[0].label.assign(200, 'x');
    fixture.history.document.charts.front().sections[1].label.assign(200, 'y');
    auto moved = std::move(fixture.flattened);

    check_marker_prefix(moved, chart);
    moved = {}; arena.reset();
    ++fixture.history.document.revision;
    project_geometry_arena(fixture);
    check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
}

TEST_CASE("Standalone Compose markers retain owned label text across source destruction", "[sys][frame-arena][compose-markers]") {
    frontend_workspace_view_t owned;
    const auto song = marker_arena_song();
    {
        compose_test_fixture_t fixture(song);
        fixture.project();
        owned = std::move(fixture.flattened);
    }

    check_marker_prefix(owned, song.charts.front());
}

TEST_CASE("Compose marker spans preserve all four caps and empty-array flags", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song(65));
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.beat_bookmarks.size() == 65 && view.audio_bookmarks.size() == 65);
    expect_compose_test(view.beat_sections.size() == 65 && view.audio_sections.size() == 65);

    check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
    compose_test_fixture_t empty(marker_arena_song(0));
    empty.project();
    const auto& none = empty.flattened.view.compose;
    check_marker_frame_spans(empty.flattened);
    expect_compose_test(none.beat_bookmarks.size() == 0 && none.audio_bookmarks.size() == 0 && none.beat_sections.size() == 0 && none.audio_sections.size() == 0);

}

TEST_CASE("Compose marker ownership survives exhausted unrelated frame storage", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song(65));
    wormedit::frame_arena_t arena({.chunk_bytes = 64, .maximum_bytes = 64});
    project_geometry_arena(fixture);
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.beat_bookmarks.size() == 65 && view.audio_bookmarks.size() == 65);
    expect_compose_test(view.beat_sections.size() == 65 && view.audio_sections.size() == 65);
    check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
}

TEST_CASE("Warmed Compose markers keep stable bytes and allocations for 1000 cached frames", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture);
    arena.reset(); project_geometry_arena(fixture);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    const auto source = fixture.cache.scene.static_source.value().storage;
    arena.reset(); allocations = 0; count_allocations = true;
    project_geometry_arena(fixture);
    count_allocations = false;
    const auto remaining = allocations;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset(); allocations = 0; count_allocations = true;
        project_geometry_arena(fixture);
        count_allocations = false;
        stable = stable && allocations == remaining && arena.used_bytes() == used &&
            arena.committed_bytes() == committed && fixture.cache.scene.static_source.value().storage == source &&
            fixture.flattened.view.compose.beat_bookmarks.size() == 2 && fixture.flattened.view.compose.audio_sections.size() == 2;
    }
    INFO("Remaining actual projection allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    expect_compose_test(stable);
    expect_compose_test(fixture.cache.scene.static_projection_reused_frames > 0);
}

namespace {
auto corrupt_marker_anchor(chart_document_t& chart, int kind) -> const char* {
    switch (kind) {
    case 0:
        chart.bookmarks[0].position.reset();
        return "A beat-anchored bookmark has no valid chart position.";
    case 1:
        chart.bookmarks[0].position->row = 960;
        return "A beat-anchored bookmark is outside the chart measure geometry.";
    case 2:
        chart.bookmarks[1].audio_milliseconds.reset();
        return "An audio-time bookmark has inconsistent position data.";
    case 3:
        chart.sections[0].beat_span.reset();
        return "A beat-anchored section has no valid chart span.";
    case 4:
        chart.sections[0].beat_span->end.measure = measure_id_t{999};
        return "A beat-anchored section is outside the chart measure geometry.";
    default:
        chart.sections[1].audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{1600, 200};
        return "An audio-time section has inconsistent span data.";
    }
}

void expect_marker_projection_error(const frontend_workspace_view_t& flattened,
                                    std::string_view expected) {
    expect_compose_test(!flattened.projection_feedback.empty());
    const auto& diagnostic = flattened.projection_feedback.front();
    expect_compose_test(diagnostic.message == expected);
    expect_compose_test(diagnostic.severity == notification_severity_t::error);
    expect_compose_test(diagnostic.source == notification_source_t::document);
}

void check_marker_rejection(int kind, std::size_t limit) {
    compose_test_fixture_t fixture(marker_arena_song());
    const auto expected = corrupt_marker_anchor(fixture.history.document.charts.front(), kind);
    wormedit::frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
    project_geometry_arena(fixture);
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(!view.scene_available);
    expect_marker_projection_error(fixture.flattened, expected);
    expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
}

void check_marker_capacity_validation_order() {
    compose_test_fixture_t fixture(marker_arena_song(65));
    auto& chart = fixture.history.document.charts.front();
    chart.bookmarks[128].position->measure = measure_id_t{999};
    chart.sections[128].beat_span->end.measure = measure_id_t{999};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_marker_projection_error(fixture.flattened, "A beat-anchored bookmark is outside the chart measure geometry.");

    chart.bookmarks[128].position.reset();
    ++fixture.history.document.revision;
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(!view.scene_available);
    expect_marker_projection_error(fixture.flattened,
        "A beat-anchored bookmark has no valid chart position.");
}
} // namespace

TEST_CASE("Compose marker validation precedes arena exhaustion and preserves cap ordering", "[sys][frame-arena][compose-markers]") {
    for (int kind = 0; kind < 6; ++kind) {
        check_marker_rejection(kind, 8);
        check_marker_rejection(kind, 1024 * 1024);
    }
    check_marker_capacity_validation_order();
}

namespace {
void compare_marker_bookmarks(const auto& actual, const auto& expected) {
    expect_compose_test(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto& a = actual[index]; const auto& b = expected[index];
        expect_compose_test(std::tie(a.bookmark_id, a.projection_visible, a.projected_y_pixels) ==
                            std::tie(b.bookmark_id, b.projection_visible, b.projected_y_pixels));
        expect_compose_test(std::string_view(a.label) == std::string_view(b.label));
        if constexpr (requires { a.proposed_projection_visible; })
            expect_compose_test(std::tie(a.proposed_projection_visible, a.proposed_y_pixels) ==
                                std::tie(b.proposed_projection_visible, b.proposed_y_pixels));
    }
}

void compare_marker_sections(const auto& actual, const auto& expected) {
    expect_compose_test(actual.size() == expected.size());
    for (std::size_t index = 0; index < actual.size(); ++index) {
        const auto& a = actual[index]; const auto& b = expected[index];
        expect_compose_test(std::tie(a.section_id, a.begin_projection_visible, a.end_projection_visible,
            a.span_projection_visible, a.projected_begin_y_pixels, a.projected_end_y_pixels,
            a.projected_span_top_y_pixels, a.projected_span_bottom_y_pixels) ==
            std::tie(b.section_id, b.begin_projection_visible, b.end_projection_visible,
            b.span_projection_visible, b.projected_begin_y_pixels, b.projected_end_y_pixels,
            b.projected_span_top_y_pixels, b.projected_span_bottom_y_pixels));
        expect_compose_test(std::string_view(a.label) == std::string_view(b.label));
        if constexpr (requires { a.proposed_span_projection_visible; })
            expect_compose_test(std::tie(a.proposed_begin_projection_visible, a.proposed_end_projection_visible,
                a.proposed_span_projection_visible, a.proposed_begin_y_pixels, a.proposed_end_y_pixels,
                a.proposed_span_top_y_pixels, a.proposed_span_bottom_y_pixels) ==
                std::tie(b.proposed_begin_projection_visible, b.proposed_end_projection_visible,
                b.proposed_span_projection_visible, b.proposed_begin_y_pixels, b.proposed_end_y_pixels,
                b.proposed_span_top_y_pixels, b.proposed_span_bottom_y_pixels));
    }
}

void check_marker_reused_frame(std::uint32_t direction) {
    compose_test_fixture_t reused(marker_arena_song()), fresh(marker_arena_song());
    reused.application.preferences.editor_preferences.scroll_directions.fill(direction);
    fresh.application.preferences.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t arena, reference;
    project_geometry_arena(reused); project_geometry_arena(fresh);
    const auto source = reused.cache.scene.static_source.value().storage;
    REQUIRE(source);
    arena.reset(); reference.reset();
    reused.interaction->viewport_scroll_pixels = 0.5;
    fresh.interaction->viewport_scroll_pixels = 0.5;
    fresh.cache = {};
    project_geometry_arena(reused); project_geometry_arena(fresh);
    expect_compose_test(reused.cache.scene.static_source.value().storage == source && reused.cache.scene.static_projection_reused_frames > 0);
    const auto& a = reused.flattened.view.compose; const auto& b = fresh.flattened.view.compose;
    compare_marker_bookmarks(a.beat_bookmarks, b.beat_bookmarks);
    compare_marker_bookmarks(a.audio_bookmarks, b.audio_bookmarks);
    compare_marker_sections(a.beat_sections, b.beat_sections);
    compare_marker_sections(a.audio_sections, b.audio_sections);
    expect_compose_test(source->view.compose.beat_bookmarks.size() == 0 && source->view.compose.beat_bookmarks.empty());
    expect_compose_test(source->view.compose.audio_sections.size() == 0 && source->view.compose.audio_sections.empty());
}
} // namespace

TEST_CASE("Compose marker arena views agree across fresh and reused scene windows", "[sys][frame-arena][compose-markers]") {
    check_marker_reused_frame(1);
    check_marker_reused_frame(2);
}
