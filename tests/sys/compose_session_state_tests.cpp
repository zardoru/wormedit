// Compose session state: lane modes, snap, range pause, timing geometry and lane-key replacement.
#include "frame_timing_fixtures.h"
#include "sys/project/compare_view.h"
#include "sys/compose/actions/compose_quantize.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("BMSON lane_mode projects current channel ownership in Compose and Sounds",
          "[sys][bmson][lane_mode]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.history.document.resources[1].missing = true;
    fixture.history.document.charts.front().sound_channels[1].events.front().continuation = true;
    t475_project(fixture);
    const auto& compose = fixture.flattened.view.compose;
    const auto lanes = std::span(compose.bgm_lanes);
    const auto lane = std::ranges::find(lanes, object_id_t{601}, &wormedit::ui::compose_bgm_lane_view_t::source_lane_id);
    t475_require(lane != lanes.end());
    t475_check(lane->bmson_channel_id == object_id_t{301} && lane->bmson_channel_continues);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    t475_project(fixture);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds);
    t475_require(set_bmson_channel_lane_mode(fixture.history,
        chart_id_t{1}, object_id_t{302}, bmson_channel_lane_mode_t::stem));
    fixture.binding.value->active_tab = workspace_tab_t::audio_slices;
    wormedit::test::require_workspace_result(rebind_workspace(*fixture.binding.value, fixture.history));
    const auto model = workspace_view_t::from(*fixture.binding.value, fixture.history.document);
    t475_require(model.value.has_value());
    fixture.flattened = frontend_workspace_view_t::from(*model.value, fixture.history.document, "", {}, false);
    const auto& audio = fixture.flattened.view.audio_slices;
    const auto rows = std::span(audio.sound_rows);
    const auto offline = std::ranges::find(rows, wormedit::resource_id_t{101}, &wormedit::ui::sound_row_view_t::resource_id);
    t475_require(offline != rows.end());
    t475_check(offline->bmson_channel_id == wormedit::object_id_t{301} && offline->bmson_channel_continues);
    const auto changed = std::ranges::find(rows, wormedit::resource_id_t{102}, &wormedit::ui::sound_row_view_t::resource_id);
    t475_require(changed != rows.end());
    t475_check(changed->bmson_channel_id == wormedit::object_id_t{302} && changed->kind == wormedit::sound_kind_t::stem);
}

TEST_CASE("Select lane keys cannot start notes at authored or virtual cursor rows",
          "[sys][compose-input][lane-keys][select-tool]") {
    lane_key_callback_fixture_t f;
    f.compose.application.project.compose_gesture.note_paint_mode_active = false;
    SECTION("authored cursor") {}
    SECTION("virtual cursor") {
        for (int step = 0; step < 8; ++step)
            (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
        expect_compose_test(f.compose.interaction->virtual_bms_cursor_row == 1920);
    }
    SECTION("non-note selection") {
        f.compose.interaction->selected_objects = {
            {editor_selection_object_kind_t::bpm_event, object_id_t{2}}};
    }
    const auto revision = f.compose.history.document.revision;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_compose_test(!f.compose.interaction->hold());
    expect_compose_test(f.compose.interaction->additional_keyboard_holds().empty());
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
    expect_compose_test(f.notes().empty());
    expect_compose_test(f.compose.history.document.revision == revision);
    expect_compose_test(f.compose.history.undo.empty());
    expect_compose_test(placed_resource_notifications.empty());
}

TEST_CASE("Compose snap colors use measure-local grid and note endpoint phase",
          "[sys][compose-geometry][measure-local-grid]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures = {
        {.id = measure_id_t{1}, .definition = {{1, 16}, std::nullopt}},
        {.id = measure_id_t{2}, .notes = {
            {.id = object_id_t{30}, .position = {measure_id_t{2}, 240}, .lane = lane_id_t{1}},
            {.id = object_id_t{31}, .position = {measure_id_t{2}, 480}, .lane = lane_id_t{1},
             .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{2}, 720}},
        }},
    };
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto* rendered = view.static_scene ? view.static_scene : &view;
    const auto grid = std::span(rendered->grid_lines);
    const auto beat = std::ranges::find(grid, 300, &wormedit::ui::compose_grid_line_view_t::chart_row);
    REQUIRE(beat != grid.end());
    CHECK(beat->snap_class == 4);
    const auto endpoints = std::span(rendered->note_endpoints);
    REQUIRE(endpoints.size() == 3);
    for (const auto& endpoint : endpoints) CHECK(endpoint.snap_class == 4);
}

