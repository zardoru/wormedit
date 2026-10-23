#include "sys/compose/actions/compose_audition_requests.h"
#include "compose_scene_cache_fixture.h"
#include "../support/stale_action_assertions.h"
#include "../support/workspace_result_assertions.h"
#include "../sys/application_dispatch_fixture.h"
#include "compose_area_dispatch_fixture.h"
#include "project_audio_sources_fixture.h"
#include "sys/commands/application_command_dispatch.h"
#include "sys/app/application_defaults.h"
#include "sys/audio/bmson_waveform_view.h"
#include "sys/audio/chart_preview_state.h"
#include "sys/project/compare_view.h"
#include "sys/compose/actions/compose_actions.h"
#include "sys/compose/actions/compose_area_actions.h"
#include "sys/compose/input/compose_input.h"
#include "sys/compose/input/compose_input_internal.h"
#include "sys/compose/input/compose_minimap_navigation.h"
#include "sys/compose/compose_playhead_cursor.h"
#include "sys/compose/actions/compose_quantize.h"
#include "sys/compose/views/compose_view.h"
#include "sys/compose/views/compose_view_internal.h"
#include "sys/app/frame_timing.h"
#include <wormedit/core/arena.h>
#include "sys/resources/bga_dock_runtime.h"
#include "../support/temporary_directory.h"
#include <fstream>
#include "sys/preferences/options_runtime.h"
#include "sys/compose/views/recipe_form.h"
#include "sys/timing/timing_view.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/audio/waveform_analysis_runtime.h"
#include "sys/audio/waveform_view.h"
#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdio>
#include <cstring>
#include <span>
#include <wormedit/core/bmson_lane_mode.h>
#include <wormedit/core/command.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/workspace.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif
#include <array>
#include <limits>
#include <cmath>
#include <filesystem>
#include <functional>
#include <SDL3/SDL_timer.h>
#include <cstdlib>
#include <new>
#include <source_location>

// The real Compose input selector is linked below. Its unrelated UI-only
// resource-placement notification has no recipient in this headless harness.
namespace {
std::vector<uint64_t> placed_resource_notifications;
}
void worm_ui_frontend_resource_placed(uint64_t placed, uint64_t) {
    placed_resource_notifications.push_back(placed);
}

namespace {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
}
void* operator new(std::size_t size) {
    wormedit::sys::record_frame_heap_allocation(size);
    if (count_allocations) ++allocations;
    if (auto* value = std::malloc(size == 0 ? 1 : size)) return value;
    throw std::bad_alloc();
}
void operator delete(void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }

using namespace wormedit;
using namespace wormedit::sys;

TEST_CASE("Frame timing retains exactly 240 chronological samples and nearest rank p99", "[sys][frame-timing]") {
    frame_timing_t timer;

    for (std::size_t i = 1; i <= 300; ++i) {
        frame_timing_sample_t sample;
        sample.frame_ms = static_cast<double>(i);
        sample.measured[1] = true;
        sample.phase_ms[1] = static_cast<double>(i) / 100.0;
        timer.record(sample);
    }
    REQUIRE(timer.size() == 240);
    CHECK(timer.sample(0).frame_ms == 61.0);
    CHECK(timer.sample(239).frame_ms == 300.0);
    CHECK(timer.summary(frame_phase_t::audio).p99_ms == Catch::Approx(2.98));

}

TEST_CASE("Frame timing nested phases exclude child work from parent", "[sys][frame-timing]") {
    frame_timing_t timer;
    timer.begin_frame();
    {
        frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
        SDL_Delay(2);
        {
            frame_phase_scope_t scene(frame_phase_t::scene, timer);
            SDL_Delay(5);
        }
        SDL_Delay(2);
    }
    timer.end_frame();
    REQUIRE(timer.size() == 1);
    const auto& sample = timer.sample(0);
    const auto workspace = sample.phase_ms[2];
    const auto scene = sample.phase_ms[3];
    CHECK(workspace >= 3.0);
    CHECK(scene >= 4.0);
    CHECK(workspace + scene <= sample.frame_ms + 0.001);
    CHECK_FALSE(sample.measured[1]);
}

#if defined(__unix__) || defined(__APPLE__)
namespace {
auto test_thread_cpu_nanoseconds() -> std::uint64_t {
    timespec now{};
    REQUIRE(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &now) == 0);
    return static_cast<std::uint64_t>(now.tv_sec) * 1'000'000'000ULL + now.tv_nsec;
}
// One frame with a spinning workspace phase (6 ms) and a sleeping scene phase (8 ms).
void time_busy_then_waiting_frame(frame_timing_t& timer) {
    timer.begin_frame();
    {
        frame_phase_scope_t busy(frame_phase_t::workspace, timer);
        const auto until = test_thread_cpu_nanoseconds() + 6'000'000;
        volatile std::uint64_t spin = 0;
        while (test_thread_cpu_nanoseconds() < until) spin = spin + 1;
    }
    {
        frame_phase_scope_t waiting(frame_phase_t::scene, timer);
        SDL_Delay(8);
    }
    timer.end_frame();
}
} // namespace
#endif

#if defined(__unix__) || defined(__APPLE__)
TEST_CASE("Frame timing separates CPU time from waiting in each phase", "[sys][frame-timing]") {
    frame_timing_t timer;
    time_busy_then_waiting_frame(timer);
    REQUIRE(timer.size() == 1);
    const auto& sample = timer.sample(0);
    REQUIRE(sample.cpu_measured);
    const auto busy = static_cast<std::size_t>(frame_phase_t::workspace);
    const auto waiting = static_cast<std::size_t>(frame_phase_t::scene);
    // CPU work remains measurable even if another task preempts this thread.
    CHECK(sample.phase_cpu_ms[busy] >= 5.0);
    CHECK(sample.phase_cpu_ms[waiting] <= sample.phase_ms[waiting] * 0.25);
    CHECK(sample.frame_cpu_ms <= sample.frame_ms + 0.5);
    CHECK(timer.cpu_summary(frame_phase_t::scene).count == 1);
}
#endif

#if defined(__linux__)
TEST_CASE("Frame timing counts the context switches of a frame", "[sys][frame-timing]") {
    frame_timing_t timer;
    time_busy_then_waiting_frame(timer);
    const auto& sample = timer.sample(0);
    REQUIRE(sample.switches_measured);
    CHECK(sample.voluntary_switches >= 1); // the sleep yields the CPU
}
#endif

TEST_CASE("Frame timing hidden collection allocates no C++ heap storage", "[sys][frame-timing]") {
    frame_timing_t timer;
    // Warm SDL's counters before tracking the instrumentation's allocations.
    timer.begin_frame();
    timer.end_frame();
    allocations = 0;
    count_allocations = true;
    for (std::size_t i = 0; i < 240; ++i) {
        timer.begin_frame();
        {
            frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
            frame_phase_scope_t scene(frame_phase_t::scene, timer);
        }
        timer.end_frame();
    }
    count_allocations = false;
    CHECK(allocations == 0);
    CHECK(timer.size() == 240);
}

namespace {
auto dense_song(std::uint64_t measure_count = 1000,
                std::uint64_t notes_per_measure = 50) -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 1;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.lane_layouts = {{.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "frame-timing", .display_name = "Timing workload",
                   .lanes = {{.id = lane_id_t{1}, .name = "Key", .role = lane_role_t::playable}}}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    for (std::uint64_t measure = 0; measure < measure_count; ++measure) {
        measure_chunk_t value;
        value.id = measure_id_t{measure + 1};
        for (std::uint64_t note = 0; note < notes_per_measure; ++note)
            value.notes.push_back({.id = object_id_t{3 + measure * notes_per_measure + note},
                                   .position = {value.id, note * 18}, .lane = lane_id_t{1}});
        chart.measures.push_back(std::move(value));
    }
    song.charts.push_back(std::move(chart));
    return song;
}
void expect_compose_test(bool condition,
                         std::source_location source = std::source_location::current()) {
    INFO("Compose expectation at " << source.line());
    REQUIRE(condition);
}
auto compose_test_song() -> song_document_t {
    return dense_song(1, 8);
}
struct compose_test_fixture_t {
    document_history_t history;
    decltype(workspace_controller_t::from(history)) binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    frontend_workspace_view_t flattened;
    explicit compose_test_fixture_t(song_document_t song = compose_test_song())
        : history(document_history_t::from(std::move(song))), binding(workspace_controller_t::from(history)) {
        expect_compose_test(binding.value.has_value());
        binding.value->active_tab = workspace_tab_t::compose;
        binding.value->selected_chart = chart_id_t{1};
    }
    void project() {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *binding.value, cache, 1280, 980,
                              interaction, application, nullptr);
        CAPTURE(flattened.projection_feedback.size(), cache.frame.projection_error,
                flattened.view.active_tab, history.document.charts.front().measures.size(),
                interaction.has_value());
        for (const auto& feedback : flattened.projection_feedback)
            UNSCOPED_INFO(feedback.message);
        expect_compose_test(flattened.view.compose.scene_available);
    }
};

void dispatch_t437_quantize_settings(compose_test_fixture_t& fixture,
    quantization_tie_t tie, quantization_hold_t holds, std::uint64_t divisions = 4,
    quantization_space_t space = quantization_space_t::standard_measure,
    bool apply = false) {
    notification_center_t center;
    notification_sink_t sink{center};
    task_scheduler_t scheduler{1, 1};
    runtime::resource_copy_t resource_copy{scheduler, sink};
    runtime::output_group_export_t export_runtime{sink};
    wormedit::ui::workspace_action_t action{};
    action.compose_action = apply ? WORM_UI_COMPOSE_ACTION_AREA_QUANTIZE
                                  : WORM_UI_COMPOSE_ACTION_SET_QUANTIZE_SETTINGS;
    action.compose_input_project_id.value = fixture.history.document.id.value;
    action.compose_input_chart_id.value = fixture.history.document.charts.front().id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_quantize_divisions = divisions;
    action.compose_quantize_space = space;
    action.compose_quantize_tie = tie;
    action.compose_quantize_holds = holds;
    (void)dispatch_compose_view_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, fixture.flattened.view.compose.viewport_height_pixels,
        fixture.application, {}, scheduler, resource_copy, export_runtime, false, sink);
    fixture.application.project.quantize.quantize_editor->preview_enabled = true;
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

auto bga_virtual_preview_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.bga_events = {{.id = object_id_t{500},
        .position = {measure_id_t{1}, 120}, .resource = resource_id_t{501}}};
    song.resources = {{.id = resource_id_t{501}, .name = "preview image",
        .asset = asset_id_t{601}, .roles = {resource_role_t::bga}}};
    song.visual_assets = {{.id = asset_id_t{601}, .kind = asset_kind_t::image}};
    return song;
}

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
}

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

TEST_CASE("Frame timing child phases accumulate into comparable parent totals", "[sys][frame-timing]") {
    frame_timing_t timer;
    timer.begin_frame();
    {
        frame_phase_scope_t workspace(frame_phase_t::workspace, timer);
        SDL_Delay(2);
        frame_phase_scope_t flatten(frame_phase_t::workspace_flatten, timer);
        SDL_Delay(2);
    }
    timer.end_frame();
    CHECK(timer.summary(frame_phase_t::workspace_flatten).count == 1);
    CHECK(timer.summary(frame_phase_t::workspace).average_ms >=
          timer.summary(frame_phase_t::workspace_flatten).average_ms + 1.0);
    CHECK(frame_phase_parent(frame_phase_t::audio_pump) == frame_phase_t::audio);
    CHECK(frame_phase_parent(frame_phase_t::scene) == frame_phase_t::count);
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


#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("Waveform runtime reports decoder failures instead of silently losing the waveform",
          "[sys][waveform-projection]") {
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    notification_center_t center;
    notification_sink_t sink{center};
    runtime::waveform_analysis_t waveform(sources, nullptr, sink);
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{42};
    compose_scene_cache_t cache;
    song_document_t document;
    waveform.service(document, workspace, cache, nullptr);
    for (std::size_t attempt = 0; attempt < 200 && waveform.waveform_job().is_busy(); ++attempt) {
        SDL_Delay(5);
        waveform.service(document, workspace, cache, nullptr);
    }
    expect_compose_test(!waveform.waveform_job().is_busy());
    center.drain_inbox(std::chrono::seconds{1});
    const auto notifications = center.snapshot(10);
    expect_compose_test(notifications.size() == 1);
    expect_compose_test(notifications.front().message == "Waveform analysis failed: the audio source is unavailable.");
    expect_compose_test(notifications.front().severity == notification_severity_t::error);
}
namespace {
auto overview_analysis(float peak) -> audio::waveform_analysis_result_t {
    audio::waveform_t waveform;
    waveform.key = {{"fake:overview", "content:v1"}, 256, 0, 0};
    waveform.total_frames = 256 * 4096;
    waveform.bins.resize(4096);
    for (auto& bin : waveform.bins) {
        bin.minimum = {-peak, -peak};
        bin.maximum = {peak, peak};
        bin.absolute_peak = {peak, peak};
        bin.frame_count = 256;
    }
    return {.waveform = std::move(waveform)};
}
void check_overview_replacement(frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
                                const editor_interaction_state_t& interaction) {
    auto replacement = overview_analysis(0.75f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
    CHECK(flattened.view.compose.waveform_overview_rows.front().absolute_peak == 0.75f);
    auto scene_binding = cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
}
}
TEST_CASE("Compose overview keeps cached rows while moving its visible range", "[sys][waveform-projection]") {
    auto history = document_history_t::from(dense_song());
    compose_scene_cache_t cache;
    auto timing = timing_index_t::from(history.document.charts.front());
    REQUIRE(timing.value);
    cache.timing.marker_timing_index = std::move(timing.value);
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 200;
    flattened.view.compose.pixels_per_standard_measure = 200;
    editor_interaction_state_t interaction;
    auto analysis = overview_analysis(0.25f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    const auto fraction = flattened.view.compose.waveform_overview_visible_begin_fraction;
    const auto* rows = cache.waveform.overview.value().rows.data();
    interaction.viewport_scroll_pixels = 200;
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    CHECK(cache.waveform.overview.value().rows.data() == rows);
    CHECK(flattened.view.compose.waveform_overview_visible_begin_fraction > fraction);
    check_overview_replacement(flattened, cache, interaction);
    flattened.view.compose.pixels_per_standard_measure = 0;
    CHECK_FALSE(populate_compose_waveform_projection(flattened, cache, &analysis, false,
                                                      true, &interaction));
}
TEST_CASE("Compose waveform remains visible in a padded one-measure viewport", "[sys][waveform-projection]") {
    auto song = dense_song();
    song.charts.front().measures.resize(1);
    compose_scene_cache_t cache;
    cache.timing.marker_timing_index = timing_index_t::from(song.charts.front()).value;
    REQUIRE(cache.timing.marker_timing_index);
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 800;
    flattened.view.compose.pixels_per_standard_measure = 192;
    editor_interaction_state_t interaction;
    interaction.viewport_scroll_pixels = -160;
    auto analysis = overview_analysis(0.5f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false,
                                                true, &interaction));
    REQUIRE(flattened.view.compose.waveform_rows.size() > 0);
    CHECK(flattened.view.compose.waveform_rows.front().y_pixels >= 160);
    CHECK(flattened.view.compose.waveform_rows.front().absolute_peak[0] == 0.5f);
}
namespace { void expect_gpu_work(bool value); }
TEST_CASE("Compose envelope projects only exposed stable world rows during follow", "[sys][waveform-projection]") {
    auto history = document_history_t::from(dense_song());
    compose_scene_cache_t cache;
    cache.timing.marker_timing_index = timing_index_t::from(history.document.charts.front()).value;
    expect_gpu_work(cache.timing.marker_timing_index.has_value());
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 200;
    flattened.view.compose.pixels_per_standard_measure = 200;
    editor_interaction_state_t interaction;
    interaction.viewport_scroll_pixels = 10;
    auto analysis = overview_analysis(.25f);
    audio::decoded_block_cache_t blocks;
    const audio::aligned_waveform_style_t style{};
    const auto project = [&] {
        flattened.view.compose.waveform_rows.clear();
        return populate_compose_waveform_projection(flattened, cache, &analysis, false, true,
                                                    &interaction, nullptr, &blocks, &style);
    };
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projection_calls == 1);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().reused_rows == 202);
    interaction.viewport_scroll_pixels = 11.25;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 1);
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().reused_rows == 201);
    const audio::audio_block_key_t key{{"unrelated", "immutable"}, 0};
    std::array<float, 16> pcm{};
    expect_gpu_work(blocks.store(key, 8, pcm).has_value());
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 1);
    interaction.viewport_scroll_pixels = 2000;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    auto scene_binding = cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
}
namespace {
struct envelope_projection_fixture_t {
    document_history_t history{document_history_t::from(dense_song())};
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis{overview_analysis(.25f)};
    audio::decoded_block_cache_t blocks;
    audio::aligned_waveform_style_t style;
    envelope_projection_fixture_t() {
        static_cast<void>(cache.scene.index.get_or_build(compose_scene_source_key_t{
            .stamp = {.document = {.revision = history.document.revision},
                      .chart = history.document.charts.front().id}}, retained_scene_fixture_t{}));
        cache.timing.marker_timing_index = timing_index_t::from(history.document.charts.front()).value;
        expect_gpu_work(cache.timing.marker_timing_index.has_value());
        auto& view = flattened.view.compose;
        view.visible = view.scene_available = true;
        view.project_id.value = history.document.id.value;
        view.viewport_height_pixels = view.pixels_per_standard_measure = 200;
        interaction.viewport_scroll_pixels = 10;
        for (std::size_t index = 0; index < analysis.waveform->bins.size(); ++index) {
            auto& bin = analysis.waveform->bins[index];
            const auto value = static_cast<float>(index + 1) / analysis.waveform->bins.size();
            bin.absolute_peak = {value, value};
            bin.rms = {value * .5f, value * .5f};
        }
    }
    void project() {
        expect_gpu_work(populate_compose_waveform_projection(flattened, cache, &analysis, false,
            true, &interaction, nullptr, &blocks, &style));
    }
};
void expect_published_waveform_rows(std::span<const wormedit::ui::compose_waveform_row_view_t> rows, double height) {
    for (const auto& row : rows)
        expect_gpu_work(std::isfinite(row.y_pixels) && row.y_pixels >= 0 && row.y_pixels <= height);
}
void compare_cached_envelope_source(envelope_projection_fixture_t& fixture) {
    const auto height = fixture.flattened.view.compose.viewport_height_pixels;
    const auto scroll = std::floor(fixture.interaction.viewport_scroll_pixels);
    const auto reference = audio::aligned_waveform_view_result_t::from(*fixture.analysis.waveform,
        nullptr, *fixture.cache.timing.marker_timing_index,
        {.viewport_scroll_pixels = scroll - 1, .viewport_height_pixels = height + 2,
         .pixels_per_standard_measure = fixture.flattened.view.compose.pixels_per_standard_measure},
        &fixture.blocks, fixture.style);
    expect_gpu_work(static_cast<bool>((!reference.status.has_failure())));
    std::size_t index = 0;
    for (const auto& row : reference.rows) {
        const auto y = row.y_pixels - 1;
        if (y < 0 || y > height) continue;
        expect_gpu_work(index < fixture.flattened.view.compose.waveform_rows.size());
        const auto& cached = fixture.flattened.view.compose.waveform_rows[index++];
        expect_gpu_work(std::abs(cached.y_pixels - y) < 1e-8);
        expect_gpu_work(std::abs(cached.absolute_peak[0] - row.absolute_peak[0]) < 1e-6f);
        expect_gpu_work(std::abs(cached.rms[0] - row.rms[0]) < 1e-6f);
    }
    expect_gpu_work(index == fixture.flattened.view.compose.waveform_rows.size());
}
void check_waveform_axis(envelope_projection_fixture_t& fixture, bool sample_points) {
    auto& view = fixture.flattened.view.compose;
    view.downscroll = false;
    fixture.project();
    const auto& source = fixture.flattened.view.compose.waveform_rows;
    const std::vector<wormedit::ui::compose_waveform_row_view_t> upscroll(source.begin(), source.end());
    expect_gpu_work(!upscroll.empty());
    view.downscroll = true;
    fixture.project();
    const auto& downscroll = fixture.flattened.view.compose.waveform_rows;
    expect_gpu_work(downscroll.size() == upscroll.size());
    for (std::size_t index = 0; index < upscroll.size(); ++index) {
        const auto& before = upscroll[index];
        const auto& after = downscroll[index];
        expect_gpu_work(before.sample_point == sample_points && after.sample_point == sample_points);
        expect_gpu_work(std::ranges::equal(after.absolute_peak, before.absolute_peak) &&
                        std::ranges::equal(after.samples, before.samples));
        expect_gpu_work(std::abs(after.y_pixels + before.y_pixels - view.viewport_height_pixels) < 1e-8);
    }
}
void check_waveform_scroll(envelope_projection_fixture_t& fixture) {
    const auto before = fixture.flattened.view.compose.waveform_rows[80];
    const auto& view = fixture.flattened.view.compose;
    const auto pixels_per_row = view.pixels_per_standard_measure /
        fixture.cache.timing.marker_timing_index->divisions_per_standard_measure;
    const auto world_row = (fixture.interaction.viewport_scroll_pixels +
        view.viewport_height_pixels - before.y_pixels) / pixels_per_row;
    fixture.interaction.viewport_scroll_pixels += 25;
    fixture.project();
    const auto& after = fixture.flattened.view.compose.waveform_rows[55];
    expect_gpu_work(std::ranges::equal(after.absolute_peak, before.absolute_peak));
    expect_gpu_work(after.y_pixels == before.y_pixels + 25);
    const auto chart_y = compose_project_y(world_row, pixels_per_row,
        fixture.interaction.viewport_scroll_pixels, fixture.flattened.view.compose.viewport_height_pixels, true);
    expect_gpu_work(std::abs(chart_y - after.y_pixels) < 1e-8);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 25);
}
void prepare_waveform_sample_points(envelope_projection_fixture_t& fixture) {
    fixture.flattened.view.compose.pixels_per_standard_measure = 200'000;
    fixture.style = {.sample_points = true, .detail_level = true};
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm).has_value());
}
void prepare_seven_key_mp3_project(envelope_projection_fixture_t& fixture) {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "fixtures" / "audio" / "sliced-seek-source.mp3";
    audio::decoder_t decoder;
    expect_gpu_work(decoder.open(path));
    const auto info = decoder.info();
    expect_gpu_work(info.has_value());
    const auto song = make_audio_project_document(song_id_t{42}, path, "7K MP3", "", 7,
        {info->source_sample_rate, info->source_channel_count, info->source_frame_count, info->seekable});
    expect_gpu_work(song.has_value());
    fixture.history = document_history_t::from(*song);
    const auto& chart = fixture.history.document.charts.front();
    expect_gpu_work(chart.lane_layouts.front().layout.lanes.size() == 7);
    fixture.cache = {};
    static_cast<void>(fixture.cache.scene.index.get_or_build(compose_scene_source_key_t{
        .stamp = {.document = {.revision = fixture.history.document.revision}, .chart = chart.id}},
        retained_scene_fixture_t{}));
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    fixture.flattened.view.compose.downscroll = compose_field_downscroll(chart);
    expect_gpu_work(fixture.flattened.view.compose.downscroll);
    audio::waveform_cache_t waveforms;
    fixture.analysis = audio::waveform_analysis_result_t::from(
        audio::waveform_analysis_t::from(decoder, fixture.blocks, waveforms,
            {"fixture:7k-mp3", "immutable:v1"}, 256));
    expect_gpu_work(fixture.analysis.waveform.has_value());
}
}
TEST_CASE("MP3 project 7K waveform follows downscroll chart movement while reusing cached rows",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    prepare_seven_key_mp3_project(fixture);
    check_waveform_axis(fixture, false);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    check_waveform_scroll(fixture);
}
TEST_CASE("Compose waveform sample points follow the downscroll chart axis",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    prepare_waveform_sample_points(fixture);
    check_waveform_axis(fixture, true);
}
TEST_CASE("Incomplete envelope detail retries after PCM arrives and retains resolved rows",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 32;
    fixture.flattened.view.compose.pixels_per_standard_measure = 32000;
    fixture.style.detail_level = true;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 34);
    expect_gpu_work(std::ranges::any_of(fixture.cache.waveform.waveform_envelope_ring.value().rows,
        [](const auto& row) { return row.incomplete; }));
    const auto coarse_peak = fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0];
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm).has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 34);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projection_calls == 1);
    expect_gpu_work(std::ranges::all_of(fixture.cache.waveform.waveform_envelope_ring.value().rows,
        [](const auto& row) { return row.present && !row.incomplete; }));
    const auto& resolved = fixture.flattened.view.compose.waveform_rows.front();
    expect_gpu_work(std::abs(resolved.absolute_peak[0] - .8f) < .00001f);
    expect_gpu_work(std::abs(resolved.rms[0] - .8f) < .00001f);
    expect_gpu_work(resolved.absolute_peak[0] != coarse_peak);
    const audio::audio_block_key_t unrelated{{"elsewhere", "unchanged"}, 0};
    expect_gpu_work(fixture.blocks.store(unrelated, 16384, pcm).has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().reused_rows == 34);
    expect_gpu_work(std::abs(fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0] - .8f) < .00001f);
}
TEST_CASE("Compose envelope chart identity changes recompute actual source timing at unchanged revision",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.project();
    compare_cached_envelope_source(fixture);
    expect_gpu_work(!fixture.flattened.view.compose.waveform_rows.empty());
    const auto peak = fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0];
    auto second = fixture.history.document.charts.front();
    second.id = chart_id_t{2};
    second.bpms.front().beats_per_minute = {240, 1};
    auto scene_binding = fixture.cache.scene.index.key().value_or(compose_scene_source_key_t{});
    scene_binding.stamp.chart = second.id;
    static_cast<void>(fixture.cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    fixture.cache.timing.marker_timing_index = timing_index_t::from(second).value;
    expect_gpu_work(fixture.cache.timing.marker_timing_index.has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
    compare_cached_envelope_source(fixture);
    expect_gpu_work(fixture.flattened.view.compose.waveform_rows.front().absolute_peak[0] < peak);
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
    ++fixture.flattened.view.compose.project_id.value;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 202);
}
TEST_CASE("Compose envelope padding stays private at zero fractional and normal viewport scroll",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 532;
    for (const auto scroll : {0.0, .75, 10.0, 10.75, 100.25}) {
        fixture.interaction.viewport_scroll_pixels = scroll;
        fixture.project();
        expect_published_waveform_rows(fixture.flattened.view.compose.waveform_rows, 532);
        expect_gpu_work(fixture.flattened.view.compose.waveform_rows.size() <= 532);
    }
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_envelope_ring.value().projected_rows == 0);
}
namespace {
struct channel_projection_fixture_t : envelope_projection_fixture_t {
    workspace_controller_t workspace;
    wormedit::sys::project_audio_sources_t project_sources;
    wormedit::audio::decoded_source_registry_t& sources = project_sources.registry();
    std::optional<asset_id_t> asset{asset_id_t{7}};
    std::shared_ptr<const audio::waveform_analysis_result_t> channel_analysis;
    channel_projection_fixture_t() {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "fixtures" / "audio" / "keysound-c4.ogg";
        const auto added = sources.create_source({asset_id_t{7}, path, {"waveform:test", "immutable:v1"}});
        expect_gpu_work(static_cast<bool>(added));
        history.document.audio_assets.push_back({.id = asset_id_t{7}, .source_path = path,
            .stream = {added->source_sample_rate, 2, added->source_frame_count, true}});
        auto& waveform = *analysis.waveform;
        waveform.key.source = added->source;
        waveform.sample_rate = added->output_sample_rate;
        waveform.total_frames = added->output_frame_count;
        waveform.bins.resize((waveform.total_frames + 255) / 256);
        for (std::size_t index = 0; index < waveform.bins.size(); ++index) {
            auto& bin = waveform.bins[index];
            const auto peak = static_cast<float>(index + 1) / waveform.bins.size();
            bin.absolute_peak = {peak, peak};
            bin.rms = {peak * .5f, peak * .5f};
            bin.frame_count = std::min<std::uint64_t>(256, waveform.total_frames - index * 256);
        }
        channel_analysis = std::make_shared<audio::waveform_analysis_result_t>(analysis);
        auto& chart = history.document.charts.front();
        chart.source_family = format_family_t::bmson;
        chart.lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::arrow_left;
        chart.sound_channels = {
            {.id = object_id_t{71000}, .source_clip = {clip_id_t{72000}, asset_id_t{7}},
             .events = {{.id = object_id_t{73000}, .position = {measure_id_t{1}, 0},
                         .lane = lane_id_t{3}, .playable = false}}},
            {.id = object_id_t{71001}, .source_clip = {clip_id_t{72001}, asset_id_t{7}},
             .events = {{.id = object_id_t{73001}, .position = {measure_id_t{1}, 240},
                         .lane = lane_id_t{3}, .playable = false}}}};
        chart.bgm_source_lanes = {{.id = object_id_t{3}, .stem_channel = object_id_t{71000}},
                                  {.id = object_id_t{4}, .stem_channel = object_id_t{71001}}};
        chart.measures.front().bgm_events = {
            {.id = object_id_t{74000}, .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{3},
             .sound_channel_event = object_id_t{73000}},
            {.id = object_id_t{74001}, .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{4},
             .sound_channel_event = object_id_t{73001}}};
        workspace.selected_chart = chart.id;
        flattened.view.compose.selected_bmson_bgm_event_id.value = 74000;
        interaction.viewport_scroll_pixels = 0;
    }
    void project_channel() {
        cache.waveform.channel_projection.view.value().rows.clear();
        const bmson_waveform_projection_context_t context{flattened, history, workspace, &interaction,
            cache, retained_sources_for(history.document, project_sources), {asset, channel_analysis}, {asset, channel_analysis}, 1};
        populate_bmson_waveform_projection(context);
        expect_published_waveform_rows(cache.waveform.channel_projection.view.value().rows, flattened.view.compose.viewport_height_pixels);
    }
};
}
TEST_CASE("BMSON channel switches sharing an asset preserve independent timing and bounded padded rows",
          "[sys][waveform-projection][bmson]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    expect_gpu_work(!fixture.cache.waveform.channel_projection.view.value().rows.empty());
    const auto first = fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels;
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74001;
    fixture.project_channel();
    expect_gpu_work(std::get<2>(*fixture.cache.waveform.channel_projection.ring.key()).channel == 71001);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
    expect_gpu_work(!fixture.cache.waveform.channel_projection.view.value().rows.empty());
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels > first + 40);
    fixture.interaction.viewport_scroll_pixels = .75;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 0);
    fixture.interaction.viewport_scroll_pixels = 1.25;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 1);
    fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::bar;
    fixture.flattened.view.compose.downscroll = true;
    fixture.cache.waveform.channel_projection.view.invalidate();
    fixture.interaction.viewport_scroll_pixels = 0;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.front().y_pixels > 140);
}
namespace {
void check_swapped_stem_publication(const wormedit::ui::compose_view_t& view) {
    REQUIRE(view.stem_waveforms.size() == 1);
    const auto& swapped = view.stem_waveforms.front();
    CHECK(swapped.stem_key == 71000);
    CHECK(swapped.source_lane_id == object_id_t{3});
    CHECK(!swapped.rows.empty());
}
}
TEST_CASE("BMSON stems other than the selected one get their own lane rows or a pending band",
          "[sys][waveform-projection][bmson][stems]") {
    channel_projection_fixture_t fixture;
    std::shared_ptr<const audio::waveform_analysis_result_t> stem_result = fixture.channel_analysis;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources), {fixture.asset, fixture.channel_analysis},
        {fixture.asset, fixture.channel_analysis}, 1,
        [&](asset_id_t) { return stem_result; }};

    // Channel 71000 is selected through its marker, so only 71001 is a stem.
    populate_bmson_waveform_projection(context);
    CHECK(fixture.flattened.view.compose.stem_waveforms.size() == 1);
    const auto& stem = fixture.flattened.view.compose.stem_waveforms[0];
    CHECK(stem.source_lane_id == object_id_t{4});
    expect_gpu_work(stem.stem_key == 71001);
    CHECK((!stem.pending && stem.rows.size() > 0));
    CHECK(stem.rows.size() <= fixture.flattened.view.compose.viewport_height_pixels + 2);

    // Selecting the other marker swaps which stem is the extra one.
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74001;
    populate_bmson_waveform_projection(context);
    check_swapped_stem_publication(fixture.flattened.view.compose);
    // The selected channel keeps an entry too, for its field taps and cut lines.
    CHECK(fixture.cache.stems.stem_waveforms.size() == 2);
    CHECK(fixture.cache.stems.stem_waveforms.front().channel == object_id_t{71000});

    // Audio that has not finished decoding is a placeholder, never a blocked frame.
    stem_result.reset();
    const bmson_waveform_projection_context_t pending_context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources), {fixture.asset, nullptr},
        {std::nullopt, nullptr}, 1, [&](asset_id_t) { return stem_result; }};
    populate_bmson_waveform_projection(pending_context);
    CHECK(fixture.flattened.view.compose.stem_waveforms.size() == 1);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].pending);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveforms[0].stem_key == 71000);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].rows.size() == 0);
}

