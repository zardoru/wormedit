// Retained projection state: play follow, playhead, static style, layout order and scalar reuse.
#include "frame_timing_fixtures.h"
#include "sys/compose/compose_playhead_cursor.h"
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

using namespace wormedit;
using namespace wormedit::sys;

namespace {
auto bga_authored_preview_song() -> song_document_t {
    auto song = bga_virtual_preview_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.bga_events.front().position.row = 60;
    chart.measures.resize(3);
    for (std::size_t index = 0; index < chart.measures.size(); ++index) {
        chart.measures[index].id = measure_id_t{index + 1};
        chart.measures[index].notes.clear();
        chart.measures[index].bgm_events.clear();
    }
    return song;
}

void project_bga_preview(compose_test_fixture_t& fixture,
                         const compose_bga_move_preview_input_t& input) {
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr, {}, &input);
    expect_compose_test(fixture.flattened.view.compose.scene_available);
}

auto project_bga_preview_row(compose_test_fixture_t& fixture,
                            bool downscroll, bool snap_enabled,
                            std::uint64_t raw_row = 2 * 960 + 120) -> double {
    auto& state = *fixture.interaction;
    state.selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{500}}};
    fixture.application.project.compose_view.edit_snap_enabled = snap_enabled;
    state.snap_enabled = snap_enabled;
    state.active_snap_denominator = 4;
    state.viewport_scroll_pixels = 0;
    state.pixels_per_standard_measure = 192;
    state.viewport_height_pixels = fixture.flattened.view.compose.viewport_height_pixels;
    const auto direction = downscroll ? std::uint8_t{2} : std::uint8_t{1};
    state.scroll_direction = downscroll ? editor_field_scroll_direction_t::down
                                        : editor_field_scroll_direction_t::up;
    fixture.application.preferences.editor_preferences.scroll_directions[
        static_cast<std::size_t>(editor_mode_family_t::other)] = direction;
    const auto raw_y = static_cast<double>(raw_row) * 192.0 / 960.0;
    const auto pointer_y = downscroll ? state.viewport_height_pixels - raw_y : raw_y;
    const compose_bga_move_preview_input_t preview{
        .project_id = fixture.history.document.id.value,
        .chart_id = 1,
        .document_revision = fixture.history.document.revision,
        .event_id = 500,
        .y_pixels = pointer_y};
    project_bga_preview(fixture, preview);
    const auto& view = fixture.flattened.view.compose;
    INFO("preview stamps: input project=" << preview.project_id << " view=" << view.project_id.value
         << " input chart=" << preview.chart_id << " view chart=" << view.chart_id.value
         << " input revision=" << preview.document_revision
         << " view revision=" << view.document_revision << " event count="
         << view.bga_events.size() << " lane visible=" << view.bga_lane_visible
         << " selected ids=" << state.selected_objects.size() << " scroll="
         << state.viewport_scroll_pixels << " px/measure=" << state.pixels_per_standard_measure
         << " viewport height=" << state.viewport_height_pixels);
    if (view.bga_events.size() != 0)
        INFO("first event=" << view.bga_events[0].event_id.value << " selected="
             << view.bga_events[0].selected << " kind="
             << static_cast<int>(view.bga_events[0].kind));
    REQUIRE(view.bga_move_preview_valid);
    CHECK(view.bga_move_preview_event_id == object_id_t{500});
    const auto source_y = downscroll ? view.viewport_height_pixels - view.bga_move_preview_y_pixels
                                     : view.bga_move_preview_y_pixels;
    const auto preview_row = (state.viewport_scroll_pixels + source_y) *
        960.0 / state.pixels_per_standard_measure;
    return (state.viewport_scroll_pixels + source_y) *
        960.0 / state.pixels_per_standard_measure;
}

auto authored_release_position(const document_history_t& history,
                               const editor_interaction_state_t& state,
                               std::uint64_t raw_row, snap_grid_t grid)
    -> std::optional<local_position_t> {
    const auto& chart = history.document.charts.front();
    const auto model = inspect_editor_interaction(history, state);
    REQUIRE(std::holds_alternative<editor_interaction_view_model_t>(model));
    const auto position = editor_position_at_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(model),
        static_cast<double>(raw_row) * 192.0 / 960.0);
    if (!position) return position;
    const auto index = measure_index_t::from(chart);
    REQUIRE(index.has_value());
    return snap_position_or_exact(chart, index.value(), *position, grid);
}