TEST_CASE("Quantize ghost preview defaults off and toggles without editing the chart", "[sys][frame-timing][quantize-intent]") {
    auto song = compose_test_song();
    song.charts.front().measures.front().notes.resize(1);
    song.charts.front().measures.front().notes.front().position.row = 130;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    fixture.interaction->selected_notes = {fixture.history.document.charts.front().measures.front().notes.front().id};
    fixture.project();
    const auto revision = fixture.history.document.revision;
    expect_compose_test(!fixture.flattened.view.compose.quantize_preview_enabled);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 0);
    expect_compose_test(toggle_compose_quantize_preview(
        fixture.history, *fixture.interaction, fixture.application));
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_preview_enabled);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 1);
    expect_compose_test(!toggle_compose_quantize_preview(
        fixture.history, *fixture.interaction, fixture.application));
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.quantize_preview_enabled);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 0);
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
    fixture.application.project.quantize.quantize_editor->preview_enabled = true;
    fixture.application.project.quantize.quantize_editor->stamp.chart = chart_id_t{999};
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.quantize_preview_enabled);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 0);
}

TEST_CASE("Default chart snap choices include exact fifth grids while filtering incompatible resolution", "[unit][frame-timing][fitting-snap]") {
    using namespace wormedit;
    const auto common=filter_snap_denominators(960,wormedit::sys::chart_snap_preferences,4);
    REQUIRE(common);
    for (const auto fifth : {5U,10U,20U,40U,80U})
        CHECK(std::ranges::find(common.supported_denominators,fifth)!=common.supported_denominators.end());
    const auto coarse=filter_snap_denominators(192,wormedit::sys::chart_snap_preferences,4);
    REQUIRE(coarse);
    CHECK(std::ranges::find(coarse.supported_denominators,5)==coarse.supported_denominators.end());
}

TEST_CASE("Project replacement preserves the live BMSON width service while clearing chart session state", "[unit][frame-timing][bmson-resize]") {
    wormedit::sys::application_command_state_t state;
    std::uint64_t requested_project{}, requested_chart{}, requested_lane{};
    state.services.compose_bgm_lane_width=[&](auto project,auto chart,auto lane) {
        requested_project=project;requested_chart=chart;requested_lane=lane;return 116.0;
    };
    state.services.compose_left_dock_pixels=[] { return 240; };
    wormedit::sys::runtime::chart_preview_t preview;
    state.bookmarks.preview = &preview;
    const auto now=std::chrono::steady_clock::now();
    state.bookmarks.fixed_clock = &now;
    state.bookmarks.record_seek(wormedit::chart_id_t{7}, 44);
    state.project.semantic_layer.layer_assist_chart=wormedit::chart_id_t{7};
    state.project.semantic_layer.layer_assist_layers={wormedit::layer_id_t{3}};
    wormedit::sys::reset_application_project_edit_state(state);
    expect_compose_test(static_cast<bool>(state.services.compose_bgm_lane_width));
    expect_compose_test(state.services.compose_bgm_lane_width(123,7,44)==116.0);
    expect_compose_test(requested_project==123);
    expect_compose_test(requested_chart==7);
    expect_compose_test(requested_lane==44);
    expect_compose_test(static_cast<bool>(state.services.compose_left_dock_pixels));
    expect_compose_test(state.bookmarks.preview == &preview);
    expect_compose_test(state.bookmarks.fixed_clock == &now);
    expect_compose_test(state.services.compose_left_dock_pixels()==240);
    expect_compose_test(!state.bookmarks.recent_seek_row(wormedit::chart_id_t{7}));
    expect_compose_test(state.bookmarks.now()==now);
    expect_compose_test(!state.project.semantic_layer.layer_assist_chart);
    expect_compose_test(state.project.semantic_layer.layer_assist_layers.empty());
}