namespace {
auto project_stable_stem_keys(channel_projection_fixture_t& fixture, bool pending = false)
    -> std::vector<std::uint64_t> {
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 0;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {fixture.asset, fixture.channel_analysis}, {std::nullopt, nullptr}, 1,
        [&](asset_id_t) { return pending ? nullptr : fixture.channel_analysis; }};
    populate_bmson_waveform_projection(context);
    std::vector<std::uint64_t> keys;
    for (std::size_t i = 0; i < fixture.flattened.view.compose.stem_waveforms.size(); ++i) {
        const auto& view = fixture.flattened.view.compose.stem_waveforms[i];
        expect_gpu_work(view.pending == pending);
        expect_gpu_work((view.rows.size() == 0) == pending);
        keys.push_back(view.stem_key);
    }
    std::ranges::sort(keys);
    return keys;
}
}
TEST_CASE("BMSON stem keys retain channel identity when visible channel order changes",
          "[sys][waveform-projection][bmson][stem-key]") {
    channel_projection_fixture_t fixture;
    const auto before = project_stable_stem_keys(fixture);
    expect_gpu_work(before == std::vector<std::uint64_t>{71000, 71001});
    auto& chart = fixture.history.document.charts.front();
    std::ranges::reverse(chart.sound_channels);
    ++fixture.history.document.revision;
    expect_gpu_work(project_stable_stem_keys(fixture) == before);
}

namespace {
void populate_three_hundred_stems(channel_projection_fixture_t& fixture) {
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes.clear();
    chart.measures.front().bgm_events.clear();
    for (std::uint64_t index = 0; index < 300; ++index) {
        chart.sound_channels.push_back({.id = object_id_t{71000 + index},
            .source_clip = {clip_id_t{72000 + index}, asset_id_t{7}},
            .events = {{.id = object_id_t{73000 + index}, .position = {measure_id_t{1}, 0},
                        .lane = lane_id_t{3}, .playable = false}}});
        chart.bgm_source_lanes.push_back({.id = object_id_t{50000 + index},
            .source_order = index, .stem_channel = object_id_t{71000 + index}});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{74000 + index},
            .position = {measure_id_t{1}, 0}, .source_lane = object_id_t{50000 + index},
            .sound_channel_event = object_id_t{73000 + index}});
    }
    ++fixture.history.document.revision;
}
}
TEST_CASE("BMSON projection publishes all 300 visible stems with one stable piece per channel",
          "[sys][waveform-projection][bmson][stem-key][stem-300]") {
    channel_projection_fixture_t fixture;
    populate_three_hundred_stems(fixture);
    const auto before = project_stable_stem_keys(fixture);
    expect_gpu_work(before.size() == 300);
    expect_gpu_work(std::ranges::adjacent_find(before) == before.end());
    expect_gpu_work(before.front() == 71000 && before.back() == 71299);
    std::ranges::reverse(fixture.history.document.charts.front().sound_channels);
    ++fixture.history.document.revision;
    expect_gpu_work(project_stable_stem_keys(fixture) == before);
    expect_gpu_work(project_stable_stem_keys(fixture, true) == before);
}

namespace {
void register_prepared_stem_source(channel_projection_fixture_t& fixture,
                                  std::string_view filename, std::string_view version) {
    fixture.sources.clear();
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "fixtures" / "audio" / filename;
    REQUIRE(fixture.sources.create_source({*fixture.asset, path,
        {"prepared-span", std::string(version)}}));
}

void configure_prepared_stem_frame(channel_projection_fixture_t& fixture) {
    fixture.history.document.audio_assets.back().stream = {};
    register_prepared_stem_source(fixture, "sliced-seek-source.ogg", "long");
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels[0].events.front().position.measure = measure_id_t{1};
    chart.sound_channels[1].events.front().position.measure = measure_id_t{1};
    fixture.interaction.cursor = {measure_id_t{4}, 0};
    fixture.flattened.view.compose.pixels_per_standard_measure = 200;
    fixture.flattened.view.compose.viewport_height_pixels = 200;
}

struct prepared_stem_frame_t {
    std::vector<const sound_channel_t*> channels;
    std::vector<object_id_t> ids;
    std::vector<audio::decoded_source_binding_t> bindings;
    std::uint64_t selection_build_count{};
};

auto prepare_stem_frame(channel_projection_fixture_t& fixture) -> prepared_stem_frame_t {
    const auto bindings = fixture.sources.bindings();
    const auto& chart = fixture.history.document.charts.front();
    const auto prepared = indexed_visible_stem_channels(fixture.cache.stems.stem_index,
        fixture.history.document, chart, fixture.interaction.cursor.measure, 0,
        bindings, wormedit::sys::stem_geometry_indices_t{.timing = &*fixture.cache.timing.marker_timing_index});
    REQUIRE(prepared.size() == 2);
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->first == measure_id_t{4});
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->second == 0);
    return {prepared, {prepared[0]->id, prepared[1]->id},
            fixture.cache.stems.stem_index.key()->bindings, fixture.cache.stems.stem_index.value().selection.build_count()};
}

auto shorten_prepared_stem_source(channel_projection_fixture_t& fixture,
                                 const prepared_stem_frame_t& prepared)
    -> std::vector<audio::decoded_source_binding_t> {
    register_prepared_stem_source(fixture, "silence.ogg", "short");
    const auto bindings = fixture.sources.bindings();
    REQUIRE(bindings != prepared.bindings);
    REQUIRE(bindings.front().asset == *fixture.asset);
    REQUIRE(bindings.front().source_frame_count != prepared.bindings.front().source_frame_count);
    return bindings;
}

void consume_prepared_stem_frame(channel_projection_fixture_t& fixture,
                                const prepared_stem_frame_t& prepared) {
    const auto channels = std::span<const sound_channel_t* const>(prepared.channels);
    runtime::waveform_analysis_t runtime{fixture.sources};
    runtime.service_compose_stem_waveforms(fixture.history.document, fixture.workspace,
        fixture.cache, &fixture.interaction, channels);
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {fixture.asset, fixture.channel_analysis}, {fixture.asset, fixture.channel_analysis}, 1,
        [](asset_id_t) { return std::shared_ptr<const audio::waveform_analysis_result_t>{}; },
        channels};
    populate_bmson_waveform_projection(context);
}

void check_prepared_stem_channels(const channel_projection_fixture_t& fixture,
                                  const prepared_stem_frame_t& prepared) {
    REQUIRE(fixture.cache.stems.stem_waveforms.size() == prepared.ids.size());
    for (std::size_t i = 0; i < prepared.ids.size(); ++i)
        CHECK(fixture.cache.stems.stem_waveforms[i].channel == prepared.ids[i]);
}

void check_prepared_stem_cache(const channel_projection_fixture_t& fixture,
                               const prepared_stem_frame_t& prepared) {
    CHECK(fixture.cache.stems.stem_index.key()->bindings == prepared.bindings);
    CHECK(fixture.cache.stems.stem_index.value().selection.value().size() == prepared.ids.size());
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->first == measure_id_t{4});
    CHECK(fixture.cache.stems.stem_index.value().selection.key()->second == 0);
    CHECK(fixture.cache.stems.stem_index.value().selection.build_count() == prepared.selection_build_count);
}
} // namespace

TEST_CASE("prepared BMSON stem list stays authoritative across consumers and binding changes",
          "[sys][waveform-projection][bmson][stems][frame-timing]") {
    channel_projection_fixture_t fixture;
    configure_prepared_stem_frame(fixture);
    const auto prepared = prepare_stem_frame(fixture);
    const auto changed_bindings = shorten_prepared_stem_source(fixture, prepared);
    consume_prepared_stem_frame(fixture, prepared);
    check_prepared_stem_channels(fixture, prepared);
    check_prepared_stem_cache(fixture, prepared);

    stem_index_t next_frame_index;
    const auto& chart = fixture.history.document.charts.front();
    const auto next_frame = indexed_visible_stem_channels(next_frame_index,
        fixture.history.document, chart, fixture.interaction.cursor.measure, 0,
        changed_bindings, wormedit::sys::stem_geometry_indices_t{.timing = &*fixture.cache.timing.marker_timing_index});
    CHECK(next_frame.empty());
}

namespace {
auto reference_frame(const timing_index_t& timing, long double row, bool end,
                     const audio::waveform_t& waveform) -> std::uint64_t {
    const auto integer = static_cast<std::uint64_t>(std::floor(row));
    const auto fraction = row - integer;
    const auto first = timing.seconds_at(integer,
        fraction == 0 && !end ? timing_boundary_side_t::before_delays
                             : timing_boundary_side_t::after_stops);
    auto seconds = static_cast<long double>(first.seconds->numerator) / first.seconds->denominator;
    if (fraction != 0) {
        const auto next = timing.seconds_at(integer + 1, timing_boundary_side_t::before_delays);
        seconds += (static_cast<long double>(next.seconds->numerator) / next.seconds->denominator - seconds) * fraction;
    }
    return static_cast<std::uint64_t>(std::clamp(end ? std::ceil(seconds * waveform.sample_rate)
                                                   : std::floor(seconds * waveform.sample_rate),
                                               0.0L, static_cast<long double>(waveform.total_frames)));
}
auto discontinuous_timing_fixture() -> timing_index_t {
    auto chart = dense_song().charts.front();
    chart.pauses = {{.id = object_id_t{60000}, .position = {measure_id_t{1}, 240},
                     .duration = {1, 3}},
                    {.id = object_id_t{60001}, .position = {measure_id_t{1}, 480},
                     .kind = pause_kind_t::delay, .duration = {1, 7}}};
    chart.bpms.push_back({.id = object_id_t{60002}, .position = {measure_id_t{1}, 600},
                         .beats_per_minute = {137, 1}});
    chart.warps = {{.id = object_id_t{60003},
                    .span = {{measure_id_t{1}, 720}, {measure_id_t{2}, 120}}}};
    return *timing_index_t::from(chart).value;
}
void check_clock_projection(const audio::waveform_t& waveform, const timing_index_t& timing,
                            double scroll, bool descending = false) {
    audio::aligned_waveform_request_t request{.viewport_scroll_pixels = scroll,
        .viewport_height_pixels = 64, .pixels_per_standard_measure = 192};
    const auto result = audio::aligned_waveform_view_result_t::from(waveform, nullptr, timing,
                                                               request, nullptr, {});
    REQUIRE(!result.status.has_failure());
    for (const auto& row : result.rows) {
        const auto pixel = std::floor(row.y_pixels);
        const auto begin = reference_frame(timing, (scroll + pixel) * 5, false, waveform);
        const auto end = reference_frame(timing, (scroll + pixel + 1) * 5, true, waveform);
        REQUIRE(end > begin);
        const auto peak_bin = (descending ? begin : end - 1) / waveform.key.frames_per_bin;
        CHECK(row.absolute_peak[0] == Catch::Approx(waveform.bins[peak_bin].absolute_peak[0]).margin(0.000001));
    }
}
}
TEST_CASE("Affine waveform clock preserves BPM pause warp boundaries during600 playback frames", "[sys][waveform-projection]") {
    const auto timing = discontinuous_timing_fixture();
    auto analysis = overview_analysis(0.5f);
    auto& waveform = *analysis.waveform;
    for (std::size_t index = 0; index < waveform.bins.size(); ++index)
        waveform.bins[index].absolute_peak = {static_cast<float>(index + 1) / waveform.bins.size(), 0};
    for (unsigned frame = 0; frame < 600; ++frame)
        check_clock_projection(waveform, timing, frame * 0.75);
    for (std::size_t index = 0; index < waveform.bins.size(); ++index)
        waveform.bins[index].absolute_peak = {1.0f - static_cast<float>(index) / waveform.bins.size(), 0};
    for (unsigned frame = 0; frame < 600; ++frame)
        check_clock_projection(waveform, timing, frame * 0.75, true);
}
#endif

namespace {
void populate_measure_cache_frame(document_history_t& history, workspace_controller_t& workspace,
                                  compose_scene_cache_t& cache,
                                  std::optional<editor_interaction_state_t>& interaction) {
    frontend_workspace_view_t flattened;
    flattened.view.compose.visible = true;
    application_command_state_t application;
    populate_compose_view(flattened, history, workspace, cache, 1280, 980, interaction,
                          application, nullptr);
    INFO(flattened.projection_feedback.size());
    REQUIRE(flattened.view.compose.scene_available);
}
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

namespace {
void expect_gpu_work(bool value) { REQUIRE(value); }
}

TEST_CASE("GPU workload keeps unique frame counters and nearest rank statistics", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    for (std::uint64_t frame = 1; frame <= 300; ++frame) {
        frame_timing_sample_t sample;
        sample.gpu_work = {{frame, frame * 3, 4, 3, 2}, true};
        timer.record(sample);
    }
    expect_gpu_work(timer.size() == 240);
    expect_gpu_work(timer.sample(0).gpu_work.values[0] == 61);
    expect_gpu_work(timer.gpu_work_summary(0).count == 240);
    expect_gpu_work(timer.gpu_work_summary(0).average == 180.5);
    expect_gpu_work(timer.gpu_work_summary(0).p99 == 298);
    expect_gpu_work(timer.gpu_work_summary(1).p99 == 894);
    expect_gpu_work(timer.gpu_work_summary(2).average == 4);
    expect_gpu_work(timer.gpu_work_summary(frame_gpu_work_count).count == 0);
}

TEST_CASE("GPU workload distinguishes unrendered frames from measured empty frames", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.record_gpu_work({{100, 300, 2, 1, 0}, true});
    timer.end_frame();
    timer.begin_frame();
    timer.end_frame();
    expect_gpu_work(!timer.sample(1).gpu_work.measured);
    expect_gpu_work(timer.gpu_work_summary(0).count == 1);
    expect_gpu_work(timer.gpu_work_summary(0).average == 100);
    timer.begin_frame();
    timer.record_gpu_work({{}, true});
    timer.end_frame();
    expect_gpu_work(timer.gpu_work_summary(0).count == 2);
    expect_gpu_work(timer.gpu_work_summary(0).average == 50);
    timer.record_gpu_work({{999, 999, 999, 999, 999}, true});
    expect_gpu_work(timer.sample(2).gpu_work.values[0] == 0);
}

TEST_CASE("Diagnostic GPU work is recorded and summarized apart from application work", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.record_gpu_work({{100, 300, 5, 4, 3}, true});
    timer.record_gpu_work({{40, 60, 2, 1, 1}, true}, frame_gpu_work_source_t::diagnostic);
    timer.end_frame();
    timer.begin_frame();
    timer.record_gpu_work({{200, 500, 7, 6, 5}, true});
    timer.end_frame();
    const auto& first = timer.sample(0);
    expect_gpu_work(first.gpu_work.values[0] == 100 && first.diagnostic_gpu_work.values[0] == 40);
    expect_gpu_work(!timer.sample(1).diagnostic_gpu_work.measured);
    expect_gpu_work(&frame_timing_t::gpu_work(first, frame_gpu_work_source_t::diagnostic) ==
                    &first.diagnostic_gpu_work);
    expect_gpu_work(timer.gpu_work_summary(0).count == 2);
    expect_gpu_work(timer.gpu_work_summary(0).average == 150);
    const auto diagnostic = timer.gpu_work_summary(2, frame_gpu_work_source_t::diagnostic);
    expect_gpu_work(diagnostic.count == 1 && diagnostic.average == 2 && diagnostic.p99 == 2);
}

TEST_CASE("Frame timing view exposes application and diagnostic GPU work separately", "[sys][frame-timing][gpu-work]") {
    auto& timer = frame_timing();
    timer.begin_frame();
    worm_ui_frame_timing_record_gpu_work({1000, 3000, 50, 20, 10});
    worm_ui_frame_timing_record_diagnostic_gpu_work({400, 700, 3, 2, 1});
    timer.end_frame();
    wormedit::ui::frame_timing_view_t view;
    worm_ui_frame_timing_view(view);
    expect_gpu_work(view.gpu_work_available && view.diagnostic_gpu_work_available);
    expect_gpu_work(view.gpu_work_latest.vertices == 1000 && view.gpu_work_latest.draw_calls == 50);
    expect_gpu_work(view.diagnostic_gpu_work_latest.vertices == 400);
    expect_gpu_work(view.diagnostic_gpu_work_latest.texture_switches == 1);
    expect_gpu_work(view.diagnostic_gpu_work[0].count >= 1);
    timer.begin_frame();
    worm_ui_frame_timing_record_gpu_work({1000, 3000, 50, 20, 10});
    timer.end_frame();
    worm_ui_frame_timing_view(view);
    expect_gpu_work(view.gpu_work_available && !view.diagnostic_gpu_work_available);
}

namespace {
template<class Actual, class Expected>
void check_heap_counter(const Actual& actual, const Expected& expected,
                        std::source_location location = std::source_location::current()) {
    INFO("heap assertion at " << location.file_name() << ':' << location.line()
         << ", actual=" << actual << ", expected=" << expected);
    CHECK(actual == expected);
}
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
    action.set_sound_resources(ids);
    const auto* storage = action.compose_sound_resource_ids.data();
    const auto before = frame_heap_allocation_snapshot();
    action.set_sound_resources(ids);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    CHECK(action.compose_sound_resource_ids.data() == storage);
    CHECK(action.compose_sound_resource_ids ==
        std::vector<resource_id_t>{resource_id_t{7}, resource_id_t{11}, resource_id_t{19}});
}

TEST_CASE("Workspace action copies own branch text and measure positions", "[sys][frontend-actions]") {
    auto copy = [] {
        ui::workspace_action_t source{};
        source.compose_branch_statement_text = "#BPM 140";
        source.compose_measure_ids = {0, 4, 9};
        auto result = source;
        source.compose_branch_statement_text.assign(128, 'x');
        source.compose_measure_ids.clear();
        return result;
    }();
    CHECK(copy.compose_branch_statement_text == "#BPM 140");
    CHECK(copy.compose_measure_ids == std::vector<std::uint64_t>{0, 4, 9});
}

TEST_CASE("Render convert submit and present child phases retain inclusive parent timing", "[sys][frame-timing][gpu-work]") {
    auto& timer = frame_timing();
    timer.begin_frame();
    {
        frame_phase_scope_t render(frame_phase_t::render);
        const auto submit = worm_ui_frame_render_phase_begin(WORM_UI_FRAME_RENDER_SUBMIT);
        const auto convert = worm_ui_frame_render_phase_begin(WORM_UI_FRAME_RENDER_CONVERT);
        worm_ui_frame_render_phase_end(convert);
        worm_ui_frame_render_phase_end(submit);
        frame_phase_scope_t present(frame_phase_t::render_present);
    }
    timer.end_frame();
    const auto& sample = timer.sample(timer.size() - 1);
    const auto at = [](frame_phase_t phase) { return static_cast<std::size_t>(phase); };
    expect_gpu_work(sample.measured[at(frame_phase_t::render_convert)]);
    expect_gpu_work(sample.measured[at(frame_phase_t::render_submit)]);
    expect_gpu_work(sample.measured[at(frame_phase_t::render_present)]);
    const auto children = sample.phase_ms[at(frame_phase_t::render_convert)] +
        sample.phase_ms[at(frame_phase_t::render_submit)] + sample.phase_ms[at(frame_phase_t::render_present)];
    expect_gpu_work(sample.phase_ms[at(frame_phase_t::render)] + 0.001 >= children);
    expect_gpu_work(frame_phase_parent(frame_phase_t::render_convert) == frame_phase_t::render);
}

TEST_CASE("Hidden GPU workload collection and summaries allocate no C++ storage", "[sys][frame-timing][gpu-work]") {
    frame_timing_t timer;
    timer.begin_frame();
    timer.end_frame();
    allocations = 0;
    count_allocations = true;
    for (std::uint64_t frame = 0; frame < frame_timing_capacity; ++frame) {
        timer.begin_frame();
        timer.record_gpu_work({{frame, frame * 3, 2, 1, 0}, true});
        {
            frame_phase_scope_t submit(frame_phase_t::render_submit, timer);
            frame_phase_scope_t convert(frame_phase_t::render_convert, timer);
        }
        timer.end_frame();
    }
    const auto summary = timer.gpu_work_summary(0);
    count_allocations = false;
    expect_gpu_work(allocations == 0);
    expect_gpu_work(summary.count == frame_timing_capacity);
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
    const auto step = chart.divisions_per_standard_measure / expected.active_snap_denominator;
    const auto snapped_row = static_cast<std::uint64_t>(std::floor(bounded / step)) * step;
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
}
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
void expect_static_style(bool condition,
                         std::source_location source=std::source_location::current()) {
    INFO("static style expectation at " << source.line());
    REQUIRE(condition);
}
auto static_style_song() -> song_document_t {
    auto song=dense_song();
    auto& chart=song.charts.front();
    chart.measures.resize(1);
    chart.measures.front().notes.resize(8);
    chart.semantic_layers={
        {.id=layer_id_t{101},.name="First",.color={99,120,140,255}},
        {.id=layer_id_t{102},.name="Second",.color={150,100,130,255}}};
    for (std::size_t index=0; index<chart.measures.front().notes.size(); ++index)
        chart.measures.front().notes[index].semantic_layers={layer_id_t{101+index%2}};
    return song;
}
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
}
TEST_CASE("Compose static style generation follows exact semantic filters without invalidating scroll",
          "[sys][field-tile-style]") {
    static_style_fixture_t fixture;
    const auto initial=fixture.project();
    expect_static_style(fixture.project()==initial);
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{101};
    fixture.application.project.semantic_layer.semantic_layer_solo_active=true;
    const auto first=fixture.project();
    const auto first_note=fixture.first_note();
    expect_static_style(first>initial);
    fixture.application.project.semantic_layer.semantic_layer_target=layer_id_t{102};
    const auto second=fixture.project();
    expect_static_style(second>first);
    expect_static_style(fixture.first_note()!=first_note);
    fixture.application.project.semantic_layer.semantic_layer_solo_active=false;
    fixture.application.project.semantic_layer.required_semantic_layer_filters={layer_id_t{101}};
    const auto required=fixture.project();
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
           narrow.bgm_pane_width_pixels > 0.0;
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
auto scalar_reuse_song() -> song_document_t {
    auto song = dense_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.profile_id = "stepmania:dance-single";
    auto& hold = chart.measures.front().notes.front();
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{8}, 240};
    chart.bgm_source_lanes = {{.id = object_id_t{70000}, .name = "Retained BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{70001},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{70000}}};
    return song;
}
void compare_scalar_bounds(wormedit::ui::compose_rect_t actual, wormedit::ui::compose_rect_t expected) {
    expect_static_style(std::abs(actual.left - expected.left) < 1e-8);
    expect_static_style(std::abs(actual.top - expected.top) < 1e-8);
    expect_static_style(std::abs(actual.right - expected.right) < 1e-8);
    expect_static_style(std::abs(actual.bottom - expected.bottom) < 1e-8);
}
void compare_scalar_notes(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.note_endpoints.size() == expected.note_endpoints.size());
    for (std::size_t index = 0; index < actual.note_endpoints.size(); ++index) {
        const auto& note = actual.note_endpoints[index];
        const auto& fresh = expected.note_endpoints[index];
        compare_scalar_bounds(note.bounds, fresh.bounds);
        expect_static_style(std::tie(note.note_id.value, note.lane_id.value, note.display_lane_number,
                                     note.glyph, note.snap_class, note.kind, note.tail,
                                     note.selected, note.semantic_color_visible) ==
                            std::tie(fresh.note_id.value, fresh.lane_id.value, fresh.display_lane_number,
                                     fresh.glyph, fresh.snap_class, fresh.kind, fresh.tail,
                                     fresh.selected, fresh.semantic_color_visible));
        expect_static_style(std::string_view(note.sound_label) ==
                            std::string_view(fresh.sound_label));
        expect_static_style(std::string_view(note.sound_detail) ==
                            std::string_view(fresh.sound_detail));
    }
}
void compare_scalar_holds_bgm(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.hold_bodies.size() == expected.hold_bodies.size());
    for (std::size_t index = 0; index < actual.hold_bodies.size(); ++index) {
        compare_scalar_bounds(actual.hold_bodies[index].bounds, expected.hold_bodies[index].bounds);
        expect_static_style(actual.hold_bodies[index].note_id.value ==
                            expected.hold_bodies[index].note_id.value);
        expect_static_style(actual.hold_bodies[index].selected ==
                            expected.hold_bodies[index].selected);
    }
    expect_static_style(actual.bgm_events.size() == expected.bgm_events.size());
    for (std::size_t index = 0; index < actual.bgm_events.size(); ++index) {
        compare_scalar_bounds(actual.bgm_events[index].bounds, expected.bgm_events[index].bounds);
        expect_static_style(actual.bgm_events[index].event_id ==
                            expected.bgm_events[index].event_id);
    }
}
void compare_scalar_grid(const wormedit::ui::compose_view_t& actual, const wormedit::ui::compose_view_t& expected) {
    expect_static_style(actual.grid_lines.size() == expected.grid_lines.size());
    for (std::size_t index = 0; index < actual.grid_lines.size(); ++index) {
        const auto& line = actual.grid_lines[index];
        const auto& fresh = expected.grid_lines[index];
        expect_static_style(std::abs(line.y_pixels - fresh.y_pixels) < 1e-8);
        expect_static_style(
            std::tie(line.kind, line.emphasis, line.measure_number, line.chart_row) ==
            std::tie(fresh.kind, fresh.emphasis, fresh.measure_number, fresh.chart_row));
    }
}
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

namespace {
auto paste_preview_song() -> song_document_t {
    auto song = compose_test_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes.resize(1);
    notes.front().position.row = 0;
    return song;
}
void copy_preview_note(compose_test_fixture_t& fixture) {
    fixture.project();
    fixture.interaction->selected_notes = {fixture.history.document.charts.front().measures.front().notes.front().id};
    auto copied = chart_clipboard_t::from(fixture.history.document, *fixture.interaction,
                                      fixture.history.document.revision);
    REQUIRE(copied);
    fixture.application.project.clipboard.chart_clipboard = std::move(*copied);
}
}
TEST_CASE("Paste preview owns geometry across frames and publishes one undoable command",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 240;
    std::string status;
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history,
                               *fixture.interaction, status)));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.document.charts.front().measures.front().notes.size() == 1));
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_active));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts.size() == 1));
    const auto first = fixture.flattened.view.compose.paste_ghosts[0].bounds;
    fixture.flattened = {};
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts[0].bounds.top == first.top));
    expect_compose_test(static_cast<bool>(apply_compose_paste(fixture.application, fixture.history,
                                 *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.paste_session));
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.chart_clipboard.has_value()));
    expect_compose_test(static_cast<bool>(fixture.history.undo.size() == 1));
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(static_cast<bool>(notes.size() == 2));
    expect_compose_test(static_cast<bool>(notes.back().position.row == 240));
    expect_compose_test(static_cast<bool>(notes.back().id != notes.front().id));
    expect_compose_test(static_cast<bool>(undo_document_command(fixture.history)));
    expect_compose_test(static_cast<bool>(fixture.history.document.charts.front().measures.front().notes.size() == 1));
}
TEST_CASE("Paste collision remains visible and stale previews cannot publish",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history,
                               *fixture.interaction, status)));
    fixture.project();
    expect_compose_test(!static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts.size() == 1));
    expect_compose_test(!static_cast<bool>(apply_compose_paste(fixture.application, fixture.history,
                                     *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(status.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    fixture.interaction->cursor.row = 240;
    expect_compose_test(static_cast<bool>(revise_compose_paste(fixture.application, fixture.history,
                                fixture.interaction->chart, fixture.interaction->cursor)));
    expect_compose_test(static_cast<bool>(fixture.application.project.clipboard.paste_session->proposal.value.has_value()));
    ++fixture.history.document.revision;
    expect_compose_test(!static_cast<bool>(apply_compose_paste(fixture.application, fixture.history,
                                     *fixture.interaction, status)));
    wormedit::test::check_stale_action_message(status, "Paste preview");
    expect_compose_test(!static_cast<bool>(fixture.application.project.clipboard.paste_session));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace {
auto paste_pointer_action(compose_test_fixture_t& fixture, bool commit, std::uint64_t row = 240) -> wormedit::ui::workspace_action_t {
    const auto& chart = fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.history, *fixture.interaction);
    const auto* view = std::get_if<editor_interaction_view_model_t>(&inspected);
    expect_compose_test(view != nullptr);
    const auto y = editor_position_y_pixels(chart, *view, {chart.measures.front().id, row});
    expect_compose_test(y.has_value());
    // Use an interior point in the first snap cell, avoiding a chart-boundary
    // cancellation error when the downscroll screen transform is reversed.
    const bool downscroll = compose_field_downscroll(chart, fixture.application.preferences.editor_preferences);
    const auto screen_y = compose_source_y(*y + (row == 0 ? 0.5 : 0.0),
                                           view->viewport_height_pixels, downscroll);
    const auto raw = editor_position_at_y_pixels(chart, *view,
        compose_source_y(screen_y, view->viewport_height_pixels, downscroll));
    const auto snapped = raw ? resolve_resource_placement_position(chart, *raw,
        active_snap_grid(*fixture.interaction)) : std::nullopt;
    expect_compose_test(snapped.has_value());
    expect_compose_test(snapped->row == row);
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = chart.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = screen_y};
    action.compose_inputs.resize(1);
    if (commit) {
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{2}));
        action.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT};
        action.compose_inputs.resize(2);
    }
    return action;
}
auto dispatch_paste_action(compose_test_fixture_t& fixture, wormedit::ui::workspace_action_t& action) -> std::string {
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
auto dispatch_paste_pointer(compose_test_fixture_t& fixture, bool commit) -> std::string {
    auto action = paste_pointer_action(fixture, commit);
    return dispatch_paste_action(fixture, action);
}
}
namespace {
auto kiai_test_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto measure = chart.measures.front().id;
    chart.kiai = {{.id = object_id_t{9001}, .position = {measure, 240}, .enabled = true,
                   .provenance = {.source_order = 1}},
                  {.id = object_id_t{9002}, .position = {measure, 720}, .enabled = false,
                   .provenance = {.source_order = 2}}};
    return song;
}
auto kiai_screen_y(compose_test_fixture_t& fixture, std::uint64_t row) -> double {
    const auto& chart = fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.history, *fixture.interaction);
    const auto* view = std::get_if<editor_interaction_view_model_t>(&inspected);
    expect_compose_test(view != nullptr);
    const auto y = editor_position_y_pixels(chart, *view, {chart.measures.front().id, row});
    expect_compose_test(y.has_value());
    const bool downscroll = compose_field_downscroll(chart, fixture.application.preferences.editor_preferences);
    return compose_source_y(*y, view->viewport_height_pixels, downscroll);
}
auto invoke_compose_registered_command(compose_test_fixture_t& fixture,
    std::string_view command_id = "edit.delete") -> command_dispatch_outcome_t {
    notification_center_t center;
    notification_sink_t sink{center};
    std::optional<timing_lane_pseudonote_outcome_t> timing;
    const auto registry = command_registry_t::from([&](const command_invocation_context_t& context) {
        const auto command = parse_application_command(context.command.value);
        if (!command) return dispatch_outcome_t{};
        return invoke_application_command(*command, fixture.history, *fixture.binding.value,
            fixture.interaction, timing, fixture.application, sink);
    });
    return invoke_command(registry, {std::string(command_id)}, {.workspace_active = true,
        .active_tab = workspace_tab_t::compose, .visible_tabs = {workspace_tab_t::compose}});
}