auto committed_bga_row(const chart_document_t& chart) -> std::uint64_t {
    const auto event = std::ranges::find(chart.bga_events, object_id_t{500}, &bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    const auto index = measure_index_t::from(chart);
    REQUIRE(index.has_value());
    const auto row = index.value().row_of(event->position);
    REQUIRE(row);
    return *row;
}

auto commit_authored_bga_move(compose_test_fixture_t& fixture,
                              std::uint64_t raw_row) -> std::uint64_t {
    auto history = fixture.history;
    const auto& chart = history.document.charts.front();
    const auto denominator = edit_snap_grid(chart, *fixture.interaction);
    const auto position = authored_release_position(history, *fixture.interaction,
                                                    raw_row, denominator);
    REQUIRE(position);
    const auto moved = apply_bga_event_move(history,
        {.chart = chart_id_t{1}, .event = object_id_t{500}, .position = *position,
         .snap = denominator});
    REQUIRE(moved);
    return committed_bga_row(history.document.charts.front());
}

auto commit_virtual_bga_move(compose_test_fixture_t& fixture,
                             std::uint64_t raw_row) -> std::uint64_t {
    auto history = fixture.history;
    const auto& chart = history.document.charts.front();
    const auto moved = apply_virtual_bms_bga_event_move(history,
        {.chart = chart_id_t{1}, .event = object_id_t{500}, .canvas_row = raw_row,
         .snap = edit_snap_grid(chart, *fixture.interaction)});
    REQUIRE(moved);
    return committed_bga_row(history.document.charts.front());
}

auto commit_bga_move_row(compose_test_fixture_t& fixture, std::uint64_t raw_row,
                         bool virtual_destination) -> std::uint64_t {
    return virtual_destination ? commit_virtual_bga_move(fixture, raw_row)
                               : commit_authored_bga_move(fixture, raw_row);
}

void check_bga_preview_matches_release(compose_test_fixture_t& fixture, bool downscroll,
                                       bool snap_enabled, std::uint64_t raw_row,
                                       bool virtual_destination) {
    const auto preview_row = project_bga_preview_row(fixture, downscroll, snap_enabled, raw_row);
    const auto committed_row = commit_bga_move_row(fixture, raw_row, virtual_destination);
    CHECK(preview_row == Catch::Approx(static_cast<double>(committed_row)));
}

void check_stale_bga_preview_is_hidden(compose_test_fixture_t& fixture) {
    auto& state = *fixture.interaction;
    state.selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{500}}};
    const compose_bga_move_preview_input_t stale{
        .project_id = fixture.history.document.id.value,
        .chart_id = 1,
        .document_revision = fixture.history.document.revision + 1,
        .event_id = 500,
        .y_pixels = 24.0};
    project_bga_preview(fixture, stale);
    CHECK_FALSE(fixture.flattened.view.compose.bga_move_preview_valid);
}
} // namespace

void project_follow_frame(document_history_t& history, workspace_controller_t& workspace,
                          compose_scene_cache_t& cache, application_command_state_t& application,
                          std::optional<editor_interaction_state_t>& interaction, std::size_t frame) {
    auto& timer = frame_timing();
        timer.begin_frame();
        {
            frame_phase_scope_t phase(frame_phase_t::workspace);
            const double row = 960.0 + static_cast<double>(frame) * 5.0;
            if (interaction) REQUIRE(follow_compose_playhead(history, *interaction, row, true));
            application.project.transport.preview_playhead_absolute_row = row;
            const auto model = workspace_view_t::from(workspace, history.document);
            REQUIRE(model.value);
            auto flattened = frontend_workspace_view_t::from(*model.value, history.document, "", {}, false);

            populate_compose_view(flattened, history, workspace, cache, 1920, 1080,
                                  interaction, application, nullptr);
            REQUIRE(flattened.view.compose.scene_available);
            REQUIRE(!flattened.view.compose.note_endpoints.empty());
        }
        timer.end_frame();
}

TEST_CASE("Frame timing projects real play follow on a 50000 note chart", "[sys][frame-timing][performance]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    auto& workspace = *binding.value;
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    auto& timer = frame_timing();
    timer = {};
    for (std::size_t frame = 0; frame < frame_timing_capacity; ++frame)
        project_follow_frame(history, workspace, cache, application, interaction, frame);
    REQUIRE(interaction);
    CHECK(interaction->viewport_scroll_pixels > 0);
    CHECK(history.document.revision == 1);
    std::puts("Headless actual 50000-note play-follow projection, 240 frames (no audio/device/UI):");
    const auto total = timer.summary();
    std::printf("Frame: average %.6f ms, p99 %.6f ms\n", total.average_ms, total.p99_ms);
    for (std::size_t i = 0; i < frame_phase_count; ++i) {
        const auto stats = timer.summary(static_cast<frame_phase_t>(i));
        if (stats.count == 0) std::printf("%s: N/A (not executed headlessly)\n", frame_phase_names[i]);
        else std::printf("%s: average %.6f ms, p99 %.6f ms (%zu samples)\n",
                         frame_phase_names[i], stats.average_ms, stats.p99_ms, stats.count);
    }
}