TEST_CASE("Workspace frame cache clears when the same document is reopened", "[sys][frame-timing][T558]") {
    wormedit::sys::compare_scene_cache_t cache;
    cache.bind_project_session(7);
    const auto document = compose_test_song();
    const workspace_limits_t limits;
    const workspace_document_cache_query_t key{.document = document, .limits = limits};
    static_cast<void>(cache.binding_validation.get_or_build(key,
        std::bind_front(validate_document, std::cref(document))));
    const workspace_bms_branch_outline_cache_query_t branch_key{
        .document = key, .chart = document.charts.front()};
    static_cast<void>(cache.branch_outline.get_or_build(branch_key,
        [] { return std::make_shared<const workspace_bms_branch_outline_view_t>(); }));
    static_cast<void>(cache.diagnostics.get_or_build(wormedit::document_stamp_t{},
        [](wormedit::diagnostic_list_t& diagnostics) { diagnostics.push_back({}); }));
    cache.bind_project_session(7);
    expect_compose_test(cache.binding_validation.has_value());
    cache.bind_project_session(8);
    expect_compose_test(!cache.binding_validation.has_value());
    expect_compose_test(cache.binding_validation.build_count() == 0);
    expect_compose_test(!cache.branch_outline.has_value() && cache.diagnostics.value().empty());
    expect_compose_test(cache.session_generation == 8);
}

namespace {
auto range_pause_action(compose_test_fixture_t& fixture) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.compose.kind = WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE;
    action.stamp.project_id.value = fixture.history.document.id.value;
    action.stamp.chart_id.value = fixture.interaction->chart.value;
    action.stamp.document_revision = fixture.history.document.revision;
    return action;
}

auto dispatch_range_pause(compose_test_fixture_t& fixture, const wormedit::ui::workspace_action_t& action) -> std::string {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    runtime::output_group_export_t export_runtime{sink};
    return dispatch_compose_view_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, fixture.flattened.view.compose.viewport_height_pixels,
        fixture.application, {}, scheduler, resource_copy, export_runtime, false, sink);
}

auto range_pause_routing(unsigned guard) -> bool {
    compose_test_fixture_t fixture;
    fixture.project();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    fixture.interaction->retained_selection_range = position_span_t{{measure, 240}, {measure, 288}};
    auto action = range_pause_action(fixture);
    switch (guard) {
    case 1: ++action.stamp.project_id.value; break;
    case 2: ++action.stamp.chart_id.value; break;
    case 3: ++action.stamp.document_revision; break;
    case 4: fixture.application.project.timing_recipe.timing_locked = true; break;
    case 5: fixture.interaction->active_gesture = wormedit::proposal_t<wormedit::pending_editor_area_selection_t, wormedit::document_stamp_t>{}; break;
    case 6: fixture.interaction->retained_selection_range.reset(); break;
    case 7: fixture.application.project.clipboard.paste_session = application_command_state_t::paste_session_t{}; break;
    case 8: fixture.application.project.compose_gesture.bga_draw_stroke = application_command_state_t::bga_draw_stroke_proposal_t{}; break;
    case 9: fixture.application.project.compose_gesture.bmson_razor_stroke = application_command_state_t::bmson_razor_stroke_proposal_t{}; break;
    default: break;
    }
    const auto feedback = dispatch_range_pause(fixture, action);
    const auto& chart = fixture.history.document.charts.front();
    if (guard != 0)
        return !feedback.empty() && chart.pauses.empty() && fixture.history.undo.empty();
    return feedback == "Selected rows converted to Stop." && chart.pauses.size() == 1 &&
        fixture.history.undo.size() == 1 && !fixture.interaction->retained_selection_range;
}
} // namespace

TEST_CASE("Range pause conversion routes once and rejects stale or owned interactions", "[sys][range-pause][T560]") {
    for (unsigned guard = 0; guard < 10; ++guard) {
        CAPTURE(guard);
        expect_compose_test(range_pause_routing(guard));
    }
}

TEST_CASE("Timing geometry keeps numeric widths and compact boolean lanes",
          "[sys][horizontal-layout][timing-geometry][T563]") {
    using namespace wormedit::ui;
    wormedit::ui::compose_view_t view{};
    view.viewport_height_pixels = 600;
    view.timing_lane_visibility_mask = 0xff;
    view.playable_field_left_pixels = 426;
    const auto bpm = compose_timing_lane_extent(view, wormedit::timing_lane_kind_t::bpm);
    const auto sv = compose_timing_lane_extent(view, wormedit::timing_lane_kind_t::scroll);
    const auto stop = compose_timing_lane_extent(view, wormedit::timing_lane_kind_t::stop);
    const auto kiai = compose_timing_lane_extent(view, wormedit::timing_lane_kind_t::kiai);
    const auto anchor = compose_timing_lane_extent(view, wormedit::timing_lane_kind_t::anchor);
    REQUIRE(bpm); REQUIRE(sv); REQUIRE(stop); REQUIRE(kiai); REQUIRE(anchor);
    expect_compose_test(bpm->first == 42); expect_compose_test(bpm->second - bpm->first == 64);
    expect_compose_test(sv->first == 106); expect_compose_test(sv->second - sv->first == 64);
    expect_compose_test(stop->second - stop->first == 48);
    expect_compose_test(kiai->second - kiai->first == 24);
    expect_compose_test(anchor->second - anchor->first == 24);
    expect_compose_test(anchor->second == 426);
    expect_compose_test(!(compose_timing_lane_at(view, 426)));
    expect_compose_test(!(compose_timing_lane_at(view, 41)));
    expect_compose_test(!(compose_timing_lane_extent(view,
        static_cast<wormedit::timing_lane_kind_t>(wormedit::timing_lane_kind_count))));
    expect_compose_test(!(compose_timing_lane_extent(view, static_cast<wormedit::timing_lane_kind_t>(-1))));
}