auto kiai_input(compose_test_fixture_t& fixture, WormUiComposeInputKind kind, std::uint64_t head,
                std::uint8_t edge = WORM_UI_SPAN_HANDLE_BEGIN, double y = 0.0,
                double anchor_y = 0.0) -> wormedit::ui::workspace_action_t {
    const auto& chart = fixture.history.document.charts.front();
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = chart.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
    action.compose_inputs[0] = {.kind = kind, .object_id = {head}, .timing_chart_id = {chart.id.value},
                                .timing_document_revision = fixture.history.document.revision,
                                .y_pixels = y, .span_edge = edge,
                                .span_anchor_y_pixels = anchor_y};
    action.compose_inputs.resize(1);
    return action;
}
auto kiai_areas(compose_test_fixture_t& fixture) -> std::vector<kiai_span_t> {
    const auto& chart = fixture.history.document.charts.front();
    const auto built = measure_index_t::from(chart);
    return kiai_spans_of(chart, built.value());
}
}
TEST_CASE("Captured BMSON draft edits and auditions reject stale revisions independently of outer input",
          "[sys][bmson-draft-stamp]") {
    const auto request = GENERATE(
        std::pair{WORM_UI_COMPOSE_INPUT_BMSON_REMOVE_DRAFT, std::string_view{"BMSON region edit"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BMSON_AUDITION_DRAFT, std::string_view{"BMSON region audition"}});
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.project();
    fixture.application.project.compose_gesture.bmson_marker_draft = application_command_state_t::bmson_marker_draft_proposal_t{
        .stamp = chart_stamp_t::from(fixture.history.document, fixture.interaction->chart),
        .payload = {.reference = object_id_t{42}, .markers = {bmson_sound_marker_insertion_t{}}}};
    ++fixture.application.project.compose_gesture.bmson_marker_draft->stamp.document.revision;
    auto action = kiai_input(fixture, request.first, 1);
    test::check_stale_action_message(dispatch_paste_action(fixture, action), request.second);
    expect_compose_test(fixture.history.undo.empty());
    expect_compose_test(fixture.application.project.compose_gesture.bmson_marker_draft.has_value());
}

TEST_CASE("Compose inner timing stamps reject stale selection and move requests",
          "[sys][timing-input-stamp]") {
    const auto request = GENERATE(
        std::pair{WORM_UI_COMPOSE_INPUT_TIMING_MARKER_SELECT, std::string_view{"Timing marker selection"}},
        std::pair{WORM_UI_COMPOSE_INPUT_TIMING_GROUP_MOVE, std::string_view{"Timing marker move"}},
        std::pair{WORM_UI_COMPOSE_INPUT_SPEED_SPAN_EDIT, std::string_view{"Speed edit"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BGA_EVENT_CLICK, std::string_view{"BGA event selection"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BGA_EVENT_MOVE, std::string_view{"BGA event move"}},
        std::pair{WORM_UI_COMPOSE_INPUT_BOOKMARK_MOVE, std::string_view{"Bookmark move"}});
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.project();
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{9001}}};
    auto action = kiai_input(fixture, request.first, 9001);
    ++action.compose_inputs[0].timing_document_revision;
    test::check_stale_action_message(dispatch_paste_action(fixture, action), request.second);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Kiai edge drags edit the area, Delete removes the selected area and stale input is rejected",
          "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.project.compose_view.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
    REQUIRE(kiai_areas(fixture).size() == 1);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                            kiai_screen_y(fixture, 480));
    stale.compose_inputs[0].timing_document_revision += 1;
    test::check_stale_action_message(dispatch_paste_action(fixture, stale), "Kiai edit");
    CHECK(fixture.history.undo.empty());

    fixture.application.project.timing_recipe.timing_locked = true;
    auto locked = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                             kiai_screen_y(fixture, 480));
    CHECK(dispatch_paste_action(fixture, locked) == "Timing is locked.");
    fixture.application.project.timing_recipe.timing_locked = false;

    auto tail_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                                kiai_screen_y(fixture, 480), kiai_screen_y(fixture, 720));
    CHECK(dispatch_paste_action(fixture, tail_drag) == "Kiai area edited.");
    auto areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 240);
    CHECK(areas[0].end_row == 480);
    CHECK(areas[0].head == object_id_t{9001});
    CHECK(fixture.history.undo.size() == 1);

    // Dragging the head past the fixed tail swaps the edges.
    fixture.project();
    auto head_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 0,
                                kiai_screen_y(fixture, 600), kiai_screen_y(fixture, 240));
    CHECK(dispatch_paste_action(fixture, head_drag) == "Kiai area edited.");
    areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 480);
    CHECK(areas[0].end_row == 600);

    fixture.project();
    auto select = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_SELECT, 9001);
    CHECK(dispatch_paste_action(fixture, select) == "Kiai area selected.");
    CHECK(fixture.application.project.timing_recipe.kiai_selected_head == 9001);
    CHECK(invoke_compose_registered_command(fixture).value.is_invoked());
    CHECK(kiai_areas(fixture).empty());
    CHECK(fixture.application.project.timing_recipe.kiai_selected_head == 0);
    CHECK(fixture.history.undo.size() == 3);
}

TEST_CASE("A Kiai creation drag adds a merged area and is one undo step", "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.project.compose_view.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
    auto create = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 0,
                             WORM_UI_SPAN_HANDLE_CREATE, kiai_screen_y(fixture, 120),
                             kiai_screen_y(fixture, 360));
    CHECK(dispatch_paste_action(fixture, create) == "Kiai area edited.");
    // 120..360 overlaps the existing 240..720 area: one merged area.
    const auto areas = kiai_areas(fixture);
    REQUIRE(areas.size() == 1);
    CHECK(areas[0].begin_row == 120);
    CHECK(areas[0].end_row == 720);
    CHECK(fixture.history.undo.size() == 1);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(kiai_areas(fixture).front().begin_row == 240);
}

namespace {
auto recipe_test_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    sv_recipe_t recipe{.id = recipe_id_t{7001},
                       .name = "rise",
                       .range = {{chart.measures.front().id, 240}, {chart.measures.front().id, 720}},
                       .output = sv_recipe_output_t::sv,
                       .curve = sv_recipe_curve_t::linear,
                       .parameters = {.value_from = {1, 1}, .value_to = {3, 1}}};
    recipe.sampling.fixed_point_count = 3;
    chart.sv_recipes.push_back(recipe);
    return song;
}
auto recipe_range_rows(compose_test_fixture_t& fixture) -> std::pair<std::uint64_t, std::uint64_t> {
    const auto& range = fixture.history.document.charts.front().sv_recipes.front().range;
    return {range.begin.row, range.end.row};
}
}
TEST_CASE("SV recipe inputs select, resize and move the span, run commands and reject stale input",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.recipe_areas.size() == 1);

    auto select = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, 7001);
    CHECK(dispatch_paste_action(fixture, select) == "SV recipe selected.");
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 7001);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].selected);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                            kiai_screen_y(fixture, 480));
    stale.compose_inputs[0].timing_document_revision += 1;
    test::check_stale_action_message(dispatch_paste_action(fixture, stale), "SV recipe edit");

    // End edge dragged to row 480; the span stays ordered when an edge crosses the other.
    auto end_drag = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                               kiai_screen_y(fixture, 480));
    end_drag.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 720);
    CHECK(dispatch_paste_action(fixture, end_drag) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{240, 480});
    CHECK(fixture.history.undo.size() == 1);

    fixture.project();
    auto cross = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 0,
                            kiai_screen_y(fixture, 720));
    cross.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 240);
    CHECK(dispatch_paste_action(fixture, cross) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{480, 720});

    // The body dragged by the pointer travel moves the whole span, keeping its length.
    fixture.project();
    auto body = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 2,
                           kiai_screen_y(fixture, 240));
    body.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 480);
    CHECK(dispatch_paste_action(fixture, body) == "SV recipe moved.");
    CHECK(recipe_range_rows(fixture) == std::pair<std::uint64_t, std::uint64_t>{240, 480});

    // A locked timeline rejects edits and commands without a revision.
    fixture.project();
    fixture.application.project.timing_recipe.timing_locked = true;
    const auto revision = fixture.history.document.revision;
    auto locked_edit = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                                  kiai_screen_y(fixture, 720));
    locked_edit.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 480);
    CHECK(dispatch_paste_action(fixture, locked_edit) != "SV recipe moved.");
    auto locked_command = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, locked_command) == "Timing is locked.");
    CHECK(fixture.history.document.revision == revision);
    fixture.application.project.timing_recipe.timing_locked = false;

    auto toggle = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, toggle) == "SV recipe disabled.");
    CHECK_FALSE(fixture.history.document.charts.front().sv_recipes.front().enabled);

    fixture.project();
    auto materialize = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 1);
    CHECK(dispatch_paste_action(fixture, materialize) == "SV recipe materialized into events.");
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK_FALSE(fixture.history.document.charts.front().scrolls.empty());
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 0);
    auto missing = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 2);
    CHECK(dispatch_paste_action(fixture, missing) == "The SV recipe no longer exists.");
}

TEST_CASE("Recipe duplication input publishes one adjacent copy and inspector entry",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    auto duplicate = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::duplicate));
    CHECK(dispatch_paste_action(fixture, duplicate) == "SV recipe duplicated.");
    REQUIRE(fixture.history.undo.size() == 1);
    fixture.project();
    const auto& entries = fixture.flattened.view.compose.recipe_entries;
    REQUIRE(entries.size() == 2);
    CHECK(entries[0].id == recipe_id_t{7001});
    CHECK(entries[1].id == fixture.history.document.charts.front().sv_recipes.back().id);
    test::check_stale_action_message(dispatch_paste_action(fixture, duplicate), "Compose input");
}

TEST_CASE("Recipe order inputs project stored order and undo restores it",
          "[sys][recipe-areas]") {
    auto song = recipe_test_song();
    auto second = song.charts.front().sv_recipes.front();
    second.id = recipe_id_t{7002};
    song.charts.front().sv_recipes.push_back(second);
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    auto earlier = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7002,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::earlier));
    CHECK(dispatch_paste_action(fixture, earlier) == "SV recipe reordered.");
    fixture.project();
    const auto& entries = fixture.flattened.view.compose.recipe_entries;
    CHECK(entries == std::vector<ui::compose_recipe_entry_view_t>{
        {.id = recipe_id_t{7002}, .label = "1. rise"}, {.id = recipe_id_t{7001}, .label = "2. rise"}});
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().sv_recipes.front().id == recipe_id_t{7001});
}

TEST_CASE("Recipe boundary and unknown inputs preserve the stored recipe list",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    auto boundary = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001,
        static_cast<std::uint8_t>(ui::compose_recipe_command_t::earlier));
    CHECK(dispatch_paste_action(fixture, boundary) == "SV recipe is already at the order boundary.");
    auto unknown = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 255);
    CHECK(dispatch_paste_action(fixture, unknown) == "Unknown SV recipe command.");
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Delete removes the selected SV recipe and the area menu adds one over the retained area",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    CHECK(invoke_compose_registered_command(fixture).value.is_invoked());
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK(fixture.application.project.timing_recipe.recipe_selected == 0);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().sv_recipes.size() == 1);

    fixture.project();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    fixture.interaction->retained_selection_range =
        position_span_t{{measure, 0}, {measure, 240}};
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE;
    action.compose_input_chart_id.value = fixture.history.document.charts.front().id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    const auto status = dispatch_compose_area_action(action, fixture.history, *fixture.interaction);
    REQUIRE(status.has_value());
    CHECK(*status == "SV recipe added over the selected area.");
    const auto& recipes = fixture.history.document.charts.front().sv_recipes;
    REQUIRE(recipes.size() == 2);
    CHECK(recipes.back().range.begin.row == 0);
    CHECK(recipes.back().range.end.row == 240);
    CHECK(recipes.back().curve == sv_recipe_curve_t::linear);

    action.compose_input_document_revision += 1;
    const auto stale = dispatch_compose_area_action(action, fixture.history, *fixture.interaction);
    REQUIRE(stale.has_value());
    CHECK(stale->find("ignored") != std::string::npos);
    CHECK(fixture.history.document.charts.front().sv_recipes.size() == 2);
}

TEST_CASE("Compose projects SPEEDS transitions as spans and names the transition on the marker",
          "[sys][speed-spans]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto measure = chart.measures.front().id;
    chart.speeds = {{.id = object_id_t{8001},
                     .position = {measure, 0},
                     .multiplier = {2, 1},
                     .transition_beats = {2, 1},
                     .provenance = {.source_order = 0}},
                    {.id = object_id_t{8002},
                     .position = {measure, 720},
                     .multiplier = {1, 1},
                     .transition_beats = {1, 2},
                     .transition_is_seconds = true,
                     .provenance = {.source_order = 1}}};
    compose_test_fixture_t fixture{song};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    REQUIRE(view.speed_spans.size() == 2);
    for (size_t index = 0; index < view.speed_spans.size(); ++index) {
        CHECK(view.speed_spans[index].bounds.bottom > view.speed_spans[index].bounds.top);
        CHECK(view.speed_spans[index].object_id.value != 0);
    }
    CHECK(view.speed_spans[0].multiplier == Catch::Approx(2.0));
    CHECK_FALSE(view.speed_spans[0].transition_is_seconds);
    CHECK(view.speed_spans[0].transition_length == Catch::Approx(2.0));
    CHECK(view.speed_spans[1].transition_is_seconds);
    bool named = false;
    for (size_t index = 0; index < view.timing_markers.size(); ++index)
        if (view.timing_markers[index].lane == wormedit::timing_lane_kind_t::speed &&
            std::string(view.timing_markers[index].detail).find("transition 2 beats") != std::string::npos)
            named = true;
    CHECK(named);
}

TEST_CASE("the selected recipe's form and its unapplied preview reach the Compose view",
          "[sys][recipe-areas][recipe-form]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.recipe_form.visible);
    CHECK_FALSE(fixture.flattened.view.compose.recipe_areas[0].previewing);

    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    const auto& form_view = fixture.flattened.view.compose.recipe_form;
    REQUIRE(form_view.visible);
    CHECK(form_view.recipe_id == recipe_id_t{7001});
    CHECK(std::string(form_view.name) == "rise");
    CHECK(form_view.form.curve == wormedit::sv_recipe_curve_t::linear);
    CHECK(std::string(form_view.form.value_to.numerator) == "3");
    CHECK(form_view.status_text.empty());

    // A preview replaces that recipe's graph until the selection or the document moves on.
    const auto& chart = fixture.history.document.charts.front();
    auto form = wormedit::ui::timing_recipe_form_t::from(chart.sv_recipes.front());
    std::snprintf(form.value_to.numerator, sizeof(form.value_to.numerator), "9");
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].previewing);
    CHECK(fixture.flattened.view.compose.recipe_areas[0].graph_count >= 2);

    form.curve = wormedit::sv_recipe_curve_t::exponential;
    std::snprintf(form.shape.numerator, sizeof(form.shape.numerator), "99");
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    fixture.project();
    REQUIRE(!fixture.flattened.view.compose.recipe_form.status_text.empty());
    CHECK_FALSE(std::string(fixture.flattened.view.compose.recipe_form.status_text).empty());

    fixture.application.project.timing_recipe.recipe_selected = 0;
    fixture.project();
    CHECK_FALSE(fixture.application.project.timing_recipe.recipe_preview.has_value());
    CHECK_FALSE(fixture.flattened.view.compose.recipe_form.visible);

    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.application.project.timing_recipe.recipe_preview = wormedit::sys::recipe_form_preview_t::propose(
        fixture.history.document, chart, chart.sv_recipes.front(), form);
    auto& preview = *fixture.application.project.timing_recipe.recipe_preview;
    auto expired = *preview.editor();
    expired.stamp.document.revision += 5;
    recipe_form_outcome_t rejected{std::move(expired)};
    rejected.diagnostics = std::move(preview.diagnostics);
    rejected.complete(false);
    preview = std::move(rejected);
    fixture.project();
    CHECK_FALSE(fixture.application.project.timing_recipe.recipe_preview.has_value());
}

TEST_CASE("Pointer paste retains cursor and viewport and FIFO release commits that anchor",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history,
                                   *fixture.interaction, status));
    const auto cursor = fixture.interaction->cursor;
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    (void)dispatch_paste_pointer(fixture, false);
    expect_compose_test(fixture.interaction->cursor == cursor);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels == scroll);
    expect_compose_test(fixture.history.undo.empty());
    fixture.project();
    expect_compose_test(fixture.application.project.clipboard.paste_session->anchor.row == 240);
    expect_compose_test(fixture.interaction->cursor == cursor);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels == scroll);
    status = dispatch_paste_pointer(fixture, true);
    expect_compose_test(status == "Selection pasted.");
    expect_compose_test(fixture.history.undo.size() == 1);
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(notes.size() == 2);
    expect_compose_test(notes.back().position.row == 240);
    expect_compose_test(fixture.interaction->cursor == cursor);
}
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("Paste cached anchor keeps collision diagnostics across an outside pointer",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history,
                                   *fixture.interaction, status));
    const std::string collision{fixture.application.project.clipboard.paste_session->proposal.diagnostics.front().message};
    expect_compose_test(!collision.empty());
    auto outside = paste_pointer_action(fixture, false);
    outside.compose_inputs[0].y_pixels = 10000;
    (void)dispatch_paste_action(fixture, outside);
    fixture.project();
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic}.find("outside") != std::string::npos);
    auto original = paste_pointer_action(fixture, false, 0);
    (void)dispatch_paste_action(fixture, original);
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.paste_valid);
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic} == collision);
    (void)dispatch_paste_pointer(fixture, false);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paste_valid);
    (void)dispatch_paste_action(fixture, outside);
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.paste_valid);
    (void)dispatch_paste_pointer(fixture, false);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paste_valid);
    expect_compose_test(std::string{fixture.flattened.view.compose.paste_diagnostic}.empty());
    expect_compose_test(fixture.history.undo.empty());
}
#endif

TEST_CASE("Offset lock action rejects stale binding without clearing captured edits",
          "[sys][offset-stamp]") {
    compose_test_fixture_t fixture;
    fixture.project();
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision + 1;
    std::optional<proposal_t<audio_offset_drag_t>> drag{proposal_t<audio_offset_drag_t>{}};
    std::optional<workspace_tab_t> tab{workspace_tab_t::compose};
    const auto message = toggle_compose_audio_offset_lock(action, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    test::check_stale_action_message(message, "Audio offset lock");
    expect_compose_test(!fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(drag.has_value() && tab.has_value());
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset fit capture expiry remains separate from an absent drag",
          "[sys][offset-stamp]") {
    const auto missing = GENERATE(false, true);
    compose_test_fixture_t fixture;
    fixture.project();
    fixture.binding.value->transport.audio_asset = asset_id_t{42};
    if (!missing) {
        fixture.cache.timing.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{};
        fixture.cache.timing.offset_fit_drag->asset = asset_id_t{42};
        fixture.cache.timing.offset_fit_drag->beat.stamp.document.revision = fixture.history.document.revision + 1;
    }
    auto action = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_OFFSET_FIT_UPDATE, 2);
    const auto message = dispatch_paste_action(fixture, action);
    if (missing) CHECK(message == "Offset fit expired after its chart or waveform changed.");
    else test::check_stale_action_message(message, "Offset fit");
    expect_compose_test(!fixture.cache.timing.offset_fit_drag.has_value());
    expect_compose_test(action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_CANCEL);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset padlock disables exact offset controls independently of BPM timing",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    const auto revision = fixture.history.document.revision;
    const auto bpm = fixture.history.document.charts.front().bpms.front().beats_per_minute;
    fixture.application.project.timing_recipe.audio_offset_locked = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_locked);
    expect_compose_test(!fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(!fixture.flattened.view.compose.waveform_timing_locked);
    expect_compose_test(fixture.history.document.revision == revision);
    expect_compose_test(fixture.history.document.charts.front().bpms.front().beats_per_minute == bpm);
    expect_compose_test(fixture.history.undo.empty());
    fixture.application.project.timing_recipe.audio_offset_locked = false;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Locked offset fit rejects captured input without clearing a BPM preview",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    fixture.application.project.timing_recipe.audio_offset_locked = true;
    fixture.cache.timing.beat_fit_drag = proposal_t<beat_interval_drag_t>{};
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, .object_id = {2}};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history,
        *fixture.binding.value, *fixture.interaction, viewport, navigation,
        fixture.application, fixture.cache, 0);
    INFO(status);
    expect_compose_test(status.find("unlocked waveform") != std::string::npos);
    expect_compose_test(fixture.cache.timing.beat_fit_drag.has_value());
    expect_compose_test(!fixture.cache.timing.offset_fit_drag.has_value());
    expect_compose_test(action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset locking takes precedence over a captured release and never replays it",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    auto begun = audio_offset_drag_t::propose(fixture.history.document, chart_id_t{1}, 48000);
    REQUIRE(begun.value);
    auto updated = begun.value->revise(fixture.history.document, {}, 4800);
    REQUIRE(updated.value);
    std::optional<proposal_t<audio_offset_drag_t>> drag{updated.value};
    std::optional<workspace_tab_t> tab = workspace_tab_t::compose;
    fixture.cache.timing.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{};
    fixture.cache.timing.beat_fit_drag = proposal_t<beat_interval_drag_t>{};
    static_cast<void>(fixture.cache.timing.proposed_offset_timing_index.get_or_build(signed_rational_t{1, 10},
        [] { return std::optional<timing_index_t>{}; }));
    wormedit::ui::workspace_action_t action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK;
    action.compose_input_chart_id.value = 1;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_COMMIT;
    (void)toggle_compose_audio_offset_lock(action, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.timing.offset_fit_drag);
    expect_compose_test(!fixture.cache.timing.proposed_offset_timing_index.key());
    expect_compose_test(fixture.cache.timing.beat_fit_drag.has_value());
    expect_compose_test(fixture.history.undo.empty());
    (void)toggle_compose_audio_offset_lock(action, fixture.history, *fixture.binding.value,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(!fixture.application.project.timing_recipe.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.timing.offset_fit_drag);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Compose overview BGM presence follows the full cached chart index",
          "[sys][compose-overview]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{100001}, .name = "Autoplay"}};
    for (std::uint64_t id = 2; id <= 20; ++id)
        chart.measures.push_back({.id = measure_id_t{id}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.has_bgm_events);

    auto& retained_chart = fixture.history.document.charts.front();
    retained_chart.measures.back().bgm_events.push_back({
        .id = object_id_t{100002}, .position = {measure_id_t{20}, 0},
        .source_lane = object_id_t{100001}});
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.has_bgm_events);
    expect_compose_test(fixture.flattened.view.compose.bgm_events.size() == 0);
    const auto* full_index = &*fixture.cache.scene.index.value();
    fixture.project();
    expect_compose_test(&*fixture.cache.scene.index.value() == full_index);
    expect_compose_test(fixture.flattened.view.compose.has_bgm_events);
    expect_compose_test(fixture.flattened.view.compose.bgm_events.size() == 0);

    retained_chart.measures.back().bgm_events.clear();
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.has_bgm_events);
}

namespace {
void project_anchor_scene(compose_test_fixture_t& fixture, int height) {
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, height, fixture.interaction, fixture.application, nullptr);
    INFO(fixture.flattened.projection_feedback.size());
    expect_compose_test(fixture.flattened.view.compose.scene_available);
}
void check_short_chart_anchor_frame(compose_test_fixture_t& fixture, int height,
                                     double row, bool playing) {
    project_anchor_scene(fixture, height);
    expect_compose_test(follow_compose_playhead(fixture.history, *fixture.interaction,
                                                row, playing, {}, 0.35));
    project_anchor_scene(fixture, height);
    expect_compose_test(fixture.interaction->cursor.row == (row == 0 ? 0 : 720));
    const auto source_anchor = fixture.interaction->scroll_direction == editor_field_scroll_direction_t::down
        ? fixture.interaction->viewport_height_pixels - *fixture.interaction->fixed_cursor_screen_y_pixels
        : *fixture.interaction->fixed_cursor_screen_y_pixels;
    if (playing && row == 0) {
        INFO("height " << height << " scroll " << fixture.interaction->viewport_scroll_pixels << " anchor " << source_anchor);
        expect_compose_test(std::abs(fixture.interaction->viewport_scroll_pixels + source_anchor) < 1e-9);
    }
    expect_compose_test(fixture.history.undo.empty());
}
}
TEST_CASE("Configured anchor projects a short chart through audio-tail rows stop and resize",
          "[sys][anchor-scene]") {
    for (const std::uint32_t direction : {1U, 2U}) {
        compose_test_fixture_t fixture;
        fixture.application.preferences.editor_preferences.scroll_directions.fill(direction);
        for (const int height : {980, 640, 1100}) {
            check_short_chart_anchor_frame(fixture, height, 0, true);
            check_short_chart_anchor_frame(fixture, height, 9600, true);
            check_short_chart_anchor_frame(fixture, height, 9600, false);
            check_short_chart_anchor_frame(fixture, height, 0, false);
        }
    }
}

namespace {
struct overview_generation_fixture_t {
    song_document_t song = dense_song();
    compose_scene_cache_t cache;
    frontend_workspace_view_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis = overview_analysis(.25f);
    overview_generation_fixture_t() {
        update_timing();
        flattened.view.compose.visible = true;
        flattened.view.compose.scene_available = true;
        flattened.view.compose.viewport_height_pixels = 200;
        flattened.view.compose.pixels_per_standard_measure = 200;
    }
    void update_timing() {
        auto timing = timing_index_t::from(song.charts.front());
        expect_compose_test(timing.value.has_value());
        cache.timing.marker_timing_index = std::move(timing.value);
    }
    auto project(frontend_workspace_view_t& output,
                 const audio::waveform_analysis_result_t* selected = nullptr) -> std::uint64_t {
        expect_compose_test(populate_compose_waveform_projection(output, cache,
            selected ? selected : &analysis, false, true, &interaction));
        expect_compose_test(output.view.compose.waveform_overview_generation != 0);
        return output.view.compose.waveform_overview_generation;
    }
    auto project() -> std::uint64_t { return project(flattened); }
};
}
TEST_CASE("Compose overview generation ignores playback scroll timing offsets and snapshot row pointers",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto generation = fixture.project();
    const auto fraction = fixture.flattened.view.compose.waveform_overview_visible_begin_fraction;
    fixture.flattened.view.compose.source_playhead_available = true;
    fixture.flattened.view.compose.source_playhead_frame = 96'000;
    fixture.flattened.view.compose.waveform_offset_drag_active = true;
    expect_compose_test(fixture.project() == generation);
    fixture.interaction.viewport_scroll_pixels = 200;
    expect_compose_test(fixture.project() == generation);
    expect_compose_test(fixture.flattened.view.compose.waveform_overview_visible_begin_fraction > fraction);
    auto scene_binding = fixture.cache.scene.index.key().value_or(compose_scene_source_key_t{});
    ++scene_binding.stamp.document.revision;
    static_cast<void>(fixture.cache.scene.index.get_or_build(scene_binding, retained_scene_fixture_t{}));
    fixture.song.charts.front().beat_zero_audio_offset_seconds = {1, 10};
    fixture.update_timing();
    expect_compose_test(fixture.project() == generation);
    frontend_workspace_view_t second;
    second.view.compose = fixture.flattened.view.compose;
    expect_compose_test(fixture.project(second) == generation);
    expect_compose_test(second.view.compose.waveform_overview_rows.data() !=
        fixture.flattened.view.compose.waveform_overview_rows.data());
    expect_compose_test(second.view.compose.waveform_overview_rows ==
        fixture.flattened.view.compose.waveform_overview_rows);
}
TEST_CASE("Compose overview source revision changes generation even when waveform address is reused",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto before = fixture.project();
    const auto* identity = std::get<1>(*fixture.cache.waveform.overview.key());
    fixture.analysis.waveform->key.source.source_identity = "content:v2";
    for (auto& bin : fixture.analysis.waveform->bins) bin.absolute_peak = {.75f, .75f};
    const auto replacement = fixture.project();
    expect_compose_test(std::get<1>(*fixture.cache.waveform.overview.key()) == identity);
    expect_compose_test(replacement > before);
    expect_compose_test(fixture.flattened.view.compose.waveform_overview_rows.front().absolute_peak == .75f);
    expect_compose_test(fixture.project() == replacement);
}
TEST_CASE("Compose overview detail identity and cleared cache publish unique generations",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto initial = fixture.project();
    fixture.analysis.waveform->key.frames_per_bin = 128;
    fixture.analysis.waveform->total_frames = 128 * 4096;
    for (auto& bin : fixture.analysis.waveform->bins) bin.frame_count = 128;
    const auto detail = fixture.project();
    expect_compose_test(detail > initial);
    auto replacement = fixture.analysis;
    const auto identity = fixture.project(fixture.flattened, &replacement);
    expect_compose_test(identity > detail);
    fixture.cache = {};
    fixture.update_timing();
    const auto cleared = fixture.project(fixture.flattened, &replacement);
    expect_compose_test(cleared > identity);
    expect_compose_test(fixture.project(fixture.flattened, &replacement) == cleared);
}

namespace {
void check_static_source_has_no_frame_overlays(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.status_text.empty() && source.selected_note_label.empty() && source.hover_tooltip.empty());
    expect_compose_test(source.paste_ghosts.empty() && source.paste_diagnostic.empty());
    expect_compose_test(source.resources.empty());
    expect_compose_test(source.output_groups.empty());
    expect_compose_test(source.branch_rows.empty());
    expect_compose_test(source.branch_choices.empty());
    expect_compose_test(source.branch_diagnostic_count() == 0);
    for (const auto& diagnostic : source.branch_diagnostics) expect_compose_test(diagnostic.empty());
}
void check_static_source_has_no_frame_markers(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.beat_bookmarks.empty());
    expect_compose_test(source.beat_sections.empty());
    expect_compose_test(source.audio_bookmarks.empty());
    expect_compose_test(source.audio_sections.empty());
    expect_compose_test(source.semantic_layers.empty() && source.semantic_layers.size() == 0);
    expect_compose_test(source.staged_bmson_marker_playable_lanes.empty() && source.staged_bmson_marker_restarts.empty() &&
        source.staged_bmson_marker_restarts.size() == 0);
    expect_compose_test(source.lane_key_bindings.empty() && source.lane_key_bindings.size() == 0);
}
void check_static_source_has_no_frame_previews(const wormedit::ui::compose_view_t& source) {
    expect_compose_test(source.proposed_grid_lines.empty());
    expect_compose_test(source.timing_preview_rects.empty());
    expect_compose_test(source.timing_preview_markers.empty());
    expect_compose_test(source.waveform_rows.empty());
    expect_compose_test(source.channel_waveform_rows.empty());
    expect_compose_test(source.waveform_overview_rows.empty());
    expect_compose_test(source.bmson_marker_drafts.empty());
    expect_compose_test(!source.bmson_marker_move_preview && !source.cursor && !source.static_scene);
}
void project_static_source_arena_frame(compose_test_fixture_t& fixture) {
    workspace_view_t model;
    model.active_tab = workspace_tab_t::compose;
    model.tab_view = workspace_compose_view_t{.chart = chart_id_t{1}};
    fixture.flattened =
        frontend_workspace_view_t::from(model, fixture.history.document, "ready", {}, false);
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    expect_compose_test(fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.view.compose.resources.size() == 1);
    expect_compose_test(fixture.flattened.view.compose.output_groups.size() == 1);
    expect_compose_test(fixture.flattened.view.compose.static_scene != nullptr);
}
void check_static_source_owned_geometry(const frontend_workspace_view_t& storage) {
    const auto& source = storage.view.compose;
    check_static_source_has_no_frame_overlays(source);
    check_static_source_has_no_frame_markers(source);
    check_static_source_has_no_frame_previews(source);
}
void check_owned_hitsound_note(const wormedit::ui::compose_note_view_t& note) {
    expect_compose_test(note.hitsound != nullptr);
    expect_compose_test(note.hitsound->resolved_normal_set == osu_sample_set_t::soft);
    expect_compose_test(note.hitsound->resolved_addition_set == osu_sample_set_t::drum);
    expect_compose_test(note.hitsound->additions == 10);
    expect_compose_test(note.hitsound->resolved_custom_index == 3);
    expect_compose_test(note.hitsound->resolved_volume == 75);
    expect_compose_test(note.hitsound->filename == "custom-retained-hit.wav");
}
}
TEST_CASE("Persistent Compose static source owns geometry across frame arena reset",
          "[sys][frame-arena][static-source-lifetime]") {
    auto song = compose_test_song();
    song.audio_assets.push_back({.id = asset_id_t{9}, .source_path = "sample.wav"});
    song.resources.push_back({.id = resource_id_t{9}, .name = "Long independently owned sound label",
        .asset = asset_id_t{9}, .roles = {resource_role_t::sample}});
    song.output_groups.push_back({.name = "Disposable frame output group",
        .charts = {chart_id_t{1}}, .relative_output_path = "exports/chart.bms"});
    song.charts.front().measures.front().notes.front().sound = resource_id_t{9};
    song.charts.front().measures.front().notes.front().osu_hitsample = osu_hitsample_t{
        .whistle = true, .clap = true, .normal_set = osu_sample_set_t::soft,
        .addition_set = osu_sample_set_t::drum, .custom_index = 3, .volume = 75,
        .filename = "custom-retained-hit.wav"};
    compose_test_fixture_t fixture(std::move(song));
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_static_source_arena_frame(fixture);
    const auto stored = fixture.cache.scene.static_source.value().storage;
    const auto generation = fixture.cache.scene.static_source.build_count();
    check_static_source_owned_geometry(*stored);
    expect_compose_test(!stored->view.compose.note_endpoints.empty());
    const auto before = stored->view.compose.note_endpoints[0];
    check_owned_hitsound_note(before);
    const std::string label{before.sound_label};
    const std::string detail{before.sound_detail};
    expect_compose_test(!stored->view.compose.timing_markers.empty());
    const auto timing = stored->view.compose.timing_markers.front();
    const std::string timing_label{timing.short_label};
    const std::string timing_detail{timing.detail};
    fixture.flattened = {};
    arena.reset();
    const auto overwritten = arena.allocate(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwritten));
    std::memset(*overwritten, 0xA5, 4096);
    expect_compose_test(stored->view.compose.note_endpoints[0].note_id.value == before.note_id.value);
    expect_compose_test(stored->view.compose.note_endpoints[0].bounds.top == before.bounds.top);
    expect_compose_test(stored->view.compose.note_endpoints[0].sound_label == label);
    expect_compose_test(stored->view.compose.note_endpoints[0].sound_detail == detail);
    check_owned_hitsound_note(stored->view.compose.note_endpoints[0]);
    expect_compose_test(stored->view.compose.timing_markers.front().short_label == timing_label);
    expect_compose_test(stored->view.compose.timing_markers.front().detail == timing_detail);
    fixture.interaction->viewport_scroll_pixels = 0.5;
    project_static_source_arena_frame(fixture);
    expect_compose_test(fixture.cache.scene.static_source.value().storage == stored);
    expect_compose_test(fixture.cache.scene.static_source.build_count() == generation);
    check_static_source_owned_geometry(*fixture.cache.scene.static_source.value().storage);
    ++fixture.history.document.revision;
    project_static_source_arena_frame(fixture);
    expect_compose_test(fixture.cache.scene.static_source.value().storage != stored);
    expect_compose_test(fixture.cache.scene.static_source.build_count() > generation);
    check_static_source_owned_geometry(*fixture.cache.scene.static_source.value().storage);
}