namespace {
void check_inline_timing_fields(const wormedit::ui::timing_view_t& view, std::uint64_t revision) {
    CHECK(view.chart_id == chart_id_t{1});
    CHECK(view.document_revision == revision);
    CHECK(view.initial_bpm_present);
    CHECK(view.initial_bpm_value == 120.0);
    CHECK(view.stage_active);
    CHECK(view.stage_valid);
}

void check_slim_timing_geometry(const wormedit::ui::timing_view_t& view) {
    CHECK(view.grid_lines.empty());
    CHECK(view.markers.empty());
    CHECK(view.speeds.empty());
}
} // namespace

TEST_CASE("Compose timing projection skips whole-chart geometry and preserves inline fields", "[sys][timing-projection]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    auto& workspace = *binding.value;
    workspace.selected_chart = chart_id_t{1};
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    timing_scene_cache_t cache;
    timing_grid_state_t grid;
    grid.active_denominator = 4;
    timing_lane_pseudonote_outcome_t stage{timing_lane_pseudonote_proposal_t::from(
        chart_stamp_t::from(history.document, chart_id_t{1}),
        timing_lane_pseudonote_preview_t{.report = {.position = {measure_id_t{1}, 0}}})};
    populate_timing_view_for_compose(flattened, history, workspace, cache, 1280, 980,
                                    grid, &stage);
    const auto& view = flattened.view.timing;
    check_inline_timing_fields(view, history.document.revision);
    check_slim_timing_geometry(view);
    const auto& indexed = history.indices(chart_id_t{1})->measures;
    REQUIRE(indexed);
    const auto* entries = indexed->entries.data();
    populate_timing_view_for_compose(flattened, history, workspace, cache, 1280, 980,
                                    grid, &stage);
    CHECK(history.indices(chart_id_t{1})->measures->entries.data() == entries);
    CHECK(cache.build_count() == 1);
}

TEST_CASE("Idle Compose timing projection caches lightweight measure capacities", "[sys][timing-projection]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    binding.value->selected_chart = chart_id_t{1};
    frontend_workspace_view_t flattened;
    timing_scene_cache_t cache;
    populate_timing_view_for_compose(flattened, history, *binding.value, cache,
                                    1280, 980, {}, nullptr);
    CHECK(flattened.view.timing.scene_available);
    CHECK(flattened.view.timing.total_rows == 960000);
    CHECK(cache.value() == flattened.view.timing.total_rows);
    populate_timing_view_for_compose(flattened, history, *binding.value, cache,
                                    1280, 980, {}, nullptr);
    CHECK(cache.build_count() == 1);
}

TEST_CASE("Compose controls retain measure geometry until the document revision changes", "[sys][timing-projection]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    auto& workspace = *binding.value;
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    std::optional<editor_interaction_state_t> interaction;
    populate_measure_cache_frame(history, workspace, cache, interaction);
    REQUIRE(cache.scene.cached_measure_index);
    const auto* entries = cache.scene.cached_measure_index->entries.data();
    populate_measure_cache_frame(history, workspace, cache, interaction);
    CHECK(cache.scene.cached_measure_index->entries.data() == entries);
    history.document.charts.front().measures.back().definition = {{2, 1}, meter_t{8, 4}};
    ++history.document.revision;
    history.document.reset_chart_indices(); // Match publication through the transaction runner.
    static_cast<void>(rebind_workspace(workspace, history));
    interaction.reset(); // Match the application stale-interaction boundary after an edit.
    populate_measure_cache_frame(history, workspace, cache, interaction);
    CHECK(cache.scene.cached_measure_index->total_rows == 960960);
    CHECK(cache.stamp().document.revision == history.document.revision);
}

TEST_CASE("Retained runtime timing view refills without heap allocations", "[sys][frame-timing][allocation]") {
    auto& timer = frame_timing();
    timer.record({.frame_ms = 12.5});
    wormedit::ui::frame_timing_view_t view;
    worm_ui_frame_timing_view(view);
    const auto* samples = view.frame_ms.data();
    const auto* phases = view.phases.data();
    const auto before = frame_heap_allocation_snapshot();
    worm_ui_frame_timing_view(view);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    CHECK(view.frame_ms.data() == samples);
    CHECK(view.phases.data() == phases);
    CHECK(view.frame_ms.size() == timer.size());
    CHECK(view.frame_ms.back() == 12.5);
    CHECK(view.phases.size() == frame_phase_count);
    CHECK(view.gpu_work.size() == frame_gpu_work_count);
}