namespace {
void verify_timing_partition(std::uint8_t requested, double right) {
    using namespace wormedit::ui;
    // The view layout fits the mask to the region first; lanes it drops have no extent.
    const auto mask = compose_timing_fit_mask(requested, right - compose_measure_rail_width);
    wormedit::ui::compose_view_t view{};
    view.timing_lane_visibility_mask = mask;
    view.playable_field_left_pixels = right + 48;
    view.bga_lane_visible = true;
    view.bga_lane_bounds.left = right;
    view.viewport_height_pixels = 600;
    double previous = 42;
    for (unsigned ordinal = 0; ordinal < wormedit::timing_lane_kind_count; ++ordinal) {
        const auto lane = static_cast<wormedit::timing_lane_kind_t>(ordinal);
        const auto extent = compose_timing_lane_extent(view, lane);
        if (!(mask & (1U << ordinal))) { expect_compose_test(!(extent)); continue; }
        REQUIRE(extent);
        expect_compose_test(std::abs(extent->first - previous) < 1e-9);
        expect_compose_test(compose_timing_lane_at(view, (extent->first + extent->second) * .5) == lane);
        expect_compose_test(compose_timing_lane_at(view, extent->first) == lane);
        const wormedit::ui::compose_timing_marker_view_t marker{.marker_y_pixels = 200,
            .marker_left_fraction = 0, .marker_right_fraction = 1, .lane = lane};
        const auto bounds = compose_timing_marker_bounds(view, marker);
        expect_compose_test(bounds.left >= extent->first);
        expect_compose_test(bounds.right <= extent->second);
        expect_compose_test(bounds.bottom - bounds.top == 16);
        previous = extent->second;
    }
    if (mask) expect_compose_test(std::abs(previous - right) < 1e-9);
    expect_compose_test(!(compose_timing_lane_at(view, right)));
    expect_compose_test(!(compose_timing_lane_at(view, right + 24)));
}
} // namespace

TEST_CASE("Every timing lane mask partitions normal and compressed geometry without BGA hits",
          "[sys][horizontal-layout][timing-geometry][T563]") {
    for (unsigned mask = 0; mask <= 255; ++mask)
        for (const auto right : {72.0, 234.0, 426.0})
            verify_timing_partition(static_cast<std::uint8_t>(mask), right);
}

TEST_CASE("Timing payloads retain five significant digits in compact numeric labels",
          "[sys][timing-geometry][T563]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.bpms.front().beats_per_minute = {12345, 100};
    chart.scrolls = {{.id = object_id_t{90001}, .position = {measure_id_t{1}, 240},
                      .multiplier = {12345, 10000}}};
    chart.speeds = {{.id = object_id_t{90002}, .position = {measure_id_t{1}, 480},
                     .multiplier = {23456, 10000}}};
    compose_test_fixture_t fixture(song);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    for (const auto& marker : std::span(view.timing_markers)) {
        if (marker.lane == wormedit::timing_lane_kind_t::bpm) expect_compose_test(std::string(marker.short_label) == "123.45");
        if (marker.lane == wormedit::timing_lane_kind_t::scroll) expect_compose_test(std::string(marker.short_label) == "1.2345");
        if (marker.lane == wormedit::timing_lane_kind_t::speed) expect_compose_test(std::string(marker.short_label) == "2.3456");
    }
    expect_compose_test(view.timing_markers.size() == 3);
}