namespace {
auto geometry_arena_song() -> song_document_t {
    auto song = compose_test_song();
    auto& hold = song.charts.front().measures.front().notes[1];
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{1}, 360};
    return song;
}
void project_geometry_arena(compose_test_fixture_t& fixture,
                            int height = 980) {
    fixture.flattened = frontend_workspace_view_t{};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, height, fixture.interaction, fixture.application, nullptr);
}
template<class Rows>
void check_geometry_between(const Rows& rows, std::uintptr_t begin, std::uintptr_t end) {
    if (rows.empty()) return;
    const auto address = reinterpret_cast<std::uintptr_t>(rows.data());
    expect_compose_test(address >= begin && address + rows.size() * sizeof(rows[0]) <= end);
}
template<class Rows>
void check_owned_geometry_storage(const Rows& rows, std::uintptr_t begin, std::uintptr_t end) {
    if (rows.empty()) return;
    const auto address = reinterpret_cast<std::uintptr_t>(rows.data());
    expect_compose_test(address < begin || address >= end);
}
void compare_geometry_rect(const wormedit::ui::compose_rect_t& a, const wormedit::ui::compose_rect_t& b) {
    expect_compose_test(a.left == Catch::Approx(b.left));
    expect_compose_test(a.top == Catch::Approx(b.top));
    expect_compose_test(a.right == Catch::Approx(b.right));
    expect_compose_test(a.bottom == Catch::Approx(b.bottom));
}
void compare_geometry_note(const wormedit::ui::compose_note_view_t& a, const wormedit::ui::compose_note_view_t& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id.value == b.note_id.value && a.lane_id.value == b.lane_id.value && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.glyph == b.glyph && a.kind == b.kind && a.snap_class == b.snap_class && a.tail == b.tail);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
    expect_compose_test(a.paint_preview == b.paint_preview && a.paint_erase_preview == b.paint_erase_preview && a.paint_invalid_preview == b.paint_invalid_preview);
    expect_compose_test(a.sound_label == b.sound_label);
    expect_compose_test(a.sound_detail == b.sound_detail);
}
void compare_geometry_hold(const wormedit::ui::compose_hold_view_t& a, const wormedit::ui::compose_hold_view_t& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id.value == b.note_id.value && a.lane_id.value == b.lane_id.value && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
}
void compare_geometry_frames(const wormedit::ui::compose_view_t& a, const wormedit::ui::compose_view_t& b) {
    expect_compose_test(a.bands.size() == b.bands.size() && a.lanes.size() == b.lanes.size() && a.differences.size() == b.differences.size());
    expect_compose_test(a.note_endpoints.size() == b.note_endpoints.size() && a.hold_bodies.size() == b.hold_bodies.size());
    for (std::size_t i = 0; i < a.bands.size(); ++i) {
        compare_geometry_rect(a.bands[i].bounds, b.bands[i].bounds);
        expect_compose_test(a.bands[i].split_stage == b.bands[i].split_stage);
    }
    for (std::size_t i = 0; i < a.lanes.size(); ++i) {
        compare_geometry_rect(a.lanes[i].bounds, b.lanes[i].bounds);
        expect_compose_test(a.lanes[i].lane_id.value == b.lanes[i].lane_id.value && a.lanes[i].display_order == b.lanes[i].display_order);
        expect_compose_test(a.lanes[i].stage == b.lanes[i].stage && a.lanes[i].role == b.lanes[i].role && a.lanes[i].glyph == b.lanes[i].glyph);
    }
    for (std::size_t i = 0; i < a.note_endpoints.size(); ++i) compare_geometry_note(a.note_endpoints[i], b.note_endpoints[i]);
    for (std::size_t i = 0; i < a.hold_bodies.size(); ++i) compare_geometry_hold(a.hold_bodies[i], b.hold_bodies[i]);
}
void check_fresh_reused_geometry(std::uint32_t direction, format_family_t family) {
    auto song = geometry_arena_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t reused(song), fresh(song);
    reused.application.preferences.editor_preferences.scroll_directions.fill(direction);
    fresh.application.preferences.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t reused_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    wormedit::frame_arena_t fresh_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(reused);
    project_geometry_arena(fresh);
    const auto source = reused.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    for (const bool legend : {false, true}) {
        reused.application.preferences.editor_preferences.lane_key_legend_visible = legend;
        fresh.application.preferences.editor_preferences.lane_key_legend_visible = legend;
        reused.interaction->viewport_scroll_pixels = 0.5;
        fresh.interaction->viewport_scroll_pixels = 0.5;
        reused_arena.reset();
        fresh_arena.reset();
        fresh.cache = {};
        project_geometry_arena(reused);
        project_geometry_arena(fresh);
        expect_compose_test(reused.flattened.view.compose.scene_available && fresh.flattened.view.compose.scene_available);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
        reused_arena.reset();
        project_geometry_arena(reused);
        expect_compose_test(reused.cache.scene.static_projection_reused_frames > 0);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
    }
}
}

TEST_CASE("Compose geometry owns storage outside the supplied arena and accounts static chunks",
          "[sys][frame-arena][compose-geometry]") {
    frontend_workspace_view_t untouched;
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto begin = arena.allocate(1, alignof(char));
    project_geometry_arena(fixture);
    const auto end = arena.allocate(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(fixture.flattened.view.compose.scene_available && !fixture.flattened.view.compose.truncated);
    const auto first = reinterpret_cast<std::uintptr_t>(*begin);
    const auto last = reinterpret_cast<std::uintptr_t>(*end);
    check_owned_geometry_storage(fixture.flattened.view.compose.bands, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.lanes, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.hold_bodies, first, last);
    check_owned_geometry_storage(fixture.flattened.view.compose.note_endpoints, first, last);
    expect_compose_test(!fixture.flattened.view.compose.hold_bodies.empty() && !fixture.flattened.view.compose.note_endpoints.empty());
    const auto source = fixture.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->view.compose.note_endpoints.data() != fixture.flattened.view.compose.note_endpoints.data());
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes >= sizeof(*source));
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes <= 16 * 1024 * 1024);
}

TEST_CASE("Compose geometry arena fresh and reused primitives agree in both directions and label modes",
          "[sys][frame-arena][compose-geometry]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_fresh_reused_geometry(direction, family);
}

TEST_CASE("Compose geometry owns complete lists when the frame arena is exhausted",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    const auto revision = fixture.history.document.revision;
    for (const auto bytes : {8U, 128U}) {
        wormedit::frame_arena_t arena({.chunk_bytes = bytes, .maximum_bytes = bytes});
        fixture.cache = {};
        project_geometry_arena(fixture);
        const auto& view = fixture.flattened.view.compose;
        expect_compose_test(!view.bands.empty() && !view.lanes.empty());
        expect_compose_test(!view.note_endpoints.empty() && !view.hold_bodies.empty());
        expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
    }
}

TEST_CASE("Warmed Compose geometry arena stays bounded through 1000 actual cached projections",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture);
    arena.reset();
    project_geometry_arena(fixture);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    const auto source = fixture.cache.scene.static_source.value().storage;
    arena.reset();
    allocations = 0;
    count_allocations = true;
    project_geometry_arena(fixture);
    count_allocations = false;
    const auto remaining_allocations = allocations;
    bool stable = true;
    bool consistent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        allocations = 0;
        count_allocations = true;
        project_geometry_arena(fixture);
        count_allocations = false;
        consistent = consistent && allocations == remaining_allocations;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            fixture.cache.scene.static_source.value().storage == source &&
            !fixture.flattened.view.compose.truncated;
    }
    INFO("Remaining actual Compose projection allocations: " << remaining_allocations);
    expect_compose_test(stable);
    expect_compose_test(consistent);
}

TEST_CASE("Compose geometry scene budget rejects dense excess without exceeding published caps",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(dense_song());
    const auto revision = fixture.history.document.revision;
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(!fixture.flattened.projection_feedback.empty());
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Compose projection failure posts once until repaired or revision changes", "[sys][notification][compose-geometry]") {
    notification_center_t center;
    notification_sink_t sink{center};
    compose_test_fixture_t fixture(dense_song());
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    projection_notification_feedback_t feedback;
    auto identity = projection_identity_t{fixture.history.document.id, fixture.history.document.revision, chart_id_t{1}, 1};
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.projection_feedback.empty());
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{0});
    const auto first = center.snapshot(notification_capacity);
    expect_compose_test(first.size() == 1);
    expect_compose_test(first[0].severity == notification_severity_t::error);
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{3000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 1);
    feedback.publish(identity, {}, sink);
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{6000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 2);
    ++identity.revision;
    feedback.publish(identity, fixture.flattened.projection_feedback, sink);
    center.drain_inbox(notification_time_t{9000});
    expect_compose_test(center.snapshot(notification_capacity)[0].count == 3);
    expect_compose_test(fixture.history.undo.empty());
}

namespace {
void check_grid_pairs(const frontend_workspace_view_t& frame) {
    const auto& grid = frame.compose.grid;
    const auto& view = frame.view.compose;
    expect_compose_test(view.grid_lines.size() == grid.row.size());
    expect_compose_test(view.grid_lines.size() <= compose_scene_grid_work_limit);
    for (std::size_t index = 0; index < view.grid_lines.size(); ++index) {
        const auto& row = grid.row[index];
        expect_compose_test(view.grid_lines[index].chart_row ==
            (row.fractional_rows.numerator == 0 ? row.whole_rows : 0));
    }
}
void compare_grid_frames(const frontend_workspace_view_t& actual, const frontend_workspace_view_t& expected) {
    compare_scalar_grid(actual.view.compose, expected.view.compose);
    expect_compose_test(actual.compose.grid.row.size() == expected.compose.grid.row.size());
    for (std::size_t index = 0; index < actual.compose.grid.row.size(); ++index)
        expect_compose_test(actual.compose.grid.row[index] == expected.compose.grid.row[index]);
    check_grid_pairs(actual);
}
void check_grid_cache_parity(std::uint32_t direction, format_family_t family) {
    auto song = geometry_arena_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t cached(song), fresh(song);
    cached.application.preferences.editor_preferences.scroll_directions.fill(direction);
    fresh.application.preferences.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t arena, reference;
    project_geometry_arena(cached);
    project_geometry_arena(fresh);
    const auto source = cached.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(cached.flattened.view.compose.bms_measure_labels == (family == format_family_t::bms));
    if (family == format_family_t::bms)
        expect_compose_test(std::any_of(cached.flattened.view.compose.grid_lines.begin(),
            cached.flattened.view.compose.grid_lines.end(), [](const auto& line) { return line.measure_number > 1; }));
    for (const auto scroll : {0.5, 20.0, 0.5}) {
        arena.reset(); reference.reset();
        cached.interaction->viewport_scroll_pixels = scroll;
        fresh.interaction->viewport_scroll_pixels = scroll;
        fresh.cache = {};
        project_geometry_arena(cached);
        project_geometry_arena(fresh);
        compare_grid_frames(cached.flattened, fresh.flattened);
        expect_compose_test(cached.cache.scene.static_source.value().storage == source);
    }
    expect_compose_test(cached.cache.scene.static_projection_reused_frames > 0);
    ++cached.history.document.revision;
    ++fresh.history.document.revision;
    arena.reset(); reference.reset(); fresh.cache = {};
    project_geometry_arena(cached);
    project_geometry_arena(fresh);
    expect_compose_test(cached.cache.scene.static_source.value().storage != source);
    compare_grid_frames(cached.flattened, fresh.flattened);
}
}

TEST_CASE("Compose paired grid rows own independent retained storage", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena;
    const auto begin = arena.allocate(1, alignof(char));
    project_geometry_arena(fixture);
    const auto end = arena.allocate(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(!fixture.flattened.view.compose.grid_lines.empty());
    check_grid_pairs(fixture.flattened);
    check_owned_geometry_storage(fixture.flattened.view.compose.grid_lines,
        reinterpret_cast<std::uintptr_t>(*begin), reinterpret_cast<std::uintptr_t>(*end));
    check_owned_geometry_storage(fixture.flattened.compose.grid.row,
        reinterpret_cast<std::uintptr_t>(*begin), reinterpret_cast<std::uintptr_t>(*end));
    const auto source = fixture.cache.scene.static_source.value().storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->view.compose.grid_lines.data() != fixture.flattened.view.compose.grid_lines.data());
    const auto line = source->view.compose.grid_lines[0];
    const auto row = source->compose.grid.row[0];
    auto moved = std::move(fixture.flattened);
    check_grid_pairs(moved);
    fixture.flattened = {};
    arena.reset();
    const auto overwrite = arena.allocate(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwrite));
    std::memset(*overwrite, 0xA5, 4096);
    expect_compose_test(source->view.compose.grid_lines[0].y_pixels == line.y_pixels);
    expect_compose_test(source->compose.grid.row[0] == row);
    expect_compose_test(fixture.cache.scene.static_source.value().accounted_bytes >=
        sizeof(*source));
}

TEST_CASE("Compose paired grid cached filters preserve fresh BMS and native labels", "[sys][frame-arena][compose-grid]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_grid_cache_parity(direction, family);
}

TEST_CASE("Compose grid storage is independent of frame arena capacity", "[sys][frame-arena][compose-grid]") {
    for (std::size_t limit = 128; limit <= 2048; limit += 32) {
        compose_test_fixture_t fixture(geometry_arena_song());
        wormedit::frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        project_geometry_arena(fixture);
        check_grid_pairs(fixture.flattened);
        expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
        expect_compose_test(!fixture.flattened.view.compose.grid_lines.empty());
        expect_compose_test(!fixture.flattened.view.compose.lanes.empty());
    }
}

TEST_CASE("Warmed paired grid rows remain stable through 1000 actual arena resets", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture);
    arena.reset(); project_geometry_arena(fixture);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    const auto source = fixture.cache.scene.static_source.value().storage;
    const auto count = fixture.flattened.view.compose.grid_lines.size();
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
            fixture.flattened.view.compose.grid_lines.size() == count &&
            fixture.flattened.compose.grid.row.size() == count && !fixture.flattened.view.compose.truncated;
    }
    INFO("remaining projection allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    expect_compose_test(stable);
    check_grid_pairs(fixture.flattened);
}

TEST_CASE("Compose paired grid limits remain bounded when scene budget rejects excess", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(dense_song());
    wormedit::frame_arena_t arena;
    project_geometry_arena(fixture, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.view.compose.grid_lines.empty() && fixture.flattened.compose.grid.row.empty());
    check_grid_pairs(fixture.flattened);
}

namespace {
auto bgm_arena_song(format_family_t family) -> song_document_t {
    auto song = scalar_reuse_song();
    auto& chart = song.charts.front();
    chart.source_family = family;
    song.resources.push_back({.id = resource_id_t{81000}, .name = "Independent BGM resource label",
        .roles = {resource_role_t::bgm}, .target_identities = {
            {.family = format_family_t::bms, .identifier = "01", .namespace_name = "WAV"}}});
    chart.measures.front().bgm_events.front().sound = resource_id_t{81000};
    if (family == format_family_t::bmson) {
        chart.sound_channels = {{.id = object_id_t{82000}, .name = "BGM slices",
            .source_clip = {.id = clip_id_t{82001}, .source = asset_id_t{82002}},
            .events = {{.id = object_id_t{82003}, .position = {measure_id_t{1},240},
                .end = local_position_t{measure_id_t{1},480}, .lane = lane_id_t{0},
                .continuation = false, .playable = false}}}};
        chart.measures.front().bgm_events.front().sound_channel_event = object_id_t{82003};
    }
    return song;
}
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
}
namespace {
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
}
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
auto semantic_arena_song(std::size_t count=2) -> song_document_t {
    auto song=compose_test_song();
    auto& chart=song.charts.front();
    for(std::size_t i=0;i<count;++i)
        chart.semantic_layers.push_back({.id=layer_id_t{101+i},.name=std::string(80,'L')+std::to_string(i),
            .color={99,120,140,255},.visible=true,.locked=i==0});
    if(count) chart.measures.front().notes.front().semantic_layers={layer_id_t{101}};
    return song;
}
void check_semantic_first(const wormedit::ui::compose_view_t& view) {
    expect_compose_test(view.semantic_layers.size()>0);
    const auto& row=view.semantic_layers[0];
    expect_compose_test(row.layer_id.value==101 && row.name.size()==81);
    expect_compose_test(row.red==99 && row.green==120 && row.blue==140 && row.alpha==255);
    expect_compose_test(row.assigned_note_count==1 && row.visible && row.locked);
}
}
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
    action.shortcut_window_focused = true;
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_viewport_inputs.resize(1);
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_PINCH_ZOOM,
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
}
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
auto project_waveform_frame(envelope_projection_fixture_t& fixture) -> bool {
    return populate_compose_waveform_projection(fixture.flattened, fixture.cache,
        &fixture.analysis, false, true, &fixture.interaction, nullptr,
        &fixture.blocks, &fixture.style);
}
void check_empty_frame_waveform(const wormedit::ui::compose_view_t& view) {
    expect_waveform_arena(view.waveform_rows.empty());
    expect_waveform_arena(view.waveform_overview_rows.empty());
    expect_waveform_arena(view.waveform_overview_generation == 0);
    expect_waveform_arena(view.waveform_overview_total_frames == 0 && view.waveform_overview_sample_rate == 0);
}
}
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
auto marker_arena_song(std::size_t count = 2) -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    for (std::size_t index = 0; index < count; ++index) {
        chart.bookmarks.push_back({.id = object_id_t{10000 + index * 2},
            .label = std::string(100, 'b') + std::to_string(index), .anchor = marker_anchor_t::beat,
            .position = local_position_t{measure_id_t{1}, 96 + index % 100}});
        chart.bookmarks.push_back({.id = object_id_t{10001 + index * 2},
            .label = std::string(100, 'a') + std::to_string(index), .anchor = marker_anchor_t::audio_time,
            .audio_milliseconds = static_cast<std::int64_t>(500 + index)});
        chart.sections.push_back({.id = object_id_t{20000 + index * 2},
            .label = std::string(100, 's') + std::to_string(index), .anchor = marker_anchor_t::beat,
            .beat_span = position_span_t{{measure_id_t{1}, 96}, {measure_id_t{1}, 480}}});
        chart.sections.push_back({.id = object_id_t{20001 + index * 2},
            .label = std::string(100, 't') + std::to_string(index), .anchor = marker_anchor_t::audio_time,
            .audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{200, 1600}});
    }
    return song;
}
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

}

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
}
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
}
TEST_CASE("Compose marker arena views agree across fresh and reused scene windows", "[sys][frame-arena][compose-markers]") {
    check_marker_reused_frame(1);
    check_marker_reused_frame(2);
}

TEST_CASE("Imported five-key BMS materializes a Compose field that responds to the wheel",
          "[sys][compose][bms-detection][regression]") {
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.value.song));
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoints.size() > 0);
    const auto before = fixture.interaction->viewport_scroll_pixels;
    wormedit::ui::workspace_action_t action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_viewport_inputs.resize(1);
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = -1, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.area_scroll_pixels > before);
    expect_compose_test(fixture.history.undo.empty());
}

namespace {
auto cursor_placement_song() -> song_document_t {
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    return std::move(*parsed.value.song);
}

auto cursor_placement_action(compose_test_fixture_t& fixture) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    return action;
}

void place_pointer_test_note(compose_test_fixture_t& fixture, int mode) {
    const auto resource = mode == 1;
    auto& state = *fixture.interaction;
    state.fixed_cursor_screen_y_pixels = 48;
    state.cursor_subrow = 7;
    const auto cursor_before = capture_cursor_pose(state);
    const local_position_t position{fixture.history.document.charts.front().measures.front().id, 240};
    const auto& chart = fixture.history.document.charts.front();
    const auto view = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(view), position);
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    action.compose_inputs.resize(1);
    fixture.application.project.compose_gesture.note_paint_mode_active = !resource;
    action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
    action.compose_inputs[0] = {.kind = resource ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE
                                               : WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .object_id = {fixture.history.document.resources.front().id.value},
        .resource_role = wormedit::resource_role_t::sample,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences))};
    if (mode == 2) {
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN;
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{2}));
        action.compose_inputs[1] = action.compose_inputs[0];
        action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
        action.compose_inputs.resize(2);
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(capture_cursor_pose(state) == cursor_before);
}

void check_note_placement_preserves_cursor(int mode, bool downscroll) {
    INFO("Mode " << mode << ", downscroll " << downscroll);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    place_pointer_test_note(fixture, mode);
    expect_compose_test(fixture.interaction->fixed_cursor_screen_y_pixels == 48);
    expect_compose_test(fixture.interaction->cursor_subrow == 7);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.cursor.has_value());
    INFO("drawn cursor " << fixture.flattened.view.compose.cursor->bounds.top << " anchor " << *fixture.interaction->fixed_cursor_screen_y_pixels);
    expect_compose_test(std::abs(fixture.flattened.view.compose.cursor->bounds.top - 47) < 1e-9);
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    auto action = cursor_placement_action(fixture);
    action.compose_viewport_inputs.resize(1);
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = downscroll ? -1.0 : 1.0, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    INFO(status << ", scroll " << scroll << " -> " << fixture.interaction->viewport_scroll_pixels);
    expect_compose_test(fixture.interaction->fixed_cursor_screen_y_pixels ==
        compose_cursor_screen_anchor(fixture.interaction->viewport_height_pixels, downscroll));
    expect_compose_test(fixture.interaction->viewport_scroll_pixels > scroll);
    expect_compose_test(fixture.history.undo.size() == 1);
}
} // namespace

namespace {
void check_ten_key_note_commit() {
    auto song = cursor_placement_song();
    auto& chart = song.charts.front();
    const auto profile = *make_osu_mania_profile(10);
    chart.lane_layouts.front().layout = profile.layout;
    chart.input_profile = profile.input;
    for (auto& measure : chart.measures) measure.notes.clear();
    compose_test_fixture_t fixture{std::move(song)};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.lane_key_bindings.size() == 10);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    for (const auto& binding : profile.input.bindings) {
        auto action = cursor_placement_action(fixture);
        action.compose_inputs.resize(1);
        action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
            .lane_id = {binding.lane.value}};
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test((fixture.interaction->hold() != nullptr));
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE;
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test(!(fixture.interaction->hold() != nullptr));
    }
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(notes.size() == 10);
    for (const auto& binding : profile.input.bindings)
        expect_compose_test(std::ranges::count(notes, binding.lane, &note_t::lane) == 1);
    expect_compose_test(fixture.history.undo.size() == 10);
}

void check_latched_range_projection(bool downscroll) {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.active_snap_denominator = 4;
    auto action = cursor_placement_action(fixture);
    action.compose_inputs.resize(1);
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(static_cast<bool>(state.gesture<wormedit::pending_space_range_selection_t>()));
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.space_range_preview_visible);
    const auto height = view.space_range_preview_bounds.bottom - view.space_range_preview_bounds.top;
    expect_compose_test(std::abs(height - state.pixels_per_standard_measure / 4) < 1e-9);
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(!static_cast<bool>(state.gesture<wormedit::pending_space_range_selection_t>()));
    expect_compose_test(state.retained_canvas_selection_range.has_value());
    const auto range = *state.retained_canvas_selection_range;
    expect_compose_test(range.end_row - range.begin_row ==
        fixture.history.document.charts.front().divisions_per_standard_measure / 4);
    expect_compose_test(fixture.history.undo.empty());
}

void check_wheel_displacement(bool downscroll, double delta,
                              WormUiComposeViewportInputKind kind, bool playing) {
    INFO("downscroll " << downscroll << ", delta " << delta
         << ", kind " << kind << ", playing " << playing);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.preferences.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.viewport_scroll_pixels = 1000.0;
    state.active_snap_denominator = 4;
    fixture.application.project.transport.preview_playhead_absolute_row = 960.0;
    fixture.application.services.transport_preview_playing = [playing] { return playing; };
    std::size_t seeks = 0;
    fixture.application.services.transport_seek = [&seeks](std::uint64_t) {
        ++seeks;
        return compose_action_outcome_t::from(std::string{"Seeked."}, notification_severity_t::hint,
                                              notification_source_t::input);
    };
    auto action = cursor_placement_action(fixture);
    action.compose_viewport_inputs.resize(1);
    action.compose_viewport_inputs[0] = {.kind = kind, .delta_y = delta,
        .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0.016);
    const bool forward = (delta > 0.0) != downscroll;
    if (playing) {
        expect_compose_test(fixture.application.project.transport.preview_playhead_absolute_row == (forward ? 1200.0 : 720.0));
        expect_compose_test(seeks == 1);
        expect_compose_test(state.viewport_scroll_pixels == 1000.0);
    } else {
        expect_compose_test((state.viewport_scroll_pixels > 1000.0) == forward);
        expect_compose_test(state.viewport_scroll_pixels != 1000.0);
        expect_compose_test(seeks == 0);
    }
    expect_compose_test(fixture.history.undo.empty());
}
} // namespace

TEST_CASE("Compose batches reject later object or viewport input after an earlier edit",
          "[sys][batch-stamp]") {
    const auto viewport_later = GENERATE(false, true);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    auto& state = *fixture.interaction;
    const auto& chart = fixture.history.document.charts.front();
    const auto count = chart.measures.front().notes.size();
    const auto inspected = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart, std::get<editor_interaction_view_model_t>(inspected),
        {chart.measures.front().id, 240});
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .object_id = {fixture.history.document.resources.front().id.value},
        .resource_role = wormedit::resource_role_t::sample,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences))};
    if (viewport_later) {
        action.compose_viewport_inputs.resize(1);
        action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
            .delta_y = -1, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    } else {
        action.compose_inputs.resize(2);
        action.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_DRAW_SOURCE, .resource_role = wormedit::resource_role_t::bga, .resource_id = {999}};
    }
    const auto scroll = state.viewport_scroll_pixels;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    test::check_stale_action_message(status, viewport_later ? "Remaining Compose viewport input" : "Remaining Compose input");
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(fixture.history.document.charts.front().measures.front().notes.size() == count + 1);
    expect_compose_test(fixture.application.project.compose_gesture.draw_source_id != 999);
    expect_compose_test(state.viewport_scroll_pixels == scroll);
}

TEST_CASE("Compose 10K lane presses commit Tap notes in every number row lane",
          "[sys][compose][lane-keys][regression]") {
    check_ten_key_note_commit();
}

TEST_CASE("Latched range input projects and commits one snap in both scroll directions",
          "[sys][compose][range-selection][regression]") {
    check_latched_range_projection(false);
    check_latched_range_projection(true);
}

TEST_CASE("Compose wheel displacement follows the same direction during editing and playback",
          "[sys][compose][playhead-skip][regression]") {
    for (const bool downscroll : {false, true}) {
        for (const double delta : {-1.0, 1.0}) {
            for (const auto kind : {WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
                                    WORM_UI_COMPOSE_VIEWPORT_INPUT_SMOOTH_WHEEL}) {
                for (const bool playing : {false, true}) {
                    check_wheel_displacement(downscroll, delta, kind, playing);
                }
            }
        }
    }
}

TEST_CASE("Pointer note placement preserves the yellow cursor and normal scrolling",
          "[sys][compose][cursor-placement][regression]") {
    check_note_placement_preserves_cursor(0, false);
    check_note_placement_preserves_cursor(0, true);
    check_note_placement_preserves_cursor(1, false);
    check_note_placement_preserves_cursor(1, true);
    check_note_placement_preserves_cursor(2, false);
    check_note_placement_preserves_cursor(2, true);
}

namespace {
auto paint_stroke_inputs(compose_test_fixture_t& fixture, std::uint64_t sound) -> wormedit::ui::workspace_action_t {
    auto& state = *fixture.interaction;
    const local_position_t position{fixture.history.document.charts.front().measures.front().id, 240};
    const auto& chart = fixture.history.document.charts.front();
    const auto view = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(view), position);
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    action.compose_inputs.resize(2);
    action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.preferences.editor_preferences)),
        .resource_id = {sound}};
    action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{2}));
    action.compose_inputs[1] = action.compose_inputs[0];
    action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
    return action;
}
} // namespace

TEST_CASE("Note paint begin input carries the armed sound into the document and advances once",
          "[sys][compose][paint][sound]") {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    placed_resource_notifications.clear();
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto count_notes_with_sound = [&] {
        std::size_t count{};
        for (const auto& measure : fixture.history.document.charts.front().measures)
            for (const auto& note : measure.notes)
                if (note.sound == sound) ++count;
        return count;
    };
    const auto before = count_notes_with_sound();
    auto action = paint_stroke_inputs(fixture, sound.value);
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(count_notes_with_sound() == before + 1);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{sound.value});

    // A silent stroke is committed without notifying the dock.
    fixture.project();
    placed_resource_notifications.clear();
    auto silent = paint_stroke_inputs(fixture, 0);
    (void)dispatch_compose_input_action(silent, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(placed_resource_notifications.empty());
}

namespace {
void check_armed_layer_placement(int mode, bool no_layer) {
    auto song = cursor_placement_song();
    auto& chart = song.charts.front();
    chart.semantic_layers = {{.id = layer_id_t{7}, .name = "Chosen"},
                             {.id = layer_id_t{8}, .name = "Sound default"}};
    chart.sound_layer_defaults = {{.resource = song.resources.front().id, .layers = {layer_id_t{8}}}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.project.semantic_layer.semantic_layer_target = no_layer ? std::nullopt : std::optional{layer_id_t{7}};
    fixture.application.project.semantic_layer.semantic_layer_target_none = no_layer;
    fixture.application.project.compose_gesture.note_paint_mode_active = mode == 0 || mode == 1;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    auto action = paint_stroke_inputs(fixture, sound.value);
    if (mode == 1) {
        place_cursor_at(*fixture.interaction,
            {fixture.history.document.charts.front().measures.front().id, 240});
        const auto lane = fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().id;
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
        action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = {lane.value}};
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{2}));
        action.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = {lane.value}};
    } else if (mode == 2) {
        action.compose_inputs.resize(1);
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE;
        action.compose_inputs[0].object_id.value = sound.value;
        action.compose_inputs[0].resource_role = wormedit::resource_role_t::sample;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    const auto note = std::ranges::find(notes, 240U, [](const auto& value) { return value.position.row; });
    expect_compose_test(note != notes.end());
    expect_compose_test(note->semantic_layers == (no_layer ? std::vector<layer_id_t>{}
                                                          : std::vector{layer_id_t{7}}));
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(undo_document_command(fixture.history));
    const auto& restored = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(std::ranges::none_of(restored, [](const auto& value) { return value.position.row == 240; }));
}
}

TEST_CASE("Compose layer target arms new pointer keyboard and resource notes with one undo",
          "[sys][compose][semantic-layer][regression]") {
    for (const auto mode : {0, 1, 2}) {
        check_armed_layer_placement(mode, false);
        check_armed_layer_placement(mode, true);
    }
}