TEST_CASE("Owned action sound lists reuse capacity after warming", "[sys][frontend-actions][allocation]") {
    ui::workspace_action_t action{};
    const std::array<std::uint64_t, 3> ids{7, 11, 19};
    action.resource.set_sound_resources(ids);
    const auto* storage = action.resource.sound_resource_ids.data();
    const auto before = frame_heap_allocation_snapshot();
    action.resource.set_sound_resources(ids);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    CHECK(action.resource.sound_resource_ids.data() == storage);
    CHECK(action.resource.sound_resource_ids ==
        std::vector<resource_id_t>{resource_id_t{7}, resource_id_t{11}, resource_id_t{19}});
}

TEST_CASE("Workspace action copies own scoped Text and measure positions", "[sys][frontend-actions]") {
    auto copy = [] {
        ui::workspace_action_t source{};
        source.bms_program.text = "#BPM 140";
        source.measure.ids = {0, 4, 9};
        auto result = source;
        source.bms_program.text.assign(128, 'x');
        source.measure.ids.clear();
        return result;
    }();
    CHECK(copy.bms_program.text == "#BPM 140");
    CHECK(copy.measure.ids == std::vector<std::uint64_t>{0, 4, 9});
}

namespace {
auto playhead_test_state(const document_history_t& history) -> editor_interaction_state_t {
    const std::uint64_t snaps[]{4, 8, 16};
    auto setup = editor_interaction_state_t::from(history, chart_id_t{1}, lane_id_t{1},
        {measure_id_t{1}, 0}, snaps, 16, 800);
    REQUIRE(setup.state);
    return *setup.state;
}

void compare_follow_state(const editor_interaction_state_t& actual,
                          const editor_interaction_state_t& expected) {
    CHECK(actual.cursor == expected.cursor);
    CHECK(actual.virtual_bms_cursor_row == expected.virtual_bms_cursor_row);
    CHECK(actual.active_lane == expected.active_lane);
    CHECK(actual.viewport_scroll_pixels == Catch::Approx(expected.viewport_scroll_pixels));
    CHECK((actual.fixed_cursor_screen_y_pixels == expected.fixed_cursor_screen_y_pixels &&
           actual.scroll_direction == expected.scroll_direction &&
           actual.armed_note_kind == expected.armed_note_kind));
}

void compare_follow_step(document_history_t& history, const measure_index_t& index,
                         editor_interaction_state_t& actual, editor_interaction_state_t& expected,
                         double row, bool anchored) {
    const auto& chart = history.document.charts.front();
    const auto bounded = std::min(static_cast<long double>(row),
                                   static_cast<long double>(index.total_rows - 1));
    const auto snapped_row = snap_canvas_row(chart, index, bounded,
        active_snap_grid(expected), index.total_rows).value();
    const auto after = std::ranges::upper_bound(index.entries, snapped_row, {},
        &measure_index_entry_t::absolute_begin_row);
    const auto measure = std::prev(after);
    const local_position_t position{measure->id, snapped_row - measure->absolute_begin_row};
    const auto previous_scroll = expected.viewport_scroll_pixels;
    if (expected.cursor != position || expected.virtual_bms_cursor_row)
        REQUIRE(dispatch_editor_action(history, expected,
            jump_edit_cursor_action_t{position, history.document.revision}));
    const auto target = compose_playhead_scroll_target(expected, chart, bounded, previous_scroll, anchored);
    REQUIRE(target);
    if (std::abs(*target - expected.viewport_scroll_pixels) >= 0.001)
        REQUIRE(dispatch_editor_action(history, expected,
            scroll_editor_view_action_t{.vertical_pixels = *target - expected.viewport_scroll_pixels}));
    expected.cursor_subrow = static_cast<double>(bounded - snapped_row);
    REQUIRE(follow_compose_playhead(history, actual, row, anchored,
        {chart.id, history.document.revision, &index}));
    compare_follow_state(actual, expected);
}
} // namespace