TEST_CASE("Lane-key dispatcher replaces or moves same sound to BGM with correct placement callbacks",
          "[sys][compose][lane-entry]") {
    const auto same = GENERATE(false, true);
    const auto displace = GENERATE(false, true);
    lane_key_callback_fixture_t f;
    f.compose.application.preferences.editor_preferences.move_collision_policy = displace
        ? move_collision_policy_t::displace_to_bgm : move_collision_policy_t::reject;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
    placed_resource_notifications.clear();
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, same ? 7 : 8);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, same ? 7 : 8);
    expect_compose_test(f.notes().size() == (same ? 0 : 1));
    expect_compose_test(placed_resource_notifications == (same ? std::vector<uint64_t>{}
                                                              : std::vector<uint64_t>{8}));
    std::size_t bgm{};
    for (const auto& measure : f.compose.history.document.charts.front().measures) bgm += measure.bgm_events.size();
    expect_compose_test(bgm == (same || displace ? 1 : 0));
    expect_compose_test(f.compose.history.undo.size() == 2);
}

TEST_CASE("Lane-key dispatcher uses revision-bound decoded geometry for linked BMSON entry",
          "[sys][compose][lane-entry]") {
    const bool converted = GENERATE(false, true);
    auto song = lane_key_callback_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    const auto resource = std::ranges::find(song.resources, resource_id_t{7}, &resource_entry_t::id);
    resource->roles = {resource_role_t::sample, resource_role_t::bgm};
    resource->sound_kind = converted ? sound_kind_t::keysound : sound_kind_t::stem;
    const auto asset = resource->asset;
    chart.sound_channels = {{.id = object_id_t{71000}, .source_clip = {clip_id_t{71000}, asset},
        .events = {{.id = object_id_t{73000}, .position = {measure_id_t{1}, 0}, .playable = false}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{74000}, .stem_channel = object_id_t{71000}}};
    chart.measures.front().bgm_events = {{.id = object_id_t{75000}, .position = {measure_id_t{1}, 0},
        .source_lane = object_id_t{74000}, .sound = resource_id_t{7}, .sound_channel_event = object_id_t{73000}}};
    compose_test_fixture_t f{std::move(song)};
    if (converted) {
        const auto result = set_bmson_channel_lane_mode(f.history,
            chart_id_t{1}, object_id_t{71000}, bmson_channel_lane_mode_t::stem);
        expect_compose_test(result.applied);
        expect_compose_test(f.history.document.resources.front().sound_kind == sound_kind_t::stem);
    }
    f.application.project.compose_gesture.note_paint_mode_active = true;
    f.project();
    f.interaction->cursor = {measure_id_t{1}, 480};
    seed_stem_geometry_fixture(f.cache.stems.stem_index, chart_stamp_t::from(f.history.document, f.history.document.charts.front().id),
        {{.asset = asset, .source_sample_rate = 48000,
                                    .source_frame_count = 480000}});
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    auto action = cursor_placement_action(f);
    action.stamp.project_id.value = f.history.document.id.value;
    action.input.compose_inputs.resize(1);
    action.input.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
                               .lane_id = {2}, .resource_id = {7}};
    const auto status = dispatch_compose_input_action(action, f.history, *f.binding.value,
        *f.interaction, viewport, navigation, f.application, f.cache, 0);
    INFO(status);
    expect_compose_test(f.history.undo.size() == (converted ? 2 : 1));
    const auto& changed = f.history.document.charts.front();
    expect_compose_test(changed.sound_channels.front().events.back().playable);
    expect_compose_test(changed.sound_channels.front().events.back().continuation);
    expect_compose_test(changed.measures.front().notes.back().sound_channel_event.has_value());
    expect_compose_test(changed.measures.front().notes.back().sound == resource_id_t{7});
    expect_compose_test(changed.measures.front().notes.back().position.row == 480);
    const auto original_id = changed.measures.front().notes.back().id;
    const auto source_id = changed.measures.front().notes.back().sound_channel_event;
    const auto source_count = changed.sound_channels.front().events.size();
    action.stamp.document_revision = f.history.document.revision;
    action.input.compose_inputs[0].lane_id.value = 1;
    (void)dispatch_compose_input_action(action, f.history, *f.binding.value,
        *f.interaction, viewport, navigation, f.application, f.cache, 0);
    const auto& moved_chart = f.history.document.charts.front();
    expect_compose_test(moved_chart.measures.front().notes.size() == 1);
    expect_compose_test(moved_chart.measures.front().notes.front().id == original_id);
    expect_compose_test(moved_chart.measures.front().notes.front().lane == lane_id_t{1});
    expect_compose_test(moved_chart.measures.front().notes.front().sound_channel_event == source_id);
    expect_compose_test(moved_chart.sound_channels.front().events.size() == source_count);
    expect_compose_test(moved_chart.sound_channels.front().events.back().continuation);
    expect_compose_test(undo_document_command(f.history));
    expect_compose_test(f.history.document.charts.front().measures.front().notes.front().lane == lane_id_t{2});
    expect_compose_test(undo_document_command(f.history));
    expect_compose_test(f.history.document.charts.front().sound_channels.front().events.size() == 1);
}