TEST_CASE("Compose BMS layout picker includes DP5 without free zones",
          "[sys][compose][dp5-standard]") {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto names = std::span(view.lane_layout_profile_names);
    CHECK(std::ranges::any_of(names, [](const auto& name) {
        return std::string_view{name} == "BMS DP5 / 10K";
    }));
}

TEST_CASE("Stopped BMS transport seek moves cursor and viewport without editing",
          "[sys][compose][seek-bar][regression]") {
    notification_center_t center;
    notification_sink_t sink{center};
    const std::string source{"#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n"};
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.value.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.value.song));
    fixture.binding.value->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto before = fixture.interaction->cursor;
    auto status = finish_compose_action(
        sink, seek_stopped_compose_transport(fixture.history, *fixture.interaction, fixture.cache,
                                             20 * 44'100));
    expect_compose_test(status.message == "Compose cursor moved to the seek position.");
    expect_compose_test(fixture.interaction->cursor != before);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels > 0);
    expect_compose_test(!fixture.interaction->virtual_bms_cursor_row);
    expect_compose_test(fixture.history.undo.empty());
    center.drain_inbox(notification_time_t{0});
    auto messages = center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages[0].severity == notification_severity_t::hint);
    expect_compose_test(messages[0].source == notification_source_t::input);
    const auto sought = fixture.interaction->cursor;
    ++fixture.history.document.revision;
    (void)finish_compose_action(sink, seek_stopped_compose_transport(
                                          fixture.history, *fixture.interaction, fixture.cache, 0));
    expect_compose_test(fixture.interaction->cursor == sought);
    center.drain_inbox(notification_time_t{1});
    messages = center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 2);
    expect_compose_test(messages[0].severity == notification_severity_t::warning);
    expect_compose_test(messages[0].message == "Compose seek is unavailable until the chart is ready.");
}

namespace {
void check_click_audition_preference(bool enabled) {
    compose_test_fixture_t fixture;
    fixture.application.preferences.editor_preferences.audition_on_click = enabled;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoints.size() > 0);
    const auto& bounds = fixture.flattened.view.compose.note_endpoints[0].bounds;
    wormedit::ui::workspace_action_t action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .x_pixels = (bounds.left + bounds.right) / 2,
        .y_pixels = (bounds.top + bounds.bottom) / 2};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    auto notifications = notification_sink_t::muted();
    const compose_input_inputs_t environment{
        .document = {.history = fixture.history, .workspace = *fixture.binding.value,
                     .notifications = notifications, .auditions = &fixture.application.project.auditions},
        .commands = fixture.application, .controls = {fixture.cache, viewport, navigation}};
    (void)dispatch_compose_input_action(action, environment, *fixture.interaction, 0.016, false);
    expect_compose_test(!fixture.interaction->selected_notes.empty());
    expect_compose_test(fixture.application.project.auditions.has_pending_request() == enabled);
    expect_compose_test(fixture.history.undo.empty());
}
}
TEST_CASE("Note click audition can be disabled without suppressing selection",
          "[sys][compose][audition][regression]") {
    check_click_audition_preference(true);
    check_click_audition_preference(false);
}

TEST_CASE("Idle paint ghost follows hover without rebuilding the retained scene", "[sys][paint-ghost]") {
    compose_test_fixture_t fixture;
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto lane = view.lanes[0];
    const auto x = (lane.bounds.left + lane.bounds.right) * .5;
    const auto source_y = view.pixels_per_standard_measure * .375 - fixture.interaction->viewport_scroll_pixels;
    const auto y = view.downscroll ? view.viewport_height_pixels - source_y : source_y;
    fixture.cache.frame.paint_pointer = std::pair{x, y};
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.paint_ghost_visible);
    const auto builds = fixture.cache.scene.static_source.key()->rebuild_count;
    fixture.cache.frame.paint_pointer = std::pair{x, y + 30};
    fixture.project();
    CHECK(fixture.flattened.view.compose.paint_ghost_visible);
    CHECK(fixture.cache.scene.static_source.key()->rebuild_count == builds);
    CHECK(fixture.history.undo.empty());
    const auto endpoint = fixture.flattened.view.compose.note_endpoints[0];
    fixture.cache.frame.paint_pointer = std::pair{(endpoint.bounds.left + endpoint.bounds.right) * .5,
                                           (endpoint.bounds.top + endpoint.bounds.bottom) * .5};
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost_visible);
    const auto ghost = fixture.flattened.view.compose.paint_ghost;
    expect_compose_test(ghost.paint_erase_preview);
    expect_compose_test(std::abs(ghost.bounds.top - endpoint.bounds.top) < 1e-9);
    expect_compose_test(std::abs(ghost.bounds.bottom - endpoint.bounds.bottom) < 1e-9);
    fixture.interaction->armed_note_kind = note_kind_t::hold;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost.kind == note_kind_t::hold);
    expect_compose_test(fixture.flattened.view.compose.hold_bodies.size() == 0);
    fixture.cache.frame.paint_pointer.reset();
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
    fixture.cache.frame.paint_pointer = std::pair{x, y};
    fixture.application.project.compose_gesture.note_paint_mode_active = false;
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
}

TEST_CASE("Lane profile switcher uses key roles instead of recognizing the current profile ID",
          "[sys][lane-profile-switch][compatible-layout]") {
    auto scratch_song = compose_test_song();
    scratch_song.charts.front().lane_layouts.front().layout = make_bms_7k_profile().layout;
    scratch_song.charts.front().measures.front().notes.clear();
    scratch_song.charts.front().input_profile = make_bms_7k_profile().input;
    compose_test_fixture_t scratch{std::move(scratch_song)};
    scratch.project();
    expect_compose_test(scratch.flattened.view.compose.lane_profile_names.size() == 0);

    auto song = compose_test_song();
    song.charts.front().lane_layouts.front().layout = make_osu_mania_profile(7)->layout;
    song.charts.front().lane_layouts.front().layout.profile_id = "created-7k";
    song.charts.front().input_profile = make_osu_mania_profile(7)->input;
    song.charts.front().measures.front().notes.clear();
    compose_test_fixture_t created{std::move(song)};
    created.project();
    const auto& view = created.flattened.view.compose;
    expect_compose_test(view.lane_profile_names.size() == 1);
    expect_compose_test(view.lane_profile_current == -1);
    expect_compose_test(std::string_view(view.lane_profile_names[0]) == "osu!mania 7K");
    const auto ids = std::span(view.lane_layout_profile_ids);
    expect_compose_test(std::ranges::any_of(ids, [](const auto& id) {
        return std::string_view(id) == "osu-mania-7k";
    }));
}

TEST_CASE("BGM-only projection offers every initial profile with distinct SpecialStyle labels",
          "[sys][lane-profile-switch][initial-profile-projection]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bms;
    chart.lane_layouts.clear();
    chart.measures.front().notes.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{70001}, .name = "Autoplay"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{70002},
        .position = {chart.measures.front().id, 0}, .source_lane = object_id_t{70001}}};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.has_bgm_events);
    expect_compose_test(view.lane_profile_names.size() == 41);
    expect_compose_test(view.lane_profile_names.size() < 64);
    expect_compose_test(view.lane_profile_current == -1);
    std::vector<std::string_view> names;
    for (std::size_t index = 0; index < view.lane_profile_names.size(); ++index)
        names.emplace_back(view.lane_profile_names[index]);
    for (const auto label : {"osu!mania 12K", "osu!mania 16K",
                             "osu!mania 12K (SpecialStyle)", "osu!mania 16K (SpecialStyle)"})
        expect_compose_test(std::ranges::find(names, label) != names.end());
}


namespace {
auto lane_key_callback_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.audio_assets = {{.id = asset_id_t{1}}};
    song.resources = {
        {.id = resource_id_t{7}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
        {.id = resource_id_t{8}, .asset = asset_id_t{1}, .roles = {resource_role_t::sample}}};
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.source_family = format_family_t::bms;
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.bpms = {{.id = object_id_t{2}, .position = {measure_id_t{1}, 0},
                   .beats_per_minute = {120, 1}}};
    chart.lane_layouts = {{.id = object_id_t{3}, .position = {measure_id_t{1}, 0},
        .layout = {.profile_id = "lane-key-callback", .lanes = {
            {.id = lane_id_t{1}, .name = "Key 1"}, {.id = lane_id_t{2}, .name = "Key 2"}}}}};
    chart.semantic_layers = {{.id = layer_id_t{3}, .name = "Open"},
                             {.id = layer_id_t{4}, .name = "Locked", .locked = true}};
    chart.sound_layer_defaults = {{.resource = resource_id_t{7},
                                   .layers = {layer_id_t{3}, layer_id_t{4}}}};
    song.charts.push_back(std::move(chart));
    return song;
}

struct lane_key_callback_fixture_t {
    compose_test_fixture_t compose{lane_key_callback_song()};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    lane_key_callback_fixture_t() {
        placed_resource_notifications.clear();
        compose.application.project.compose_gesture.note_paint_mode_active = true;
        compose.project();
        compose.interaction->active_snap_denominator = 4;
    }
    ~lane_key_callback_fixture_t() { placed_resource_notifications.clear(); }
    auto dispatch(WormUiComposeInputKind kind, std::uint64_t lane = 1,
                  std::uint64_t sound = 7, bool successive = false) -> std::string {
        auto action = cursor_placement_action(compose);
        action.compose_input_project_id.value = compose.history.document.id.value;
        action.compose_inputs.resize(1);
        action.compose_inputs[0] = {.kind = kind, .lane_id = {lane},
                                    .successive = successive, .resource_id = {sound}};
        return dispatch_compose_input_action(action, compose.history, *compose.binding.value,
            *compose.interaction, viewport, navigation, compose.application, compose.cache, 0);
    }
    auto notes() const -> std::vector<note_t> {
        std::vector<note_t> result;
        for (const auto& measure : compose.history.document.charts.front().measures)
            result.insert(result.end(), measure.notes.begin(), measure.notes.end());
        return result;
    }
};

void check_lane_key_callback(bool virtual_measure, bool hold) {
    lane_key_callback_fixture_t f;
    if (virtual_measure) {
        for (int step = 0; step < 8; ++step)
            (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
        expect_compose_test(f.compose.interaction->virtual_bms_cursor_row == 1920);
    }
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_compose_test((f.compose.interaction->hold() != nullptr));
    expect_compose_test(f.compose.interaction->hold()->sound == resource_id_t{7});
    expect_compose_test(f.compose.history.undo.empty());
    expect_compose_test(placed_resource_notifications.empty());
    if (hold) (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
    // A different release payload must never retarget the sound captured on key-down.
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    const auto notes = f.notes();
    expect_compose_test(notes.size() == 1);
    expect_compose_test(notes.front().sound == resource_id_t{7});
    expect_compose_test(notes.front().semantic_layers == std::vector{layer_id_t{3}});
    expect_compose_test(notes.front().end.has_value() == hold);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    expect_compose_test(f.compose.history.undo.size() == 1);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_UNDO);
    expect_compose_test(f.notes().empty());
    expect_compose_test(f.compose.history.document.charts.front().measures.size() == 2);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
}
}

TEST_CASE("Lane-key dispatcher emits one captured-sound callback per authored or virtual tap and hold",
          "[sys][compose][lane-key-sound]") {
    for (const bool virtual_measure : {false, true})
        for (const bool hold : {false, true})
            check_lane_key_callback(virtual_measure, hold);
}

TEST_CASE("Lane-key dispatcher keeps simultaneous held sounds and emits each release once",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 7);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 2, 8);
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_MOVE_CURSOR_FORWARD);
    expect_compose_test(placed_resource_notifications.empty());
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{7});
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 2, 7);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>({7, 8}));
    const auto notes = f.notes();
    expect_compose_test(notes.size() == 2);
    expect_compose_test(notes[0].lane == lane_id_t{1} && notes[0].sound == resource_id_t{7});
    expect_compose_test(notes[1].lane == lane_id_t{2} && notes[1].sound == resource_id_t{8});
    expect_compose_test(notes[0].end == notes[1].end);
    expect_compose_test(f.compose.history.undo.size() == 2);
}

TEST_CASE("Lane-key dispatcher does not advance for rejection silent notes deletion or selected-note movement",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    SECTION("missing resource rejects atomically") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 999);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 999);
        expect_compose_test(f.notes().empty());
        expect_compose_test(f.compose.history.undo.empty());
    }
    SECTION("silent insertion") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 0);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 0);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && !notes.front().sound);
    }
    SECTION("sounded note removal never re-sounds or advances") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
        placed_resource_notifications.clear();
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 0);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 0);
        expect_compose_test(f.notes().empty());
        expect_compose_test(f.compose.history.undo.size() == 2);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_UNDO);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && notes.front().sound == resource_id_t{7});
    }
    SECTION("Shift lane move preserves the selected sound") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
        placed_resource_notifications.clear();
        const auto note = f.notes().front();
        f.compose.interaction->selected_notes = {note.id};
        f.compose.interaction->selected_objects = {{editor_selection_object_kind_t::note, note.id}};
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 2, 8, true);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 2, 8);
        const auto notes = f.notes();
        expect_compose_test(notes.size() == 1 && notes.front().lane == lane_id_t{2});
        expect_compose_test(notes.front().sound == resource_id_t{7});
    }
    expect_compose_test(placed_resource_notifications.empty());
}

TEST_CASE("Lane-key dispatcher cancels held sound sources without placement callbacks",
          "[sys][compose][lane-key-sound]") {
    lane_key_callback_fixture_t f;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS);
    expect_compose_test((f.compose.interaction->hold() != nullptr));
    SECTION("explicit cancellation") {
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE);
    }
    SECTION("resource deleted after key-down") {
        auto& document = f.compose.history.document;
        std::erase_if(document.resources, [](const auto& sound) { return sound.id == resource_id_t{7}; });
        document.charts.front().sound_layer_defaults.clear();
        ++document.revision;
    }
    const auto revision = f.compose.history.document.revision;
    (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE);
    expect_compose_test(f.notes().empty());
    expect_compose_test(f.compose.history.document.revision == revision);
    expect_compose_test(f.compose.history.undo.empty());
    expect_compose_test(placed_resource_notifications.empty());
}

#include "sys/audio/stem_waveform_window.h"
namespace {
void link_window_channels(chart_document_t& chart) {
    chart.source_family = format_family_t::bmson;
    for (std::size_t index = 0; index < chart.sound_channels.size(); ++index)
        chart.bgm_source_lanes.push_back({.id = object_id_t{90000 + index},
            .source_order = index, .stem_channel = chart.sound_channels[index].id});
}
} // namespace
TEST_CASE("stem waveforms cover channels with events near the cursor or audio reaching it",
          "[sys][waveform-projection][bmson][stems]") {
    song_document_t song;
    song.audio_assets = {{.id = asset_id_t{1}, .stream = {48'000, 2, 48'000 * 4, true}},
                         {.id = asset_id_t{2}, .stream = {48'000, 2, 48'000 * 60, true}}};
    auto& chart = song.charts.emplace_back();
    for (std::uint64_t id = 1; id <= 80; ++id) chart.measures.push_back({.id = measure_id_t{id}});
    chart.bpms = {{.id = object_id_t{900}, .position = {measure_id_t{1}, 0}, .beats_per_minute = {120, 1}}};
    const auto channel = [](std::uint64_t id, std::uint64_t measure, std::uint64_t asset) {
        return sound_channel_t{.id = object_id_t{id}, .source_clip = {.source = asset_id_t{asset}},
            .events = {{.id = object_id_t{id + 1000}, .position = {measure_id_t{measure}, 0}}}};
    };
    // 120 BPM is two seconds per measure: the 4 s clip reaches about 3 measures, the 60 s clip 31.
    chart.sound_channels = {channel(1, 2, 1), channel(2, 40, 1), channel(3, 12, 1), channel(4, 80, 1),
                            channel(5, 2, 2), channel(6, 70, 1)};
    link_window_channels(chart);
    const auto at = [&](std::uint64_t measure) {
        std::vector<std::uint64_t> ids;
        for (const auto* found : sys::visible_stem_channels(song, chart, measure_id_t{measure}, 10))
            ids.push_back(found->id.value);
        return ids;
    };
    CHECK(at(1) == (std::vector<std::uint64_t>{1, 5}));
    // The long clip started at measure 2 is still audible 30 measures later, the short ones are not.
    CHECK(at(30) == (std::vector<std::uint64_t>{2, 5}));
    CHECK(at(40) == (std::vector<std::uint64_t>{2, 5}));
    CHECK(at(45) == (std::vector<std::uint64_t>{2}));
    CHECK(at(60) == (std::vector<std::uint64_t>{6}));
    // A cursor that is not in the chart behaves like the start.
    CHECK(at(9999) == at(1));
}

namespace {
auto configure_mixed_projection(channel_projection_fixture_t& fixture)
    -> std::shared_ptr<const audio::waveform_analysis_result_t> {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                      "fixtures" / "audio" / "keysound-c4.ogg";
    const auto packed_source = fixture.sources.create_source(
        {asset_id_t{8}, path, {"waveform:packed", "immutable:v1"}});
    expect_gpu_work(static_cast<bool>(packed_source));
    auto packed_asset = fixture.history.document.audio_assets.back();
    packed_asset.id = asset_id_t{8};
    fixture.history.document.audio_assets.push_back(std::move(packed_asset));
    auto& chart = fixture.history.document.charts.front();
    chart.sound_channels.clear();
    chart.bgm_source_lanes = {{.id = object_id_t{90000}, .name = "BGM 1"}};
    chart.measures.front().bgm_events.clear();
    for (std::uint64_t index = 0; index < 1003; ++index) {
        const auto stem = index == 1 || index == 500 || index == 1001;
        const auto lane = object_id_t{stem ? 90001 + index : 90000};
        chart.sound_channels.push_back({.id = object_id_t{71000 + index},
            .source_clip = {clip_id_t{72000 + index}, asset_id_t{stem ? 7U : 8U}},
            .events = {{.id = object_id_t{73000 + index}, .position = {measure_id_t{1}, 0},
                        .playable = false}}});
        if (stem) chart.bgm_source_lanes.push_back({.id = lane,
            .source_order = index, .stem_channel = object_id_t{71000 + index}});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{74000 + index},
            .position = {measure_id_t{1}, 0}, .source_lane = lane,
            .sound_channel_event = object_id_t{73000 + index}});
    }
    ++fixture.history.document.revision;
    fixture.workspace.transport.audio_asset.reset();
    fixture.flattened.view.compose.selected_bmson_bgm_event_id.value = 74000;
    auto packed_analysis = std::make_shared<audio::waveform_analysis_result_t>(*fixture.channel_analysis);
    packed_analysis->waveform->key.source = packed_source->source;
    return packed_analysis;
}

void check_mixed_production_projection(channel_projection_fixture_t& fixture,
    const std::shared_ptr<const audio::waveform_analysis_result_t>& packed_analysis, bool supply_all) {
    std::vector<const sound_channel_t*> all;
    for (const auto& channel : fixture.history.document.charts.front().sound_channels)
        all.push_back(&channel);
    std::optional<std::span<const sound_channel_t* const>> visible;
    if (supply_all) visible = all;
    std::size_t requests{};
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, retained_sources_for(fixture.history.document, fixture.project_sources),
        {std::nullopt, nullptr}, {asset_id_t{8}, packed_analysis}, 1,
        [&](asset_id_t) { ++requests; return fixture.channel_analysis; }, visible};
    populate_bmson_waveform_projection(context);
    INFO("requests=" << requests << " cache entries=" << fixture.cache.stems.stem_waveforms.size()
         << " projected views=" << fixture.flattened.view.compose.stem_waveforms.size());
    expect_gpu_work(requests == 3 && fixture.cache.stems.stem_waveforms.size() == 3);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveforms.size() == 3);
    expect_gpu_work(fixture.cache.waveform.channel_projection.view.value().rows.empty());
    expect_gpu_work(!fixture.cache.waveform.channel_waveform_slices.value());
    const std::vector<std::uint64_t> expected{71001, 71500, 72001};
    std::vector<std::uint64_t> keys;
    for (std::size_t index = 0; index < fixture.flattened.view.compose.stem_waveforms.size(); ++index)
        keys.push_back(fixture.flattened.view.compose.stem_waveforms[index].stem_key);
    std::ranges::sort(keys);
    expect_gpu_work(keys == expected);
}
} // namespace

TEST_CASE("BMSON production projection excludes one thousand packed channels and selected packed slices",
          "[sys][waveform-projection][bmson][stems][hybrid-lanes]") {
    channel_projection_fixture_t fixture;
    const auto packed_analysis = configure_mixed_projection(fixture);
    check_mixed_production_projection(fixture, packed_analysis, false);
    check_mixed_production_projection(fixture, packed_analysis, true);
}

namespace {
struct workspace_timing_snapshot_t {
    std::uint64_t id{}, source{}, storage{}, row{};
    double y{}, left{}, right{};
    wormedit::timing_lane_kind_t lane{};
    std::string label, detail;
    bool fixed{}, selected{};
    auto operator==(const workspace_timing_snapshot_t&) const -> bool = default;
};
auto workspace_timing_snapshot(const compose_test_fixture_t& fixture)
    -> std::vector<workspace_timing_snapshot_t> {
    std::vector<workspace_timing_snapshot_t> result;
    for (const auto& marker : fixture.flattened.view.compose.timing_markers)
        result.push_back({marker.object_id.value, marker.source_order, marker.storage_order,
            marker.chart_row, marker.marker_y_pixels, marker.marker_left_fraction,
            marker.marker_right_fraction, marker.lane, std::string{marker.short_label}, std::string{marker.detail},
            marker.fixed_initial, marker.selected});
    return result;
}
auto workspace_timing_cache_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.bpms.clear();
    for (std::uint64_t index = 0; index < 70; ++index)
        chart.bpms.push_back({.id = object_id_t{10000 + index},
            .position = {measure_id_t{1}, index * 12},
            .beats_per_minute = {120 + static_cast<std::int64_t>(index), 1}});
    song.audio_assets = {{.id = asset_id_t{1}}};
    for (std::uint64_t id = 1; id <= 600; ++id)
        song.resources.push_back({.id = resource_id_t{id}, .name = "sound" + std::to_string(id),
            .asset = asset_id_t{1}, .roles = {resource_role_t::sample}});
    return song;
}
auto workspace_timing_cache_tracks_frame_state() -> bool {
    const auto song = workspace_timing_cache_song();
    compose_test_fixture_t cached(song), reference(song);
    reference.cache.timing_payload_cache_enabled = false;
    cached.project(); reference.project();
    const auto original = workspace_timing_snapshot(cached);
    for (double scroll : {0.0, 150.0, 0.0}) {
        for (auto* fixture : {&cached, &reference}) {
            fixture->interaction->viewport_scroll_pixels = scroll;
            fixture->interaction->selected_objects = {
                {editor_selection_object_kind_t::bpm_event, object_id_t{10005}}};
            fixture->project();
        }
        if (workspace_timing_snapshot(cached) != workspace_timing_snapshot(reference))
            return false;
    }
    const auto selected = std::ranges::find_if(cached.flattened.view.compose.timing_markers,
        [](const auto& marker) { return marker.object_id == object_id_t{10005}; });
    if (selected == cached.flattened.view.compose.timing_markers.end() || !selected->selected ||
        original == workspace_timing_snapshot(cached)) return false;
    for (auto* fixture : {&cached, &reference}) {
        fixture->interaction->selected_objects.clear();
        fixture->project();
    }
    return workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference) &&
        std::ranges::none_of(cached.flattened.view.compose.timing_markers,
                            [](const auto& marker) { return marker.selected; }) &&
        cached.cache.timing_payload_cache.build_count() == 1 &&
        reference.cache.timing_payload_cache.build_count() == 0;
}
auto workspace_timing_cache_invalidates_revision() -> bool {
    compose_test_fixture_t cached(workspace_timing_cache_song());
    cached.project();
    auto& document = cached.history.document;
    document.charts.front().bpms[5].beats_per_minute = {999, 1};
    ++document.revision;
    cached.binding.value->document_revision = document.revision;
    cached.project();
    compose_test_fixture_t reference(document);
    reference.cache.timing_payload_cache_enabled = false;
    reference.project();
    return cached.cache.timing_payload_cache.build_count() == 2 &&
        workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference);
}
auto workspace_timing_cache_rejects_reused_ids() -> bool {
    auto original = workspace_timing_cache_song();
    original.charts.front().measures.push_back({.id = measure_id_t{2}});
    original.charts.front().bpms.back().position = {measure_id_t{2}, 0};
    compose_test_fixture_t previous(original);
    previous.project();
    auto replacement = original;
    auto& chart = replacement.charts.front();
    chart.measures.front().id = measure_id_t{10};
    chart.measures.back().id = measure_id_t{20};
    chart.measures.front().definition.length = {2, 1};
    chart.measures.front().definition.meter = meter_t{8, 4};
    for (auto& note : chart.measures.front().notes) note.position.measure = measure_id_t{10};
    chart.lane_layouts.front().position.measure = measure_id_t{10};
    for (auto& bpm : chart.bpms) bpm.position.measure = measure_id_t{10};
    chart.bpms.back().position.measure = measure_id_t{20};
    chart.bpms.back().beats_per_minute = {777, 1};
    compose_test_fixture_t cached(replacement), reference(replacement);
    cached.cache = std::move(previous.cache);
    reference.cache.timing_payload_cache_enabled = false;
    cached.project(); reference.project();
    const auto& payloads = cached.cache.timing_payload_cache.value();
    const auto changed = std::ranges::find(payloads, std::uint64_t{10069},
                                          &compose_timing_marker_payload_t::id);
    return changed != payloads.end() && changed->row == 1920 &&
        cached.cache.timing_payload_cache.build_count() == 2 &&
        workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference);
}
} // namespace

TEST_CASE("Workspace timing cache matches reference with current scrolling and selection",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_tracks_frame_state());
}
TEST_CASE("Workspace timing payload cache invalidates document revisions",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_invalidates_revision());
}
TEST_CASE("Workspace timing cache rebuilds measure mapping for reused document IDs",
          "[sys][compose][workspace-cache]") {
    expect_compose_test(workspace_timing_cache_rejects_reused_ids());
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("Shared stem row table is prepared once for song and selected channel and reuses scroll rows",
          "[sys][waveform-projection][bmson][stem][row-table]") {
    channel_projection_fixture_t fixture;
    fixture.project();
    const auto generation = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == generation);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 202);
    fixture.interaction.viewport_scroll_pixels += 1;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 1);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().reused_rows == 201);
    const auto scrolled_generation = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == scrolled_generation);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 1);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().reused_rows == 201);
    fixture.flattened.view.compose.pixels_per_standard_measure *= 2;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().built_rows == 202);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
}

TEST_CASE("Stem row table retires display rings and native cut derivation when timing identity changes",
          "[sys][waveform-projection][bmson][stem][row-table]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    const auto generation = fixture.cache.waveform.waveform_row_table.clock.value().generation;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.front().beats_per_minute = {240, 1};
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    REQUIRE(fixture.cache.timing.marker_timing_index);
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.clock.value().generation == generation + 1);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == 202);
    expect_gpu_work(fixture.cache.waveform.channel_waveform_slices.value().has_value());
    const auto unchanged = fixture.cache.waveform.waveform_row_table.viewport.value().generation;
    const auto projected = fixture.cache.waveform.channel_projection.ring.value().projected_rows;
    const auto calls = fixture.cache.waveform.channel_projection.ring.value().projection_calls;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.viewport.value().generation == unchanged);
    // A cache hit leaves the last projection counters intact.
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projected_rows == projected);
    expect_gpu_work(fixture.cache.waveform.channel_projection.ring.value().projection_calls == calls);
}
#endif

namespace {
void t475_check(bool value, std::source_location source = std::source_location::current()) {
    INFO("dense projection assertion at " << source.line());
    CHECK(value);
}
void t475_require(const auto& value) { REQUIRE(static_cast<bool>(value)); }
void t475_project(compose_test_fixture_t& fixture) {
    wormedit::test::require_workspace_result(rebind_workspace(*fixture.binding.value, fixture.history));
    const auto model = workspace_view_t::from(*fixture.binding.value, fixture.history.document);
    t475_require(model.value.has_value());
    fixture.flattened = frontend_workspace_view_t::from(*model.value, fixture.history.document, "", {}, false, dock_sort_t::natural_name,
        {}, {}, {}, &fixture.cache.keysound_list_cache);

    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    std::string diagnostics;
    for (const auto& feedback : fixture.flattened.projection_feedback) diagnostics += feedback.message;
    INFO(diagnostics);
    t475_require(fixture.flattened.view.compose.scene_available);
}

auto t475_inspector_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes = {{.id = object_id_t{500},
        .position = {measure_id_t{1}, 0}, .lane = lane_id_t{1}, .label = "first"}};
    chart.measures.push_back({.id = measure_id_t{2},
        .notes = {{.id = object_id_t{600}, .position = {measure_id_t{2}, 240},
                   .lane = lane_id_t{1}, .label = "late"}},
        .bgm_events = {{.id = object_id_t{700}, .position = {measure_id_t{2}, 480},
                       .source_lane = object_id_t{800}}}});
    chart.bgm_source_lanes = {{.id = object_id_t{800}, .name = "BGM"}};
    song.audio_assets = {{.id = asset_id_t{900}, .source_path = "retained.wav"}};
    song.resources = {{.id = resource_id_t{901}, .name = "Retained sound",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample, resource_role_t::bgm}}};
    return song;
}
void t475_compare_fresh(compose_test_fixture_t& retained) {
    compose_test_fixture_t fresh{retained.history.document};
    fresh.binding.value->selected_chart = retained.binding.value->selected_chart;
    fresh.interaction = retained.interaction;
    fresh.application = retained.application;
    t475_project(fresh); // Establish the source before copying transient pointer state.
    fresh.interaction = retained.interaction;
    fresh.cache.frame.paint_pointer = retained.cache.frame.paint_pointer;
    t475_project(fresh);
    const auto& a = retained.flattened.view.compose;
    const auto& b = fresh.flattened.view.compose;
    t475_check(a.selected_note_label_note_id.value == b.selected_note_label_note_id.value);
    t475_check(std::string_view(a.selected_note_label) ==
          std::string_view(b.selected_note_label));
    t475_check(a.selected_bgm_event_id.value == b.selected_bgm_event_id.value);
    t475_check(a.selected_assigned_resource_id.value == b.selected_assigned_resource_id.value);
    t475_check(a.selected_bmson_note_event_id.value == b.selected_bmson_note_event_id.value);
    t475_check(a.selected_bmson_source_lane_id.value == b.selected_bmson_source_lane_id.value);
    std::string labels;
    for (std::size_t i = 0; i < std::min(a.note_endpoints.size(), b.note_endpoints.size()); ++i) {
        labels += std::string{"note "} + std::to_string(a.note_endpoints[i].note_id.value) +
            " actual=" + std::string{a.note_endpoints[i].sound_label} + " reference=" + std::string{b.note_endpoints[i].sound_label} +
            " detail actual=" + std::string{a.note_endpoints[i].sound_detail} +
            " reference=" + std::string{b.note_endpoints[i].sound_detail} + "; ";
    }
    INFO(labels);
    std::string sources = "doc revision=" + std::to_string(retained.history.document.revision) +
        " scene revision=" + std::to_string(retained.cache.scene.index.value()->document_revision);
    for (const auto& note : retained.cache.scene.index.value()->notes)
        sources += " id=" + std::to_string(note.note.value) + " sound=" +
            std::to_string(note.sound.value_or(resource_id_t{}).value);
    if (retained.cache.scene.static_source.value().storage) {
        for (const auto& [id, label] : retained.cache.scene.static_source.value().storage->compose.sound_labels)
            sources += " static label " + std::to_string(id) + "=" + label.first;
    }
    INFO(sources);
    compare_scalar_notes(a, b);
    compare_scalar_holds_bgm(a, b);
    compare_scalar_grid(a, b);
    t475_check(a.paint_ghost_visible == b.paint_ghost_visible);
    if (a.paint_ghost_visible) {
        compare_scalar_bounds(a.paint_ghost.bounds, b.paint_ghost.bounds);
        t475_check(a.paint_ghost.paint_erase_preview == b.paint_ghost.paint_erase_preview);
    }
}
void t475_select_note(compose_test_fixture_t& fixture, object_id_t id) {
    fixture.interaction->selected_notes = {id};
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::note, id}};
    t475_project(fixture);
}
void t475_project_at_row(compose_test_fixture_t& fixture, std::uint64_t row, bool downscroll) {
    fixture.application.preferences.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    t475_project(fixture);
    fixture.interaction->viewport_scroll_pixels = -20; // Keep row zero inside both hit-test directions.
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.downscroll == downscroll);
    const auto lane = view.lanes[0];
    const auto source_y = row * view.pixels_per_standard_measure / 960.0 -
                          fixture.interaction->viewport_scroll_pixels;
    fixture.cache.frame.paint_pointer = std::pair{(lane.bounds.left + lane.bounds.right) / 2,
        downscroll ? view.viewport_height_pixels - source_y : source_y};
    t475_project(fixture);
    INFO("paint target row=" << row << " downscroll=" << downscroll
        << " x=" << fixture.cache.frame.paint_pointer->first << " y=" << fixture.cache.frame.paint_pointer->second
        << " lane bounds=" << lane.bounds.top << "," << lane.bounds.bottom);
    t475_require(fixture.flattened.view.compose.paint_ghost_visible);
}
} // namespace