TEST_CASE("Retained playhead geometry preserves dispatcher cursor and viewport transitions", "[sys][frame-timing][follow]") {
    auto song = dense_song();
    auto& chart = song.charts.front();
    auto layout = chart.lane_layouts.front();
    layout.id = object_id_t{60000};
    layout.position = {measure_id_t{2}, 0};
    layout.layout.lanes.front().id = lane_id_t{2};
    chart.lane_layouts.push_back(layout);
    auto history = document_history_t::from(std::move(song));
    const auto index = measure_index_t::from(history.document.charts.front()).value();
    auto actual = playhead_test_state(history);
    auto expected = actual;
    for (const auto row : {0.0, 59.9, 60.0, 959.9, 960.0, 1920.0, 959999.0, 2000000.0, 60.0}) {
        compare_follow_step(history, index, actual, expected, row, true);
        compare_follow_step(history, index, actual, expected, row, false);
    }
    CHECK(history.document.revision == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("Retained playhead geometry rejects stale provenance and active gestures", "[sys][frame-timing][follow]") {
    auto history = document_history_t::from(dense_song());
    const auto index = measure_index_t::from(history.document.charts.front()).value();
    auto state = playhead_test_state(history);
    const auto before = state;
    CHECK_FALSE(follow_compose_playhead(history, state, 1920, true, {chart_id_t{2}, 1, &index}));
    CHECK_FALSE(follow_compose_playhead(history, state, 1920, true, {chart_id_t{1}, 2, &index}));
    compare_follow_state(state, before);
    state.active_gesture = wormedit::proposal_t<wormedit::pending_editor_area_selection_t, wormedit::document_stamp_t>{};
    CHECK_FALSE(follow_compose_playhead(history, state, 1920, true, {chart_id_t{1}, 1, &index}));
    CHECK_FALSE(follow_editor_playhead(history, state, {chart_id_t{1}, 1, &index},
        {measure_id_t{3}, 0}, 100));
    compare_follow_state(state, before);
}

namespace {
struct static_style_fixture_t {
    document_history_t history=document_history_t::from(static_style_song());
    decltype(workspace_controller_t::from(history)) binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    static_style_fixture_t(song_document_t song=static_style_song())
        : history(document_history_t::from(std::move(song))), binding(workspace_controller_t::from(history)) {
        expect_static_style(binding.value.has_value());
        binding.value->active_tab=workspace_tab_t::compose;
        binding.value->selected_chart=chart_id_t{1};
    }
    auto project() -> std::uint64_t {
        // The application creates a fresh flattened frame; source vectors are
        // appended during projection and must not retain the previous frame.
        flattened={};
        flattened.view.compose.visible=true;
        populate_compose_view(flattened,history,*binding.value,cache,1280,980,
                              interaction,application,nullptr);
        expect_static_style(flattened.view.compose.scene_available);
        return flattened.view.compose.static_scene_style_generation;
    }
    auto first_note() const -> std::uint64_t {
        expect_static_style(flattened.view.compose.note_endpoints.size()!=0);
        return flattened.view.compose.note_endpoints[0].note_id.value;
    }
};
} // namespace

TEST_CASE("Compose static style generation follows exact semantic filters without invalidating scroll",
          "[sys][field-tile-style]") {
    static_style_fixture_t fixture;
    const auto initial=fixture.project();
    expect_static_style(fixture.project()==initial);
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{101};
    fixture.application.project.semantic_layer.semantic_layer_solo_active=true;
    const auto first=fixture.project();
    CHECK_FALSE(fixture.interaction->solo_semantic_layer); // Projection must not repair retained policy.
    REQUIRE(fixture.cache.frame.interaction_view);
    CHECK(fixture.cache.frame.interaction_view->solo_semantic_layer == layer_id_t{101});
    const auto first_note=fixture.first_note();
    expect_static_style(first>initial);
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{102};
    const auto second=fixture.project();
    expect_static_style(second>first);
    expect_static_style(fixture.first_note()!=first_note);
    fixture.application.project.semantic_layer.semantic_layer_solo_active=false;
    fixture.application.project.semantic_layer.required_semantic_layer_filters={layer_id_t{101}};
    const auto required=fixture.project();
    CHECK_FALSE(fixture.interaction->solo_semantic_layer);
    expect_static_style(required>second);
    expect_static_style(fixture.first_note()==first_note);
    fixture.application.project.semantic_layer.required_semantic_layer_filters={layer_id_t{102}};
    const auto replacement=fixture.project();
    expect_static_style(replacement>required);
    expect_static_style(fixture.first_note()!=first_note);
    fixture.application.project.semantic_layer.excluded_semantic_layer_filters={layer_id_t{101}};
    const auto excluded=fixture.project();
    expect_static_style(excluded>replacement);
    expect_static_style(fixture.project()==excluded);
    fixture.interaction->viewport_scroll_pixels=65.3;
    expect_static_style(fixture.project()==excluded);
}

TEST_CASE("BGA move preview matches committed target rows for scroll and snap modes",
          "[sys][bga-lane][preview]") {
    compose_test_fixture_t fixture{bga_virtual_preview_song()};
    fixture.project();
    REQUIRE(fixture.interaction.has_value());
    check_bga_preview_matches_release(fixture, false, true, 2 * 960 + 120, true);
    check_bga_preview_matches_release(fixture, true, true, 2 * 960 + 120, true);
    check_bga_preview_matches_release(fixture, false, false, 2 * 960 + 120, true);
    check_bga_preview_matches_release(fixture, true, false, 2 * 960 + 120, true);
    compose_test_fixture_t authored{bga_authored_preview_song()};
    authored.project();
    REQUIRE(authored.interaction.has_value());
    check_bga_preview_matches_release(authored, false, true, 120, false);
    check_bga_preview_matches_release(authored, true, true, 360, false);
    check_bga_preview_matches_release(authored, false, false, 360, false);
    check_bga_preview_matches_release(authored, true, false, 120, false);
    check_stale_bga_preview_is_hidden(authored);
}

TEST_CASE("Compose static tile epoch distinguishes reloaded content with identical native IDs",
          "[sys][field-tile-style]") {
    static_style_fixture_t fixture;
    const auto initial=fixture.project();
    const auto first_note=fixture.first_note();
    const auto project=fixture.flattened.view.compose.project_id.value;
    const auto chart=fixture.flattened.view.compose.chart_id.value;
    const auto revision=fixture.flattened.view.compose.document_revision;
    fixture.history.document.charts.front().measures.front().notes.front().position.row=60;
    fixture.cache={}; // Match main's real project-open cache replacement boundary.
    const auto replacement=fixture.project();
    expect_static_style(replacement!=initial);
    expect_static_style(fixture.flattened.view.compose.project_id.value==project);
    expect_static_style(fixture.flattened.view.compose.chart_id.value==chart);
    expect_static_style(fixture.flattened.view.compose.document_revision==revision);
    expect_static_style(fixture.first_note()!=first_note);
    expect_static_style(fixture.project()==replacement);
}

TEST_CASE("configured playing anchor preserves paused comfort scrolling", "[frame-timing]") {
    editor_interaction_state_t state;
    state.viewport_height_pixels = 600;
    state.pixels_per_standard_measure = 192;
    chart_document_t chart;
    chart.divisions_per_standard_measure = 1920;
    const auto default_target = compose_playhead_scroll_target(state, chart, 4000, 0, true);
    const auto configured = compose_playhead_scroll_target(state, chart, 4000, 0, true, 0.35);
    REQUIRE(default_target);
    REQUIRE(configured);
    CHECK(*default_target == 280);
    CHECK(*configured == 190);
    CHECK(compose_playhead_scroll_target(state, chart, 4000, 0, false, 0.35) ==
          compose_playhead_scroll_target(state, chart, 4000, 0, false));
    CHECK_FALSE(compose_playhead_scroll_target(state, chart, 4000, 0, true, 1.1));
}

namespace {
auto compose_full_layout_is_ordered() -> bool {
    using namespace wormedit::sys;
    const auto full = compose_horizontal_layout(900, 5, true, true, 0xff);
    return full.bga_pane_left_pixels == 426.0 && full.bga_pane_width_pixels == 48.0 &&
           full.field_left_pixels == full.bga_pane_left_pixels + full.bga_pane_width_pixels &&
           full.bgm_pane_left_pixels ==
               full.field_left_pixels + full.playable_field_width_pixels &&
           full.bgm_pane_left_pixels + full.bgm_pane_width_pixels == 900.0;
}

auto compose_narrow_layout_is_ordered() -> bool {
    using namespace wormedit::sys;
    const auto hidden_timing = compose_horizontal_layout(900, 5, true, true, 0);
    const auto narrow = compose_horizontal_layout(280, 5, true, true, 0xff);
    return hidden_timing.bga_pane_left_pixels == compose_measure_rail_width_pixels &&
           hidden_timing.field_left_pixels == 90.0 &&
           narrow.bga_pane_left_pixels >= compose_measure_rail_width_pixels &&
           narrow.bga_pane_left_pixels + narrow.bga_pane_width_pixels ==
               narrow.field_left_pixels &&
           narrow.field_left_pixels + narrow.playable_field_width_pixels ==
               narrow.bgm_pane_left_pixels &&
           narrow.bgm_pane_left_pixels + narrow.bgm_pane_width_pixels == 280.0 &&
           narrow.playable_field_width_pixels >= compose_minimum_playable_field_width_pixels;
}

auto compose_hidden_lane_layout_is_ordered() -> bool {
    using namespace wormedit::sys;
    const auto no_bga = compose_horizontal_layout(900, 5, true, false, 0xff);
    const auto no_bgm = compose_horizontal_layout(900, 5, false, true, 0xff);
    return no_bga.bga_pane_width_pixels == 0.0 &&
           no_bga.field_left_pixels == no_bga.bga_pane_left_pixels &&
           no_bga.field_left_pixels == 426.0 && no_bgm.bga_pane_left_pixels == 426.0 &&
           no_bgm.field_left_pixels == 474.0 &&
           no_bgm.playable_field_width_pixels == 240.0 &&
           no_bgm.bgm_pane_width_pixels == 0.0;
}

auto compose_horizontal_layout_is_ordered() -> bool {
    return compose_full_layout_is_ordered() && compose_narrow_layout_is_ordered() &&
           compose_hidden_lane_layout_is_ordered();
}
} // namespace

TEST_CASE("Compose horizontal layout orders timing, BGA, playable and BGM panes",
          "[sys][bga-lane][horizontal-layout]") {
    CHECK(compose_horizontal_layout_is_ordered());
}

TEST_CASE("A user-chosen timing width replaces the natural one within its limits",
          "[sys][horizontal-layout][timing-region]") {
    using namespace wormedit::sys;
    const auto automatic = compose_horizontal_layout(900, 5, true, 0, 0xFF);
    CHECK(automatic.bga_pane_left_pixels == 426.0);
    CHECK(automatic.timing_lane_mask == 0xFFU);
    const auto narrowed = compose_horizontal_layout(900, 5, true, 0, 0xFF, 100.0);
    CHECK(narrowed.bga_pane_left_pixels == compose_measure_rail_width_pixels + 100.0);
    CHECK(narrowed.timing_lane_mask == 0x1BU);
    // Wider than twice natural is capped; the window still limits what fits.
    const auto widened = compose_horizontal_layout(2000, 5, true, 0, 0xFF, 5000.0);
    CHECK(widened.bga_pane_left_pixels == compose_measure_rail_width_pixels + 768.0);
    const auto cramped = compose_horizontal_layout(900, 5, true, 0, 0xFF, 5000.0);
    CHECK(cramped.bga_pane_left_pixels == 900.0 - 240.0);
    // Never smaller than one icon, which keeps BPM.
    const auto minimum = compose_horizontal_layout(900, 5, true, 0, 0xFF, 1.0);
    CHECK(minimum.bga_pane_left_pixels == compose_measure_rail_width_pixels + 24.0);
    CHECK(minimum.timing_lane_mask == 1U);
    // BGA keeps its fixed width regardless.
    CHECK(compose_horizontal_layout(900, 5, true, 2, 0xFF, 100.0).bga_pane_width_pixels == 96.0);
}

TEST_CASE("scroll overrides follow mode identity across native and imported charts", "[frame-timing]") {
    chart_document_t chart;
    lane_layout_t layout;
    layout.profile_id = "stepmania:dance-single";
    chart.lane_layouts.push_back({.layout = layout});
    editor_preferences_t preferences;
    preferences.scroll_directions[static_cast<size_t>(editor_mode_family_t::dance)] = 1;
    chart.source_family = format_family_t::stepmania;
    CHECK_FALSE(compose_field_downscroll(chart, preferences));
    chart.source_family = format_family_t::native_project;
    CHECK_FALSE(compose_field_downscroll(chart, preferences));
    CHECK(compose_mode_family(layout) == editor_mode_family_t::dance);
    layout.profile_id = "stepmania:pump-double";
    CHECK(compose_mode_family(layout) == editor_mode_family_t::pump);
    layout.profile_id = "custom-layout";
    CHECK(compose_mode_family(layout) == editor_mode_family_t::other);
    layout.bmson_generic_keys_only = true;
    CHECK(compose_mode_family(layout) == editor_mode_family_t::keyboard);
}

TEST_CASE("Retained static scalar source reuses storage while fractional scrolling", "[sys][field-tile-style]") {
    static_style_fixture_t fixture(dense_song());
    fixture.project();
    auto& view=fixture.flattened.view.compose;
    expect_static_style(view.static_scene!=nullptr);
    const auto generation=view.static_scene_source_generation;
    const auto storage=fixture.cache.scene.static_source.value().storage;
    const auto style=view.static_scene_style_generation;
    fixture.interaction->viewport_scroll_pixels=65.3;
    fixture.project();
    expect_static_style(view.static_scene!=nullptr);
    expect_static_style(view.static_scene_source_generation==generation);
    expect_static_style(fixture.cache.scene.static_source.value().storage==storage);
    expect_static_style(view.static_scene_style_generation==style);
    expect_static_style(view.static_scene_accounted_bytes<=16*1024*1024);
    expect_static_style(view.static_scene->note_endpoints.size()>view.note_endpoints.size());
    fixture.application.project.compose_view.compose_timing_lane_visibility_mask^=1;
    fixture.project();
    expect_static_style(view.static_scene!=nullptr);
    expect_static_style(view.static_scene_source_generation!=generation);
    expect_static_style(view.static_scene->timing_lane_visibility_mask==view.timing_lane_visibility_mask);
}

TEST_CASE("new-chart requested custom snap is admitted without replacing existing chart preferences", "[frame-timing]") {
    application_command_state_t state;
    state.preferences.editor_preferences.default_snap = 5;
    state.project.compose_view.requested_snap_denominators.emplace(1, 8);
    state.project.compose_view.requested_snap_denominators.emplace(2, state.preferences.editor_preferences.default_snap);
    const auto preferences = compose_snap_preferences(state, chart_id_t{2});
    const auto custom = filter_snap_denominators(1920, preferences, requested_snap_denominator(state, chart_id_t{2}));
    CHECK(custom.active_denominator == 5);
    CHECK(std::ranges::find(custom.supported_denominators, 5) != custom.supported_denominators.end());
    CHECK(requested_snap_denominator(state, chart_id_t{1}) == 8);
    CHECK(requested_snap_denominator(state, chart_id_t{3}) == 4);
    const auto incompatible = filter_snap_denominators(384, preferences, 5);
    CHECK(incompatible.active_was_fallback);
    CHECK(incompatible.active_denominator != 5);
}

namespace {
void compare_scalar_reuse_frame(static_style_fixture_t& retained, static_style_fixture_t& fresh,
                                double scroll) {
    retained.interaction->viewport_scroll_pixels = scroll;
    retained.project();
    fresh.interaction = retained.interaction;
    fresh.cache = {}; // Independent full-composition reference, not retained scalar extraction.
    fresh.project();
    compare_scalar_notes(retained.flattened.view.compose, fresh.flattened.view.compose);
    compare_scalar_holds_bgm(retained.flattened.view.compose, fresh.flattened.view.compose);
    compare_scalar_grid(retained.flattened.view.compose, fresh.flattened.view.compose);
    expect_static_style(retained.cache.scene.static_projection_reused_frames != 0);
    expect_static_style(retained.cache.scene.static_projection_scanned <
                        retained.cache.scene.static_source.value().storage->view.compose.note_endpoints.size());
}
} // namespace

TEST_CASE(
    "Indexed retained scalar publication matches full composition while following and reversing",
    "[sys][field-tile-style][scalar-reuse]") {
    for (const auto direction : {std::uint8_t{0}, std::uint8_t{1}}) {
        static_style_fixture_t retained(scalar_reuse_song()), fresh(scalar_reuse_song());
        retained.application.preferences.editor_preferences.scroll_directions.fill(direction);
        fresh.application.preferences.editor_preferences = retained.application.preferences.editor_preferences;
        retained.project();
        fresh.project();
        for (const auto scroll : {0.0, 19.75, 65.3, 120.5, 65.3, 0.0})
            compare_scalar_reuse_frame(retained, fresh, scroll);
    }
}

TEST_CASE("Debug heap counters attribute nested projection allocations exactly once",
          "[sys][frame-timing][allocation]") {
#ifndef NDEBUG
    frame_timing_t timer;
    timer.begin_frame();
    const auto parent = timer.enter(frame_phase_t::workspace);
    void* workspace = ::operator new(17);
    const auto child = timer.enter(frame_phase_t::scene);
    void* scene = ::operator new(31);
    timer.restore(child);
    void* tail = ::operator new(13);
    timer.restore(parent);
    void* outside = ::operator new(7);
    timer.end_frame();
    ::operator delete(workspace);
    ::operator delete(scene);
    ::operator delete(tail);
    ::operator delete(outside);
    const auto& sample = timer.sample(0);
    check_heap_counter(sample.heap_measured, true);
    check_heap_counter(sample.frame_heap.count, 4);
    check_heap_counter(sample.frame_heap.bytes, 68);
    const auto projection = sample.phase_heap[static_cast<std::size_t>(frame_phase_t::workspace)];
    check_heap_counter(projection.count, 3);
    check_heap_counter(projection.bytes, 61);
    const auto geometry = sample.phase_heap[static_cast<std::size_t>(frame_phase_t::scene)];
    check_heap_counter(geometry.count, 1);
    check_heap_counter(geometry.bytes, 31);
    check_heap_counter(timer.heap_summary(frame_phase_t::workspace).average, 3);
    check_heap_counter(timer.heap_summary(frame_phase_t::workspace, true).p99, 61);
#else
    check_heap_counter(frame_heap_allocation_snapshot().count, 0);
#endif
}