TEST_CASE("Song bar defaults hide BMSON and BMS while keeping manual overrides",
          "[sys][compose][project-access]") {
    const auto family = GENERATE(format_family_t::bms, format_family_t::bmson,
                                  format_family_t::stepmania, format_family_t::osu_mania);
    auto song = compose_test_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    fixture.cache.frame.bgm_bar_audio_seen = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.bgm_bar_available);
    const bool hidden = family == format_family_t::bms || family == format_family_t::bmson;
    expect_compose_test(fixture.flattened.view.compose.bgm_bar_reserved != hidden);
    fixture.application.project.bgm.bgm_bar_hidden = false;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.bgm_bar_reserved);
    fixture.application.project.bgm.bgm_bar_hidden = true;
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.bgm_bar_reserved);
}

TEST_CASE("Compose classifies stem note heads after geometry projection and cache reuse",
          "[sys][compose][bmson][scratchpad-stem-projection]") {
    auto song = t463_shared_asset_song();
    auto note = compose_test_song().charts.front().measures.front().notes.front();
    note.sound = resource_id_t{100};
    song.charts.front().measures.front().notes.push_back(note);
    compose_test_fixture_t fixture(std::move(song));
    for (int pass = 0; pass < 2; ++pass) {
        t475_project(fixture);
        const auto& view = fixture.flattened.view.compose;
        t475_require(view.note_endpoints.size() > 0);
        t475_check(view.note_endpoints[0].sound_resource_id.value == 100);
        t475_check(view.note_endpoints[0].sound_is_stem);
        t475_check(!view.note_endpoints[0].sound_missing);
    }
}

TEST_CASE("Existing 16K charts offer distinctly named SpecialStyle layouts and projected scratch classes",
          "[sys][compose][scratchpad-specialstyle]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::osu_mania;
    chart.lane_layouts.front().layout = make_osu_mania_profile(16)->layout;
    chart.input_profile = make_osu_mania_profile(16)->input;
    compose_test_fixture_t fixture(std::move(song));
    fixture.project();
    auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.lane_profile_names.size() == 2);
    expect_compose_test(std::string_view(view.lane_profile_names[1]) == "osu!mania 16K (SpecialStyle)");
    const auto& active = fixture.history.document.charts.front().lane_layouts.front();
    expect_compose_test(
        static_cast<bool>(apply_lane_profile_switch(fixture.history, chart_id_t{1}, active.position,
                                                    make_osu_mania_profile(16, true)->layout)));
    fixture.application.project.compose_view.compose_editor_right_pixels = 820;
    fixture.application.scratch_on_right = {false, false};
    fixture.project();
    const auto lanes = std::span(fixture.flattened.view.compose.lanes);
    expect_compose_test(std::ranges::all_of(lanes, [](const auto& lane) {
        return lane.bounds.right <= 820;
    }));
    expect_compose_test(std::ranges::any_of(lanes, [](const auto& lane) {
        return lane.lane_id == lane_id_t{16} && lane.key_color == lane_key_color_t::scratch &&
               lane.stage == stage_group_t::right;
    }));
}

TEST_CASE("Compose reflows wide layouts from stamped canvas width without changing authored data",
          "[sys][compose][scratchpad-specialstyle]") {
    compose_test_fixture_t fixture;
    fixture.project();
    auto action = cursor_placement_action(fixture);
    action.input.compose_inputs.resize(0);
    action.input.compose_input_editor_right_pixels = 700;
    editor_navigation_input_controller_t navigation;
    viewport_input_controller_t viewport;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 700);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.playable_field_right_pixels <= 700);
    expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
    ++action.stamp.document_revision;
    action.input.compose_input_editor_right_pixels = 200;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 700);
    expect_compose_test(compose_editor_layout_width(1000, std::numeric_limits<double>::quiet_NaN()) == 1000);
}