TEST_CASE("Dense inspector locations reuse exact source values through selection and viewport changes",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    for (const auto id : {object_id_t{500}, object_id_t{600}}) {
        t475_select_note(fixture, id);
        const auto scanned = fixture.cache.scene.selected_source_scanned;
        fixture.interaction->pixels_per_standard_measure = 250;
        fixture.interaction->viewport_scroll_pixels = 15;
        fixture.interaction->cursor = {measure_id_t{2}, 120};
        t475_project(fixture);
        t475_check(fixture.cache.scene.selected_source_scanned == scanned);
        t475_check(fixture.cache.scene.selected_source_reused > 0);
        t475_compare_fresh(fixture);
    }
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{700}}};
    t475_project(fixture);
    const auto scanned = fixture.cache.scene.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.scene.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_bgm_event_id.value == 700);
    t475_compare_fresh(fixture);
}

TEST_CASE("Dense inspector locations invalidate after label edit delete and undo",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{600});
    t475_require(apply_note_label_edit(
        fixture.history, {.chart = chart_id_t{1}, .notes = {object_id_t{600}}, .label = "edited"}));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "edited");
    t475_compare_fresh(fixture);
    t475_require(dispatch_editor_action(fixture.history, *fixture.interaction,
        delete_editor_selection_action_t{}));
    t475_select_note(fixture, object_id_t{600});
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id.value == 0);
    t475_require(undo_document_command(fixture.history));
    t475_select_note(fixture, object_id_t{600});
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "edited");
    t475_compare_fresh(fixture);
    t475_require(undo_document_command(fixture.history));
    t475_select_note(fixture, object_id_t{600});
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "late");
    t475_compare_fresh(fixture);
}

TEST_CASE("Dense inspector geometry belongs to the actual document and chart source",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t prior{t475_inspector_song()};
    t475_project(prior);
    t475_select_note(prior, object_id_t{600});
    auto replacement_song = t475_inspector_song();
    replacement_song.charts.front().measures.back().notes.front().label = "replacement";
    replacement_song.charts.front().measures.front().definition = {{2, 1}, meter_t{8, 4}};
    compose_test_fixture_t replacement{std::move(replacement_song)};
    replacement.cache = std::move(prior.cache);
    replacement.interaction = prior.interaction;
    t475_project(replacement);
    t475_check(std::string_view(replacement.flattened.view.compose.selected_note_label) == "replacement");
    t475_check(replacement.cache.scene.index.key()->document_identity == &replacement.history.document);
    t475_compare_fresh(replacement);
    auto& charts = replacement.history.document.charts;
    auto chart = charts.front();
    chart.id = chart_id_t{2};
    chart.measures.back().notes.front().label = "other chart";
    charts.push_back(std::move(chart));
    replacement.binding.value->selected_chart = chart_id_t{2};
    replacement.interaction.reset();
    t475_project(replacement);
    t475_select_note(replacement, object_id_t{600});
    t475_check(std::string_view(replacement.flattened.view.compose.selected_note_label) == "other chart");
}

TEST_CASE("Indexed paint occupancy uses only matching heads including across hold tails and lanes",
          "[sys][frame-timing][dense-projection]") {
    for (const auto downscroll : {false, true}) {
        auto song = compose_test_song();
        auto& chart = song.charts.front();
        chart.lane_layouts.front().layout.lanes.push_back(
            {.id = lane_id_t{2}, .name = "Other", .display_order = 1});
        chart.measures.front().notes = {
            {.id = object_id_t{500}, .position = {measure_id_t{1}, 0},
             .lane = lane_id_t{1}, .kind = note_kind_t::hold, .end = local_position_t{measure_id_t{1}, 480}},
            {.id = object_id_t{600}, .position = {measure_id_t{1}, 240}, .lane = lane_id_t{2}}};
        compose_test_fixture_t fixture{std::move(song)};
        fixture.application.project.compose_gesture.note_paint_mode_active = true;
        for (const auto row : {0U, 240U, 480U, 720U}) {
            const auto scanned = fixture.cache.scene.paint_occupancy_scanned;
            t475_project_at_row(fixture, row, downscroll);
            t475_check(fixture.flattened.view.compose.paint_ghost.paint_erase_preview == (row == 0));
            t475_check(fixture.cache.scene.paint_occupancy_scanned - scanned <= 3);
            t475_compare_fresh(fixture);
        }
    }
}

TEST_CASE("Dense inspector missing locations recover on insertion and read current resource values",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{999});
    const auto scanned = fixture.cache.scene.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.scene.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id.value == 0);
    auto inserted = fixture.history.document;
    auto& notes = inserted.charts.front().measures.back().notes;
    notes.insert(notes.begin(), {.id = object_id_t{999}, .position = {measure_id_t{2}, 0},
        .lane = lane_id_t{1}, .label = "inserted"});
    t475_require(commit_document_command(fixture.history, std::move(inserted), "Insert memo target"));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "inserted");
    t475_require(apply_resource_assignment(fixture.history,
        {.chart = chart_id_t{1}, .resource = resource_id_t{901},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{999}}}}));
    t475_project(fixture);
    t475_check(fixture.flattened.view.compose.selected_assigned_resource_id.value == 901);
    t475_compare_fresh(fixture);
    auto reordered = fixture.history.document;
    std::ranges::reverse(reordered.charts.front().measures.back().notes);
    t475_require(commit_document_command(fixture.history, std::move(reordered), "Reorder memo targets"));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "inserted");
    t475_compare_fresh(fixture);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace {
struct packed_projection_fixture_t : channel_projection_fixture_t {
    packed_projection_fixture_t() {
        auto& chart = history.document.charts.front();
        chart.measures.resize(2);
        chart.bpms = {{.id = object_id_t{99000}, .position = {measure_id_t{1}, 0},
                       .beats_per_minute = {2400, 1}}};
        chart.bgm_source_lanes = {{.id = object_id_t{3}}, {.id = object_id_t{4}}};
        chart.sound_channels[0].source_clip.begin_frame =
            history.document.audio_assets.back().stream.frame_count.value() / 4;
        chart.sound_channels[1].source_clip.end_frame = 1;
        chart.measures[0].bgm_events.push_back({.id = object_id_t{74002},
            .position = {measure_id_t{1}, 480}, .source_lane = object_id_t{3}});
        flattened.view.compose.selected_bmson_bgm_event_id.value = 0;
        interaction.chart = chart.id;
        interaction.pixels_per_standard_measure = interaction.viewport_height_pixels = 200;
        cache.timing.marker_timing_index = timing_index_t::from(chart).value;
        expect_gpu_work(cache.timing.marker_timing_index.has_value());
    }
    void project_packed(bool ready = true) {
        const std::optional<asset_id_t> absent;
        const std::shared_ptr<const audio::waveform_analysis_result_t> missing;
        const bmson_waveform_projection_context_t context{flattened, history, workspace,
            &interaction, cache, retained_sources_for(history.document, project_sources), {absent, missing}, {absent, missing}, 1,
            [&](asset_id_t) { return ready ? channel_analysis : nullptr; }};
        populate_bmson_waveform_projection(context);
    }
    auto piece() const -> const wormedit::ui::compose_stem_waveform_view_t& {
        const auto views = std::span(flattened.view.compose.stem_waveforms);
        const auto found = std::ranges::find(views, std::uint64_t{74000},
                                             &wormedit::ui::compose_stem_waveform_view_t::stem_key);
        expect_gpu_work(found != views.end());
        return *found;
    }
};
void check_packed_clip_rows(const packed_projection_fixture_t& fixture) {
    const auto& piece = fixture.piece();
    expect_gpu_work(piece.piece_kind == wormedit::ui::compose_waveform_piece_kind_t::packed_event);
    expect_gpu_work(piece.source_lane_id == object_id_t{3} && !piece.pending && piece.rows.size() > 0);
    const auto downscroll = compose_field_downscroll(fixture.history.document.charts.front());
    for (const auto& row : piece.rows) {
        expect_gpu_work(downscroll ? row.y_pixels >= 99 : row.y_pixels <= 101);
        expect_gpu_work(row.absolute_peak[0] >= .20f);
    }
}
}
TEST_CASE("BMSON packed waveform uses its native clip and next column event",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    fixture.project_packed();
    check_packed_clip_rows(fixture);
    const auto key = fixture.piece().stem_key;
    const auto builds = fixture.cache.stems.packed_waveform_index.build_count();
    fixture.interaction.viewport_scroll_pixels = 1;
    fixture.project_packed();
    expect_gpu_work(fixture.piece().stem_key == key);
    expect_gpu_work(fixture.cache.stems.packed_waveform_index.build_count() == builds);
    INFO("Scrolled packed rows: " << fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows == 1);
    const auto projected = fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows;
    const auto calls = fixture.cache.stems.packed_waveforms.front().projection.ring.value().projection_calls;
    const auto rows = fixture.cache.stems.packed_waveforms.front().projection.view.value().rows.data();
    const auto source = std::get<0>(*fixture.cache.stems.packed_waveforms.front().slices.key());
    fixture.project_packed();
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projected_rows == projected);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.ring.value().projection_calls == calls);
    expect_gpu_work(fixture.cache.stems.packed_waveforms.front().projection.view.value().rows.data() == rows);
    expect_gpu_work(std::get<0>(*fixture.cache.stems.packed_waveforms.front().slices.key()) == source);
    expect_gpu_work(fixture.cache.stems.packed_waveform_index.build_count() == builds);
}
TEST_CASE("BMSON packed waveform pending band stays inside its event interval",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    fixture.project_packed(false);
    const auto piece = fixture.piece();
    expect_gpu_work(piece.pending && piece.rows.size() == 0 && piece.bounded);
    expect_gpu_work(piece.end_y_pixels - piece.begin_y_pixels <= 101);
    expect_gpu_work(piece.begin_y_pixels >= 0 && piece.end_y_pixels <= 200);
    fixture.project_packed();
    check_packed_clip_rows(fixture);
    fixture.history.document.audio_assets.back().stream.sample_rate = 0;
    fixture.history.document.audio_assets.back().stream.frame_count.reset();
    fixture.sources.clear();
    ++fixture.history.document.revision;
    fixture.project_packed(false);
    expect_gpu_work(fixture.piece().pending && fixture.piece().bounded);
}
TEST_CASE("BMSON packed sample tail draws beyond authored timing without changing stems",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    auto& chart = fixture.history.document.charts.front();
    chart.measures.resize(1);
    chart.measures[0].bgm_events.resize(1);
    ++fixture.history.document.revision;
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    const auto authored_rows = fixture.cache.timing.marker_timing_index->total_rows;
    fixture.interaction.viewport_scroll_pixels = 220;
    fixture.project_packed();
    const auto& piece = fixture.piece();
    expect_gpu_work(!piece.pending && piece.rows.size() > 0);
    expect_gpu_work(fixture.cache.stems.packed_waveform_row_table.clock.value().total_rows > authored_rows);
    expect_gpu_work(fixture.cache.waveform.waveform_row_table.clock.value().total_rows == authored_rows);
    expect_gpu_work(fixture.cache.timing.marker_timing_index->total_rows == authored_rows);
    expect_gpu_work(chart.measures.size() == 1);
    const auto previous_peak = piece.rows[0].absolute_peak[0];
    chart.bpms.front().beats_per_minute = {4800, 1};
    ++fixture.history.document.revision;
    fixture.cache.timing.marker_timing_index = timing_index_t::from(chart).value;
    fixture.project_packed();
    expect_gpu_work(fixture.piece().rows[0].absolute_peak[0] < previous_peak);
}
#endif

namespace {
auto resizable_stem_song() -> song_document_t {
    auto song = compose_test_song();
    song.audio_assets = {{.id = asset_id_t{900}, .source_path = "音楽-long-stem.ogg"}};
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.sound_channels = {{.id = object_id_t{901},
        .source_clip = {.id = clip_id_t{902}, .source = asset_id_t{900}}},
        {.id = object_id_t{906}, .source_clip = {.id = clip_id_t{907}, .source = asset_id_t{900}}}};
    chart.bgm_source_lanes = {{.id = object_id_t{903}, .stem_channel = object_id_t{901}},
        {.id = object_id_t{904}}, {.id = object_id_t{905}, .stem_channel = object_id_t{906}}};
    return song;
}
void scroll_resized_bgm(compose_test_fixture_t& fixture, double delta) {
    auto action = cursor_placement_action(fixture);
    action.shortcut_window_focused = true;
    action.compose_viewport_inputs.resize(1);
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_x = delta,
        .cursor_x_pixels = fixture.flattened.view.compose.playable_field_right_pixels + 20,
        .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
}
TEST_CASE("Compose stem width changes reproject same revision and preserve packed width",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    double width = 36;
    std::size_t calls{};
    fixture.application.services.compose_bgm_lane_width = [&](auto, auto, auto) {
        ++calls;
        return width;
    };
    fixture.project();
    const auto revision = fixture.history.document.revision;
    const auto generation = fixture.flattened.view.compose.static_scene_source_generation;
    expect_compose_test(calls == 2);
    width = 300;
    fixture.project();
    expect_compose_test(calls == 4);
    expect_compose_test(fixture.cache.scene.bgm_lane_widths_pixels == std::vector<double>{300, 36, 300});
    expect_compose_test(fixture.flattened.view.compose.static_scene_source_generation != generation);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].width_pixels == 300);
    expect_compose_test(std::string(fixture.flattened.view.compose.bgm_lanes[0].filename) == "音楽-long-stem.ogg");
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
}
TEST_CASE("Compose BGM dispatcher reaches last widened lane and clamps resized content",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    fixture.application.services.compose_bgm_lane_width = [](auto, auto, auto) { return 480; };
    fixture.project();
    scroll_resized_bgm(fixture, 10000);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto maximum = 996.0 - fixture.cache.frame.preview_scene_request.bgm_pane_width_pixels;
    expect_compose_test(std::abs(fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[1] - maximum) < 1e-6);
    expect_compose_test(view.bgm_lanes[view.bgm_lanes.size() - 1].source_lane_id == object_id_t{905});
    scroll_resized_bgm(fixture, -10000);
    expect_compose_test(fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[1] == 0);
    expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
}

namespace {
auto t463_shared_asset_song() -> song_document_t {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.source_family = format_family_t::bmson;
    chart.measures.front().notes.clear();
    song.audio_assets = {{.id = asset_id_t{200}, .stream = {44'100, 2, 441'000, true}}};
    for (uint64_t index = 0; index < 3; ++index) {
        song.resources.push_back({.id = resource_id_t{100 + index},
            .name = "Shared source " + std::to_string(index), .asset = asset_id_t{200},
            .roles = {resource_role_t::sample, resource_role_t::bgm},
            .sound_kind = index < 2 ? sound_kind_t::stem : sound_kind_t::keysound});
        chart.sound_channels.push_back({.id = object_id_t{300 + index},
            .name = song.resources.back().name,
            .source_clip = {clip_id_t{300 + index}, asset_id_t{200}, 0, std::nullopt},
            .events = {{.id = object_id_t{400 + index}, .position = {measure_id_t{1}, index * 120},
                        .playable = false}}});
        chart.bgm_source_lanes.push_back({.id = object_id_t{600 + index},
            .name = song.resources.back().name, .source_order = static_cast<size_t>(index),
            .stem_channel = index < 2 ? std::optional{object_id_t{300 + index}} : std::nullopt});
        chart.measures.front().bgm_events.push_back({.id = object_id_t{500 + index},
            .position = {measure_id_t{1}, index * 120}, .source_lane = object_id_t{600 + index},
            .sound = resource_id_t{100 + index}, .sound_channel_event = object_id_t{400 + index}});
    }
    return song;
}
void t463_check_projected_rows(const compose_test_fixture_t& fixture) {
    const auto& view = fixture.flattened.view.compose;
    for (uint64_t index = 0; index < 3; ++index) {
        const auto rows = std::span(view.keysounds);
        const auto row = std::ranges::find(rows, wormedit::resource_id_t{100 + index}, &wormedit::ui::compose_keysound_row_view_t::resource_id);
        t475_require(row != rows.end());
        t475_check(row->stem_source_lane_id.value == (index < 2 ? 600 + index : 0));
    }
}
}
TEST_CASE("BMSON Draw projection keeps duplicate asset stem and packed resource ownership distinct",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 101;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id.value == 601);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    fixture.application.project.compose_gesture.draw_source_id = 102;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id.value == 0);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds);
}
TEST_CASE("BMSON Draw resource ownership cache invalidates revision and replacement identity",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    t475_project(fixture);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].stem_channel.reset();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Unlink stem"));
    t475_project(fixture);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds + 1);
    t475_check(!fixture.cache.stems.draw_stem_sources.value().resource_lanes.contains(101));
    compose_test_fixture_t replacement(t463_shared_asset_song());
    replacement.history.document.revision = fixture.history.document.revision;
    replacement.cache = std::move(fixture.cache);
    t475_project(replacement);
    t463_check_projected_rows(replacement);
    t475_check(replacement.cache.stems.draw_stem_sources.build_count() == builds + 2);
}

TEST_CASE("BMSON Draw source names remain frame owned after document edits and frame moves",
          "[sys][compose][bmson][stem][draw-source][frame-arena]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 101;
    t475_project(fixture);
    const std::string original{fixture.flattened.view.compose.draw_source_stem_lane_name};
    auto retained = std::move(fixture.flattened);
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].name = std::string(256, 'v');
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Rename stem"));
    t475_check(std::string(retained.view.compose.draw_source_stem_lane_name) == original);
    const auto rows = std::span(retained.view.compose.keysounds);
    const auto row = std::ranges::find(rows, wormedit::resource_id_t{uint64_t{101}}, &wormedit::ui::compose_keysound_row_view_t::resource_id);
    t475_require(row != rows.end());
    t475_check(std::string(row->stem_lane_name.c_str()) == original);
    t475_project(fixture);
    t475_check(std::string(fixture.flattened.view.compose.draw_source_stem_lane_name) == std::string(256, 'v'));
}

namespace {
auto chip_source_song() -> song_document_t {
    auto song = compose_test_song();
    song.audio_assets = {{.id = asset_id_t{200}, .stream = {44'100, 2, 441'000, true}}};
    for (std::uint64_t index = 0; index < 80; ++index)
        song.resources.push_back({.id = resource_id_t{100 + index},
            .name = "sample " + std::to_string(index), .asset = asset_id_t{200},
            .roles = {resource_role_t::sample}});
    song.resources.back().name = "音楽.wav";
    song.resources.back().target_identities = {{.family = format_family_t::bms, .identifier = "0A", .namespace_name = "wav"}};
    return song;
}
void project_chip_request(compose_test_fixture_t& fixture, wormedit::ui::keysound_dock_request_t request) {
    wormedit::test::require_workspace_result(rebind_workspace(*fixture.binding.value, fixture.history));
    const auto model = workspace_view_t::from(*fixture.binding.value, fixture.history.document);
    t475_require(model.value.has_value());
    fixture.flattened = frontend_workspace_view_t::from(*model.value, fixture.history.document, "", {}, false, dock_sort_t::natural_name,
        request, {}, {}, &fixture.cache.keysound_list_cache);

    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.value,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    t475_require(fixture.flattened.view.compose.scene_available);
}
auto dispatch_chip_input(compose_test_fixture_t& fixture, wormedit::ui::compose_input_action_t input) -> std::string {
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = 1;
    action.compose_input_project_id.value = fixture.history.document.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = input;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
}

TEST_CASE("Tool chip projects the actual off-page filtered sound with a retained source cache",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    wormedit::ui::keysound_dock_request_t request{};
    request.include_ungrouped = true;
    request.primary_resource_id = wormedit::resource_id_t{100};
    request.automatic_search = "not this source";
    project_chip_request(fixture, request);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "0A 音楽.wav");
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(!fixture.flattened.view.compose.keysound_projected_filters_unrestricted);
    const auto builds = fixture.cache.stems.draw_stem_sources.build_count();
    request.automatic_search = {};
    request.primary_resource_id = wormedit::resource_id_t{179};
    request.automatic_row_limit = 8;
    project_chip_request(fixture, request);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.keysound_projected_primary_resource_id.value == 179);
    t475_check(view.keysound_projected_filters_unrestricted && view.keysound_primary_display_index_valid);
    t475_check(view.keysound_primary_display_index == 80);
    t475_check(view.keysound_display_total_count == 81);
    t475_check(fixture.cache.stems.draw_stem_sources.build_count() == builds);
}

TEST_CASE("Tool chip names are bounded UTF-8 and survive a moved frame and source rename",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    t475_project(fixture);
    auto retained = std::move(fixture.flattened);
    auto proposed = fixture.history.document;
    proposed.resources.back().name.clear();
    for (int index = 0; index < 400; ++index) proposed.resources.back().name += "音";
    const auto renamed = commit_document_command(fixture.history, std::move(proposed), "Rename chip source");
    t475_require(renamed);
    t475_project(fixture);
    const std::string_view label = fixture.flattened.view.compose.draw_source_label;
    t475_check(label.size() <= 512 && label.ends_with("…"));
    t475_check(label.substr(3, label.size() - 6).size() % 3 == 0);
    t475_check(std::string_view(retained.view.compose.draw_source_label) == "0A 音楽.wav");
}

TEST_CASE("Deleted Draw source remains picked and warns until an explicit repick",
          "[sys][compose][source-chip][missing]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.application.project.compose_gesture.draw_source_kind = 1;
    fixture.application.project.compose_gesture.draw_source_id = 179;
    t475_project(fixture);
    auto proposed = fixture.history.document;
    proposed.resources.pop_back();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Delete chip source"));
    t475_project(fixture);
    t475_check(fixture.application.project.compose_gesture.draw_source_kind == 1 && fixture.application.project.compose_gesture.draw_source_id == 179);
    t475_check(fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "missing source");
    const auto revision = fixture.history.document.revision;
    const auto undos = fixture.history.undo.size();
    const auto attempted = paint_stroke_inputs(fixture, 179);
    const auto rejected = dispatch_chip_input(fixture, attempted.compose_inputs[0]);
    t475_check(rejected.find("paint sound is not an existing sample resource") != std::string::npos);
    t475_check(fixture.history.document.revision == revision && fixture.history.undo.size() == undos);
    t475_check(!fixture.interaction->gesture<wormedit::pending_note_paint_gesture_t>());
    const auto repicked = dispatch_chip_input(fixture, {.kind = WORM_UI_COMPOSE_INPUT_DRAW_SOURCE});
    t475_check(repicked == "Draw source selected.");
    t475_project(fixture);
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
}

TEST_CASE("Unassigned Draw source describes resource assignment rather than hitsound audibility",
          "[sys][compose][source-chip][hitsound]") {
    compose_test_fixture_t fixture(chip_source_song());
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
    fixture.interaction->armed_hitsample = osu_hitsample_t{.whistle = true, .clap = true};
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "no sound resource");
}

TEST_CASE("Tool chip BGA uses resource identity and Layer reveals the off-page target ordinal",
          "[sys][compose][source-chip][bga][semantic-layer]") {
    compose_test_fixture_t fixture(bga_virtual_preview_song());
    fixture.application.project.compose_gesture.draw_source_kind = 2;
    fixture.application.project.compose_gesture.draw_source_id = 501;
    for (std::uint64_t index = 0; index < 100; ++index)
        fixture.history.document.charts.front().semantic_layers.push_back({
            .id = layer_id_t{index + 1}, .name = "Drums " + std::to_string(index)});
    fixture.application.project.semantic_layer.semantic_layer_target = layer_id_t{100};
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(std::string_view(view.draw_source_label) == "501 preview image");
    t475_check(!view.draw_source_missing && view.draw_source_id.value != fixture.history.document.resources.front().asset.value);
    t475_check(view.semantic_layer_target_row == 99);
    t475_check(std::string_view(view.semantic_layer_target.name) == "Drums 99");
}

namespace {
auto move_feedback_dispatch_song() -> song_document_t {
    auto song = compose_test_song();
    song.charts.front().measures.front().notes = {
        {.id = object_id_t{101},
         .position = {measure_id_t{1}, 240},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 360}},
        {.id = object_id_t{201},
         .position = {measure_id_t{1}, 600},
         .lane = lane_id_t{1},
         .kind = note_kind_t::hold,
         .end = local_position_t{measure_id_t{1}, 660}},
        {.id = object_id_t{202}, .position = {measure_id_t{1}, 720}, .lane = lane_id_t{1}}};
    return song;
}
struct move_feedback_dispatch_fixture_t {
    compose_test_fixture_t compose;
    notification_center_t center;
    notification_sink_t sink{center};
    double x{}, pixels_per_row{};
    explicit move_feedback_dispatch_fixture_t(song_document_t song = move_feedback_dispatch_song())
        : compose(std::move(song)) {
        compose.application.project.compose_view.edit_snap_enabled = false;
        compose.project();
        expect_compose_test(static_cast<bool>(
            dispatch_editor_action(compose.history, *compose.interaction,
                                   select_editor_notes_action_t{{object_id_t{101}}})));
        compose.project();
        const auto& view = compose.flattened.view.compose;
        const auto endpoints = std::span(view.note_endpoints);
        const auto source = std::ranges::find_if(
            endpoints, [](const auto& note) { return note.note_id == object_id_t{101} && !note.tail; });
        expect_compose_test(source != endpoints.end());
        x = (source->bounds.left + source->bounds.right) * .5;
        pixels_per_row = view.pixels_per_standard_measure / 960;
        (void)dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
        expect_compose_test(compose.interaction->gesture<wormedit::pending_note_move_gesture_t>() != nullptr);
    }
    auto dispatch(WormUiComposeInputKind kind, double row, bool to_bgm = false) -> std::string {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_project_id.value = compose.history.document.id.value;
        action.compose_input_chart_id.value = 1;
        action.compose_input_document_revision = compose.history.document.revision;
        action.compose_inputs.resize(1);
        const auto& view = compose.flattened.view.compose;
        const auto downscroll = compose.cache.frame.preview_scene_request.scroll_direction ==
            editor_field_scroll_direction_t::down;
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
        action.compose_inputs[0] = {
            .kind = kind,
            .note_move_to_bgm = to_bgm,
            .x_pixels = to_bgm ? view.playable_field_right_pixels + 10 : x,
            .y_pixels = compose_project_y(row + .000001, pixels_per_row,
                compose.interaction->viewport_scroll_pixels, view.viewport_height_pixels,
                downscroll)};
        action.compose_input_viewport_width_pixels =
            compose.flattened.view.compose.viewport_width_pixels;
        viewport_input_controller_t viewport;
        editor_navigation_input_controller_t navigation;
        return dispatch_compose_input_action(action, compose.history, *compose.binding.value,
                                             *compose.interaction, viewport, navigation,
                                             compose.application, compose.cache, 0, &sink);
    }
};
void check_projected_move_conflicts(move_feedback_dispatch_fixture_t& fixture) {
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.move_conflict_count == 1);
    const auto endpoints = std::span(view.note_endpoints);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.proposal_ghost && note.move_conflict != note_move_conflict_t::none;
                        }) == 2);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.move_conflict_occupant;
                        }) == 3);
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies),
        [](const auto& hold) { return hold.proposal_ghost && hold.move_conflict != note_move_conflict_t::none; }));
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies),
        [](const auto& hold) { return hold.note_id == object_id_t{201} && hold.move_conflict_occupant; }));
}
void check_move_release_warning(move_feedback_dispatch_fixture_t& fixture) {
    fixture.center.drain_inbox(notification_time_t{0});
    const auto messages = fixture.center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages.front().severity == notification_severity_t::warning);
    expect_compose_test(messages.front().message == "1 notes would overlap");
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
    expect_compose_test(fixture.compose.history.redo.empty());
}
} // namespace

TEST_CASE("Compose move motion projects all conflicts without notifications and release warns once",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    for (int motion = 0; motion < 3; ++motion)
        (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.center.drain_inbox(notification_time_t{0});
    expect_compose_test(fixture.center.snapshot(notification_capacity).empty());
    INFO("Actual move target " << fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position.measure.value
         << ":" << fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position.row
         << " snap=" << fixture.compose.interaction->snap_enabled
         << " scroll=" << fixture.compose.interaction->viewport_scroll_pixels);
    expect_compose_test(fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>()->preview_position ==
                        local_position_t{measure_id_t{1}, 600});
    check_projected_move_conflicts(fixture);
    const auto status = fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(status == "1 notes would overlap");
    check_move_release_warning(fixture);
}

TEST_CASE("Compose move conflict recovery commits one undo and clears projected flags",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    fixture.compose.project();
    expect_compose_test(fixture.compose.flattened.view.compose.move_conflict_count == 0);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 120);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(fixture.compose.history.document.revision == 2);
    const auto& notes = fixture.compose.history.document.charts.front().measures.front().notes;
    const auto moved = std::ranges::find(notes, object_id_t{101}, [](const auto& note) { return note.id; });
    expect_compose_test(moved != notes.end());
    INFO("Actual recovered note " << moved->position.measure.value << ":" << moved->position.row);
    expect_compose_test(moved->position == local_position_t{measure_id_t{1}, 120});
    expect_compose_test(moved->end == local_position_t{measure_id_t{1}, 240});
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.move_conflict_count == 0);
    expect_compose_test(std::ranges::none_of(
        std::span(view.note_endpoints),
        [](const auto& note) { return note.move_conflict != note_move_conflict_t::none || note.move_conflict_occupant; }));
}

TEST_CASE("Compose move release outside actual chart geometry cancels without editing",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 100000);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
}

TEST_CASE("Compose vertical move through an empty target layout keeps red proposals until release",
          "[sys][compose][move][t420]") {
    auto song = move_feedback_dispatch_song();
    auto& chart = song.charts.front();
    chart.measures.push_back({.id = measure_id_t{2}});
    chart.lane_layouts.push_back({.id = object_id_t{300},
                                  .position = {measure_id_t{2}, 0},
                                  .layout = {.profile_id = "empty-target"}});
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 1000);
    expect_compose_test(fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>() != nullptr);
    const auto& pending = *fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>();
    INFO("Actual empty-layout target " << pending.preview_position.measure.value << ":" << pending.preview_position.row);
    expect_compose_test(pending.preview_position == local_position_t{measure_id_t{2}, 40});
    expect_compose_test(pending.preview.front().conflict == note_move_conflict_t::out_of_lane);
    fixture.compose.project();
    expect_compose_test(fixture.compose.flattened.view.compose.move_conflict_count == 1);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 1000);
    check_move_release_warning(fixture);
}

TEST_CASE("Compose no-hit move outside the horizontal field cancels without editing",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    fixture.x = -10000;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
}

TEST_CASE("Compose routes a shifted BGM drop and projects transient amber hold ghosts", "[sys][compose][bgm-move]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 480, true);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 0);
    expect_compose_test(view.move_to_bgm_count == 1);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::any_of(events, [](const auto& event) {
        return event.proposal_ghost && event.has_tail && !event.move_conflict;
    }));
    expect_compose_test(fixture.compose.history.undo.empty());
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 480, true);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
    const auto& measures = fixture.compose.history.document.charts.front().measures;
    const auto event = std::ranges::find(measures.front().bgm_events, object_id_t{101}, &bgm_event_t::id);
    expect_compose_test(event != measures.front().bgm_events.end());
    expect_compose_test(event->position == local_position_t{measure_id_t{1}, 480});
    expect_compose_test(event->end == local_position_t{measure_id_t{1}, 600});
}

TEST_CASE("Compose captures the collision preference and previews every displaced occupant", "[sys][compose][bgm-move]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    // The option is captured at begin; changing it mid-drag cannot change the transaction.
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::reject;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 0 && view.move_to_bgm_count == 2);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::count_if(events, [](const auto& event) {
        return event.proposal_ghost && !event.move_conflict;
    }) == 2);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(fixture.compose.history.undo.size() == 1);
    expect_compose_test(!fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
}

TEST_CASE("Compose shows rejected BGM displacement destinations in red and commits nothing", "[sys][compose][bgm-move]") {
    auto song = move_feedback_dispatch_song();
    song.charts.front().source_family = format_family_t::stepmania;
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.application.preferences.editor_preferences.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 600);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_conflict_count == 1);
    expect_compose_test(std::ranges::any_of(std::span(view.bgm_events), [](const auto& event) {
        return event.proposal_ghost && event.move_conflict;
    }));
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 600);
    expect_compose_test(fixture.compose.history.undo.empty() && !fixture.compose.interaction->gesture<wormedit::pending_note_move_gesture_t>());
}

TEST_CASE("Same sound registered command uses hover selection and dock adds without Undo", "[sys][compose][select-same-sound]") {
    compose_test_fixture_t fixture;
    auto& song = fixture.history.document;
    song.audio_assets = {{.id = asset_id_t{1}, .source_path = "kick.wav"},
                         {.id = asset_id_t{2}, .source_path = "snare.wav"}};
    song.resources = {{.id = resource_id_t{1}, .name = "kick", .asset = asset_id_t{1}, .roles = {resource_role_t::sample}},
                      {.id = resource_id_t{2}, .name = "snare", .asset = asset_id_t{2}, .roles = {resource_role_t::sample}}};
    auto& notes = song.charts[0].measures[0].notes;
    notes[0].sound = notes[1].sound = resource_id_t{1};
    notes[2].sound = resource_id_t{2};
    fixture.project();
    auto& application = fixture.application;
    application.project.sound_selection.sound_selection_chart = chart_id_t{1};
    application.project.sound_selection.sound_selection_revision = song.revision;
    application.project.sound_selection.sound_selection_dock = {resource_id_t{2}};
    application.project.sound_selection.sound_selection_hover = notes[0].id;
    notification_center_t center;
    notification_sink_t sink{center};
    std::optional<timing_lane_pseudonote_outcome_t> timing;
    const auto command = application_command_t{.kind = application_command_kind_t::selection_select_same_sound_chart};
    auto invoke = [&] {
        return invoke_application_command(command, fixture.history, *fixture.binding.value,
            fixture.interaction, timing, application, sink);
    };
    const auto revision = song.revision;
    expect_compose_test(invoke().value);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{notes[0].id, notes[1].id});
    application.project.sound_selection.sound_selection_hover.reset();
    expect_compose_test(invoke().value); // Selection takes precedence over the dock.
    expect_compose_test(fixture.interaction->selected_notes.size() == 2);
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    expect_compose_test(invoke().value);
    expect_compose_test(fixture.interaction->selected_notes == std::vector<object_id_t>{notes[2].id});
    application.project.sound_selection.sound_selection_dock.clear();
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.clear();
    expect_compose_test(!invoke().value);
    expect_compose_test(song.revision == revision && fixture.history.undo.empty());
}

TEST_CASE("Compose mixed move projects BGM tails and collision ghosts without a conversion count",
          "[sys][compose][mixed-move]") {
    const auto collision = GENERATE(false, true);
    auto song = move_feedback_dispatch_song();
    auto& chart = song.charts.front();
    chart.bgm_source_lanes = {{.id = object_id_t{400}, .name = "BGM"}};
    chart.measures.front().bgm_events = {{.id = object_id_t{401},
        .position = {measure_id_t{1}, 120}, .source_lane = object_id_t{400},
        .end = local_position_t{measure_id_t{1}, 240}}};
    if (collision) chart.measures.front().bgm_events.push_back({.id = object_id_t{402},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{400}});
    move_feedback_dispatch_fixture_t fixture{std::move(song)};
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_CANCEL_GESTURE, 240);
    fixture.compose.interaction->selected_objects.push_back(
        {editor_selection_object_kind_t::bgm_event, object_id_t{401}});
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 360);
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.scene_available && view.move_to_bgm_count == 0);
    expect_compose_test((view.move_conflict_count != 0) == collision);
    const auto events = std::span(view.bgm_events);
    expect_compose_test(std::ranges::any_of(events, [collision](const auto& event) {
        return event.event_id == object_id_t{401} && event.proposal_ghost && event.has_tail && event.move_conflict == collision;
    }));
    expect_compose_test(fixture.compose.history.undo.empty());
}

TEST_CASE("Mixed quantize projects BGM hold ghosts and refreshes a BGM-only selection",
          "[sys][frame-timing][mixed-quantize]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes.resize(1);
    chart.measures.front().notes.front().position.row = 130;
    const auto id = chart.measures.front().notes.front().id;
    for (std::uint64_t i = 0; i < 50; ++i)
        chart.bgm_source_lanes.push_back({.id = object_id_t{100000 + i}, .source_order = i});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{90002}, .position = {measure_id_t{1}, 130},
        .source_lane = object_id_t{100000}, .end = local_position_t{measure_id_t{1}, 610}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    fixture.interaction->selected_notes = {id};
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::note, id},
        {editor_selection_object_kind_t::bgm_event, object_id_t{90002}}};
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
                                    quantization_hold_t::both_endpoints);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_preview_moved == 2);
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 2);
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects.erase(fixture.interaction->selected_objects.begin());
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.selected_bgm_event_count == 1 && view.quantize_preview_moved == 1);
    expect_compose_test(view.quantize_preview_rows.size() == 1 && view.quantize_preview_rows[0].bgm);
    expect_compose_test(view.quantize_ghosts.size() == 1 && view.quantize_ghosts[0].has_tail);
    expect_compose_test(view.quantize_ghosts[0].before_head.left >= view.playable_field_right_pixels);
    const auto chart_id = fixture.history.document.charts.front().id.value;
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 12.0;
    fixture.project();
    const auto& clipped = fixture.flattened.view.compose;
    expect_compose_test(clipped.quantize_ghosts.size() == 1 &&
        clipped.quantize_ghosts[0].before_head.left == clipped.playable_field_right_pixels);
    expect_compose_test(clipped.quantize_ghosts[0].before_head.right - clipped.quantize_ghosts[0].before_head.left == 24.0);
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 36.0;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.quantize_ghosts.size() == 0);
    fixture.application.project.compose_view.bgm_horizontal_scroll_pixels[chart_id] = 0.0;
    fixture.project();
    expect_compose_test(fixture.history.undo.empty());
    dispatch_t437_quantize_settings(fixture, quantization_tie_t::earlier,
        quantization_hold_t::both_endpoints, 4, quantization_space_t::standard_measure, true);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(fixture.application.project.quantize.quantize_applied_report.has_value());
    expect_compose_test(fixture.application.project.quantize.quantize_applied_report->rows.size() == 1 &&
                        fixture.application.project.quantize.quantize_applied_report->rows.front().bgm);
    expect_compose_test(fixture.history.document.charts.front().measures.front().notes.front().position.row == 130);
    expect_compose_test(fixture.history.document.charts.front().measures.front().bgm_events.front().position.row == 240);
}

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
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_CONVERT_TO_STOP_RESIZE;
    action.compose_input_project_id.value = fixture.history.document.id.value;
    action.compose_input_chart_id.value = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
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
    case 1: ++action.compose_input_project_id.value; break;
    case 2: ++action.compose_input_chart_id.value; break;
    case 3: ++action.compose_input_document_revision; break;
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
void verify_timing_partition(std::uint8_t mask, double right) {
    using namespace wormedit::ui;
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
}
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
    action.compose_input_project_id.value = f.history.document.id.value;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
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
    action.compose_input_document_revision = f.history.document.revision;
    action.compose_inputs[0].lane_id.value = 1;
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
    action.compose_inputs.resize(0);
    action.compose_input_editor_right_pixels = 700;
    editor_navigation_input_controller_t navigation;
    viewport_input_controller_t viewport;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 700);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.playable_field_right_pixels <= 700);
    expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
    ++action.compose_input_document_revision;
    action.compose_input_editor_right_pixels = 200;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 700);
    expect_compose_test(compose_editor_layout_width(1000, std::numeric_limits<double>::quiet_NaN()) == 1000);
}


namespace {
auto bgm_mix_test_action(const compose_test_fixture_t& fixture, WormUiComposeInputKind kind,
    std::uint64_t lane) -> wormedit::ui::workspace_action_t {
    wormedit::ui::workspace_action_t action{};
    action.compose_input_chart_id.value = 1;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_inputs.resize(1);
    action.compose_inputs[0] = {.kind = kind, .lane_id = {lane}};
    return action;
}
auto bgm_mix_test_song() -> song_document_t {
    auto song = compose_test_song();
    song.charts.front().bgm_source_lanes = {{.id = object_id_t{80000}},
        {.id = object_id_t{80001}}, {.id = object_id_t{80002}}};
    return song;
}
}

TEST_CASE("BGM mute and additive solo are session-only and refresh cached lane headers",
          "[frame-timing][bgm-mix]") {
    compose_test_fixture_t fixture{bgm_mix_test_song()};
    std::vector<std::uint64_t> applied;
    fixture.application.services.transport_set_bgm_mix = [&](auto chart, auto muted) {
        expect_compose_test(chart == chart_id_t{1});
        applied.assign(muted.begin(), muted.end());
        return true;
    };
    fixture.project();
    const auto before = write_native_project(fixture.history.document);
    expect_compose_test(before.value.has_value());
    auto toggle = [&](WormUiComposeInputKind kind, std::uint64_t lane) {
        auto action = bgm_mix_test_action(fixture, kind, lane);
        (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
        expect_compose_test(action.compose_inputs.size() == 0);
        fixture.project();
    };
    toggle(WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].muted);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000, 80002});
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80002);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[1].solo);
    expect_compose_test(!fixture.flattened.view.compose.bgm_lanes[1].mix_silent);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    expect_compose_test(applied == std::vector<std::uint64_t>{80000});
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    toggle(WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80002);
    expect_compose_test(applied.empty());
    const auto after = write_native_project(fixture.history.document);
    expect_compose_test(after.value == before.value);
    expect_compose_test(fixture.history.undo.empty());
    fixture.application.project.bgm.bgm_lane_mix[1].solo.insert(99999);
    expect_compose_test(compose_bgm_muted_lanes(fixture.history.document.charts.front(),
        fixture.application.project.bgm.bgm_lane_mix.at(1)).empty());
    reset_application_project_edit_state(fixture.application);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
    expect_compose_test(static_cast<bool>(fixture.application.services.transport_set_bgm_mix));
}

TEST_CASE("BGM mixing rejects stale missing and runtime-failed controls atomically",
          "[frame-timing][bgm-mix]") {
    compose_test_fixture_t fixture{bgm_mix_test_song()};
    int calls{};
    fixture.application.services.transport_set_bgm_mix = [&](auto, auto) { ++calls; return false; };
    auto action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    ++action.compose_input_document_revision;
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 0);
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 99999);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 0);
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(calls == 1);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
    fixture.application.services.transport_set_bgm_mix = {};
    action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_SOLO, 80001);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(fixture.application.project.bgm.bgm_lane_mix.empty());
}


TEST_CASE("BGM-only charts receive canvas width and mix controls without note interaction state",
          "[frame-timing][bgm-mix]") {
    auto song = bgm_mix_test_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.clear();
    chart.input_profile = {};
    chart.measures.front().notes.clear();
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.services.transport_set_bgm_mix = [](auto, auto) { return true; };
    auto action = bgm_mix_test_action(fixture, WORM_UI_COMPOSE_INPUT_BGM_MUTE, 80000);
    action.compose_input_viewport_width_pixels = 1232;
    action.compose_input_editor_right_pixels = 800;
    refresh_compose_editor_width(action, fixture.history, *fixture.binding.value, fixture.application);
    expect_compose_test(fixture.application.project.compose_view.compose_editor_right_pixels == 800);
    (void)dispatch_compose_bgm_mix_inputs(action, fixture.history, *fixture.binding.value, fixture.application);
    fixture.project();
    expect_compose_test(!fixture.interaction);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes.size() == 3);
    expect_compose_test(fixture.flattened.view.compose.bgm_mix_available);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].muted);
    for (const auto& lane : std::span(fixture.flattened.view.compose.bgm_lanes)) expect_compose_test(lane.bounds.right <= 800);
}


TEST_CASE("Compose classifies armable BGM stem heads after projection and cache reuse",
          "[sys][compose][bmson][scratchpad-stem-projection]") {
    const auto bgm_only = GENERATE(false, true);
    const auto missing = GENERATE(false, true);
    auto song = t463_shared_asset_song();
    song.resources.front().missing = missing;
    if (bgm_only) song.charts.front().lane_layouts.clear();
    compose_test_fixture_t fixture{std::move(song)};
    for (int pass = 0; pass < 2; ++pass) {
        t475_project(fixture);
        const auto& view = fixture.flattened.view.compose;
        const auto events = std::span(view.bgm_events);
        const auto stem = std::ranges::find(events, object_id_t{500}, &wormedit::ui::compose_bgm_event_view_t::event_id);
        const auto packed = std::ranges::find(events, object_id_t{502}, &wormedit::ui::compose_bgm_event_view_t::event_id);
        t475_require(stem != events.end());
        t475_require(packed != events.end());
        t475_check(stem->armable_stem == !missing);
        t475_check(!packed->armable_stem);
    }
}


TEST_CASE("Mouse stem entry receives the same decoded source geometry as lane keys",
          "[sys][compose][bmson][stem-mouse-entry]") {
    const auto alt = GENERATE(false, true);
    const auto short_source = GENERATE(false, true);
    auto song = t463_shared_asset_song();
    song.audio_assets.front().stream = {};
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_gesture.note_paint_mode_active = true;
    fixture.project();
    seed_stem_geometry_fixture(fixture.cache.stems.stem_index, chart_stamp_t::from(fixture.history.document, fixture.history.document.charts.front().id),
        {{.asset = asset_id_t{200}, .source_sample_rate = 48000,
        .source_frame_count = short_source ? 1000U : 480000U}});
    auto action = paint_stroke_inputs(fixture, 100);
    action.compose_inputs[0].paint_intent = 1;
    if (alt) {
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_ALT_POINTER_BEGIN;
        action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_ALT_POINTER_RELEASE;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    INFO(status);
    const auto& chart = fixture.history.document.charts.front();
    expect_compose_test(chart.measures.front().notes.size() == 1);
    expect_compose_test(chart.measures.front().notes.front().sound_channel_event.has_value());
    expect_compose_test(chart.sound_channels.front().events.back().continuation == !short_source);
    expect_compose_test(chart.sound_channels.front().events.back().playable);
    expect_compose_test(!fixture.interaction->hold() && !fixture.interaction->gesture<wormedit::pending_note_paint_gesture_t>());
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(fixture.history.document.audio_assets.front().stream.sample_rate == 0);
}

TEST_CASE("Registered selection scopes preserve mixed objects and expose Select All Objects",
          "[frame-timing][selection-consolidation]") {
    auto song = bga_virtual_preview_song();
    auto& chart = song.charts.front();
    chart.bookmarks.push_back({.id = object_id_t{9001}, .label = "Marker",
        .position = local_position_t{measure_id_t{1}, 240}});
    chart.sections.push_back({.id = object_id_t{9002}, .label = "Section",
        .beat_span = position_span_t{{measure_id_t{1}, 240}, {measure_id_t{1}, 480}}});
    chart.bgm_source_lanes.push_back({.id = object_id_t{700}, .name = "BGM"});
    chart.measures.front().bgm_events.push_back({.id = object_id_t{9003},
        .position = {measure_id_t{1}, 240}, .source_lane = object_id_t{700}});
    compose_test_fixture_t f{std::move(song)};
    f.project();
    const auto revision = f.history.document.revision;
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all_objects").value.is_invoked());
    const auto all = f.interaction->selected_objects;
    const auto notes = f.interaction->selected_notes.size();
    for (const auto kind : {editor_selection_object_kind_t::note, editor_selection_object_kind_t::bga_event,
                           editor_selection_object_kind_t::bgm_event, editor_selection_object_kind_t::bookmark,
                           editor_selection_object_kind_t::section, editor_selection_object_kind_t::bpm_event})
        expect_compose_test(std::ranges::any_of(all, [=](auto object) { return object.kind == kind; }));
    expect_compose_test(invoke_compose_registered_command(f, "selection.invert").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.empty());
    expect_compose_test(f.interaction->selected_objects.size() == all.size() - notes);
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(std::ranges::is_permutation(f.interaction->selected_objects, all));
    expect_compose_test(f.history.document.revision == revision && f.history.undo.empty());
    // From the cursor onward: the BGM event at row 240 lies before it.
    f.interaction->selected_objects.clear();
    f.interaction->selected_notes.clear();
    place_cursor_at(*f.interaction, {measure_id_t{1}, 241});
    for (const auto* command : {"selection.select_all_after_cursor", "selection.select_all_objects_after_cursor"}) {
        expect_compose_test(invoke_compose_registered_command(f, command).value.is_invoked());
        expect_compose_test(std::ranges::none_of(f.interaction->selected_objects, [](auto object) {
            return object.kind == editor_selection_object_kind_t::bgm_event && object.id == object_id_t{9003};
        }));
    }
}

TEST_CASE("Registered note selection respects active editable lanes",
          "[frame-timing][selection-consolidation]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    chart.lane_layouts.front().layout.lanes.push_back(
        {.id = lane_id_t{2}, .name = "Hidden", .role = lane_role_t::invisible});
    chart.measures.front().notes.push_back(
        {.id = object_id_t{9001}, .position = {measure_id_t{1}, 18}, .lane = lane_id_t{2}});
    auto later = chart.lane_layouts.front();
    later.id = object_id_t{9002};
    later.position = {measure_id_t{1}, 60};
    later.layout.lanes.front().role = lane_role_t::autoplay;
    chart.lane_layouts.push_back(std::move(later));
    compose_test_fixture_t f{std::move(song)};
    // Imported charts can contain non-editable lane endpoints. Exercise the
    // registered verb without requiring that malformed field to render.
    constexpr std::array snaps{std::uint64_t{4}};
    auto setup = editor_interaction_state_t::from(f.history, chart_id_t{1}, lane_id_t{1},
                                         {measure_id_t{1}, 0}, snaps, 4);
    expect_compose_test(static_cast<bool>(setup));
    f.interaction = std::move(setup.state);
    expect_compose_test(invoke_compose_registered_command(f, "selection.select_all").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes == std::vector<object_id_t>{
        object_id_t{3}, object_id_t{4}, object_id_t{5}, object_id_t{6}});
    expect_compose_test(invoke_compose_registered_command(f, "selection.invert").value.is_invoked());
    expect_compose_test(f.interaction->selected_notes.empty());
    expect_compose_test(f.history.undo.empty());
}

TEST_CASE("Compose scene keys invalidate only on document or chart changes", "[unit][sys][commands]") {
    wormedit::song_document_t document;
    document.id = wormedit::song_id_t{51};
    document.charts.push_back(compose_test_song().charts.front());
    document.charts.front().id = wormedit::chart_id_t{7};
    auto& chart = document.charts.front();
    compose_scene_cache_t cache;
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.ensure_scene_key(document, chart));
    cache.frame.projection_error = "retained";
    expect_compose_test(!cache.ensure_scene_key(document, chart));
    expect_compose_test(cache.frame.projection_error == "retained");
    ++document.revision;
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.ensure_scene_key(document, chart));
    expect_compose_test(cache.frame.projection_error.empty());
    auto replacement = document;
    expect_compose_test(!cache.is_scene_key_current(replacement, replacement.charts.front()));
    cache.invalidate_scene_cache();
    expect_compose_test(!cache.is_scene_key_current(document, chart));
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::transport));
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::measure_geometry));
    seed_stem_geometry_fixture(cache.stems.stem_index, chart_stamp_t::from(document, chart.id), {});
    expect_compose_test(cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::stem_geometry));
    auto stale_stem_stamp = cache.stems.stem_index.key()->stamp;
    ++stale_stem_stamp.document.revision;
    seed_stem_geometry_fixture(cache.stems.stem_index, stale_stem_stamp,
                               cache.stems.stem_index.key()->bindings);
    expect_compose_test(!cache.is_scene_key_current(document, chart, compose_scene_key_policy_t::stem_geometry));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("stem display owner keeps channel and packed event cache keys separate", "[sys][stem][ownership]") {
    wormedit::sys::compose_scene_cache_t cache;
    cache.stems.stem_waveforms.reserve(8);
    cache.stems.packed_waveforms.reserve(8);
    auto& stem = cache.stems.stem_for(object_id_t{7});
    auto& packed = cache.stems.packed_for(object_id_t{7});
    CHECK(&stem != &packed);
    CHECK(&cache.stems.stem_for(object_id_t{7}) == &stem);
    CHECK(&cache.stems.packed_for(object_id_t{7}) == &packed);
    CHECK(cache.stems.stem_waveforms.size() == 1);
    CHECK(cache.stems.packed_waveforms.size() == 1);
}
#endif

namespace {
void check_options_text_diagnostic(const options_runtime_t& runtime, WormUiOptionsField field) {
    CHECK(std::ranges::find(runtime.diagnostics, field, &options_diagnostic_t::field) != runtime.diagnostics.end());
}
}
TEST_CASE("Options identifies malformed UTF8 in each display text field", "[sys][shared-text]") {
    options_runtime_t runtime;
    open_options(runtime, program_preferences_t::with_defaults());
    runtime.category = WORM_UI_OPTIONS_DISPLAY;
    runtime.draft.display.field_scheme = "\x80";
    runtime.draft.display.theme = "\x80";
    runtime.draft.layout.dock_layout = "\x80";
    (void)wormedit::ui::options_view_t::from(runtime);
    CHECK(runtime.diagnostics.size() == 3);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_FIELD_SCHEME);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_THEME);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_LAYOUT);
}
TEST_CASE("Options identifies malformed UTF8 in the shortcut profile path", "[sys][shared-text]") {
    options_runtime_t runtime;
    open_options(runtime, program_preferences_t::with_defaults());
    runtime.category = WORM_UI_OPTIONS_INPUT;
    runtime.draft.general.shortcut_profile_path = "\x80";
    (void)wormedit::ui::options_view_t::from(runtime);
    check_options_text_diagnostic(runtime, WORM_UI_OPTIONS_FIELD_SHORTCUT_PATH);
}
TEST_CASE("Options discards malformed UTF8 layout identities and display names", "[sys][shared-text]") {
    options_runtime_t runtime;
    set_options_field_layout(runtime, "\x80", "\x80");
    CHECK(runtime.current_field_layout_key.empty());
    CHECK(runtime.current_field_layout_name.empty());
}
TEST_CASE("Timing stage converts transition fractions through the shared careful rounding", "[sys][shared-arithmetic]") {
    auto history = document_history_t::from(dense_song());
    auto binding = workspace_controller_t::from(history);
    REQUIRE(binding.value);
    binding.value->selected_chart = chart_id_t{1};
    frontend_workspace_view_t flattened;
    timing_scene_cache_t cache;
    timing_lane_pseudonote_outcome_t stage{timing_lane_pseudonote_proposal_t::from(
        chart_stamp_t::from(history.document, chart_id_t{1}),
        timing_lane_pseudonote_preview_t{.report = {
            .kind = timing_lane_kind_t::speed, .position = {measure_id_t{1}, 0},
            .value = timing_speed_value_t{.transition_beats = {9'007'199'254'740'993ULL, 3}}}})};
    populate_timing_view_for_compose(flattened, history, *binding.value, cache, 1280, 980, {}, &stage);
    CHECK(flattened.view.timing.stage_secondary_value == 3'002'399'751'580'331.0);
}

namespace {
auto speed_span_transition_at_boundaries(signed_rational_t earlier, signed_rational_t later)
    -> std::optional<rational_t> {
    auto chart = dense_song().charts.front();
    timing_index_t timing{.divisions_per_standard_measure = chart.divisions_per_standard_measure,
                          .total_rows = 960};
    timing.rows = {
        {.absolute_row = 0, .before_delays_seconds = earlier, .event_seconds = earlier,
         .after_stops_seconds = earlier, .effective_bpm = {120, 1}},
        {.absolute_row = 960, .before_delays_seconds = later, .event_seconds = later,
         .after_stops_seconds = later, .effective_bpm = {120, 1}}};
    const speed_event_t speed{.transition_is_seconds = true};
    return compose_input_detail::speed_transition_between(chart, &timing, speed, 0, 960);
}
void check_speed_span_transition(signed_rational_t earlier, signed_rational_t later, rational_t expected) {
    const auto transition = speed_span_transition_at_boundaries(earlier, later);
    REQUIRE(transition);
    CHECK(*transition == expected);
}
}
TEST_CASE("Speed spans reduce denominator intermediates before rejecting durations", "[sys][shared-arithmetic]") {
    check_speed_span_transition({1, 10'000'000'000ULL}, {2, 10'000'000'000ULL}, {1, 10'000'000'000ULL});
}
TEST_CASE("Speed spans reduce numerator intermediates before rejecting durations", "[sys][shared-arithmetic]") {
    check_speed_span_transition({1, 1'000'000}, {30'000'000'000'000LL, 1'000'000}, {29'999'999'999'999ULL, 1'000'000});
}
TEST_CASE("Speed spans subtract opposite signed boundaries without wide overflow", "[sys][shared-arithmetic]") {
    const auto denominator = std::numeric_limits<std::uint64_t>::max();
    check_speed_span_transition({std::numeric_limits<std::int64_t>::min(), denominator},
                               {std::numeric_limits<std::int64_t>::max(), denominator}, {1, 1});
}
TEST_CASE("Speed spans retain the full unsigned duration range", "[sys][shared-arithmetic]") {
    check_speed_span_transition({std::numeric_limits<std::int64_t>::min(), 1},
                               {std::numeric_limits<std::int64_t>::max(), 1},
                               {std::numeric_limits<std::uint64_t>::max(), 1});
}

TEST_CASE("Dead span labels use the guarded shared conversion for malformed fractions", "[sys][shared-arithmetic]") {
    const dead_span_band_t band{.kind = dead_span_kind_t::stop, .seconds = {1, 0}};
    std::string label;
    compose_projection_detail::assign_dead_span_label(label, band);
    CHECK(label == "STOP 0.000s");
}

TEST_CASE("Timing stage borrowed owner survives rejected and repaired revisions",
          "[sys][timing-stage][proposal-shape]") {
    auto history = document_history_t::from(dense_song());
    runtime::timing_edit_t editor;
    editor.stage = timing_lane_pseudonote_preview_t::propose(
        history, chart_id_t{1}, timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{2}}, false);
    const auto* borrowed = editor.stage_view();
    *editor.stage = timing_lane_pseudonote_preview_t::revise(
        std::move(*editor.stage), history, timing_bpm_value_t{{0, 1}}, false);
    CHECK(editor.stage_view() == borrowed);
    CHECK_FALSE(borrowed->value);
    CHECK(std::get<timing_bpm_value_t>(borrowed->report()->value)
              .beats_per_minute == signed_rational_t{0, 1});
    *editor.stage = timing_lane_pseudonote_preview_t::revise(
        std::move(*editor.stage), history, timing_bpm_value_t{{150, 1}}, false);
    CHECK(borrowed->value.has_value());
    CHECK(history.undo.empty());
}

namespace {
struct bmson_marker_fixture_t {
    document_history_t history;
    editor_interaction_state_t state;
    object_id_t reference;

    static auto from() -> bmson_marker_fixture_t {
        constexpr std::string_view text = R"({"version":"1.0.0","info":{"title":"Markers","artist":"test","mode_hint":"beat-7k","resolution":240,"init_bpm":120},"lines":[{"y":0},{"y":960}],"sound_channels":[{"name":"stem.wav","notes":[{"x":0,"y":0,"c":false}]}]})";
        auto parsed = parse_bmson_bytes(std::as_bytes(std::span{text.data(), text.size()}));
        REQUIRE(parsed.value);
        bmson_marker_fixture_t fixture{.history = document_history_t::from(std::move(*parsed.value))};
        const auto& chart = fixture.history.document.charts.front();
        fixture.reference = chart.measures.front().bgm_events.front().id;
        fixture.state.chart = chart.id;
        fixture.state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event,
                                                  fixture.reference});
        return fixture;
    }
};
} // namespace

TEST_CASE("Rejected BMSON marker construction allocates only its existing feedback string",
          "[sys][bmson-marker][proposal-shape]") {
    document_history_t history;
    editor_interaction_state_t state;
    const std::array markers{bmson_sound_marker_insertion_t{}};
    allocations = 0;
    count_allocations = true;
    auto rejected = application_command_state_t::bmson_marker_draft_t::propose(
        history, state, object_id_t{42}, markers);
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(rejected.value);
    REQUIRE(rejected.diagnostics);
    CHECK(counted == 1);
    CHECK(rejected.diagnostics->message == "Select one BMSON BGM marker to choose its sound channel.");
    CHECK(history.undo.empty());
}

TEST_CASE("A rejected BMSON marker append keeps the prior valid batch and its storage",
          "[sys][bmson-marker][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto prior = std::move(proposed.value);
    const auto* storage = prior->payload.markers.data();
    const auto rejected = application_command_state_t::bmson_marker_draft_t::revise(
        fixture.history, fixture.state, prior, fixture.reference, markers);
    CHECK_FALSE(rejected.value);
    REQUIRE(rejected.diagnostics);
    CHECK(rejected.diagnostics->message == "That BMSON pulse is already staged.");
    CHECK(prior->payload.markers.size() == 1);
    CHECK(prior->payload.markers.data() == storage);
}

TEST_CASE("BMSON proposal adoption moves markers prepared by the existing native scratch check",
          "[sys][bmson-marker][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto prepared = proposed.value->payload.markers;
    prepared.front().restart = true;
    auto temporary = document_history_t::from(fixture.history.document, 1);
    REQUIRE(insert_bmson_sound_markers(temporary, proposed.value->stamp.chart,
                                     fixture.reference, prepared));
    const auto* storage = prepared.data();
    const auto revised = proposed.value->revise(std::move(prepared));
    REQUIRE(revised.value);
    CHECK(revised.value->payload.markers.data() == storage);
    CHECK(revised.value->payload.markers.front().restart);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Razor capture quietly has no proposal when its marker prerequisite is absent",
          "[sys][bmson-razor][proposal-shape]") {
    document_history_t history;
    allocations = 0;
    count_allocations = true;
    const auto proposed = application_command_state_t::bmson_razor_stroke_t::propose(
        history, chart_id_t{1}, nullptr, object_id_t{42}, {8}, {measure_id_t{1}, 120});
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(proposed.value);
    CHECK(proposed.diagnostics.empty());
    CHECK(counted == 0);
    CHECK(history.undo.empty());
}

TEST_CASE("Razor capture keeps revision-only marker identity and captures the current document",
          "[sys][bmson-razor][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const local_position_t position{fixture.history.document.charts.front().measures.front().id, 120};
    const std::array markers{bmson_sound_marker_insertion_t{position, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    fixture.history.document.id = song_id_t{99};
    const auto captured = application_command_state_t::bmson_razor_stroke_t::propose(
        fixture.history, fixture.state.chart, &*proposed.value, fixture.reference, {8}, position);
    REQUIRE(captured.value);
    CHECK(captured.value->stamp == chart_stamp_t::from(fixture.history.document, fixture.state.chart));
    CHECK(captured.value->payload.reference == fixture.reference);
    CHECK(captured.value->payload.snap == snap_grid_t{8});
    CHECK(captured.value->payload.last_position == position);
}

TEST_CASE("Razor revision adopts an acknowledged staged cell without allocating",
          "[sys][bmson-razor][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    const std::array markers{bmson_sound_marker_insertion_t{{measure, 120}, false},
                             bmson_sound_marker_insertion_t{{measure, 240}, false}};
    auto proposed = application_command_state_t::bmson_marker_draft_t::propose(
        fixture.history, fixture.state, fixture.reference, markers);
    REQUIRE(proposed.value);
    auto captured = application_command_state_t::bmson_razor_stroke_t::propose(
        fixture.history, fixture.state.chart, &*proposed.value, fixture.reference, {8}, {measure, 120});
    REQUIRE(captured.value);
    const auto stamp = captured.value->stamp;
    allocations = 0;
    count_allocations = true;
    const auto revised = captured.value->revise(local_position_t{measure, 240});
    count_allocations = false;
    const auto counted = allocations;
    REQUIRE(revised.value);
    CHECK(counted == 0);
    CHECK(revised.value->stamp == stamp);
    CHECK(revised.value->payload.last_position == local_position_t{measure, 240});
}

namespace {
auto make_draw_point_input(bool bgm = false) -> wormedit::ui::compose_input_action_t {
    wormedit::ui::compose_input_action_t input{};
    input.resource_role = bgm ? wormedit::resource_role_t::bgm : wormedit::resource_role_t::bga;
    input.paint_intent = 2;
    input.lane_id.value = 42;
    input.bga_lane_kind_valid = true;
    input.bga_lane_kind = static_cast<decltype(input.bga_lane_kind)>(visual_event_kind_t::layer);
    return input;
}

auto make_prepared_draw_stroke(const document_history_t& history,
    const editor_interaction_state_t& state, const wormedit::ui::compose_input_action_t& input)
    -> application_command_state_t::bga_draw_stroke_proposal_t {
    const auto& chart = history.document.charts.front();
    auto built = measure_index_t::from(chart);
    auto& geometry = built;
    REQUIRE(geometry);
    application_command_state_t::bga_draw_stroke_proposal_t initial{
        .stamp = chart_stamp_t::from(history.document, chart.id),
        .payload = {.erase = true, .geometry = std::move(*geometry)}};
    auto proposed = initial.revise(input, chart, state, 120, true);
    REQUIRE(proposed.value);
    return std::move(*proposed.value);
}
} // namespace

TEST_CASE("Draw proposal revision adds no allocations beyond the existing duplicate set insertion",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input();
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    const auto* geometry = stroke.payload.geometry.entries.data();
    const auto* targets = stroke.payload.targets.data();
    const auto kind = static_cast<uint8_t>(input.bga_lane_kind);
    std::set<std::pair<uint64_t, uint8_t>> control;
    control.emplace(uint64_t{120}, kind);
    allocations = 0;
    count_allocations = true;
    control.emplace(uint64_t{120}, kind);
    count_allocations = false;
    const auto previous_cost = allocations;
    allocations = 0;
    count_allocations = true;
    const auto revised = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, 120, false);
    count_allocations = false;
    const auto current_cost = allocations;
    REQUIRE(revised.value);
    CHECK(current_cost == previous_cost);
    CHECK(revised.value->payload.geometry.entries.data() == geometry);
    CHECK(revised.value->payload.targets.data() == targets);
    CHECK(revised.value->payload.targets.size() == 1);
    CHECK(revised.value->payload.visited.size() == 1);
}

TEST_CASE("Draw proposal begins at the first cell and preserves snap interpolation",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input(true);
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    const auto* geometry = stroke.payload.geometry.entries.data();
    const auto revised = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, 360, false);
    REQUIRE(revised.value);
    CHECK(revised.value->payload.targets.size() == 3);
    CHECK(revised.value->payload.targets.front().position.row == 120);
    CHECK(revised.value->payload.last_row == 360);
    CHECK(revised.value->payload.geometry.entries.data() == geometry);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Draw proposal rejects a missing cell without an applicable value or allocation",
          "[sys][draw-proposal][proposal-shape]") {
    auto fixture = bmson_marker_fixture_t::from();
    fixture.state.active_snap_denominator = 8;
    fixture.state.snap_enabled = true;
    const auto input = make_draw_point_input();
    auto stroke = make_prepared_draw_stroke(fixture.history, fixture.state, input);
    allocations = 0;
    count_allocations = true;
    const auto rejected = stroke.revise(input,
        fixture.history.document.charts.front(), fixture.state, std::nullopt, false);
    count_allocations = false;
    const auto counted = allocations;
    CHECK_FALSE(rejected.value);
    CHECK(rejected.diagnostics.empty());
    CHECK(counted == 0);
    CHECK(fixture.history.undo.empty());
}

TEST_CASE("Compose hit-test cache reuses a chart revision and rebuilds after invalidation",
          "[sys][hit-test-cache]") {
    auto song = compose_test_song();
    const auto& chart = song.charts.front();
    compose_scene_cache_t cache;
    const auto* index = cache.hit_test_index_for(chart, song.revision);
    REQUIRE(index);
    CHECK(cache.hit_test_index_for(chart, song.revision) == index);
    CHECK(cache.scene.hit_test_index.build_count() == 1);
    CHECK(cache.hit_test_index_for(chart, song.revision + 1)->document_revision == song.revision + 1);
    CHECK(cache.scene.hit_test_index.build_count() == 2);
    cache.invalidate_scene_cache();
    CHECK_FALSE(cache.scene.hit_test_index.has_value());
}

TEST_CASE("Compose hit-test cache retries failed geometry without retaining a failure",
          "[sys][hit-test-cache]") {
    auto song = compose_test_song();
    auto& chart = song.charts.front();
    const auto divisions = chart.divisions_per_standard_measure;
    chart.divisions_per_standard_measure = 0;
    compose_scene_cache_t cache;
    CHECK_FALSE(cache.hit_test_index_for(chart, song.revision));
    CHECK_FALSE(cache.scene.hit_test_index.has_value());
    chart.divisions_per_standard_measure = divisions;
    CHECK(cache.hit_test_index_for(chart, song.revision));
    CHECK(cache.scene.hit_test_index.build_count() == 1);
}

TEST_CASE("Compose measure geometry is shared with the committed snapshot",
          "[sys][timing-projection][chart-indices]") {
    auto history = document_history_t::from(compose_test_song());
    auto workspace = workspace_controller_t::from(history).value.value();
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    std::optional<editor_interaction_state_t> interaction;
    populate_measure_cache_frame(history, workspace, cache, interaction);
    const auto* indices = history.indices(chart_id_t{1});
    REQUIRE(indices);
    CHECK(cache.scene.cached_measure_index.get() == &indices->measures.value());
    history = document_history_t::from(compose_test_song());
    CHECK(cache.scene.cached_measure_index->total_rows == 960);
}

TEST_CASE("Area selection reaches bar lines only through a shown measure lane",
          "[frame-timing][measure-lane]") {
    std::array<wormedit::ui::compose_grid_line_view_t, 3> lines{{
        {.y_pixels = 100, .kind = editor_grid_line_kind_t::measure, .measure_number = 1, .measure_id = {10}},
        {.y_pixels = 300, .kind = editor_grid_line_kind_t::measure, .measure_number = 2, .measure_id = {11}},
        {.y_pixels = 400, .kind = editor_grid_line_kind_t::beat}}};
    wormedit::ui::compose_view_t view{};
    view.viewport_height_pixels = 600;
    view.grid_lines.assign(lines.begin(), lines.end());
    editor_area_geometry_snapshot_t hidden;
    compose_projection_detail::append_area_bar_line_geometry(hidden, view, false);
    CHECK(hidden.objects.empty());
    view.measure_lane.visible = true;
    editor_area_geometry_snapshot_t shown;
    compose_projection_detail::append_area_bar_line_geometry(shown, view, false);
    // Only measure 11's bar line: the chart's first is fixed and beat lines have none.
    REQUIRE(shown.objects.size() == 1);
    const auto& handle = shown.objects.front();
    CHECK(handle.object == editor_selection_object_t{editor_selection_object_kind_t::bar_line, object_id_t{11}});
    CHECK(handle.left_pixels == 0.0);
    CHECK(handle.right_pixels == wormedit::ui::compose_measure_rail_width);
    CHECK(handle.top_pixels < 300.0);
    CHECK(handle.bottom_pixels > 300.0);
}

TEST_CASE("Measure-lane drag inputs dispatch through the bar-line move gesture",
          "[frame-timing][measure-lane][gesture]") {
    auto song = compose_test_song();
    song.charts.front().measures.push_back({.id = measure_id_t{2}});
    song.charts.front().measures.push_back({.id = measure_id_t{3}});
    compose_test_fixture_t fixture{std::move(song)};
    fixture.application.project.compose_view.measure_lane_visible = true;
    fixture.project();
    REQUIRE(dispatch_editor_action(
        fixture.history, *fixture.interaction,
        select_editor_objects_action_t{{{
            editor_selection_object_kind_t::bar_line, object_id_t{2}}}}));
    fixture.project();

    notification_center_t center;
    notification_sink_t sink{center};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto dispatch = [&](WormUiComposeInputKind kind, std::uint64_t row) {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_project_id.value = fixture.history.document.id.value;
        action.compose_input_chart_id.value = 1;
        action.compose_input_document_revision = fixture.history.document.revision;
        action.compose_inputs.resize(1);
        const auto& view = fixture.flattened.view.compose;
        const auto downscroll = fixture.cache.frame.preview_scene_request.scroll_direction ==
            editor_field_scroll_direction_t::down;
        action.compose_inputs.resize(std::max(action.compose_inputs.size(), std::size_t{1}));
        action.compose_inputs[0] = {
            .kind = kind,
            .object_id = {2},
            .x_pixels = 20,
            .y_pixels = compose_project_y(
                static_cast<double>(row), view.pixels_per_standard_measure / 960.0,
                fixture.interaction->viewport_scroll_pixels, view.viewport_height_pixels,
                downscroll)};
        action.compose_input_viewport_width_pixels = view.viewport_width_pixels;
        return dispatch_compose_input_action(
            action, fixture.history, *fixture.binding.value, *fixture.interaction,
            viewport, navigation, fixture.application, fixture.cache, 0, &sink);
    };

    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_BEGIN, 960);
    REQUIRE(fixture.interaction->gesture<pending_bar_line_move_gesture_t>() != nullptr);
    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_UPDATE, 1200);
    CHECK(fixture.interaction->gesture<pending_bar_line_move_gesture_t>()->preview_rows == 240);
    (void)dispatch(WORM_UI_COMPOSE_INPUT_BAR_LINE_MOVE_RELEASE, 1200);
    CHECK_FALSE(fixture.interaction->gesture<pending_bar_line_move_gesture_t>());
    const auto index = measure_index_t::from(fixture.history.document.charts.front()).value();
    CHECK(index.entries[1].absolute_begin_row == 1200);
    CHECK(fixture.history.undo.size() == 1);
}

TEST_CASE("Pasted timing ghosts retain all eight lane identities",
          "[sys][clipboard][paste-preview]") {
    using lane_case = std::pair<chart_clipboard_payload_t, wormedit::timing_lane_kind_t>;
    const local_position_t position{measure_id_t{1}, 0};
    const std::array cases{
        lane_case{bpm_event_t{.position = position, .beats_per_minute = {150, 1}}, wormedit::timing_lane_kind_t::bpm},
        lane_case{scroll_event_t{.position = position, .multiplier = {2, 1}}, wormedit::timing_lane_kind_t::scroll},
        lane_case{speed_event_t{.position = position, .multiplier = {2, 1}}, wormedit::timing_lane_kind_t::speed},
        lane_case{pause_event_t{.position = position, .kind = pause_kind_t::stop, .duration = {1, 4}}, wormedit::timing_lane_kind_t::stop},
        lane_case{pause_event_t{.position = position, .kind = pause_kind_t::delay, .duration = {1, 4}}, wormedit::timing_lane_kind_t::delay},
        lane_case{warp_event_t{.span = {position, {measure_id_t{1}, 60}}}, wormedit::timing_lane_kind_t::warp},
        lane_case{kiai_event_t{.position = position, .enabled = true}, wormedit::timing_lane_kind_t::kiai},
        lane_case{timing_anchor_t{.position = position}, wormedit::timing_lane_kind_t::anchor}};
    for (const auto& [payload, lane] : cases) {
        CAPTURE(lane);
        compose_test_fixture_t fixture{paste_preview_song()};
        fixture.project();
        fixture.application.project.clipboard.chart_clipboard = chart_clipboard_t{
            .source_song = fixture.history.document.id, .source_chart = chart_id_t{1},
            .objects = {{.offset = {}, .duration = std::holds_alternative<warp_event_t>(payload)
                ? rational_t{1, 16} : rational_t{}, .payload = payload}}};
        fixture.interaction->cursor.row = 240;
        std::string status;
        REQUIRE(begin_compose_paste(fixture.application, fixture.history, *fixture.interaction, status));
        fixture.project();
        const auto& view = fixture.flattened.view.compose;
        REQUIRE(view.paste_ghosts.size() == 1);
        CHECK(view.paste_ghosts[0].kind == ui::compose_paste_ghost_kind_t::timing_object);
        CHECK(view.paste_ghosts[0].timing_lane == lane);
    }
}

TEST_CASE("Oversized owned Compose input batches cancel the active gesture",
          "[sys][compose][input-admission]") {
    const bool viewport_batch = GENERATE(false, true);
    auto song = compose_test_song();
    song.charts.front().measures.front().notes.clear();
    compose_test_fixture_t fixture{std::move(song)};
    fixture.project();
    const auto begun = dispatch_editor_action(fixture.history, *fixture.interaction,
        begin_lane_key_hold_gesture_action_t{lane_id_t{1}});
    REQUIRE(begun.accepted);
    REQUIRE(fixture.interaction->is_pending());
    auto action = cursor_placement_action(fixture);
    if (viewport_batch)
        action.compose_viewport_inputs.resize(ui::compose_viewport_input_admission_limit + 1);
    else
        action.compose_inputs.resize(ui::compose_input_admission_limit + 1);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto revision = fixture.history.document.revision;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.value,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(status.find("exceeded its capacity") != std::string::npos);
    CHECK_FALSE(fixture.interaction->is_pending());
    CHECK(fixture.history.document.revision == revision);
    CHECK(fixture.history.undo.empty());
}

namespace {
void refill_owned_scene_geometry(compose_test_fixture_t& fixture, bool bgm_only = false) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& index = *fixture.cache.scene.index.value();
    const auto& interaction = *fixture.cache.frame.interaction_view;
    const timing_lane_pseudonote_outcome_t* stage{};
    const std::span<const local_position_t> comparison{};
    const compose_bga_move_preview_input_t* bga{};
    const compose_frame_inputs_t frame{fixture.flattened, fixture.history, fixture.cache,
        fixture.interaction, fixture.application, stage, comparison, bga};
    const auto scope = compose_chart_scope_t<>::from({chart, index.measures}, interaction,
        false, compose_pixels_per_row(chart, interaction));
    const auto inputs = compose_projection_context_t<>::from(frame, scope);
    const auto& view = fixture.flattened.view.compose;
    const compose_projection_detail::compose_scene_projection_context_t projection{
        .inputs = inputs, .index = index, .scene = *fixture.cache.scene.scene_window.scene,
        .field_left_pixels = view.playable_field_left_pixels,
        .playable_field_width = view.playable_field_right_pixels - view.playable_field_left_pixels};
    compose_projection_detail::populate_compose_sound_labels(projection);
    compose_projection_detail::build_compose_bgm_view(projection, fixture.cache);
    if (bgm_only) return;
    compose_projection_detail::build_compose_layout_and_notes_view(projection);
    compose_projection_detail::build_compose_bga_view(projection);
    populate_lane_layout_segments(fixture.flattened, chart,
        index.measures, interaction, false, fixture.flattened.view.compose);
}

void refill_owned_timing_geometry(compose_test_fixture_t& fixture) {
    const auto& chart = fixture.history.document.charts.front();
    const auto& interaction = *fixture.cache.frame.interaction_view;
    auto& view = fixture.flattened.view.compose;
    const auto pixels = compose_pixels_per_row(chart, interaction);
    const auto rows = fixture.cache.scene.index.value()->measures.total_rows;
    compose_projection_detail::populate_compose_timing_markers(fixture.flattened,
        fixture.history.document, chart, fixture.cache, interaction, view, false, pixels, rows, nullptr);
    compose_projection_detail::populate_compose_dead_spans(fixture.flattened,
        fixture.cache, interaction, view, false, pixels);
    compose_projection_detail::populate_compose_kiai_and_sv(fixture.flattened,
        fixture.cache, interaction, view, false, pixels, rows, 0);
    compose_projection_detail::populate_compose_speed_spans(fixture.flattened,
        fixture.cache, interaction, view, false, pixels);
    compose_projection_detail::populate_compose_recipe_areas(fixture.flattened,
        chart, fixture.cache, interaction, view, false, pixels, rows, 0, nullptr);
}
}

TEST_CASE("Owned populated Compose geometry refills without heap allocations",
          "[sys][compose-geometry][allocation]") {
    auto song = recipe_test_song();
    auto& chart = song.charts.front();
    chart.measures.front().notes[1].kind = note_kind_t::hold;
    chart.measures.front().notes[1].end = local_position_t{measure_id_t{1}, 360};
    chart.measures.front().notes.front().sound = resource_id_t{900};
    chart.measures.front().notes.front().osu_hitsample = osu_hitsample_t{
        .whistle = true, .filename = "retained-custom-note-sample.wav"};
    song.audio_assets.push_back({.id = asset_id_t{900}, .source_path = "sample.wav"});
    song.resources.push_back({.id = resource_id_t{900},
        .name = "Long retained sample label for geometry allocation coverage.wav",
        .asset = asset_id_t{900}, .roles = {resource_role_t::sample}});
    chart.pauses.push_back({.id = object_id_t{901}, .position = {measure_id_t{1}, 240},
        .duration = {1, 2}});
    chart.scrolls.push_back({.id = object_id_t{902}, .position = {measure_id_t{1}, 120},
        .multiplier = {2, 1}});
    chart.kiai.push_back({.id = object_id_t{903}, .position = {measure_id_t{1}, 120}, .enabled = true});
    chart.speeds.push_back({.id = object_id_t{904}, .position = {measure_id_t{1}, 120},
        .multiplier = {2, 1}, .transition_beats = {1, 1}});
    compose_test_fixture_t fixture(std::move(song));
    fixture.project();
    refill_owned_scene_geometry(fixture);
    refill_owned_timing_geometry(fixture);
    auto& view = fixture.flattened.view.compose;
    expect_compose_test(!view.note_endpoints.empty() && !view.hold_bodies.empty());
    expect_compose_test(!view.grid_lines.empty() && !view.lane_layout_segments.empty());
    expect_compose_test(!view.dead_spans.empty() && !view.kiai_spans.empty());
    expect_compose_test(!view.recipe_areas.empty() && !view.recipe_graph_points.empty());
    expect_compose_test(!view.sv_graph_points.empty() && !view.speed_graph_points.empty());
    const auto* notes = view.note_endpoints.data();
    const auto* grid = view.grid_lines.data();
    const auto before = frame_heap_allocation_snapshot();
    refill_owned_scene_geometry(fixture);
    refill_owned_timing_geometry(fixture);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(view.note_endpoints.data() == notes && view.grid_lines.data() == grid);
    expect_compose_test(view.note_endpoints.front().hitsound != nullptr);
    expect_compose_test(view.note_endpoints.front().hitsound->filename == "retained-custom-note-sample.wav");
    const auto previous_label = fixture.flattened.compose.sound_labels.at(900);
    fixture.history.document.resources.back().name = "Reopened project sample.wav";
    ++fixture.flattened.query_session_generation;
    refill_owned_scene_geometry(fixture);
    expect_compose_test(fixture.flattened.compose.sound_labels.at(900) != previous_label);
    const auto note_capacity = view.note_endpoints.capacity();
    fixture.flattened.reset();
    expect_compose_test(view.note_endpoints.empty() && view.note_endpoints.capacity() == note_capacity);
}

TEST_CASE("Compose layout segment lists exceed the retired storage cutoff", "[sys][compose-geometry]") {
    constexpr std::size_t segment_count = 2049;
    auto song = dense_song(segment_count, 0);
    auto& chart = song.charts.front();
    const auto layout = chart.lane_layouts.front().layout;
    chart.lane_layouts.clear();
    for (std::size_t index = 0; index < segment_count; ++index)
        chart.lane_layouts.push_back({.id = object_id_t{10000 + index},
            .position = {chart.measures[index].id, 0}, .layout = layout});
    const auto measures = measure_index_t::from(chart);
    REQUIRE(measures);
    frontend_workspace_view_t flattened;
    auto& view = flattened.view.compose;
    view.viewport_height_pixels = 600;
    editor_interaction_view_model_t interaction;
    interaction.pixels_per_standard_measure = 240;
    populate_lane_layout_segments(flattened, chart, *measures, interaction, false, view);
    expect_compose_test(view.lane_layout_segments.size() == segment_count && !view.truncated);
    expect_compose_test(view.lane_layout_segments.back().segment_id == object_id_t{12048});
}

TEST_CASE("Owned populated Compose BGM waveform and stem views refill without heap allocations",
          "[sys][compose-bgm][waveform-projection][stem][allocation]") {
    compose_test_fixture_t bgm(bgm_arena_song(format_family_t::bms));
    bgm.project();
    refill_owned_scene_geometry(bgm, true);
    envelope_projection_fixture_t waveform;
    waveform.project();
    channel_projection_fixture_t stems;
    const bmson_waveform_projection_context_t context{stems.flattened, stems.history,
        stems.workspace, &stems.interaction, stems.cache,
        retained_sources_for(stems.history.document, stems.project_sources),
        {stems.asset, stems.channel_analysis}, {stems.asset, stems.channel_analysis}, 1,
        [&](asset_id_t) { return stems.channel_analysis; }};
    populate_bmson_waveform_projection(context);
    expect_compose_test(!bgm.flattened.view.compose.bgm_lanes.empty());
    expect_compose_test(!bgm.flattened.view.compose.bgm_events.empty());
    expect_compose_test(!waveform.flattened.view.compose.waveform_rows.empty());
    expect_compose_test(!waveform.flattened.view.compose.waveform_overview_rows.empty());
    expect_compose_test(!stems.flattened.view.compose.stem_waveforms.empty());
    expect_compose_test(!stems.flattened.view.compose.stem_waveforms.front().rows.empty());
    const auto* events = bgm.flattened.view.compose.bgm_events.data();
    const auto* rows = waveform.flattened.view.compose.waveform_rows.data();
    const auto* pieces = stems.flattened.view.compose.stem_waveforms.data();
    const auto before = frame_heap_allocation_snapshot();
    refill_owned_scene_geometry(bgm, true);
    const auto available = project_waveform_frame(waveform);
    populate_bmson_waveform_projection(context);
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(available);
    expect_compose_test(bgm.flattened.view.compose.bgm_events.data() == events);
    expect_compose_test(waveform.flattened.view.compose.waveform_rows.data() == rows);
    expect_compose_test(stems.flattened.view.compose.stem_waveforms.data() == pieces);
    const auto event_capacity = bgm.flattened.view.compose.bgm_events.capacity();
    const auto row_capacity = waveform.flattened.view.compose.waveform_rows.capacity();
    const auto piece_capacity = stems.flattened.view.compose.stem_waveforms.capacity();
    bgm.flattened.reset();
    waveform.flattened.reset();
    stems.flattened.reset();
    expect_compose_test(bgm.flattened.view.compose.bgm_events.empty() &&
        bgm.flattened.view.compose.bgm_events.capacity() == event_capacity);
    expect_compose_test(waveform.flattened.view.compose.waveform_rows.empty() &&
        waveform.flattened.view.compose.waveform_rows.capacity() == row_capacity);
    expect_compose_test(stems.flattened.view.compose.stem_waveforms.empty() &&
        stems.flattened.view.compose.stem_waveforms.capacity() == piece_capacity);
}


TEST_CASE("Compose BGM event view has no retired storage cutoff", "[sys][compose-bgm]") {
    constexpr std::size_t event_count = 32769;
    frontend_workspace_view_t flattened;
    std::vector<editor_field_bgm_event_geometry_t> events(event_count);
    for (std::size_t index = 0; index < events.size(); ++index) {
        events[index].event = object_id_t{index + 1};
        events[index].source_lane = object_id_t{1};
        events[index].bounds = {.left = 0, .top = 0, .right = 20, .bottom = 10};
    }
    // Exercise the view storage owner directly: scene admission retains its separate
    // primitive work budget, while projecting an admitted list must not truncate it.
    compose_projection_detail::build_compose_bgm_events_view(flattened, events, flattened.view.compose);
    expect_compose_test(flattened.view.compose.bgm_events.size() == event_count);
    expect_compose_test(flattened.view.compose.bgm_events.back().event_id == object_id_t{event_count});
    expect_compose_test(!flattened.view.compose.truncated);
}


TEST_CASE("Compose direct sample waveform cache refreshes its source style and viewport",
          "[sys][waveform-projection][allocation]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 32;
    prepare_waveform_sample_points(fixture);
    fixture.project();
    const auto& rows = fixture.flattened.view.compose.waveform_rows;
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .8f) < .00001f);
    const auto builds = fixture.cache.waveform.direct_projection.build_count();
    const auto before = frame_heap_allocation_snapshot();
    const auto available = project_waveform_frame(fixture);
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(available);
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds);

    const auto previous_size = rows.size();
    fixture.flattened.view.compose.viewport_height_pixels = 40;
    fixture.project();
    expect_compose_test(rows.size() > previous_size);
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds + 1);
    fixture.style.detail_level = false;
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == builds + 2);
    fixture.style.detail_level = true;
    fixture.style.sample_points = false;
    fixture.project();
    expect_compose_test(std::ranges::none_of(rows, &wormedit::ui::compose_waveform_row_view_t::sample_point));
    fixture.style.sample_points = true;
    fixture.project();
    expect_compose_test(std::ranges::all_of(rows, &wormedit::ui::compose_waveform_row_view_t::sample_point));

    auto replacement = fixture.analysis;
    replacement.waveform->key.source = {"replacement:direct-samples", "content:v2"};
    const audio::audio_block_key_t key{replacement.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .2f);
    expect_compose_test(fixture.blocks.store(key, 16384, pcm).has_value());
    fixture.analysis = std::move(replacement);
    fixture.project();
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .2f) < .00001f);

    const auto original_stats = fixture.blocks.stats();
    const auto original_builds = fixture.cache.waveform.direct_projection.build_count();
    const std::vector<float> replacement_pcm(32768, .4f);
    expect_compose_test(fixture.blocks.store(key, 16384, replacement_pcm).has_value());
    auto current_stats = fixture.blocks.stats();
    expect_compose_test(current_stats.entries == original_stats.entries &&
        current_stats.cached_frames == original_stats.cached_frames &&
        current_stats.evictions == original_stats.evictions);
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == original_builds + 1);
    expect_compose_test(!rows.empty() && rows.front().sample_point);
    expect_compose_test(std::abs(rows.front().samples[0] - .4f) < .00001f);

    fixture.blocks.clear_source(key.source);
    const audio::audio_block_key_t unrelated{{"unrelated:direct-samples", "content:v1"}, 0};
    expect_compose_test(fixture.blocks.store(unrelated, 16384, replacement_pcm).has_value());
    current_stats = fixture.blocks.stats();
    expect_compose_test(current_stats.entries == original_stats.entries &&
        current_stats.cached_frames == original_stats.cached_frames &&
        current_stats.evictions == original_stats.evictions);
    fixture.project();
    expect_compose_test(fixture.cache.waveform.direct_projection.build_count() == original_builds + 2);
    expect_compose_test(!rows.empty());
    expect_compose_test(std::abs(rows.front().samples[0] - .4f) > .1f);
}

TEST_CASE("Compose BGA dock owns complete rows and reuses warmed projection storage",
          "[sys][compose-bga][allocation]") {
    song_document_t song;
    song.id = song_id_t{7};
    song.revision = 4;
    song.visual_assets.push_back({.id = asset_id_t{1}, .kind = asset_kind_t::image,
        .source_path = "long-retained-image-source-path.png"});
    song.resources.push_back({.id = resource_id_t{2}, .name = std::string(80, 'b'),
        .asset = asset_id_t{1}, .roles = {resource_role_t::bga}, .missing = true});
    bga_dock_runtime_t runtime;
    wormedit::ui::compose_view_t view{};
    const ui::bga_dock_request_t request{.visible = true, .ids_available = true, .row_limit = 64};
    runtime.project(song, {}, request, view, 1);
    REQUIRE(view.bga_rows.size() == 1);
    const auto* rows = view.bga_rows.data();
    const auto before = frame_heap_allocation_snapshot();
    runtime.project(song, {}, request, view, 1);
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(after.count == before.count);
    expect_compose_test(after.bytes == before.bytes);
    expect_compose_test(view.bga_rows.data() == rows);
    expect_compose_test(view.bga_rows.front().resource_id == resource_id_t{2});
    expect_compose_test(view.bga_matching_ids == std::vector<std::uint64_t>{2});
    song.resources.front().name = "Reopened BGA";
    runtime.project(song, {}, request, view, 2);
    song.resources.clear();
    expect_compose_test(view.bga_rows.front().name == "Reopened BGA");
}

TEST_CASE("Compose fallback paths retain every candidate without warmed allocations",
          "[sys][compose-keysound][audio-fallback][allocation]") {
    auto directory = test::temporary_directory_t::from();
    REQUIRE(directory);
    const auto stem = std::string(80, 's');
    std::ofstream(directory->path() / (stem + ".ogg")) << "x";
    std::ofstream(directory->path() / (stem + ".flac")) << "x";
    document_history_t history;
    history.document.audio_assets.push_back({.id = asset_id_t{1}, .source_path = stem + ".wav"});
    history.document.resources.push_back({.id = resource_id_t{2}, .asset = asset_id_t{1}, .missing = true});
    frontend_workspace_view_t frame;
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    REQUIRE(frame.view.compose.keysound_fallbacks.size() == 1);
    REQUIRE(frame.view.compose.keysound_fallbacks.front().candidates.size() == 2);
    const auto before = frame_heap_allocation_snapshot();
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    const auto after = frame_heap_allocation_snapshot();
    expect_compose_test(after.count == before.count);
    expect_compose_test(after.bytes == before.bytes);
    expect_compose_test(frame.view.compose.keysound_fallbacks.front().outcome == audio_fallback_outcome_t::ambiguous);
    history.document.resources.front().missing = false;
    populate_compose_keysound_fallbacks(frame, history, directory->path());
    expect_compose_test(frame.view.compose.keysound_fallbacks.empty());
}

TEST_CASE("Remaining Compose leaf families refill retained text without warmed allocations",
          "[sys][compose-remaining][allocation]") {
    auto song = marker_arena_song();
    auto& chart = song.charts.front();
    chart.semantic_layers = semantic_arena_song().charts.front().semantic_layers;
    chart.sv_recipes = recipe_test_song().charts.front().sv_recipes;
    song.quantization_presets.push_back({.name = std::string(64, 'p'), .divisions = 8});
    compose_test_fixture_t fixture(std::move(song));
    fixture.application.project.timing_recipe.recipe_selected = 7001;
    fixture.project();
    fixture.application.project.feedback.bms_branch_statement_feedback =
        application_command_state_t::bms_branch_statement_feedback_t{
            .chart = chart_id_t{1}, .revision = fixture.history.document.revision,
            .message = std::string(100, 'f')};
    auto& app = fixture.application.project.semantic_layer;
    app.note_layer_menu_chart = chart_id_t{1};
    app.note_layer_menu_revision = fixture.history.document.revision;
    app.note_layer_menu_row_count = 18;
    const auto& stored = fixture.history.document.charts.front();
    const auto& measures = *fixture.cache.scene.cached_measure_index;
    const auto refill = [&] {
        fixture.flattened.reset();
        auto& view = fixture.flattened.view.compose;
        view.visible = true;
        const auto bookmarks = compose_projection_detail::populate_compose_bookmark_storage(
            fixture.flattened, stored, measures, view);
        const auto sections = compose_projection_detail::populate_compose_section_storage(
            fixture.flattened, stored, measures, view);
        const auto layers = compose_projection_detail::populate_semantic_layer_palette(
            fixture.flattened, stored, fixture.cache, fixture.history, fixture.application);
        compose_projection_detail::populate_note_layer_menu(fixture.flattened, stored,
            fixture.history.document.revision, fixture.application);
        compose_projection_detail::populate_quantize_preview(fixture.cache, fixture.history.document,
            stored, fixture.history.document.revision, *fixture.interaction, fixture.application, view);
        compose_projection_detail::populate_compose_recipe_form(fixture.flattened, stored,
            fixture.application, view);
        compose_projection_detail::build_branch_statement_feedback_view(view, fixture.application,
            stored.id, fixture.history.document.revision);
        return bookmarks && sections && layers;
    };
    expect_compose_test(refill());
    const auto before = frame_heap_allocation_snapshot();
    const auto valid = refill();
    const auto after = frame_heap_allocation_snapshot();
    check_heap_counter(after.count, before.count);
    check_heap_counter(after.bytes, before.bytes);
    expect_compose_test(valid);
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.beat_bookmarks.size() == 2 && view.audio_sections.size() == 2);
    expect_compose_test(view.semantic_layers.size() == 2 && view.note_menu_layers.size() == 2);
    expect_compose_test(view.quantize_presets.size() == 1 && view.recipe_form.visible);
    expect_compose_test(view.measure_rows.front().measure_id == stored.measures.front().id);
    expect_compose_test(view.branch_diagnostic_count() == 1 && view.branch_diagnostic_at(0) == std::string(100, 'f'));
}

TEST_CASE("Owned BMS statement projection preserves every retained source byte",
          "[sys][compose-remaining][bms-branch]") {
    const bms_control_node_t node{.id = branch_id_t{42}, .kind = bms_control_kind_t::raw_statement,
        .original_text = std::string{"raw\0tail", 8}, .source_order = 7};
    std::vector<wormedit::ui::compose_branch_row_view_t> rows{
        {.has_source_order = true, .source_order = 7, .node_id = branch_id_t{42}, .raw_statement = true}};
    std::size_t index{};
    compose_projection_detail::bind_branch_statement_metadata(node, rows, index);
    expect_compose_test(index == 1);
    expect_compose_test(rows.front().statement_id == node.id);
    expect_compose_test(rows.front().statement_text == node.original_text);
}
