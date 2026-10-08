#include "sys/frame_timing.h"
#include "sys/application_defaults.h"
#include "sys/timing_view.h"
#include "sys/waveform_view_projection.h"
#include "sys/waveform_analysis_runtime.h"
#include "sys/bmson_waveform_projection.h"
#include "sys/compose_view.h"
#include "sys/compose_input.h"
#include "sys/compose_area_actions.h"
#include "sys/recipe_form.h"
#include "sys/compose_playhead_cursor.h"
#include "sys/compose_minimap_navigation.h"
#include <wormedit/formats/bms.h>
#include <span>
#include <wormedit/core/command.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/workspace.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstdio>
#include <cstring>
#include <algorithm>
#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#endif
#include <array>
#include <limits>
#include <cmath>
#include <filesystem>
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
auto dense_song() -> song_document_t {
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
    for (std::uint64_t measure = 0; measure < 1000; ++measure) {
        measure_chunk_t value;
        value.id = measure_id_t{measure + 1};
        for (std::uint64_t note = 0; note < 50; ++note)
            value.notes.push_back({.id = object_id_t{3 + measure * 50 + note},
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
    auto song = dense_song();
    song.charts.front().measures.resize(1);
    song.charts.front().measures.front().notes.resize(8);
    return song;
}
struct compose_test_fixture_t {
    document_history_t history;
    decltype(make_workspace_controller(history)) binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    flattened_workspace_t flattened;
    explicit compose_test_fixture_t(song_document_t song = compose_test_song())
        : history(make_document_history(std::move(song))), binding(make_workspace_controller(history)) {
        expect_compose_test(binding.workspace.has_value());
        binding.workspace->active_tab = workspace_tab_t::compose;
        binding.workspace->selected_chart = chart_id_t{1};
    }
    void project() {
        flattened = {};
        flattened.view.compose.visible = true;
        populate_compose_view(flattened, history, *binding.workspace, cache, 1280, 980,
                              interaction, application, nullptr);
        expect_compose_test(flattened.view.compose.scene_available);
    }
};

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
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr, {}, &input);
    expect_compose_test(fixture.flattened.view.compose.scene_available);
}

auto project_bga_preview_row(compose_test_fixture_t& fixture,
                            bool downscroll, bool snap_enabled,
                            std::uint64_t raw_row = 2 * 960 + 120) -> double {
    auto& state = *fixture.interaction;
    state.selected_objects = {{editor_selection_object_kind_t::bga_event, object_id_t{500}}};
    fixture.application.edit_snap_enabled = snap_enabled;
    state.snap_enabled = snap_enabled;
    state.active_snap_denominator = 4;
    state.viewport_scroll_pixels = 0;
    state.pixels_per_standard_measure = 192;
    state.viewport_height_pixels = fixture.flattened.view.compose.viewport_height_pixels;
    const auto direction = downscroll ? std::uint8_t{2} : std::uint8_t{1};
    state.scroll_direction = downscroll ? editor_field_scroll_direction_t::down
                                        : editor_field_scroll_direction_t::up;
    fixture.application.editor_preferences.scroll_directions[
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
    INFO("preview stamps: input project=" << preview.project_id << " view=" << view.project_id
         << " input chart=" << preview.chart_id << " view chart=" << view.chart_id
         << " input revision=" << preview.document_revision
         << " view revision=" << view.document_revision << " event count="
         << view.bga_event_count << " lane visible=" << view.bga_lane_visible
         << " selected ids=" << state.selected_objects.size() << " scroll="
         << state.viewport_scroll_pixels << " px/measure=" << state.pixels_per_standard_measure
         << " viewport height=" << state.viewport_height_pixels);
    if (view.bga_event_count != 0)
        INFO("first event=" << view.bga_events[0].event_id << " selected="
             << view.bga_events[0].selected << " kind="
             << static_cast<int>(view.bga_events[0].kind));
    REQUIRE(view.bga_move_preview_valid);
    CHECK(view.bga_move_preview_event_id == 500);
    const auto source_y = downscroll ? view.viewport_height_pixels - view.bga_move_preview_y_pixels
                                     : view.bga_move_preview_y_pixels;
    const auto preview_row = (state.viewport_scroll_pixels + source_y) *
        960.0 / state.pixels_per_standard_measure;
    return (state.viewport_scroll_pixels + source_y) *
        960.0 / state.pixels_per_standard_measure;
}

auto authored_release_position(const document_history_t& history,
                               const editor_interaction_state_t& state,
                               std::uint64_t raw_row, std::uint64_t denominator)
    -> std::optional<local_position_t> {
    const auto& chart = history.document.charts.front();
    const auto model = inspect_editor_interaction(history, state);
    REQUIRE(std::holds_alternative<editor_interaction_view_model_t>(model));
    const auto position = editor_position_at_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(model),
        static_cast<double>(raw_row) * 192.0 / 960.0);
    if (!position || denominator == 0) return position;
    const auto index = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    return snap_position(chart, std::get<measure_index_t>(index), *position, denominator);
}

auto committed_bga_row(const chart_document_t& chart) -> std::uint64_t {
    const auto event = std::ranges::find(chart.bga_events, object_id_t{500}, &bga_event_t::id);
    REQUIRE(event != chart.bga_events.end());
    const auto index = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    const auto row = absolute_row(std::get<measure_index_t>(index), event->position);
    REQUIRE(row);
    return *row;
}

auto commit_authored_bga_move(compose_test_fixture_t& fixture,
                              std::uint64_t raw_row) -> std::uint64_t {
    auto history = fixture.history;
    const auto& chart = history.document.charts.front();
    const auto denominator = edit_snap_denominator(chart, *fixture.interaction);
    const auto position = authored_release_position(history, *fixture.interaction,
                                                    raw_row, denominator);
    REQUIRE(position);
    const auto moved = apply_bga_event_move(history, history.document.revision,
        {.chart = chart_id_t{1}, .event = object_id_t{500}, .position = *position,
         .snap_denominator = denominator});
    REQUIRE(moved);
    return committed_bga_row(history.document.charts.front());
}

auto commit_virtual_bga_move(compose_test_fixture_t& fixture,
                             std::uint64_t raw_row) -> std::uint64_t {
    auto history = fixture.history;
    const auto& chart = history.document.charts.front();
    const auto moved = apply_virtual_bms_bga_event_move(history, history.document.revision,
        {.chart = chart_id_t{1}, .event = object_id_t{500}, .canvas_row = raw_row,
         .snap_denominator = edit_snap_denominator(chart, *fixture.interaction)});
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
            application.preview_playhead_absolute_row = row;
            const auto model = make_workspace_view(workspace, history.document);
            REQUIRE(model.view);
            auto flattened = flatten_workspace(*model.view, history.document, "", {}, false, {});
            flattened.rebind_view_pointers();
            populate_compose_view(flattened, history, workspace, cache, 1920, 1080,
                                  interaction, application, nullptr);
            REQUIRE(flattened.view.compose.scene_available);
            REQUIRE(!flattened.compose.note.empty());
        }
        timer.end_frame();
}

TEST_CASE("Frame timing projects real play follow on a 50000 note chart", "[sys][frame-timing][performance]") {
    auto history = make_document_history(dense_song());
    auto binding = make_workspace_controller(history);
    REQUIRE(binding.workspace);
    auto& workspace = *binding.workspace;
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
void check_inline_timing_fields(const WormUiTimingView& view, std::uint64_t revision) {
    CHECK(view.chart_id == 1);
    CHECK(view.document_revision == revision);
    CHECK(view.initial_bpm_present);
    CHECK(view.initial_bpm_value == 120.0);
    CHECK(view.stage_active);
    CHECK(view.stage_valid);
}
void check_slim_timing_geometry(const WormUiTimingView& view, const timing_scene_cache_t& cache) {
    CHECK(view.grid_line_count == 0);
    CHECK(view.marker_count == 0);
    CHECK(view.speed_count == 0);
    CHECK_FALSE(cache.attempted);
}
} // namespace

TEST_CASE("Compose timing projection skips whole-chart geometry and preserves inline fields", "[sys][timing-projection]") {
    auto history = make_document_history(dense_song());
    auto binding = make_workspace_controller(history);
    REQUIRE(binding.workspace);
    auto& workspace = *binding.workspace;
    workspace.selected_chart = chart_id_t{1};
    flattened_workspace_t flattened;
    flattened.view.compose.visible = true;
    timing_scene_cache_t cache;
    timing_grid_state_t grid;
    grid.active_denominator = 4;
    timing_lane_pseudonote_preview_t stage;
    stage.status = timing_lane_pseudonote_status_t::editing;
    stage.chart = chart_id_t{1};
    stage.document_revision = history.document.revision;
    stage.position = {measure_id_t{1}, 0};
    stage.valid = true;
    populate_timing_view_for_compose(flattened, history, workspace, cache, 1280, 980,
                                    grid, &stage);
    const auto& view = flattened.view.timing;
    check_inline_timing_fields(view, history.document.revision);
    check_slim_timing_geometry(view, cache);
    REQUIRE(cache.measures);
    const auto* entries = cache.measures->entries.data();
    populate_timing_view_for_compose(flattened, history, workspace, cache, 1280, 980,
                                    grid, &stage);
    CHECK(cache.measures->entries.data() == entries);
}

TEST_CASE("Idle Compose timing projection does not build a measure index", "[sys][timing-projection]") {
    auto history = make_document_history(dense_song());
    auto binding = make_workspace_controller(history);
    REQUIRE(binding.workspace);
    binding.workspace->selected_chart = chart_id_t{1};
    flattened_workspace_t flattened;
    timing_scene_cache_t cache;
    populate_timing_view_for_compose(flattened, history, *binding.workspace, cache,
                                    1280, 980, {}, nullptr);
    CHECK_FALSE(cache.measure_attempted);
    CHECK_FALSE(cache.measures);
    CHECK(flattened.view.timing.scene_available);
    CHECK(flattened.view.timing.total_rows == 960000);
    CHECK(cache.compose_total_rows == flattened.view.timing.total_rows);
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
TEST_CASE("Waveform runtime reports decoder failures instead of silently losing the waveform",
          "[sys][waveform-projection]") {
    audio::decoded_source_registry_t sources;
    notification_center_t center;
    notification_sink_t sink{center};
    runtime::waveform_analysis_t waveform(sources, nullptr, sink);
    workspace_controller_t workspace;
    workspace.transport.audio_asset = asset_id_t{42};
    compose_scene_cache_t cache;
    song_document_t document;
    waveform.service(document, workspace, cache, nullptr);
    for (std::size_t attempt = 0; attempt < 200 && waveform.waveform_job().busy(); ++attempt) {
        SDL_Delay(5);
        waveform.service(document, workspace, cache, nullptr);
    }
    expect_compose_test(!waveform.waveform_job().busy());
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
void check_overview_replacement(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                                const editor_interaction_state_t& interaction) {
    auto replacement = overview_analysis(0.75f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
    CHECK(flattened.compose.waveform.overview_row.front().absolute_peak == 0.75f);
    ++cache.revision;
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &replacement, false,
                                                 true, &interaction));
    CHECK(cache.waveform_overview_revision == cache.revision);
}
}
TEST_CASE("Compose overview keeps cached rows while moving its visible range", "[sys][waveform-projection]") {
    auto history = make_document_history(dense_song());
    compose_scene_cache_t cache;
    auto timing = build_timing_index(history.document.charts.front());
    REQUIRE(timing.index);
    cache.marker_timing_index = std::move(timing.index);
    flattened_workspace_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 200;
    flattened.view.compose.pixels_per_standard_measure = 200;
    editor_interaction_state_t interaction;
    auto analysis = overview_analysis(0.25f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    const auto fraction = flattened.view.compose.waveform_overview_visible_begin_fraction;
    const auto* rows = cache.waveform_overview_rows.data();
    interaction.viewport_scroll_pixels = 200;
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false, true, &interaction));
    CHECK(cache.waveform_overview_rows.data() == rows);
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
    cache.marker_timing_index = build_timing_index(song.charts.front()).index;
    REQUIRE(cache.marker_timing_index);
    flattened_workspace_t flattened;
    flattened.view.compose.visible = true;
    flattened.view.compose.scene_available = true;
    flattened.view.compose.viewport_height_pixels = 800;
    flattened.view.compose.pixels_per_standard_measure = 192;
    editor_interaction_state_t interaction;
    interaction.viewport_scroll_pixels = -160;
    auto analysis = overview_analysis(0.5f);
    REQUIRE(populate_compose_waveform_projection(flattened, cache, &analysis, false,
                                                true, &interaction));
    REQUIRE(flattened.view.compose.waveform_row_count > 0);
    CHECK(flattened.compose.waveform.row.front().y_pixels >= 160);
    CHECK(flattened.compose.waveform.row.front().absolute_peak[0] == 0.5f);
}
namespace { void expect_gpu_work(bool value); }
TEST_CASE("Compose envelope projects only exposed stable world rows during follow", "[sys][waveform-projection]") {
    auto history = make_document_history(dense_song());
    compose_scene_cache_t cache;
    cache.marker_timing_index = build_timing_index(history.document.charts.front()).index;
    expect_gpu_work(cache.marker_timing_index.has_value());
    flattened_workspace_t flattened;
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
        flattened.compose.waveform.row.clear();
        return populate_compose_waveform_projection(flattened, cache, &analysis, false, true,
                                                    &interaction, nullptr, &blocks, &style);
    };
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 202);
    expect_gpu_work(cache.waveform_envelope_ring.projection_calls == 1);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 0);
    expect_gpu_work(cache.waveform_envelope_ring.reused_rows == 202);
    interaction.viewport_scroll_pixels = 11.25;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 1);
    expect_gpu_work(cache.waveform_envelope_ring.reused_rows == 201);
    const audio::audio_block_key_t key{{"unrelated", "immutable"}, 0};
    std::array<float, 16> pcm{};
    expect_gpu_work(blocks.store(key, 8, pcm) == audio::block_store_status_t::stored);
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 0);
    interaction.viewport_scroll_pixels = 10.75;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 1);
    interaction.viewport_scroll_pixels = 2000;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 202);
    ++cache.revision;
    expect_gpu_work(project());
    expect_gpu_work(cache.waveform_envelope_ring.projected_rows == 202);
}
namespace {
struct envelope_projection_fixture_t {
    document_history_t history{make_document_history(dense_song())};
    compose_scene_cache_t cache;
    flattened_workspace_t flattened;
    editor_interaction_state_t interaction;
    audio::waveform_analysis_result_t analysis{overview_analysis(.25f)};
    audio::decoded_block_cache_t blocks;
    audio::aligned_waveform_style_t style;
    envelope_projection_fixture_t() {
        cache.chart = history.document.charts.front().id;
        cache.revision = history.document.revision;
        cache.marker_timing_index = build_timing_index(history.document.charts.front()).index;
        expect_gpu_work(cache.marker_timing_index.has_value());
        auto& view = flattened.view.compose;
        view.visible = view.scene_available = true;
        view.project_id = history.document.id.value;
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
void expect_published_waveform_rows(std::span<const WormUiComposeWaveformRowView> rows, double height) {
    for (const auto& row : rows)
        expect_gpu_work(std::isfinite(row.y_pixels) && row.y_pixels >= 0 && row.y_pixels <= height);
}
void compare_cached_envelope_source(envelope_projection_fixture_t& fixture) {
    const auto height = fixture.flattened.view.compose.viewport_height_pixels;
    const auto scroll = std::floor(fixture.interaction.viewport_scroll_pixels);
    const auto reference = audio::project_composed_aligned_waveform(*fixture.analysis.waveform,
        nullptr, *fixture.cache.marker_timing_index,
        {.viewport_scroll_pixels = scroll - 1, .viewport_height_pixels = height + 2,
         .pixels_per_standard_measure = fixture.flattened.view.compose.pixels_per_standard_measure},
        &fixture.blocks, fixture.style);
    expect_gpu_work(static_cast<bool>(reference));
    std::size_t index = 0;
    for (const auto& row : reference.rows) {
        const auto y = row.y_pixels - 1;
        if (y < 0 || y > height) continue;
        expect_gpu_work(index < fixture.flattened.compose.waveform.row.size());
        const auto& cached = fixture.flattened.compose.waveform.row[index++];
        expect_gpu_work(std::abs(cached.y_pixels - y) < 1e-8);
        expect_gpu_work(std::abs(cached.absolute_peak[0] - row.absolute_peak[0]) < 1e-6f);
        expect_gpu_work(std::abs(cached.rms[0] - row.rms[0]) < 1e-6f);
    }
    expect_gpu_work(index == fixture.flattened.compose.waveform.row.size());
}
void check_waveform_axis(envelope_projection_fixture_t& fixture, bool sample_points) {
    auto& view = fixture.flattened.view.compose;
    view.downscroll = false;
    fixture.project();
    const auto& source = fixture.flattened.compose.waveform.row;
    const std::vector<WormUiComposeWaveformRowView> upscroll(source.begin(), source.end());
    expect_gpu_work(!upscroll.empty());
    view.downscroll = true;
    fixture.project();
    const auto& downscroll = fixture.flattened.compose.waveform.row;
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
    const auto before = fixture.flattened.compose.waveform.row[80];
    const auto& view = fixture.flattened.view.compose;
    const auto pixels_per_row = view.pixels_per_standard_measure /
        fixture.cache.marker_timing_index->divisions_per_standard_measure;
    const auto world_row = (fixture.interaction.viewport_scroll_pixels +
        view.viewport_height_pixels - before.y_pixels) / pixels_per_row;
    fixture.interaction.viewport_scroll_pixels += 25;
    fixture.project();
    const auto& after = fixture.flattened.compose.waveform.row[55];
    expect_gpu_work(std::ranges::equal(after.absolute_peak, before.absolute_peak));
    expect_gpu_work(after.y_pixels == before.y_pixels + 25);
    const auto chart_y = compose_project_y(world_row, pixels_per_row,
        fixture.interaction.viewport_scroll_pixels, fixture.flattened.view.compose.viewport_height_pixels, true);
    expect_gpu_work(std::abs(chart_y - after.y_pixels) < 1e-8);
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 25);
}
void prepare_waveform_sample_points(envelope_projection_fixture_t& fixture) {
    fixture.flattened.view.compose.pixels_per_standard_measure = 200'000;
    fixture.style = {.sample_points = true, .detail_level = true};
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm) == audio::block_store_status_t::stored);
}
void prepare_seven_key_mp3_project(envelope_projection_fixture_t& fixture) {
    const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
        "fixtures" / "audio" / "sliced-seek-source.mp3";
    audio::decoder_t decoder;
    expect_gpu_work(decoder.open(path));
    const auto info = decoder.info();
    expect_gpu_work(info.has_value());
    const auto song = audio_project_document(song_id_t{42}, path, "7K MP3", "", 7,
        {info->source_sample_rate, info->source_channel_count, info->source_frame_count, info->seekable});
    expect_gpu_work(song.has_value());
    fixture.history = make_document_history(*song);
    const auto& chart = fixture.history.document.charts.front();
    expect_gpu_work(chart.lane_layouts.front().layout.lanes.size() == 7);
    fixture.cache = {};
    fixture.cache.chart = chart.id;
    fixture.cache.revision = fixture.history.document.revision;
    fixture.cache.marker_timing_index = build_timing_index(chart).index;
    fixture.flattened.view.compose.downscroll = compose_field_downscroll(chart);
    expect_gpu_work(fixture.flattened.view.compose.downscroll);
    audio::waveform_cache_t waveforms;
    fixture.analysis = audio::analyze_waveform(decoder, fixture.blocks, waveforms,
        {"fixture:7k-mp3", "immutable:v1"}, 256);
    expect_gpu_work(fixture.analysis.waveform.has_value());
}
}
TEST_CASE("MP3 project 7K waveform follows downscroll chart movement while reusing cached rows",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    prepare_seven_key_mp3_project(fixture);
    check_waveform_axis(fixture, false);
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 0);
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
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 34);
    expect_gpu_work(std::ranges::any_of(fixture.cache.waveform_envelope_ring.rows,
        [](const auto& row) { return row.incomplete; }));
    const auto coarse_peak = fixture.flattened.compose.waveform.row.front().absolute_peak[0];
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 0);
    const audio::audio_block_key_t key{fixture.analysis.waveform->key.source, 0};
    const std::vector<float> pcm(32768, .8f);
    expect_gpu_work(fixture.blocks.store(key, 16384, pcm) == audio::block_store_status_t::stored);
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 34);
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projection_calls == 1);
    expect_gpu_work(std::ranges::all_of(fixture.cache.waveform_envelope_ring.rows,
        [](const auto& row) { return row.present && !row.incomplete; }));
    const auto& resolved = fixture.flattened.compose.waveform.row.front();
    expect_gpu_work(std::abs(resolved.absolute_peak[0] - .8f) < .00001f);
    expect_gpu_work(std::abs(resolved.rms[0] - .8f) < .00001f);
    expect_gpu_work(resolved.absolute_peak[0] != coarse_peak);
    const audio::audio_block_key_t unrelated{{"elsewhere", "unchanged"}, 0};
    expect_gpu_work(fixture.blocks.store(unrelated, 16384, pcm) == audio::block_store_status_t::stored);
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 0);
    expect_gpu_work(fixture.cache.waveform_envelope_ring.reused_rows == 34);
    expect_gpu_work(std::abs(fixture.flattened.compose.waveform.row.front().absolute_peak[0] - .8f) < .00001f);
}
TEST_CASE("Compose envelope chart identity changes recompute actual source timing at unchanged revision",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.project();
    compare_cached_envelope_source(fixture);
    expect_gpu_work(!fixture.flattened.compose.waveform.row.empty());
    const auto peak = fixture.flattened.compose.waveform.row.front().absolute_peak[0];
    auto second = fixture.history.document.charts.front();
    second.id = chart_id_t{2};
    second.bpms.front().beats_per_minute = {240, 1};
    fixture.cache.chart = second.id;
    fixture.cache.marker_timing_index = build_timing_index(second).index;
    expect_gpu_work(fixture.cache.marker_timing_index.has_value());
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 202);
    compare_cached_envelope_source(fixture);
    expect_gpu_work(fixture.flattened.compose.waveform.row.front().absolute_peak[0] < peak);
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 0);
    ++fixture.flattened.view.compose.project_id;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 202);
}
TEST_CASE("Compose envelope padding stays private at zero fractional and normal viewport scroll",
          "[sys][waveform-projection]") {
    envelope_projection_fixture_t fixture;
    fixture.flattened.view.compose.viewport_height_pixels = 532;
    for (const auto scroll : {0.0, .75, 10.0, 10.75, 100.25}) {
        fixture.interaction.viewport_scroll_pixels = scroll;
        fixture.project();
        expect_published_waveform_rows(fixture.flattened.compose.waveform.row, 532);
        expect_gpu_work(fixture.flattened.compose.waveform.row.size() <= 532);
    }
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_envelope_ring.projected_rows == 0);
}
namespace {
struct channel_projection_fixture_t : envelope_projection_fixture_t {
    workspace_controller_t workspace;
    audio::decoded_source_registry_t sources;
    std::optional<asset_id_t> asset{asset_id_t{7}};
    std::shared_ptr<const audio::waveform_analysis_result_t> channel_analysis;
    channel_projection_fixture_t() {
        const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                          "fixtures" / "audio" / "keysound-c4.ogg";
        const auto added = sources.register_source({asset_id_t{7}, path, {"waveform:test", "immutable:v1"}});
        expect_gpu_work(static_cast<bool>(added));
        history.document.audio_assets.push_back({.id = asset_id_t{7}, .source_path = path,
            .stream = {added.binding->source_sample_rate, 2, added.binding->source_frame_count, true}});
        auto& waveform = *analysis.waveform;
        waveform.key.source = added.binding->source;
        waveform.sample_rate = added.binding->output_sample_rate;
        waveform.total_frames = added.binding->output_frame_count;
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
        flattened.view.compose.selected_bmson_bgm_event_id = 74000;
        interaction.viewport_scroll_pixels = 0;
    }
    void project_channel() {
        cache.channel_waveform_rows.clear();
        const bmson_waveform_projection_context_t context{flattened, history, workspace, &interaction,
            cache, sources, {asset, channel_analysis}, {asset, channel_analysis}, 1};
        populate_bmson_waveform_projection(context);
        expect_published_waveform_rows(cache.channel_waveform_rows, flattened.view.compose.viewport_height_pixels);
    }
};
}
TEST_CASE("BMSON channel switches sharing an asset preserve independent timing and bounded padded rows",
          "[sys][waveform-projection][bmson]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    expect_gpu_work(!fixture.cache.channel_waveform_rows.empty());
    const auto first = fixture.cache.channel_waveform_rows.front().y_pixels;
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = 74001;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.channel_envelope_ring.channel == 71001);
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 202);
    expect_gpu_work(!fixture.cache.channel_waveform_rows.empty());
    expect_gpu_work(fixture.cache.channel_waveform_rows.front().y_pixels > first + 40);
    fixture.interaction.viewport_scroll_pixels = .75;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 0);
    fixture.interaction.viewport_scroll_pixels = 1.25;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 1);
    fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().glyph = lane_glyph_t::bar;
    fixture.flattened.view.compose.downscroll = true;
    fixture.cache.channel_waveform_projection_valid = false;
    fixture.interaction.viewport_scroll_pixels = 0;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.channel_waveform_rows.front().y_pixels > 140);
}
TEST_CASE("BMSON stems other than the selected one get their own lane rows or a pending band",
          "[sys][waveform-projection][bmson][stems]") {
    channel_projection_fixture_t fixture;
    std::shared_ptr<const audio::waveform_analysis_result_t> stem_result = fixture.channel_analysis;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, fixture.sources, {fixture.asset, fixture.channel_analysis},
        {fixture.asset, fixture.channel_analysis}, 1,
        [&](asset_id_t) { return stem_result; }};

    // Channel 71000 is selected through its marker, so only 71001 is a stem.
    populate_bmson_waveform_projection(context);
    CHECK(fixture.flattened.view.compose.stem_waveform_count == 1);
    const auto& stem = fixture.flattened.view.compose.stem_waveforms[0];
    CHECK(stem.source_lane_id == 4);
    expect_gpu_work(stem.stem_key == 71001);
    CHECK((!stem.pending && stem.row_count > 0));
    CHECK(stem.row_count <= fixture.flattened.view.compose.viewport_height_pixels + 2);

    // Selecting the other marker swaps which stem is the extra one.
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = 74001;
    populate_bmson_waveform_projection(context);
    CHECK(fixture.flattened.view.compose.stem_waveform_count == 1);
    // The selected channel keeps an entry too, for its field taps and cut lines.
    CHECK(fixture.cache.stem_waveforms.size() == 2);
    CHECK(fixture.cache.stem_waveforms.front().channel == object_id_t{71000});

    // Audio that has not finished decoding is a placeholder, never a blocked frame.
    stem_result.reset();
    const bmson_waveform_projection_context_t pending_context{fixture.flattened, fixture.history, fixture.workspace,
        &fixture.interaction, fixture.cache, fixture.sources, {fixture.asset, nullptr},
        {std::nullopt, nullptr}, 1, [&](asset_id_t) { return stem_result; }};
    populate_bmson_waveform_projection(pending_context);
    CHECK(fixture.flattened.view.compose.stem_waveform_count == 1);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].pending);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveforms[0].stem_key == 71000);
    CHECK(fixture.flattened.view.compose.stem_waveforms[0].row_count == 0);
}

namespace {
auto project_stable_stem_keys(channel_projection_fixture_t& fixture, bool pending = false)
    -> std::vector<std::uint64_t> {
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = 0;
    const bmson_waveform_projection_context_t context{fixture.flattened, fixture.history,
        fixture.workspace, &fixture.interaction, fixture.cache, fixture.sources,
        {fixture.asset, fixture.channel_analysis}, {std::nullopt, nullptr}, 1,
        [&](asset_id_t) { return pending ? nullptr : fixture.channel_analysis; }};
    populate_bmson_waveform_projection(context);
    std::vector<std::uint64_t> keys;
    for (std::size_t i = 0; i < fixture.flattened.view.compose.stem_waveform_count; ++i) {
        const auto& view = fixture.flattened.view.compose.stem_waveforms[i];
        expect_gpu_work(view.pending == pending);
        expect_gpu_work((view.row_count == 0) == pending);
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
    REQUIRE(fixture.sources.register_source({*fixture.asset, path,
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
    const auto prepared = indexed_visible_stem_channels(fixture.cache.stem_index,
        fixture.history.document, chart, fixture.interaction.cursor.measure, 0,
        bindings, &*fixture.cache.marker_timing_index);
    REQUIRE(prepared.size() == 2);
    CHECK(fixture.cache.stem_index.cursor == measure_id_t{4});
    CHECK(fixture.cache.stem_index.window == 0);
    return {prepared, {prepared[0]->id, prepared[1]->id},
            fixture.cache.stem_index.bindings, fixture.cache.stem_index.selection_build_count};
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
        fixture.workspace, &fixture.interaction, fixture.cache, fixture.sources,
        {fixture.asset, fixture.channel_analysis}, {fixture.asset, fixture.channel_analysis}, 1,
        [](asset_id_t) { return std::shared_ptr<const audio::waveform_analysis_result_t>{}; },
        channels};
    populate_bmson_waveform_projection(context);
}

void check_prepared_stem_channels(const channel_projection_fixture_t& fixture,
                                  const prepared_stem_frame_t& prepared) {
    REQUIRE(fixture.cache.stem_waveforms.size() == prepared.ids.size());
    for (std::size_t i = 0; i < prepared.ids.size(); ++i)
        CHECK(fixture.cache.stem_waveforms[i].channel == prepared.ids[i]);
}

void check_prepared_stem_cache(const channel_projection_fixture_t& fixture,
                               const prepared_stem_frame_t& prepared) {
    CHECK(fixture.cache.stem_index.bindings == prepared.bindings);
    CHECK(fixture.cache.stem_index.selected.size() == prepared.ids.size());
    CHECK(fixture.cache.stem_index.cursor == measure_id_t{4});
    CHECK(fixture.cache.stem_index.window == 0);
    CHECK(fixture.cache.stem_index.selection_build_count == prepared.selection_build_count);
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
        changed_bindings, &*fixture.cache.marker_timing_index);
    CHECK(next_frame.empty());
}

namespace {
auto reference_frame(const timing_index_t& timing, long double row, bool end,
                     const audio::waveform_t& waveform) -> std::uint64_t {
    const auto integer = static_cast<std::uint64_t>(std::floor(row));
    const auto fraction = row - integer;
    const auto first = timing_at(timing, integer,
        fraction == 0 && !end ? timing_boundary_side_t::before_delays
                             : timing_boundary_side_t::after_stops);
    auto seconds = static_cast<long double>(first.seconds->numerator) / first.seconds->denominator;
    if (fraction != 0) {
        const auto next = timing_at(timing, integer + 1, timing_boundary_side_t::before_delays);
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
    return *build_timing_index(chart).index;
}
void check_clock_projection(const audio::waveform_t& waveform, const timing_index_t& timing,
                            double scroll, bool descending = false) {
    audio::aligned_waveform_request_t request{.viewport_scroll_pixels = scroll,
        .viewport_height_pixels = 64, .pixels_per_standard_measure = 192};
    const auto result = audio::project_composed_aligned_waveform(waveform, nullptr, timing,
                                                               request, nullptr, {});
    REQUIRE(result);
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
    flattened_workspace_t flattened;
    flattened.view.compose.visible = true;
    application_command_state_t application;
    populate_compose_view(flattened, history, workspace, cache, 1280, 980, interaction,
                          application, nullptr);
    INFO(flattened.projection_feedback.size());
    REQUIRE(flattened.view.compose.scene_available);
}
}
TEST_CASE("Compose controls retain measure geometry until the document revision changes", "[sys][timing-projection]") {
    auto history = make_document_history(dense_song());
    auto binding = make_workspace_controller(history);
    REQUIRE(binding.workspace);
    auto& workspace = *binding.workspace;
    workspace.active_tab = workspace_tab_t::compose;
    workspace.selected_chart = chart_id_t{1};
    compose_scene_cache_t cache;
    std::optional<editor_interaction_state_t> interaction;
    populate_measure_cache_frame(history, workspace, cache, interaction);
    REQUIRE(cache.cached_measure_index);
    const auto* entries = cache.cached_measure_index->entries.data();
    populate_measure_cache_frame(history, workspace, cache, interaction);
    CHECK(cache.cached_measure_index->entries.data() == entries);
    history.document.charts.front().measures.back().definition = {{2, 1}, meter_t{8, 4}};
    ++history.document.revision;
    static_cast<void>(rebind_workspace(workspace, history));
    interaction.reset(); // Match the application stale-interaction boundary after an edit.
    populate_measure_cache_frame(history, workspace, cache, interaction);
    CHECK(cache.cached_measure_index->total_rows == 960960);
    CHECK(cache.revision == history.document.revision);
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
    auto view = worm_ui_frame_timing_view();
    expect_gpu_work(view.gpu_work_available && view.diagnostic_gpu_work_available);
    expect_gpu_work(view.gpu_work_latest.vertices == 1000 && view.gpu_work_latest.draw_calls == 50);
    expect_gpu_work(view.diagnostic_gpu_work_latest.vertices == 400);
    expect_gpu_work(view.diagnostic_gpu_work_latest.texture_switches == 1);
    expect_gpu_work(view.diagnostic_gpu_work[0].count >= 1);
    timer.begin_frame();
    worm_ui_frame_timing_record_gpu_work({1000, 3000, 50, 20, 10});
    timer.end_frame();
    view = worm_ui_frame_timing_view();
    expect_gpu_work(view.gpu_work_available && !view.diagnostic_gpu_work_available);
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
    auto setup = make_editor_interaction(history, chart_id_t{1}, lane_id_t{1},
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
    auto history = make_document_history(std::move(song));
    const auto index = std::get<measure_index_t>(build_measure_index(history.document.charts.front()));
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
    auto history = make_document_history(dense_song());
    const auto index = std::get<measure_index_t>(build_measure_index(history.document.charts.front()));
    auto state = playhead_test_state(history);
    const auto before = state;
    CHECK_FALSE(follow_compose_playhead(history, state, 1920, true, {chart_id_t{2}, 1, &index}));
    CHECK_FALSE(follow_compose_playhead(history, state, 1920, true, {chart_id_t{1}, 2, &index}));
    compare_follow_state(state, before);
    state.pending_editor_area_selection.emplace();
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
    document_history_t history=make_document_history(static_style_song());
    decltype(make_workspace_controller(history)) binding;
    compose_scene_cache_t cache;
    application_command_state_t application;
    std::optional<editor_interaction_state_t> interaction;
    flattened_workspace_t flattened;
    static_style_fixture_t(song_document_t song=static_style_song())
        : history(make_document_history(std::move(song))), binding(make_workspace_controller(history)) {
        expect_static_style(binding.workspace.has_value());
        binding.workspace->active_tab=workspace_tab_t::compose;
        binding.workspace->selected_chart=chart_id_t{1};
    }
    auto project() -> std::uint64_t {
        // The application creates a fresh flattened frame; source vectors are
        // appended during projection and must not retain the previous frame.
        flattened={};
        flattened.view.compose.visible=true;
        populate_compose_view(flattened,history,*binding.workspace,cache,1280,980,
                              interaction,application,nullptr);
        expect_static_style(flattened.view.compose.scene_available);
        return flattened.view.compose.static_scene_style_generation;
    }
    auto first_note() const -> std::uint64_t {
        expect_static_style(flattened.view.compose.note_endpoint_count!=0);
        return flattened.view.compose.note_endpoints[0].note_id;
    }
};
}
TEST_CASE("Compose static style generation follows exact semantic filters without invalidating scroll",
          "[sys][field-tile-style]") {
    static_style_fixture_t fixture;
    const auto initial=fixture.project();
    expect_static_style(fixture.project()==initial);
    fixture.application.semantic_layer_target=layer_id_t{101};
    fixture.application.semantic_layer_solo_active=true;
    const auto first=fixture.project();
    const auto first_note=fixture.first_note();
    expect_static_style(first>initial);
    fixture.application.semantic_layer_target=layer_id_t{102};
    const auto second=fixture.project();
    expect_static_style(second>first);
    expect_static_style(fixture.first_note()!=first_note);
    fixture.application.semantic_layer_solo_active=false;
    fixture.application.required_semantic_layer_filters={layer_id_t{101}};
    const auto required=fixture.project();
    expect_static_style(required>second);
    expect_static_style(fixture.first_note()==first_note);
    fixture.application.required_semantic_layer_filters={layer_id_t{102}};
    const auto replacement=fixture.project();
    expect_static_style(replacement>required);
    expect_static_style(fixture.first_note()!=first_note);
    fixture.application.excluded_semantic_layer_filters={layer_id_t{101}};
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
    const auto project=fixture.flattened.view.compose.project_id;
    const auto chart=fixture.flattened.view.compose.chart_id;
    const auto revision=fixture.flattened.view.compose.document_revision;
    fixture.history.document.charts.front().measures.front().notes.front().position.row=60;
    fixture.cache={}; // Match main's real project-open cache replacement boundary.
    const auto replacement=fixture.project();
    expect_static_style(replacement!=initial);
    expect_static_style(fixture.flattened.view.compose.project_id==project);
    expect_static_style(fixture.flattened.view.compose.chart_id==chart);
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
    const auto full = compose_horizontal_layout(900, 5, true, true, 8);
    return full.bga_pane_left_pixels == 234.0 && full.bga_pane_width_pixels == 48.0 &&
           full.field_left_pixels == full.bga_pane_left_pixels + full.bga_pane_width_pixels &&
           full.bgm_pane_left_pixels ==
               full.field_left_pixels + full.playable_field_width_pixels &&
           full.bgm_pane_left_pixels + full.bgm_pane_width_pixels == 900.0;
}

auto compose_narrow_layout_is_ordered() -> bool {
    using namespace wormedit::sys;
    const auto hidden_timing = compose_horizontal_layout(900, 5, true, true, 0);
    const auto narrow = compose_horizontal_layout(280, 5, true, true, 8);
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
    const auto no_bga = compose_horizontal_layout(900, 5, true, false, 8);
    const auto no_bgm = compose_horizontal_layout(900, 5, false, true, 8);
    return no_bga.bga_pane_width_pixels == 0.0 &&
           no_bga.field_left_pixels == no_bga.bga_pane_left_pixels &&
           no_bga.field_left_pixels == 234.0 && no_bgm.bga_pane_left_pixels == 234.0 &&
           no_bgm.field_left_pixels == 282.0 &&
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
    const auto storage=fixture.cache.static_scene_storage;
    const auto style=view.static_scene_style_generation;
    fixture.interaction->viewport_scroll_pixels=65.3;
    fixture.project();
    expect_static_style(view.static_scene!=nullptr);
    expect_static_style(view.static_scene_source_generation==generation);
    expect_static_style(fixture.cache.static_scene_storage==storage);
    expect_static_style(view.static_scene_style_generation==style);
    expect_static_style(view.static_scene_accounted_bytes<=16*1024*1024);
    expect_static_style(view.static_scene->note_endpoint_count>view.note_endpoint_count);
    fixture.application.compose_timing_lane_visibility_mask^=1;
    fixture.project();
    expect_static_style(view.static_scene!=nullptr);
    expect_static_style(view.static_scene_source_generation!=generation);
    expect_static_style(view.static_scene->timing_lane_visibility_mask==view.timing_lane_visibility_mask);
}

TEST_CASE("new-chart requested custom snap is admitted without replacing existing chart preferences", "[frame-timing]") {
    application_command_state_t state;
    state.editor_preferences.default_snap = 5;
    state.requested_snap_denominators.emplace(1, 8);
    state.requested_snap_denominators.emplace(2, state.editor_preferences.default_snap);
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
void compare_scalar_bounds(WormUiComposeRect actual, WormUiComposeRect expected) {
    expect_static_style(std::abs(actual.left - expected.left) < 1e-8);
    expect_static_style(std::abs(actual.top - expected.top) < 1e-8);
    expect_static_style(std::abs(actual.right - expected.right) < 1e-8);
    expect_static_style(std::abs(actual.bottom - expected.bottom) < 1e-8);
}
void compare_scalar_notes(const WormUiComposeView& actual, const WormUiComposeView& expected) {
    expect_static_style(actual.note_endpoint_count == expected.note_endpoint_count);
    for (std::size_t index = 0; index < actual.note_endpoint_count; ++index) {
        const auto& note = actual.note_endpoints[index];
        const auto& fresh = expected.note_endpoints[index];
        compare_scalar_bounds(note.bounds, fresh.bounds);
        expect_static_style(std::tie(note.note_id, note.lane_id, note.display_lane_number,
                                     note.glyph, note.snap_class, note.kind, note.tail,
                                     note.selected, note.semantic_color_visible) ==
                            std::tie(fresh.note_id, fresh.lane_id, fresh.display_lane_number,
                                     fresh.glyph, fresh.snap_class, fresh.kind, fresh.tail,
                                     fresh.selected, fresh.semantic_color_visible));
        expect_static_style(std::string_view(note.sound_label) ==
                            std::string_view(fresh.sound_label));
        expect_static_style(std::string_view(note.sound_detail) ==
                            std::string_view(fresh.sound_detail));
    }
}
void compare_scalar_holds_bgm(const WormUiComposeView& actual, const WormUiComposeView& expected) {
    expect_static_style(actual.hold_body_count == expected.hold_body_count);
    for (std::size_t index = 0; index < actual.hold_body_count; ++index) {
        compare_scalar_bounds(actual.hold_bodies[index].bounds, expected.hold_bodies[index].bounds);
        expect_static_style(actual.hold_bodies[index].note_id ==
                            expected.hold_bodies[index].note_id);
        expect_static_style(actual.hold_bodies[index].selected ==
                            expected.hold_bodies[index].selected);
    }
    expect_static_style(actual.bgm_event_count == expected.bgm_event_count);
    for (std::size_t index = 0; index < actual.bgm_event_count; ++index) {
        compare_scalar_bounds(actual.bgm_events[index].bounds, expected.bgm_events[index].bounds);
        expect_static_style(actual.bgm_events[index].event_id ==
                            expected.bgm_events[index].event_id);
    }
}
void compare_scalar_grid(const WormUiComposeView& actual, const WormUiComposeView& expected) {
    expect_static_style(actual.grid_line_count == expected.grid_line_count);
    for (std::size_t index = 0; index < actual.grid_line_count; ++index) {
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
    expect_static_style(retained.cache.static_projection_reused_frames != 0);
    expect_static_style(retained.cache.static_projection_scanned <
                        retained.cache.static_scene_storage->view.compose.note_endpoint_count);
}
} // namespace
TEST_CASE(
    "Indexed retained scalar publication matches full composition while following and reversing",
    "[sys][field-tile-style][scalar-reuse]") {
    for (const auto direction : {std::uint8_t{0}, std::uint8_t{1}}) {
        static_style_fixture_t retained(scalar_reuse_song()), fresh(scalar_reuse_song());
        retained.application.editor_preferences.scroll_directions.fill(direction);
        fresh.application.editor_preferences = retained.application.editor_preferences;
        retained.project();
        fresh.project();
        for (const auto scroll : {0.0, 19.75, 65.3, 120.5, 65.3, 0.0})
            compare_scalar_reuse_frame(retained, fresh, scroll);
    }
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
    auto copied = copy_chart_selection(fixture.history.document, *fixture.interaction,
                                      fixture.history.document.revision);
    expect_compose_test(static_cast<bool>(copied));
    fixture.application.chart_clipboard = std::move(copied.clipboard);
}
}
TEST_CASE("Paste preview owns geometry across frames and publishes one undoable command",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 240;
    std::string status;
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history.document,
                               *fixture.interaction, status)));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.document.charts.front().measures.front().notes.size() == 1));
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_active));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghost_count == 1));
    const auto first = fixture.flattened.view.compose.paste_ghosts[0].bounds;
    fixture.flattened = {};
    fixture.project();
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghosts[0].bounds.top == first.top));
    expect_compose_test(static_cast<bool>(commit_compose_paste(fixture.application, fixture.history,
                                 *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(fixture.application.paste_session));
    expect_compose_test(static_cast<bool>(fixture.application.chart_clipboard.has_value()));
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
    expect_compose_test(static_cast<bool>(begin_compose_paste(fixture.application, fixture.history.document,
                               *fixture.interaction, status)));
    fixture.project();
    expect_compose_test(!static_cast<bool>(fixture.flattened.view.compose.paste_valid));
    expect_compose_test(static_cast<bool>(fixture.flattened.view.compose.paste_ghost_count == 1));
    expect_compose_test(!static_cast<bool>(commit_compose_paste(fixture.application, fixture.history,
                                     *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(status.empty()));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
    fixture.interaction->cursor.row = 240;
    expect_compose_test(static_cast<bool>(update_compose_paste(fixture.application, fixture.history.document,
                                fixture.interaction->chart, fixture.interaction->cursor)));
    expect_compose_test(static_cast<bool>(fixture.application.paste_session->proposal.valid));
    ++fixture.history.document.revision;
    expect_compose_test(!static_cast<bool>(commit_compose_paste(fixture.application, fixture.history,
                                     *fixture.interaction, status)));
    expect_compose_test(!static_cast<bool>(fixture.application.paste_session));
    expect_compose_test(static_cast<bool>(fixture.history.undo.empty()));
}

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
namespace {
auto paste_pointer_action(compose_test_fixture_t& fixture, bool commit, std::uint64_t row = 240) -> WormUiWorkspaceAction {
    const auto& chart = fixture.history.document.charts.front();
    const auto inspected = inspect_editor_interaction(fixture.history, *fixture.interaction);
    const auto* view = std::get_if<editor_interaction_view_model_t>(&inspected);
    expect_compose_test(view != nullptr);
    const auto y = editor_position_y_pixels(chart, *view, {chart.measures.front().id, row});
    expect_compose_test(y.has_value());
    // Use an interior point in the first snap cell, avoiding a chart-boundary
    // cancellation error when the downscroll screen transform is reversed.
    const bool downscroll = compose_field_downscroll(chart, fixture.application.editor_preferences);
    const auto screen_y = compose_source_y(*y + (row == 0 ? 0.5 : 0.0),
                                           view->viewport_height_pixels, downscroll);
    const auto raw = editor_position_at_y_pixels(chart, *view,
        compose_source_y(screen_y, view->viewport_height_pixels, downscroll));
    const auto snapped = raw ? resolve_resource_placement_position(chart, *raw,
        fixture.interaction->active_snap_denominator) : std::nullopt;
    expect_compose_test(snapped.has_value());
    expect_compose_test(snapped->row == row);
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = chart.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_UPDATE,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = screen_y};
    action.compose_input_count = 1;
    if (commit) {
        action.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_PASTE_COMMIT};
        action.compose_input_count = 2;
    }
    return action;
}
auto dispatch_paste_action(compose_test_fixture_t& fixture, WormUiWorkspaceAction& action) -> std::string {
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
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
    const bool downscroll = compose_field_downscroll(chart, fixture.application.editor_preferences);
    return compose_source_y(*y, view->viewport_height_pixels, downscroll);
}
auto kiai_input(compose_test_fixture_t& fixture, WormUiComposeInputKind kind, std::uint64_t head,
                std::uint8_t edge = WORM_UI_SPAN_HANDLE_BEGIN, double y = 0.0,
                double anchor_y = 0.0) -> WormUiWorkspaceAction {
    const auto& chart = fixture.history.document.charts.front();
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = chart.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_inputs[0] = {.kind = kind, .object_id = head, .timing_chart_id = chart.id.value,
                                .timing_document_revision = fixture.history.document.revision,
                                .y_pixels = y, .span_edge = edge,
                                .span_anchor_y_pixels = anchor_y};
    action.compose_input_count = 1;
    return action;
}
auto kiai_areas(compose_test_fixture_t& fixture) -> std::vector<kiai_span_t> {
    const auto& chart = fixture.history.document.charts.front();
    const auto built = build_measure_index(chart);
    return kiai_spans_of(chart, std::get<measure_index_t>(built));
}
}
TEST_CASE("Kiai edge drags edit the area, Delete removes the selected area and stale input is rejected",
          "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.requested_snap_denominators.emplace(
        fixture.history.document.charts.front().id.value, 8);
    fixture.project();
    REQUIRE(kiai_areas(fixture).size() == 1);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                            kiai_screen_y(fixture, 480));
    stale.compose_inputs[0].timing_document_revision += 1;
    CHECK(dispatch_paste_action(fixture, stale) == "Kiai edit is stale or invalid.");
    CHECK(fixture.history.undo.empty());

    fixture.application.timing_locked = true;
    auto locked = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_KIAI_SPAN_EDIT, 9001, 1,
                             kiai_screen_y(fixture, 480));
    CHECK(dispatch_paste_action(fixture, locked) == "Timing is locked.");
    fixture.application.timing_locked = false;

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
    CHECK(fixture.application.kiai_selected_head == 9001);
    auto remove = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_DELETE_SELECTION, 0);
    CHECK(dispatch_paste_action(fixture, remove) == "Kiai area deleted.");
    CHECK(kiai_areas(fixture).empty());
    CHECK(fixture.application.kiai_selected_head == 0);
    CHECK(fixture.history.undo.size() == 3);
}

TEST_CASE("A Kiai creation drag adds a merged area and is one undo step", "[sys][kiai-spans]") {
    compose_test_fixture_t fixture{kiai_test_song()};
    fixture.application.requested_snap_denominators.emplace(
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
    REQUIRE(fixture.flattened.view.compose.recipe_area_count == 1);

    auto select = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_SELECT, 7001);
    CHECK(dispatch_paste_action(fixture, select) == "SV recipe selected.");
    CHECK(fixture.application.recipe_selected == 7001);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].selected);

    auto stale = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                            kiai_screen_y(fixture, 480));
    stale.compose_inputs[0].timing_document_revision += 1;
    CHECK(dispatch_paste_action(fixture, stale) == "SV recipe edit is stale or invalid.");

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
    fixture.application.timing_locked = true;
    const auto revision = fixture.history.document.revision;
    auto locked_edit = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_RANGE_EDIT, 7001, 1,
                                  kiai_screen_y(fixture, 720));
    locked_edit.compose_inputs[0].span_anchor_y_pixels = kiai_screen_y(fixture, 480);
    CHECK(dispatch_paste_action(fixture, locked_edit) != "SV recipe moved.");
    auto locked_command = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, locked_command) == "Timing is locked.");
    CHECK(fixture.history.document.revision == revision);
    fixture.application.timing_locked = false;

    auto toggle = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 0);
    CHECK(dispatch_paste_action(fixture, toggle) == "SV recipe disabled.");
    CHECK_FALSE(fixture.history.document.charts.front().sv_recipes.front().enabled);

    fixture.project();
    auto materialize = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 1);
    CHECK(dispatch_paste_action(fixture, materialize) == "SV recipe materialized into events.");
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK_FALSE(fixture.history.document.charts.front().scrolls.empty());
    CHECK(fixture.application.recipe_selected == 0);
    auto missing = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_RECIPE_COMMAND, 7001, 2);
    CHECK(dispatch_paste_action(fixture, missing) == "The SV recipe no longer exists.");
}

TEST_CASE("Delete removes the selected SV recipe and the area menu adds one over the retained area",
          "[sys][recipe-areas]") {
    compose_test_fixture_t fixture{recipe_test_song()};
    fixture.project();
    fixture.application.recipe_selected = 7001;
    auto remove = kiai_input(fixture, WORM_UI_COMPOSE_INPUT_DELETE_SELECTION, 0);
    CHECK(dispatch_paste_action(fixture, remove) == "SV recipe deleted.");
    CHECK(fixture.history.document.charts.front().sv_recipes.empty());
    CHECK(fixture.application.recipe_selected == 0);
    REQUIRE(undo_document_command(fixture.history));
    CHECK(fixture.history.document.charts.front().sv_recipes.size() == 1);

    fixture.project();
    const auto measure = fixture.history.document.charts.front().measures.front().id;
    fixture.interaction->retained_selection_range =
        position_span_t{{measure, 0}, {measure, 240}};
    WormUiWorkspaceAction action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_AREA_ADD_SV_RECIPE;
    action.compose_input_chart_id = fixture.history.document.charts.front().id.value;
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
    REQUIRE(view.speed_span_count == 2);
    for (size_t index = 0; index < view.speed_span_count; ++index) {
        CHECK(view.speed_spans[index].bounds.bottom > view.speed_spans[index].bounds.top);
        CHECK(view.speed_spans[index].object_id != 0);
    }
    CHECK(view.speed_spans[0].multiplier == Catch::Approx(2.0));
    CHECK_FALSE(view.speed_spans[0].transition_is_seconds);
    CHECK(view.speed_spans[0].transition_length == Catch::Approx(2.0));
    CHECK(view.speed_spans[1].transition_is_seconds);
    bool named = false;
    for (size_t index = 0; index < view.timing_marker_count; ++index)
        if (view.timing_markers[index].lane == WORM_UI_TIMING_LANE_SPEED &&
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

    fixture.application.recipe_selected = 7001;
    fixture.project();
    const auto& form_view = fixture.flattened.view.compose.recipe_form;
    REQUIRE(form_view.visible);
    CHECK(form_view.recipe_id == 7001);
    CHECK(std::string(form_view.name) == "rise");
    CHECK(form_view.form.curve == WORM_UI_TIMING_RECIPE_CURVE_LINEAR);
    CHECK(std::string(form_view.form.value_to.numerator) == "3");
    CHECK(form_view.status_text == nullptr);

    // A preview replaces that recipe's graph until the selection or the document moves on.
    const auto& chart = fixture.history.document.charts.front();
    auto form = wormedit::sys::recipe_form_from_recipe(chart.sv_recipes.front());
    std::snprintf(form.value_to.numerator, sizeof(form.value_to.numerator), "9");
    fixture.application.recipe_preview = wormedit::sys::preview_recipe_form(
        chart, fixture.history.document.revision, chart.sv_recipes.front(), form);
    fixture.project();
    CHECK(fixture.flattened.view.compose.recipe_areas[0].previewing);
    CHECK(fixture.flattened.view.compose.recipe_areas[0].graph_count >= 2);

    form.curve = WORM_UI_TIMING_RECIPE_CURVE_EXPONENTIAL;
    std::snprintf(form.shape.numerator, sizeof(form.shape.numerator), "99");
    fixture.application.recipe_preview = wormedit::sys::preview_recipe_form(
        chart, fixture.history.document.revision, chart.sv_recipes.front(), form);
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.recipe_form.status_text != nullptr);
    CHECK_FALSE(std::string(fixture.flattened.view.compose.recipe_form.status_text).empty());

    fixture.application.recipe_selected = 0;
    fixture.project();
    CHECK_FALSE(fixture.application.recipe_preview.has_value());
    CHECK_FALSE(fixture.flattened.view.compose.recipe_form.visible);

    fixture.application.recipe_selected = 7001;
    fixture.application.recipe_preview = wormedit::sys::preview_recipe_form(
        chart, fixture.history.document.revision + 5, chart.sv_recipes.front(), form);
    fixture.project();
    CHECK_FALSE(fixture.application.recipe_preview.has_value());
}

TEST_CASE("Pointer paste retains cursor and viewport and FIFO release commits that anchor",
          "[sys][clipboard][paste-preview]") {
    compose_test_fixture_t fixture{paste_preview_song()};
    copy_preview_note(fixture);
    fixture.interaction->cursor.row = 0;
    std::string status;
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history.document,
                                   *fixture.interaction, status));
    const auto cursor = fixture.interaction->cursor;
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    (void)dispatch_paste_pointer(fixture, false);
    expect_compose_test(fixture.interaction->cursor == cursor);
    expect_compose_test(fixture.interaction->viewport_scroll_pixels == scroll);
    expect_compose_test(fixture.history.undo.empty());
    fixture.project();
    expect_compose_test(fixture.application.paste_session->anchor.row == 240);
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
    expect_compose_test(begin_compose_paste(fixture.application, fixture.history.document,
                                   *fixture.interaction, status));
    const auto collision = fixture.application.paste_session->proposal.error;
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

TEST_CASE("Offset padlock disables exact offset controls independently of BPM timing",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    const auto revision = fixture.history.document.revision;
    const auto bpm = fixture.history.document.charts.front().bpms.front().beats_per_minute;
    fixture.application.audio_offset_locked = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_locked);
    expect_compose_test(!fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(!fixture.flattened.view.compose.waveform_timing_locked);
    expect_compose_test(fixture.history.document.revision == revision);
    expect_compose_test(fixture.history.document.charts.front().bpms.front().beats_per_minute == bpm);
    expect_compose_test(fixture.history.undo.empty());
    fixture.application.audio_offset_locked = false;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.audio_offset_editable);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Locked offset fit rejects captured input without clearing a BPM preview",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    fixture.application.audio_offset_locked = true;
    fixture.cache.beat_fit_drag = beat_interval_drag_t{};
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_input_count = 1;
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_OFFSET_FIT_BEGIN, .object_id = 2};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history,
        *fixture.binding.workspace, *fixture.interaction, viewport, navigation,
        fixture.application, fixture.cache, 0);
    INFO(status);
    expect_compose_test(status.find("unlocked waveform") != std::string::npos);
    expect_compose_test(fixture.cache.beat_fit_drag.has_value());
    expect_compose_test(!fixture.cache.offset_fit_drag.has_value());
    expect_compose_test(action.audio_offset_drag_action == WORM_UI_AUDIO_OFFSET_DRAG_NONE);
    expect_compose_test(fixture.history.undo.empty());
}

TEST_CASE("Offset locking takes precedence over a captured release and never replays it",
          "[sys][offset-lock]") {
    compose_test_fixture_t fixture;
    fixture.project();
    auto begun = begin_audio_offset_drag(fixture.history.document, chart_id_t{1}, 48000);
    expect_compose_test(begun.drag.has_value());
    auto updated = update_audio_offset_drag(fixture.history.document, *begun.drag, 4800);
    expect_compose_test(updated.drag.has_value());
    auto drag = updated.drag;
    std::optional<workspace_tab_t> tab = workspace_tab_t::compose;
    fixture.cache.offset_fit_drag = compose_scene_cache_t::offset_fit_drag_t{};
    fixture.cache.beat_fit_drag = beat_interval_drag_t{};
    fixture.cache.proposed_offset_seconds = signed_rational_t{1, 10};
    WormUiWorkspaceAction action{};
    action.compose_action = WORM_UI_COMPOSE_ACTION_TOGGLE_AUDIO_OFFSET_LOCK;
    action.compose_input_chart_id = 1;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.audio_offset_drag_action = WORM_UI_AUDIO_OFFSET_DRAG_COMMIT;
    (void)toggle_compose_audio_offset_lock(action, fixture.history, *fixture.binding.workspace,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(fixture.application.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.offset_fit_drag);
    expect_compose_test(!fixture.cache.proposed_offset_seconds);
    expect_compose_test(fixture.cache.beat_fit_drag.has_value());
    expect_compose_test(fixture.history.undo.empty());
    (void)toggle_compose_audio_offset_lock(action, fixture.history, *fixture.binding.workspace,
        fixture.application, fixture.cache, drag, tab);
    expect_compose_test(!fixture.application.audio_offset_locked);
    expect_compose_test(!drag && !tab && !fixture.cache.offset_fit_drag);
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
    expect_compose_test(fixture.flattened.view.compose.bgm_event_count == 0);
    const auto* full_index = &*fixture.cache.index;
    fixture.project();
    expect_compose_test(&*fixture.cache.index == full_index);
    expect_compose_test(fixture.flattened.view.compose.has_bgm_events);
    expect_compose_test(fixture.flattened.view.compose.bgm_event_count == 0);

    retained_chart.measures.back().bgm_events.clear();
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(!fixture.flattened.view.compose.has_bgm_events);
}

namespace {
void project_anchor_scene(compose_test_fixture_t& fixture, int height) {
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
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
        fixture.application.editor_preferences.scroll_directions.fill(direction);
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
    flattened_workspace_t flattened;
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
        auto timing = build_timing_index(song.charts.front());
        expect_compose_test(timing.index.has_value());
        cache.marker_timing_index = std::move(timing.index);
    }
    auto project(flattened_workspace_t& output,
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
    ++fixture.cache.revision;
    fixture.song.charts.front().beat_zero_audio_offset_seconds = {1, 10};
    fixture.update_timing();
    expect_compose_test(fixture.project() == generation);
    expect_compose_test(fixture.cache.waveform_overview_revision == fixture.cache.revision);
    flattened_workspace_t second;
    second.view.compose = fixture.flattened.view.compose;
    expect_compose_test(fixture.project(second) == generation);
    expect_compose_test(second.view.compose.waveform_overview_rows !=
        fixture.flattened.view.compose.waveform_overview_rows);
}
TEST_CASE("Compose overview source revision changes generation even when waveform address is reused",
          "[sys][waveform-projection][overview-generation]") {
    overview_generation_fixture_t fixture;
    const auto before = fixture.project();
    const auto* identity = fixture.cache.waveform_overview_identity;
    fixture.analysis.waveform->key.source.source_identity = "content:v2";
    for (auto& bin : fixture.analysis.waveform->bins) bin.absolute_peak = {.75f, .75f};
    const auto replacement = fixture.project();
    expect_compose_test(fixture.cache.waveform_overview_identity == identity);
    expect_compose_test(replacement > before);
    expect_compose_test(fixture.flattened.compose.waveform.overview_row.front().absolute_peak == .75f);
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
void check_static_source_has_no_frame_overlays(const WormUiComposeView& source) {
    expect_compose_test(!source.status_text && !source.selected_note_label && !source.hover_tooltip);
    expect_compose_test(!source.paste_ghosts && source.paste_ghost_count == 0 && !source.paste_diagnostic);
    expect_compose_test(!source.resources && source.resource_count == 0);
    expect_compose_test(!source.output_groups && source.output_group_count == 0);
    expect_compose_test(!source.branch_rows && source.branch_row_count == 0);
    expect_compose_test(!source.branch_choices && source.branch_choice_count == 0);
    expect_compose_test(source.branch_diagnostic_count == 0);
    for (const auto* diagnostic : source.branch_diagnostics) expect_compose_test(!diagnostic);
}
void check_static_source_has_no_frame_markers(const WormUiComposeView& source) {
    expect_compose_test(!source.beat_bookmarks && source.beat_bookmark_count == 0);
    expect_compose_test(!source.beat_sections && source.beat_section_count == 0);
    expect_compose_test(!source.audio_bookmarks && source.audio_bookmark_count == 0);
    expect_compose_test(!source.audio_sections && source.audio_section_count == 0);
    expect_compose_test(!source.semantic_layers && source.semantic_layer_count == 0);
    expect_compose_test(!source.staged_bmson_marker_playable_lanes && !source.staged_bmson_marker_restarts &&
        source.staged_bmson_marker_count == 0);
    expect_compose_test(!source.lane_key_bindings && source.lane_key_binding_count == 0);
}
void check_static_source_has_no_frame_previews(const WormUiComposeView& source) {
    expect_compose_test(!source.proposed_grid_lines && source.proposed_grid_line_count == 0);
    expect_compose_test(!source.timing_preview_rects && source.timing_preview_rect_count == 0);
    expect_compose_test(!source.timing_preview_markers && source.timing_preview_marker_count == 0);
    expect_compose_test(!source.waveform_rows && source.waveform_row_count == 0);
    expect_compose_test(!source.channel_waveform_rows && source.channel_waveform_row_count == 0);
    expect_compose_test(!source.waveform_overview_rows && source.waveform_overview_row_count == 0);
    expect_compose_test(!source.bmson_marker_drafts && source.bmson_marker_draft_count == 0);
    expect_compose_test(!source.bmson_marker_move_preview && !source.cursor && !source.static_scene);
}
void project_static_source_arena_frame(compose_test_fixture_t& fixture, wormedit::frame_arena_t& arena) {
    workspace_view_t model;
    model.active_tab = workspace_tab_t::compose;
    model.tab_view = workspace_compose_view_t{.chart = chart_id_t{1}};
    fixture.flattened = flatten_workspace(model, fixture.history.document, "ready", {}, false, {}, &arena);
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    expect_compose_test(fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.view.compose.resource_count == 1);
    expect_compose_test(fixture.flattened.view.compose.output_group_count == 1);
    expect_compose_test(fixture.flattened.view.compose.static_scene != nullptr);
}
void check_static_source_owned_geometry(const flattened_workspace_t& storage) {
    const auto& source = storage.view.compose;
    expect_compose_test(source.bands == storage.compose.band.data());
    expect_compose_test(source.lanes == storage.compose.lane.data());
    expect_compose_test(source.grid_lines == storage.compose.grid.line.data());
    expect_compose_test(source.differences == storage.compose.difference.data());
    expect_compose_test(source.note_endpoints == storage.compose.note.data());
    expect_compose_test(source.hold_bodies == storage.compose.hold.data());
    expect_compose_test(source.bgm_lanes == storage.compose.bgm.lane.data());
    expect_compose_test(source.bgm_events == storage.compose.bgm.event.data());
    expect_compose_test(source.timing_markers == storage.compose.timing_marker.data());
    check_static_source_has_no_frame_overlays(source);
    check_static_source_has_no_frame_markers(source);
    check_static_source_has_no_frame_previews(source);
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
    compose_test_fixture_t fixture(std::move(song));
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_static_source_arena_frame(fixture, arena);
    const auto stored = fixture.cache.static_scene_storage;
    const auto generation = fixture.cache.static_scene_source_generation;
    check_static_source_owned_geometry(*stored);
    expect_compose_test(!stored->compose.note.empty());
    const auto before = stored->compose.note[0];
    const std::string label = before.sound_label;
    const std::string detail = before.sound_detail;
    expect_compose_test(!stored->compose.timing_marker.empty());
    const auto timing = stored->compose.timing_marker.front();
    const std::string timing_label = timing.short_label;
    const std::string timing_detail = timing.detail;
    fixture.flattened = {};
    arena.reset();
    const auto overwritten = arena.allocate_bytes(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwritten));
    std::memset(overwritten.data, 0xA5, 4096);
    expect_compose_test(stored->compose.note[0].note_id == before.note_id);
    expect_compose_test(stored->compose.note[0].bounds.top == before.bounds.top);
    expect_compose_test(std::strcmp(stored->compose.note[0].sound_label, label.c_str()) == 0);
    expect_compose_test(std::strcmp(stored->compose.note[0].sound_detail, detail.c_str()) == 0);
    expect_compose_test(std::strcmp(stored->compose.timing_marker.front().short_label, timing_label.c_str()) == 0);
    expect_compose_test(std::strcmp(stored->compose.timing_marker.front().detail, timing_detail.c_str()) == 0);
    fixture.interaction->viewport_scroll_pixels = 0.5;
    project_static_source_arena_frame(fixture, arena);
    expect_compose_test(fixture.cache.static_scene_storage == stored);
    expect_compose_test(fixture.cache.static_scene_source_generation == generation);
    check_static_source_owned_geometry(*fixture.cache.static_scene_storage);
    ++fixture.history.document.revision;
    project_static_source_arena_frame(fixture, arena);
    expect_compose_test(fixture.cache.static_scene_storage != stored);
    expect_compose_test(fixture.cache.static_scene_source_generation > generation);
    check_static_source_owned_geometry(*fixture.cache.static_scene_storage);
}

namespace {
auto geometry_arena_song() -> song_document_t {
    auto song = compose_test_song();
    auto& hold = song.charts.front().measures.front().notes[1];
    hold.kind = note_kind_t::hold;
    hold.end = local_position_t{measure_id_t{1}, 360};
    return song;
}
void project_geometry_arena(compose_test_fixture_t& fixture, wormedit::frame_arena_t& arena,
                            int height = 980) {
    fixture.flattened = flattened_workspace_t{&arena};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
        fixture.cache, 1280, height, fixture.interaction, fixture.application, nullptr);
}
void check_geometry_view_spans(const flattened_workspace_t& frame) {
    const auto& view = frame.view.compose;
    expect_compose_test(view.bands == frame.compose.band.data() && view.band_count == frame.compose.band.size());
    expect_compose_test(view.lanes == frame.compose.lane.data() && view.lane_count == frame.compose.lane.size());
    expect_compose_test(view.differences == frame.compose.difference.data() && view.difference_count == frame.compose.difference.size());
    expect_compose_test(view.hold_bodies == frame.compose.hold.data() && view.hold_body_count == frame.compose.hold.size());
    expect_compose_test(view.note_endpoints == frame.compose.note.data() && view.note_endpoint_count == frame.compose.note.size());
    expect_compose_test(view.band_count <= WORM_UI_COMPOSE_BAND_CAPACITY);
    expect_compose_test(view.lane_count <= WORM_UI_COMPOSE_LANE_CAPACITY);
    expect_compose_test(view.difference_count <= WORM_UI_COMPOSE_DIFFERENCE_CAPACITY);
    expect_compose_test(view.hold_body_count <= WORM_UI_COMPOSE_HOLD_CAPACITY);
    expect_compose_test(view.note_endpoint_count <= WORM_UI_COMPOSE_NOTE_CAPACITY);
}
template<class Rows>
void check_geometry_between(const Rows& rows, std::uintptr_t begin, std::uintptr_t end) {
    if (rows.empty()) return;
    const auto address = reinterpret_cast<std::uintptr_t>(rows.data());
    expect_compose_test(address >= begin && address + rows.size() * sizeof(rows[0]) <= end);
}
void compare_geometry_rect(const WormUiComposeRect& a, const WormUiComposeRect& b) {
    expect_compose_test(a.left == Catch::Approx(b.left));
    expect_compose_test(a.top == Catch::Approx(b.top));
    expect_compose_test(a.right == Catch::Approx(b.right));
    expect_compose_test(a.bottom == Catch::Approx(b.bottom));
}
void compare_geometry_note(const WormUiComposeNoteView& a, const WormUiComposeNoteView& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id == b.note_id && a.lane_id == b.lane_id && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.glyph == b.glyph && a.kind == b.kind && a.snap_class == b.snap_class && a.tail == b.tail);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
    expect_compose_test(a.paint_preview == b.paint_preview && a.paint_erase_preview == b.paint_erase_preview && a.paint_invalid_preview == b.paint_invalid_preview);
    expect_compose_test(std::strcmp(a.sound_label, b.sound_label) == 0);
    expect_compose_test(std::strcmp(a.sound_detail, b.sound_detail) == 0);
}
void compare_geometry_hold(const WormUiComposeHoldView& a, const WormUiComposeHoldView& b) {
    compare_geometry_rect(a.bounds, b.bounds);
    expect_compose_test(a.note_id == b.note_id && a.lane_id == b.lane_id && a.display_lane_number == b.display_lane_number);
    expect_compose_test(a.selected == b.selected && a.proposal_origin == b.proposal_origin && a.proposal_ghost == b.proposal_ghost);
    expect_compose_test(a.semantic_color_visible == b.semantic_color_visible && a.semantic_red == b.semantic_red &&
        a.semantic_green == b.semantic_green && a.semantic_blue == b.semantic_blue && a.semantic_alpha == b.semantic_alpha);
}
void compare_geometry_frames(const WormUiComposeView& a, const WormUiComposeView& b) {
    expect_compose_test(a.band_count == b.band_count && a.lane_count == b.lane_count && a.difference_count == b.difference_count);
    expect_compose_test(a.note_endpoint_count == b.note_endpoint_count && a.hold_body_count == b.hold_body_count);
    for (std::size_t i = 0; i < a.band_count; ++i) {
        compare_geometry_rect(a.bands[i].bounds, b.bands[i].bounds);
        expect_compose_test(a.bands[i].split_stage == b.bands[i].split_stage);
    }
    for (std::size_t i = 0; i < a.lane_count; ++i) {
        compare_geometry_rect(a.lanes[i].bounds, b.lanes[i].bounds);
        expect_compose_test(a.lanes[i].lane_id == b.lanes[i].lane_id && a.lanes[i].display_order == b.lanes[i].display_order);
        expect_compose_test(a.lanes[i].stage == b.lanes[i].stage && a.lanes[i].role == b.lanes[i].role && a.lanes[i].glyph == b.lanes[i].glyph);
    }
    for (std::size_t i = 0; i < a.note_endpoint_count; ++i) compare_geometry_note(a.note_endpoints[i], b.note_endpoints[i]);
    for (std::size_t i = 0; i < a.hold_body_count; ++i) compare_geometry_hold(a.hold_bodies[i], b.hold_bodies[i]);
}
void check_fresh_reused_geometry(std::uint32_t direction, format_family_t family) {
    auto song = geometry_arena_song();
    song.charts.front().source_family = family;
    compose_test_fixture_t reused(song), fresh(song);
    reused.application.editor_preferences.scroll_directions.fill(direction);
    fresh.application.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t reused_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    wormedit::frame_arena_t fresh_arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(reused, reused_arena);
    project_geometry_arena(fresh, fresh_arena);
    const auto source = reused.cache.static_scene_storage;
    expect_compose_test(source != nullptr);
    for (const bool legend : {false, true}) {
        reused.application.editor_preferences.lane_key_legend_visible = legend;
        fresh.application.editor_preferences.lane_key_legend_visible = legend;
        reused.interaction->viewport_scroll_pixels = 0.5;
        fresh.interaction->viewport_scroll_pixels = 0.5;
        reused_arena.reset();
        fresh_arena.reset();
        fresh.cache = {};
        project_geometry_arena(reused, reused_arena);
        project_geometry_arena(fresh, fresh_arena);
        expect_compose_test(reused.flattened.view.compose.scene_available && fresh.flattened.view.compose.scene_available);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
        reused_arena.reset();
        project_geometry_arena(reused, reused_arena);
        expect_compose_test(reused.cache.static_projection_reused_frames > 0);
        compare_geometry_frames(reused.flattened.view.compose, fresh.flattened.view.compose);
    }
}
}

TEST_CASE("Compose geometry spans live in the supplied arena and account independent static chunks",
          "[sys][frame-arena][compose-geometry]") {
    flattened_workspace_t untouched;
    expect_compose_test(untouched.owned_arena_committed_bytes() == 0);
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto begin = arena.allocate_bytes(1, alignof(char));
    project_geometry_arena(fixture, arena);
    const auto end = arena.allocate_bytes(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(fixture.flattened.view.compose.scene_available && !fixture.flattened.view.compose.truncated);
    check_geometry_view_spans(fixture.flattened);
    const auto first = reinterpret_cast<std::uintptr_t>(begin.data);
    const auto last = reinterpret_cast<std::uintptr_t>(end.data);
    check_geometry_between(fixture.flattened.compose.band, first, last);
    check_geometry_between(fixture.flattened.compose.lane, first, last);
    check_geometry_between(fixture.flattened.compose.hold, first, last);
    check_geometry_between(fixture.flattened.compose.note, first, last);
    expect_compose_test(!fixture.flattened.compose.hold.empty() && !fixture.flattened.compose.note.empty());
    expect_compose_test(fixture.flattened.owned_arena_committed_bytes() == 0);
    const auto source = fixture.cache.static_scene_storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->owned_arena_committed_bytes() > 0);
    expect_compose_test(source->compose.note.data() != fixture.flattened.compose.note.data());
    expect_compose_test(fixture.cache.static_scene_accounted_bytes >= sizeof(*source) + source->owned_arena_committed_bytes());
    expect_compose_test(fixture.cache.static_scene_accounted_bytes <= 16 * 1024 * 1024);
}

TEST_CASE("Compose geometry arena fresh and reused primitives agree in both directions and label modes",
          "[sys][frame-arena][compose-geometry]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_fresh_reused_geometry(direction, family);
}

TEST_CASE("Compose geometry exhaustion retains bounded preceding arrays without a persistent source",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    const auto revision = fixture.history.document.revision;
    wormedit::frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    project_geometry_arena(fixture, tiny);
    expect_compose_test(fixture.flattened.view.compose.truncated);
    expect_compose_test(fixture.flattened.view.compose.band_count == 0 && fixture.flattened.view.compose.lane_count == 0);
    expect_compose_test(fixture.flattened.view.compose.note_endpoint_count == 0 && fixture.flattened.view.compose.hold_body_count == 0);
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    check_geometry_view_spans(fixture.flattened);
    wormedit::frame_arena_t limited({.chunk_bytes = 128, .maximum_bytes = 128});
    fixture.cache = {};
    project_geometry_arena(fixture, limited);
    expect_compose_test(fixture.flattened.view.compose.truncated);
    expect_compose_test(fixture.flattened.view.compose.band_count > 0 && fixture.flattened.view.compose.lane_count > 0);
    expect_compose_test(fixture.flattened.view.compose.hold_body_count == 0 && fixture.flattened.view.compose.note_endpoint_count == 0);
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
    check_geometry_view_spans(fixture.flattened);
}

TEST_CASE("Warmed Compose geometry arena stays bounded through 1000 actual cached projections",
          "[sys][frame-arena][compose-geometry]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture, arena);
    arena.reset();
    project_geometry_arena(fixture, arena);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    const auto source = fixture.cache.static_scene_storage;
    const auto static_committed = source->owned_arena_committed_bytes();
    arena.reset();
    allocations = 0;
    count_allocations = true;
    project_geometry_arena(fixture, arena);
    count_allocations = false;
    const auto remaining_allocations = allocations;
    bool stable = true;
    bool consistent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        allocations = 0;
        count_allocations = true;
        project_geometry_arena(fixture, arena);
        count_allocations = false;
        consistent = consistent && allocations == remaining_allocations;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            fixture.cache.static_scene_storage == source && source->owned_arena_committed_bytes() == static_committed &&
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
    project_geometry_arena(fixture, arena, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(!fixture.flattened.projection_feedback.empty());
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
    check_geometry_view_spans(fixture.flattened);
}

TEST_CASE("Compose projection failure posts once until repaired or revision changes", "[sys][notification][compose-geometry]") {
    notification_center_t center;
    notification_sink_t sink{center};
    compose_test_fixture_t fixture(dense_song());
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
    projection_notification_feedback_t feedback;
    auto identity = projection_identity_t{fixture.history.document.id, fixture.history.document.revision, chart_id_t{1}, 1};
    project_geometry_arena(fixture, arena, 2000000);
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
void check_grid_pairs(const flattened_workspace_t& frame) {
    const auto& grid = frame.compose.grid;
    const auto& view = frame.view.compose;
    expect_compose_test(grid.line.size() == grid.row.size());
    expect_compose_test(view.grid_lines == grid.line.data());
    expect_compose_test(view.grid_line_count == grid.line.size());
    expect_compose_test(grid.line.size() <= WORM_UI_COMPOSE_GRID_CAPACITY);
    for (std::size_t index = 0; index < grid.line.size(); ++index) {
        const auto& row = grid.row[index];
        expect_compose_test(grid.line[index].chart_row ==
            (row.fractional_rows.numerator == 0 ? row.whole_rows : 0));
    }
}
void compare_grid_frames(const flattened_workspace_t& actual, const flattened_workspace_t& expected) {
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
    cached.application.editor_preferences.scroll_directions.fill(direction);
    fresh.application.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t arena, reference;
    project_geometry_arena(cached, arena);
    project_geometry_arena(fresh, reference);
    const auto source = cached.cache.static_scene_storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(cached.flattened.view.compose.bms_measure_labels == (family == format_family_t::bms));
    if (family == format_family_t::bms)
        expect_compose_test(std::any_of(cached.flattened.compose.grid.line.begin(),
            cached.flattened.compose.grid.line.end(), [](const auto& line) { return line.measure_number > 1; }));
    for (const auto scroll : {0.5, 20.0, 0.5}) {
        arena.reset(); reference.reset();
        cached.interaction->viewport_scroll_pixels = scroll;
        fresh.interaction->viewport_scroll_pixels = scroll;
        fresh.cache = {};
        project_geometry_arena(cached, arena);
        project_geometry_arena(fresh, reference);
        compare_grid_frames(cached.flattened, fresh.flattened);
        expect_compose_test(cached.cache.static_scene_storage == source);
    }
    expect_compose_test(cached.cache.static_projection_reused_frames > 0);
    ++cached.history.document.revision;
    ++fresh.history.document.revision;
    arena.reset(); reference.reset(); fresh.cache = {};
    project_geometry_arena(cached, arena);
    project_geometry_arena(fresh, reference);
    expect_compose_test(cached.cache.static_scene_storage != source);
    compare_grid_frames(cached.flattened, fresh.flattened);
}
}

TEST_CASE("Compose paired grid spans belong to frame and independent static arenas", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena;
    const auto begin = arena.allocate_bytes(1, alignof(char));
    project_geometry_arena(fixture, arena);
    const auto end = arena.allocate_bytes(1, alignof(char));
    expect_compose_test(static_cast<bool>(begin) && static_cast<bool>(end));
    expect_compose_test(!fixture.flattened.compose.grid.line.empty());
    check_grid_pairs(fixture.flattened);
    check_geometry_between(fixture.flattened.compose.grid.line,
        reinterpret_cast<std::uintptr_t>(begin.data), reinterpret_cast<std::uintptr_t>(end.data));
    check_geometry_between(fixture.flattened.compose.grid.row,
        reinterpret_cast<std::uintptr_t>(begin.data), reinterpret_cast<std::uintptr_t>(end.data));
    const auto source = fixture.cache.static_scene_storage;
    expect_compose_test(source != nullptr);
    expect_compose_test(source->compose.grid.line.data() != fixture.flattened.compose.grid.line.data());
    const auto line = source->compose.grid.line[0];
    const auto row = source->compose.grid.row[0];
    auto moved = std::move(fixture.flattened);
    check_grid_pairs(moved);
    fixture.flattened = {};
    arena.reset();
    const auto overwrite = arena.allocate_bytes(4096, alignof(char));
    expect_compose_test(static_cast<bool>(overwrite));
    std::memset(overwrite.data, 0xA5, 4096);
    expect_compose_test(source->compose.grid.line[0].y_pixels == line.y_pixels);
    expect_compose_test(source->compose.grid.row[0] == row);
    expect_compose_test(fixture.cache.static_scene_accounted_bytes >=
        sizeof(*source) + source->owned_arena_committed_bytes());
}

TEST_CASE("Compose paired grid cached filters preserve fresh BMS and native labels", "[sys][frame-arena][compose-grid]") {
    for (const auto direction : {1U, 2U})
        for (const auto family : {format_family_t::native_project, format_family_t::bms})
            check_grid_cache_parity(direction, family);
}

TEST_CASE("Compose grid arena exhaustion publishes only complete paired units", "[sys][frame-arena][compose-grid]") {
    bool preceding = false;
    bool completed = false;
    for (std::size_t limit = 128; limit <= 2048; limit += 32) {
        compose_test_fixture_t fixture(geometry_arena_song());
        wormedit::frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        project_geometry_arena(fixture, arena);
        check_grid_pairs(fixture.flattened);
        expect_compose_test(fixture.history.document.revision == 1 && fixture.history.undo.empty());
        if (fixture.flattened.compose.grid.line.empty()) {
            expect_compose_test(fixture.flattened.view.compose.truncated);
            expect_compose_test(!fixture.flattened.view.compose.static_scene);
            preceding = preceding || !fixture.flattened.compose.lane.empty();
        } else completed = true;
    }
    expect_compose_test(preceding && completed);
}

TEST_CASE("Warmed paired grid rows remain stable through 1000 actual arena resets", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(geometry_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture, arena);
    arena.reset(); project_geometry_arena(fixture, arena);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    const auto source = fixture.cache.static_scene_storage;
    const auto count = fixture.flattened.compose.grid.line.size();
    arena.reset(); allocations = 0; count_allocations = true;
    project_geometry_arena(fixture, arena);
    count_allocations = false;
    const auto remaining = allocations;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset(); allocations = 0; count_allocations = true;
        project_geometry_arena(fixture, arena);
        count_allocations = false;
        stable = stable && allocations == remaining && arena.used_bytes() == used &&
            arena.committed_bytes() == committed && fixture.cache.static_scene_storage == source &&
            fixture.flattened.compose.grid.line.size() == count &&
            fixture.flattened.compose.grid.row.size() == count && !fixture.flattened.view.compose.truncated;
    }
    INFO("remaining projection allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    expect_compose_test(stable);
    check_grid_pairs(fixture.flattened);
}

TEST_CASE("Compose paired grid limits remain bounded when scene budget rejects excess", "[sys][frame-arena][compose-grid]") {
    compose_test_fixture_t fixture(dense_song());
    wormedit::frame_arena_t arena;
    project_geometry_arena(fixture, arena, 2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(fixture.flattened.compose.grid.line.empty() && fixture.flattened.compose.grid.row.empty());
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
void compare_bgm_arena_views(const WormUiComposeView& a, const WormUiComposeView& b) {
    expect_compose_test(a.bgm_lane_count == b.bgm_lane_count && a.bgm_event_count == b.bgm_event_count);
    for (std::size_t i = 0; i < a.bgm_lane_count; ++i) {
        compare_geometry_rect(a.bgm_lanes[i].bounds, b.bgm_lanes[i].bounds);
        expect_compose_test(a.bgm_lanes[i].source_lane_id == b.bgm_lanes[i].source_lane_id);
        expect_compose_test(a.bgm_lanes[i].source_order == b.bgm_lanes[i].source_order);
    }
    for (std::size_t i = 0; i < a.bgm_event_count; ++i) {
        const auto& x = a.bgm_events[i]; const auto& y = b.bgm_events[i];
        compare_geometry_rect(x.bounds, y.bounds);
        expect_compose_test(std::tie(x.event_id,x.source_lane_id,x.sound_id,x.has_sound,x.bmson_slice,
            x.bmson_restart,x.selected) == std::tie(y.event_id,y.source_lane_id,y.sound_id,y.has_sound,
            y.bmson_slice,y.bmson_restart,y.selected));
        expect_compose_test(std::strcmp(x.sound_label,y.sound_label)==0);
        expect_compose_test(std::strcmp(x.sound_detail,y.sound_detail)==0);
    }
}
}
namespace {
void check_bgm_arena_family(format_family_t family) {
        const auto song = bgm_arena_song(family);
        compose_test_fixture_t reused(song), fresh(song);
        wormedit::frame_arena_t a({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
        wormedit::frame_arena_t b({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
        project_geometry_arena(reused,a); project_geometry_arena(fresh,b);
        REQUIRE(reused.flattened.view.compose.bgm_lane_count > 0);
        REQUIRE(reused.flattened.view.compose.bgm_event_count > 0);
        compare_bgm_arena_views(reused.flattened.view.compose,fresh.flattened.view.compose);
        const auto& initial = reused.flattened.view.compose.bgm_events[0];
        expect_compose_test(initial.bmson_slice == (family == format_family_t::bmson));
        if (family == format_family_t::bmson) expect_compose_test(initial.bmson_restart);
        expect_compose_test(initial.bounds.bottom > initial.bounds.top);
        const auto storage = reused.cache.static_scene_storage;
        REQUIRE(storage);
        for (const double scroll : {0.5,24.0,0.0}) {
            a.reset(); b.reset(); fresh.cache={};
            reused.interaction->selected_objects={{editor_selection_object_kind_t::bgm_event,object_id_t{70001}}};
            fresh.interaction->selected_objects=reused.interaction->selected_objects;
            reused.interaction->viewport_scroll_pixels=scroll;
            fresh.interaction->viewport_scroll_pixels=scroll;
            project_geometry_arena(reused,a); project_geometry_arena(fresh,b);
            compare_bgm_arena_views(reused.flattened.view.compose,fresh.flattened.view.compose);
            if (reused.flattened.view.compose.bgm_event_count)
                expect_compose_test(reused.flattened.view.compose.bgm_events[0].selected);
            expect_compose_test(reused.flattened.view.compose.bgm_lanes == reused.flattened.compose.bgm.lane.data());
            expect_compose_test(reused.flattened.view.compose.bgm_events == reused.flattened.compose.bgm.event.data());
        }
        expect_compose_test(storage->view.compose.bgm_event_count > 0);
        expect_compose_test(std::strcmp(storage->view.compose.bgm_events[0].sound_label,"01")==0);
        expect_compose_test(std::strcmp(storage->view.compose.bgm_events[0].sound_detail,
            "Independent BGM resource label")==0);
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
    project_geometry_arena(fixture,arena); arena.reset(); project_geometry_arena(fixture,arena);
    const auto used=arena.used_bytes(), committed=arena.committed_bytes();
    bool stable=true;
    for (int i=0;i<1000;++i) {
        arena.reset(); project_geometry_arena(fixture,arena);
        stable=stable && arena.used_bytes()==used && arena.committed_bytes()==committed &&
            fixture.flattened.view.compose.bgm_lane_count>0 && fixture.flattened.view.compose.bgm_event_count>0;
    }
    expect_compose_test(stable);
}
TEST_CASE("Compose BGM arena exhaustion publishes bounded initialized rows",
          "[sys][compose-bgm-arena]") {
    compose_test_fixture_t fixture(bgm_arena_song(format_family_t::bms));
    wormedit::frame_arena_t arena({.chunk_bytes=64,.maximum_bytes=64});
    project_geometry_arena(fixture,arena);
    const auto& view=fixture.flattened.view.compose;
    expect_compose_test(view.truncated);
    expect_compose_test(view.bgm_lane_count==fixture.flattened.compose.bgm.lane.size());
    expect_compose_test(view.bgm_event_count==fixture.flattened.compose.bgm.event.size());
    expect_compose_test(view.bgm_lane_count<=WORM_UI_COMPOSE_BGM_LANE_CAPACITY);
    expect_compose_test(view.bgm_event_count<=WORM_UI_COMPOSE_BGM_EVENT_CAPACITY);
}
TEST_CASE("Compose BGM arena respects primitive budget on oversized source projection",
          "[sys][compose-bgm-arena]") {
    compose_test_fixture_t fixture(bgm_arena_song(format_family_t::bms));
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=16*1024*1024});
    project_geometry_arena(fixture,arena,2000000);
    expect_compose_test(!fixture.flattened.view.compose.scene_available);
    expect_compose_test(!fixture.flattened.view.compose.static_scene);
    expect_compose_test(fixture.flattened.view.compose.bgm_lane_count<=WORM_UI_COMPOSE_BGM_LANE_CAPACITY);
    expect_compose_test(fixture.flattened.view.compose.bgm_event_count<=WORM_UI_COMPOSE_BGM_EVENT_CAPACITY);
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
void check_semantic_first(const WormUiComposeView& view) {
    expect_compose_test(view.semantic_layer_count>0);
    const auto& row=view.semantic_layers[0];
    expect_compose_test(row.layer_id==101 && std::strlen(row.name)==81);
    expect_compose_test(row.red==99 && row.green==120 && row.blue==140 && row.alpha==255);
    expect_compose_test(row.assigned_note_count==1 && row.visible && row.locked);
}
}
TEST_CASE("Compose semantic arena preserves palette flags and independent text lifetime",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t fixture(semantic_arena_song());
    fixture.application.semantic_layer_filter_chart=chart_id_t{1};
    fixture.application.semantic_layer_target=layer_id_t{101};
    fixture.application.semantic_layer_solo_active=true;
    fixture.application.required_semantic_layer_filters={layer_id_t{101}};
    fixture.application.excluded_semantic_layer_filters={layer_id_t{102}};
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    project_geometry_arena(fixture,arena);
    const auto& view=fixture.flattened.view.compose;
    REQUIRE(view.scene_available);
    check_semantic_first(view);
    expect_compose_test(view.semantic_layer_count==2 && view.semantic_layer_total_count==2);
    expect_compose_test(view.semantic_layers[0].selected && !view.semantic_layers[1].selected);
    expect_compose_test(view.semantic_layer_target_locked && view.semantic_layer_target_visible);
    expect_compose_test(view.semantic_layer_target_required_filter && !view.semantic_layer_target_excluded_filter);
    expect_compose_test(view.semantic_layer_solo_active);
    arena.reset();
    fixture.application.semantic_layer_target=layer_id_t{102};
    fixture.application.semantic_layer_paint_mode_active=true;
    project_geometry_arena(fixture,arena);
    expect_compose_test(fixture.flattened.view.compose.semantic_layer_target_excluded_filter);
    expect_compose_test(!fixture.flattened.view.compose.semantic_layer_target_locked);
    expect_compose_test(fixture.flattened.view.compose.semantic_layer_paint_mode_active);
    fixture.application.semantic_layer_target=layer_id_t{101};
    arena.reset();
    project_geometry_arena(fixture,arena);
    fixture.history.document.charts.front().semantic_layers[0].name="changed source";
    auto moved=std::move(fixture.flattened);
    moved.rebind_view_pointers();
    check_semantic_first(moved.view.compose);
    arena.reset();
    project_geometry_arena(fixture,arena);
    expect_compose_test(std::strcmp(fixture.flattened.view.compose.semantic_layers[0].name,"changed source")==0);
}
TEST_CASE("Compose semantic arena preserves bounded scrolling and empty state",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t empty(semantic_arena_song(0)); empty.project();
    expect_compose_test(empty.flattened.view.compose.semantic_layer_count==0);
    expect_compose_test(empty.flattened.view.compose.semantic_layer_total_count==0);
    expect_compose_test(!empty.flattened.view.compose.semantic_layers_truncated);
    compose_test_fixture_t dense(semantic_arena_song(300));
    dense.application.semantic_layer_row_count=1000;
    dense.application.semantic_layer_target=layer_id_t{101};
    dense.project();
    const auto& view=dense.flattened.view.compose;
    expect_compose_test(view.semantic_layer_count==WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY);
    expect_compose_test(view.semantic_layer_total_count==300 && view.semantic_layer_first_row==0);
    expect_compose_test(!view.semantic_layers_truncated);
    dense.application.semantic_layer_first_row=280;
    dense.application.semantic_layer_row_count=18;
    dense.history.document.charts.front().semantic_layers[285].visible=false;
    dense.project();
    expect_compose_test(view.semantic_layer_first_row==280 && view.semantic_layer_count==18);
    expect_compose_test(view.semantic_layers[0].layer_id==381 && view.semantic_layers[5].layer_id==386);
    expect_compose_test(!view.semantic_layers[5].visible);
    expect_compose_test(view.semantic_layer_target.layer_id==101 && view.semantic_layer_target.locked);
}
TEST_CASE("Compose note menu projects a bounded independent catalog and retires its capture",
          "[sys][compose-semantic-arena][note-menu]") {
    compose_test_fixture_t fixture(semantic_arena_song(30)); fixture.project();
    auto& app=fixture.application;
    const auto& view=fixture.flattened.view.compose;
    expect_compose_test(view.note_menu_layer_count==0 && !view.note_menu_layers);
    app.semantic_layer_first_row=20;
    app.note_layer_menu_chart=chart_id_t{1};
    app.note_layer_menu_revision=fixture.history.document.revision;
    app.note_layer_menu_row_count=1000;
    fixture.project();
    expect_compose_test(view.note_menu_layer_count==WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY);
    expect_compose_test(view.note_menu_layer_total_count==30 && view.note_menu_layer_first_row==0);
    expect_compose_test(view.note_menu_layers[0].layer_id==101 && view.note_menu_layers[0].locked);
    expect_compose_test(view.semantic_layer_first_row==20 && view.semantic_layers[0].layer_id==121);
    app.note_layer_menu_first_row=16;
    fixture.project();
    expect_compose_test(view.note_menu_layer_first_row==16 && view.note_menu_layer_count==14);
    expect_compose_test(view.note_menu_layers[13].layer_id==130);
    expect_compose_test(view.semantic_layer_first_row==20 && view.semantic_layers[0].layer_id==121);
    app.note_layer_menu_row_count=0;
    fixture.project();
    expect_compose_test(view.note_menu_layer_count==0 && !view.note_menu_layers);
    app.note_layer_menu_row_count=18;
    ++fixture.history.document.revision;
    fixture.project();
    expect_compose_test(view.note_menu_layer_count==0 && app.note_layer_menu_row_count==0);
    app.note_layer_menu_revision=fixture.history.document.revision;
    app.note_layer_menu_chart=chart_id_t{2};
    app.note_layer_menu_row_count=18;
    fixture.project();
    expect_compose_test(view.note_menu_layer_count==0 && app.note_layer_menu_row_count==0);
}
TEST_CASE("Compose semantic arena exhaustion publishes only complete named rows",
          "[sys][compose-semantic-arena]") {
    bool found=false;
    for(std::size_t limit=64;limit<=4096;++limit) {
        compose_test_fixture_t fixture(semantic_arena_song());
        wormedit::frame_arena_t arena({.chunk_bytes=limit,.maximum_bytes=limit});
        project_geometry_arena(fixture,arena);
        const auto& view=fixture.flattened.view.compose;
        if(view.semantic_layer_count!=1) continue;
        expect_compose_test(view.semantic_layers_truncated);
        check_semantic_first(view); found=true;break;
    }
    expect_compose_test(found);
}
TEST_CASE("Compose semantic arena text remains stable across 1000 warmed frames",
          "[sys][compose-semantic-arena]") {
    compose_test_fixture_t fixture(semantic_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    project_geometry_arena(fixture,arena);arena.reset();project_geometry_arena(fixture,arena);
    const auto used=arena.used_bytes(),committed=arena.committed_bytes();bool stable=true;
    std::uint64_t allocations{};
    for(int i=0;i<1000;++i) {
        arena.reset();
        const auto before=frame_heap_allocation_snapshot();
        project_geometry_arena(fixture,arena);
        allocations=frame_heap_allocation_snapshot().count-before.count;
        stable=stable && arena.used_bytes()==used && arena.committed_bytes()==committed &&
            fixture.flattened.view.compose.semantic_layer_count==2;
    }
    INFO("Semantic narrow fixture remaining projection allocations: " << allocations);
    expect_compose_test(stable);
}

namespace {
auto pinch_dispatch_action(compose_test_fixture_t& fixture, double factor,
                           double anchor) -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_viewport_input_count = 1;
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
        compose_test_fixture_t fixture(dense_song());
        fixture.project();
        fixture.interaction->viewport_scroll_pixels = 800;
        const auto before = *fixture.interaction;
        const auto revision = fixture.history.document.revision;
        constexpr double anchor = 123;
        auto action = pinch_dispatch_action(fixture, 1.25, anchor);
        viewport_input_controller_t viewport;
        editor_navigation_input_controller_t navigation;
        start_pinch_test_motion(viewport, autoscroll);
        expect_compose_test(viewport.momentum_active() || viewport.autoscroll_active());
        const auto status = dispatch_compose_input_action(action, fixture.history,
            *fixture.binding.workspace, *fixture.interaction, viewport, navigation,
            fixture.application, fixture.cache, 0.016);
        INFO(status);
        const auto& after = *fixture.interaction;
        expect_compose_test(std::abs(after.pixels_per_standard_measure -
            before.pixels_per_standard_measure * 1.25) < 1e-9);
        expect_compose_test(std::abs((after.viewport_scroll_pixels + anchor) /
            after.pixels_per_standard_measure - (before.viewport_scroll_pixels + anchor) /
            before.pixels_per_standard_measure) < 1e-9);
        expect_compose_test(after.fixed_cursor_screen_y_pixels == before.fixed_cursor_screen_y_pixels);
        const auto index = std::get<measure_index_t>(build_measure_index(fixture.history.document.charts.front()));
        const auto cursor_row = static_cast<double>(*absolute_row(index, after.cursor)) + after.cursor_subrow;
        const auto source_anchor = after.scroll_direction == editor_field_scroll_direction_t::down
            ? after.viewport_height_pixels - *after.fixed_cursor_screen_y_pixels : *after.fixed_cursor_screen_y_pixels;
        expect_compose_test(std::abs(cursor_row * after.pixels_per_standard_measure / 960 -
            after.viewport_scroll_pixels - source_anchor) < 1e-9);
        expect_compose_test(after.selected_objects == before.selected_objects);
        expect_compose_test(after.selected_notes == before.selected_notes);
        expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
        expect_compose_test(!viewport.momentum_active() && !viewport.autoscroll_active());
        expect_compose_test(viewport.advance(0.016).empty());
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
            compose_test_fixture_t fixture(dense_song());
            fixture.project();
            fixture.interaction->viewport_scroll_pixels = 800;
            const auto before = *fixture.interaction;
            const auto revision = fixture.history.document.revision;
            auto action = pinch_dispatch_action(fixture, factor, anchor);
            viewport_input_controller_t viewport;
            editor_navigation_input_controller_t navigation;
            start_pinch_test_motion(viewport, autoscroll);
            const auto status = dispatch_compose_input_action(action, fixture.history,
                *fixture.binding.workspace, *fixture.interaction, viewport, navigation,
                fixture.application, fixture.cache, 0);
            expect_compose_test(status.find("invalid scale or anchor") != std::string::npos);
            expect_compose_test(fixture.interaction->viewport_scroll_pixels == before.viewport_scroll_pixels);
            expect_compose_test(fixture.interaction->pixels_per_standard_measure == before.pixels_per_standard_measure);
            expect_compose_test(fixture.interaction->cursor == before.cursor);
            expect_compose_test(fixture.interaction->selected_objects == before.selected_objects);
            expect_compose_test(fixture.interaction->selected_notes == before.selected_notes);
            expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
            expect_compose_test(viewport.autoscroll_active() == autoscroll);
            expect_compose_test(viewport.momentum_active() == !autoscroll);
        }
    }
}

namespace {
void expect_waveform_arena(bool value, const std::source_location location = std::source_location::current()) {
    INFO("Waveform arena expectation at " << location.file_name() << ':' << location.line());
    REQUIRE(value);
}
void borrow_waveform_frame(envelope_projection_fixture_t& fixture, frame_arena_t& arena) {
    const auto view = fixture.flattened.view;
    fixture.flattened = flattened_workspace_t{&arena};
    fixture.flattened.view = view;
}
auto project_waveform_frame(envelope_projection_fixture_t& fixture) -> bool {
    return populate_compose_waveform_projection(fixture.flattened, fixture.cache,
        &fixture.analysis, false, true, &fixture.interaction, nullptr,
        &fixture.blocks, &fixture.style);
}
void check_empty_frame_waveform(const WormUiComposeView& view) {
    expect_waveform_arena(view.waveform_rows == nullptr && view.waveform_row_count == 0);
    expect_waveform_arena(view.waveform_overview_rows == nullptr && view.waveform_overview_row_count == 0);
    expect_waveform_arena(view.waveform_overview_generation == 0);
    expect_waveform_arena(view.waveform_overview_total_frames == 0 && view.waveform_overview_sample_rate == 0);
}
}
TEST_CASE("Compose waveform arena output preserves persistent source caches across 1000 resets",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    frame_arena_t arena({.chunk_bytes=1024*1024,.maximum_bytes=2*1024*1024});
    borrow_waveform_frame(fixture, arena);
    expect_waveform_arena(project_waveform_frame(fixture));
    compare_cached_envelope_source(fixture);
    const auto* persistent_overview = fixture.cache.waveform_overview_rows.data();
    const auto* persistent_envelope = fixture.cache.waveform_envelope_ring.rows.data();
    const auto generation = fixture.flattened.view.compose.waveform_overview_generation;
    const auto projected = fixture.cache.waveform_envelope_ring.projected_rows;
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    bool stable = true;
    std::size_t minimum_allocations = std::numeric_limits<std::size_t>::max(), maximum_allocations{};
    for (int index=0; index<1000; ++index) {
        arena.reset();
        borrow_waveform_frame(fixture, arena);
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
    // allocations are removed. The remaining10 are timing_at factor vectors,
    // outside this storage unit. A later core optimization may lower this bound.
    expect_waveform_arena(stable);
    expect_waveform_arena(maximum_allocations <= 10);
    expect_waveform_arena(projected > 0);
    expect_waveform_arena(fixture.cache.waveform_overview_rows.data() == persistent_overview);
    expect_waveform_arena(fixture.flattened.view.compose.waveform_overview_rows != persistent_overview);
    expect_waveform_arena(fixture.cache.waveform_envelope_ring.rows.data() == persistent_envelope);
    expect_waveform_arena(fixture.cache.waveform_envelope_ring.projected_rows == 0);
    expect_waveform_arena(static_cast<const void*>(fixture.flattened.view.compose.waveform_rows) !=
        static_cast<const void*>(persistent_envelope));
    compare_cached_envelope_source(fixture);
}
TEST_CASE("Compose waveform arena exhaustion clears both output arrays without changing cached RMS",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    fixture.project();
    const auto projected = fixture.cache.waveform_envelope_ring.projected_rows;
    const auto generation = fixture.cache.waveform_overview_generation;
    for (const auto limit : {std::size_t{64}, sizeof(WormUiComposeWaveformRowView) * WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY}) {
        frame_arena_t arena({.chunk_bytes=limit,.maximum_bytes=limit});
        borrow_waveform_frame(fixture, arena);
        expect_waveform_arena(!project_waveform_frame(fixture));
        check_empty_frame_waveform(fixture.flattened.view.compose);
        expect_waveform_arena(fixture.flattened.view.compose.truncated);
        expect_waveform_arena(fixture.cache.waveform_envelope_ring.projected_rows == projected);
        expect_waveform_arena(fixture.cache.waveform_overview_generation == generation);
    }
}
TEST_CASE("Compose waveform unavailable projection removes stale arena pointers after reset",
          "[sys][waveform-arena]") {
    envelope_projection_fixture_t fixture;
    frame_arena_t arena;
    borrow_waveform_frame(fixture, arena); fixture.project(); arena.reset();
    expect_waveform_arena(!populate_compose_waveform_projection(fixture.flattened, fixture.cache,
        &fixture.analysis, false, false, &fixture.interaction));
    check_empty_frame_waveform(fixture.flattened.view.compose);
    expect_waveform_arena(fixture.flattened.compose.waveform.row.capacity() == 0);
    expect_waveform_arena(fixture.flattened.compose.waveform.overview_row.capacity() == 0);
}
TEST_CASE("Compose standalone waveform storage survives fixture destruction and move",
          "[sys][waveform-arena]") {
    flattened_workspace_t retained;
    double expected_y{}; float expected_peak{};
    std::size_t expected_rows{}, expected_overview{};
    {
        envelope_projection_fixture_t fixture; fixture.project();
        expected_y=fixture.flattened.view.compose.waveform_rows[0].y_pixels;
        expected_peak=fixture.flattened.view.compose.waveform_rows[0].absolute_peak[0];
        expected_rows=fixture.flattened.view.compose.waveform_row_count;
        expected_overview=fixture.flattened.view.compose.waveform_overview_row_count;
        retained=fixture.flattened;
    }
    auto moved=std::move(retained);
    expect_waveform_arena(moved.view.compose.waveform_row_count == expected_rows && expected_rows>0);
    expect_waveform_arena(moved.view.compose.waveform_overview_row_count == expected_overview && expected_overview>0);
    expect_waveform_arena(moved.view.compose.waveform_rows[0].y_pixels == expected_y);
    expect_waveform_arena(moved.view.compose.waveform_rows[0].absolute_peak[0] == expected_peak);
    expect_waveform_arena(moved.view.compose.waveform_rows == moved.compose.waveform.row.data());
    expect_waveform_arena(moved.view.compose.waveform_overview_rows == moved.compose.waveform.overview_row.data());
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
    fixture.flattened.view.compose.viewport_height_pixels = WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY;
    frame_arena_t arena;
    borrow_waveform_frame(fixture, arena); fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto reference = audio::project_aligned_waveform(*fixture.analysis.waveform,
        *fixture.cache.marker_timing_index, {.viewport_scroll_pixels=fixture.interaction.viewport_scroll_pixels,
            .viewport_height_pixels=view.viewport_height_pixels,
            .pixels_per_standard_measure=view.pixels_per_standard_measure,
            .maximum_rows=WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY});
    expect_waveform_arena(static_cast<bool>(reference));
    expect_waveform_arena(reference.rows.size() == WORM_UI_COMPOSE_WAVEFORM_ROW_CAPACITY);
    expect_waveform_arena(view.waveform_row_count == reference.rows.size());
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
void check_marker_frame_spans(const flattened_workspace_t& frame) {
    const auto& view = frame.view.compose;
    expect_compose_test(view.beat_bookmarks == frame.compose.bookmark.data() && view.beat_bookmark_count == frame.compose.bookmark.size());
    expect_compose_test(view.audio_bookmarks == frame.compose.audio_marker.bookmark.data() && view.audio_bookmark_count == frame.compose.audio_marker.bookmark.size());
    expect_compose_test(view.beat_sections == frame.compose.section.data() && view.beat_section_count == frame.compose.section.size());
    expect_compose_test(view.audio_sections == frame.compose.audio_marker.section.data() && view.audio_section_count == frame.compose.audio_marker.section.size());
    expect_compose_test(view.beat_bookmark_count <= WORM_UI_COMPOSE_BOOKMARK_CAPACITY);
    expect_compose_test(view.beat_section_count <= WORM_UI_COMPOSE_SECTION_CAPACITY);
    expect_compose_test(view.audio_bookmark_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY);
    expect_compose_test(view.audio_section_count <= WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY);
}
void check_marker_prefix(const flattened_workspace_t& frame, const chart_document_t& chart) {
    check_marker_frame_spans(frame);
    for (std::size_t index = 0; index < frame.compose.bookmark.size(); ++index) {
        const auto& row = frame.compose.bookmark[index]; const auto& source = chart.bookmarks[index * 2];
        expect_compose_test(row.bookmark_id == source.id.value && row.measure_number == 1 && row.row == source.position->row);
        expect_compose_test(row.chart_row == source.position->row && std::string_view(row.label) == source.label);
    }
    for (std::size_t index = 0; index < frame.compose.audio_marker.bookmark.size(); ++index) {
        const auto& row = frame.compose.audio_marker.bookmark[index]; const auto& source = chart.bookmarks[index * 2 + 1];
        expect_compose_test(row.bookmark_id == source.id.value && row.audio_milliseconds == *source.audio_milliseconds);
        expect_compose_test(std::string_view(row.label) == source.label);
    }
    for (std::size_t index = 0; index < frame.compose.section.size(); ++index) {
        const auto& row = frame.compose.section[index]; const auto& source = chart.sections[index * 2];
        expect_compose_test(row.section_id == source.id.value && std::string_view(row.label) == source.label);
        expect_compose_test(row.begin_measure_number == 1 && row.end_measure_number == 1 && row.begin_row == 96 && row.end_row == 480);
        expect_compose_test(row.begin_chart_row == 96 && row.end_chart_row == 480);
    }
    for (std::size_t index = 0; index < frame.compose.audio_marker.section.size(); ++index) {
        const auto& row = frame.compose.audio_marker.section[index]; const auto& source = chart.sections[index * 2 + 1];
        expect_compose_test(row.section_id == source.id.value && std::string_view(row.label) == source.label);
        expect_compose_test(row.begin_audio_milliseconds == 200 && row.end_audio_milliseconds == 1600);
    }
}
void check_marker_visible_geometry(const WormUiComposeView& view, bool downscroll, double scroll) {
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
void check_marker_text_addresses(const auto& rows, std::uintptr_t begin, std::uintptr_t end) {
    check_geometry_between(rows, begin, end);
    for (const auto& row : rows) {
        const auto address = reinterpret_cast<std::uintptr_t>(row.label);
        expect_compose_test(address >= begin && address + std::strlen(row.label) + 1 <= end);
    }
}
}

TEST_CASE("Compose marker arenas preserve anchors labels source order and projected geometry", "[sys][frame-arena][compose-markers]") {
    for (const auto direction : {1U, 2U}) {
        compose_test_fixture_t fixture(marker_arena_song());
        fixture.application.editor_preferences.scroll_directions.fill(direction);
        wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
        project_geometry_arena(fixture, arena);
        REQUIRE(fixture.flattened.view.compose.beat_bookmark_count == 2);
        REQUIRE(fixture.flattened.view.compose.audio_bookmark_count == 2);
        REQUIRE(fixture.flattened.view.compose.beat_section_count == 2);
        REQUIRE(fixture.flattened.view.compose.audio_section_count == 2);
        check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
        check_marker_visible_geometry(fixture.flattened.view.compose, direction == 2, fixture.interaction->viewport_scroll_pixels);
    }
}

TEST_CASE("Compose marker arrays and strings belong to the supplied arena through move", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song());
    const auto chart = fixture.history.document.charts.front();
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto begin = arena.allocate_bytes(1, 1);
    project_geometry_arena(fixture, arena);
    const auto end = arena.allocate_bytes(1, 1);
    REQUIRE((begin && end));
    const auto first = reinterpret_cast<std::uintptr_t>(begin.data), last = reinterpret_cast<std::uintptr_t>(end.data);
    check_marker_text_addresses(fixture.flattened.compose.bookmark, first, last);
    check_marker_text_addresses(fixture.flattened.compose.section, first, last);
    check_marker_text_addresses(fixture.flattened.compose.audio_marker.bookmark, first, last);
    check_marker_text_addresses(fixture.flattened.compose.audio_marker.section, first, last);
    fixture.history.document.charts.front().bookmarks[0].label.assign(200, 'x');
    fixture.history.document.charts.front().sections[1].label.assign(200, 'y');
    auto moved = std::move(fixture.flattened);
    moved.rebind_view_pointers();
    check_marker_prefix(moved, chart);
    moved = {}; arena.reset();
    ++fixture.history.document.revision;
    project_geometry_arena(fixture, arena);
    check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
}

TEST_CASE("Standalone Compose markers retain owned label text across source destruction", "[sys][frame-arena][compose-markers]") {
    flattened_workspace_t owned;
    const auto song = marker_arena_song();
    {
        compose_test_fixture_t fixture(song);
        fixture.project();
        owned = std::move(fixture.flattened);
    }
    owned.rebind_view_pointers();
    check_marker_prefix(owned, song.charts.front());
}

TEST_CASE("Compose marker spans preserve all four caps and empty-array flags", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song(65));
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.beat_bookmark_count == 64 && view.audio_bookmark_count == 64);
    expect_compose_test(view.beat_section_count == 64 && view.audio_section_count == 64);
    expect_compose_test(view.beat_bookmarks_truncated && view.audio_bookmarks_truncated && view.beat_sections_truncated && view.audio_sections_truncated);
    check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
    compose_test_fixture_t empty(marker_arena_song(0));
    empty.project();
    const auto& none = empty.flattened.view.compose;
    check_marker_frame_spans(empty.flattened);
    expect_compose_test(none.beat_bookmark_count == 0 && none.audio_bookmark_count == 0 && none.beat_section_count == 0 && none.audio_section_count == 0);
    expect_compose_test(!none.beat_bookmarks_truncated && !none.audio_bookmarks_truncated && !none.beat_sections_truncated && !none.audio_sections_truncated);
}

TEST_CASE("Compose marker arena exhaustion publishes complete per-anchor prefixes", "[sys][frame-arena][compose-markers]") {
    bool beat_prefix = false, audio_prefix = false, section_prefix = false, audio_section_prefix = false;
    for (std::size_t limit = 128; limit <= 4096; limit += 16) {
        compose_test_fixture_t fixture(marker_arena_song());
        wormedit::frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        project_geometry_arena(fixture, arena);
        const auto& view = fixture.flattened.view.compose;
        check_marker_prefix(fixture.flattened, fixture.history.document.charts.front());
        if (view.beat_bookmark_count < 2) expect_compose_test(view.beat_bookmarks_truncated);
        if (view.audio_bookmark_count < 2) expect_compose_test(view.audio_bookmarks_truncated);
        if (view.beat_section_count < 2) expect_compose_test(view.beat_sections_truncated);
        if (view.audio_section_count < 2) expect_compose_test(view.audio_sections_truncated);
        beat_prefix = beat_prefix || view.beat_bookmark_count == 1;
        audio_prefix = audio_prefix || view.audio_bookmark_count == 1;
        section_prefix = section_prefix || view.beat_section_count == 1;
        audio_section_prefix = audio_section_prefix || view.audio_section_count == 1;
    }
    expect_compose_test(beat_prefix && audio_prefix && section_prefix && audio_section_prefix);
}

TEST_CASE("Warmed Compose markers keep stable bytes and allocations for 1000 cached frames", "[sys][frame-arena][compose-markers]") {
    compose_test_fixture_t fixture(marker_arena_song());
    wormedit::frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    project_geometry_arena(fixture, arena);
    arena.reset(); project_geometry_arena(fixture, arena);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    const auto source = fixture.cache.static_scene_storage;
    arena.reset(); allocations = 0; count_allocations = true;
    project_geometry_arena(fixture, arena);
    count_allocations = false;
    const auto remaining = allocations;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset(); allocations = 0; count_allocations = true;
        project_geometry_arena(fixture, arena);
        count_allocations = false;
        stable = stable && allocations == remaining && arena.used_bytes() == used &&
            arena.committed_bytes() == committed && fixture.cache.static_scene_storage == source &&
            fixture.flattened.view.compose.beat_bookmark_count == 2 && fixture.flattened.view.compose.audio_section_count == 2;
    }
    INFO("Remaining actual projection allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    expect_compose_test(stable);
    expect_compose_test(fixture.cache.static_projection_reused_frames > 0);
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
void expect_marker_projection_error(const flattened_workspace_t& flattened,
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
    project_geometry_arena(fixture, arena);
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
    fixture.project(); // Geometry outside the published cap was already skipped by the original producer.
    expect_compose_test(fixture.flattened.view.compose.beat_bookmarks_truncated);
    chart.bookmarks[128].position.reset();
    ++fixture.history.document.revision;
    fixture.flattened = {};
    fixture.flattened.view.compose.visible = true;
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
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
    reused.application.editor_preferences.scroll_directions.fill(direction);
    fresh.application.editor_preferences.scroll_directions.fill(direction);
    wormedit::frame_arena_t arena, reference;
    project_geometry_arena(reused, arena); project_geometry_arena(fresh, reference);
    const auto source = reused.cache.static_scene_storage;
    REQUIRE(source);
    arena.reset(); reference.reset();
    reused.interaction->viewport_scroll_pixels = 0.5;
    fresh.interaction->viewport_scroll_pixels = 0.5;
    fresh.cache = {};
    project_geometry_arena(reused, arena); project_geometry_arena(fresh, reference);
    expect_compose_test(reused.cache.static_scene_storage == source && reused.cache.static_projection_reused_frames > 0);
    const auto& a = reused.flattened.compose; const auto& b = fresh.flattened.compose;
    compare_marker_bookmarks(a.bookmark, b.bookmark);
    compare_marker_bookmarks(a.audio_marker.bookmark, b.audio_marker.bookmark);
    compare_marker_sections(a.section, b.section);
    compare_marker_sections(a.audio_marker.section, b.audio_marker.section);
    expect_compose_test(source->view.compose.beat_bookmark_count == 0 && !source->view.compose.beat_bookmarks);
    expect_compose_test(source->view.compose.audio_section_count == 0 && !source->view.compose.audio_sections);
}
}
TEST_CASE("Compose marker arena views agree across fresh and reused scene windows", "[sys][frame-arena][compose-markers]") {
    check_marker_reused_frame(1);
    check_marker_reused_frame(2);
}

TEST_CASE("Imported five-key BMS materializes a Compose field that responds to the wheel",
          "[sys][compose][bms-detection][regression]") {
    const std::string source = "#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.song));
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoint_count > 0);
    const auto before = fixture.interaction->viewport_scroll_pixels;
    WormUiWorkspaceAction action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = -1, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.area_scroll_pixels > before);
    expect_compose_test(fixture.history.undo.empty());
}

namespace {
auto cursor_placement_song() -> song_document_t {
    const std::string source = "#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.song.has_value());
    return std::move(*parsed.song);
}

auto cursor_placement_action(compose_test_fixture_t& fixture) -> WormUiWorkspaceAction {
    WormUiWorkspaceAction action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id = fixture.interaction->chart.value;
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
    action.compose_input_count = 1;
    fixture.application.note_paint_mode_active = !resource;
    action.compose_inputs[0] = {.kind = resource ? WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE
                                               : WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .object_id = fixture.history.document.resources.front().id.value,
        .resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.editor_preferences))};
    if (mode == 2) {
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN;
        action.compose_inputs[1] = action.compose_inputs[0];
        action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
        action.compose_input_count = 2;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(fixture.history.undo.size() == 1);
    expect_compose_test(capture_cursor_pose(state) == cursor_before);
}

void check_note_placement_preserves_cursor(int mode, bool downscroll) {
    INFO("Mode " << mode << ", downscroll " << downscroll);
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    place_pointer_test_note(fixture, mode);
    expect_compose_test(fixture.interaction->fixed_cursor_screen_y_pixels == 48);
    expect_compose_test(fixture.interaction->cursor_subrow == 7);
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.cursor != nullptr);
    INFO("drawn cursor " << fixture.flattened.view.compose.cursor->bounds.top << " anchor " << *fixture.interaction->fixed_cursor_screen_y_pixels);
    expect_compose_test(std::abs(fixture.flattened.view.compose.cursor->bounds.top - 47) < 1e-9);
    const auto scroll = fixture.interaction->viewport_scroll_pixels;
    auto action = cursor_placement_action(fixture);
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_y = downscroll ? -1.0 : 1.0, .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    const auto status = dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
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
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.note_paint_mode_active = true;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.lane_key_binding_count == 10);
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    for (const auto& binding : profile.input.bindings) {
        auto action = cursor_placement_action(fixture);
        action.compose_input_count = 1;
        action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS,
            .lane_id = binding.lane.value};
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test(fixture.interaction->pending_hold.has_value());
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE;
        (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
            *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
        expect_compose_test(!fixture.interaction->pending_hold.has_value());
    }
    const auto& notes = fixture.history.document.charts.front().measures.front().notes;
    expect_compose_test(notes.size() == 10);
    for (const auto& binding : profile.input.bindings)
        expect_compose_test(std::ranges::count(notes, binding.lane, &note_t::lane) == 1);
    expect_compose_test(fixture.history.undo.size() == 10);
}

void check_latched_range_projection(bool downscroll) {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.active_snap_denominator = 4;
    auto action = cursor_placement_action(fixture);
    action.compose_input_count = 1;
    action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_LATCHED_RANGE_SELECTION;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(state.pending_space_range_selection.has_value());
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    expect_compose_test(view.space_range_preview_visible);
    const auto height = view.space_range_preview_bounds.bottom - view.space_range_preview_bounds.top;
    expect_compose_test(std::abs(height - state.pixels_per_standard_measure / 4) < 1e-9);
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        state, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(!state.pending_space_range_selection.has_value());
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
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.editor_preferences.scroll_directions[0] = downscroll ? 2U : 1U;
    fixture.project();
    auto& state = *fixture.interaction;
    state.viewport_scroll_pixels = 1000.0;
    state.active_snap_denominator = 4;
    fixture.application.preview_playhead_absolute_row = 960.0;
    fixture.application.transport_preview_playing = [playing] { return playing; };
    std::size_t seeks = 0;
    fixture.application.transport_seek = [&seeks](std::uint64_t) {
        ++seeks;
        return std::string{"Seeked."};
    };
    auto action = cursor_placement_action(fixture);
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = kind, .delta_y = delta,
        .cursor_x_pixels = 200, .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        state, viewport, navigation, fixture.application, fixture.cache, 0.016);
    const bool forward = (delta > 0.0) != downscroll;
    if (playing) {
        expect_compose_test(fixture.application.preview_playhead_absolute_row == (forward ? 1200.0 : 720.0));
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
auto paint_stroke_inputs(compose_test_fixture_t& fixture, std::uint64_t sound) -> WormUiWorkspaceAction {
    auto& state = *fixture.interaction;
    const local_position_t position{fixture.history.document.charts.front().measures.front().id, 240};
    const auto& chart = fixture.history.document.charts.front();
    const auto view = inspect_editor_interaction(fixture.history, state);
    const auto y = editor_position_y_pixels(chart,
        std::get<editor_interaction_view_model_t>(view), position);
    expect_compose_test(y.has_value());
    auto action = cursor_placement_action(fixture);
    action.compose_input_count = 2;
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_BEGIN,
        .x_pixels = fixture.flattened.view.compose.playable_field_left_pixels + 20,
        .y_pixels = compose_source_y(*y, state.viewport_height_pixels,
            compose_field_downscroll(chart, fixture.application.editor_preferences)),
        .resource_id = sound};
    action.compose_inputs[1] = action.compose_inputs[0];
    action.compose_inputs[1].kind = WORM_UI_COMPOSE_INPUT_NOTE_PAINT_RELEASE;
    return action;
}
} // namespace

TEST_CASE("Note paint begin input carries the armed sound into the document and advances once",
          "[sys][compose][paint][sound]") {
    compose_test_fixture_t fixture{cursor_placement_song()};
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    fixture.application.note_paint_mode_active = true;
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
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
    expect_compose_test(count_notes_with_sound() == before + 1);
    expect_compose_test(placed_resource_notifications == std::vector<uint64_t>{sound.value});

    // A silent stroke is committed without notifying the dock.
    fixture.project();
    placed_resource_notifications.clear();
    auto silent = paint_stroke_inputs(fixture, 0);
    (void)dispatch_compose_input_action(silent, fixture.history, *fixture.binding.workspace,
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
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.application.semantic_layer_target = no_layer ? std::nullopt : std::optional{layer_id_t{7}};
    fixture.application.semantic_layer_target_none = no_layer;
    fixture.application.note_paint_mode_active = mode == 0;
    fixture.project();
    const auto sound = fixture.history.document.resources.front().id;
    auto action = paint_stroke_inputs(fixture, sound.value);
    if (mode == 1) {
        place_cursor_at(*fixture.interaction,
            {fixture.history.document.charts.front().measures.front().id, 240});
        const auto lane = fixture.history.document.charts.front().lane_layouts.front().layout.lanes.front().id;
        action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, .lane_id = lane.value};
        action.compose_inputs[1] = {.kind = WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, .lane_id = lane.value};
    } else if (mode == 2) {
        action.compose_input_count = 1;
        action.compose_inputs[0].kind = WORM_UI_COMPOSE_INPUT_RESOURCE_POINTER_PLACE;
        action.compose_inputs[0].object_id = sound.value;
        action.compose_inputs[0].resource_role = WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE;
    }
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
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
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto names = std::span(view.lane_layout_profile_names,
                                 view.lane_layout_profile_choice_count);
    CHECK(std::ranges::any_of(names, [](const auto& name) {
        return std::string_view{name} == "BMS DP5 / 10K";
    }));
}

TEST_CASE("Stopped BMS transport seek moves cursor and viewport without editing",
          "[sys][compose][seek-bar][regression]") {
    notification_center_t center;
    notification_sink_t sink{center};
    const std::string source = "#PLAYER 1\n#BPM 154\n#WAV01 synth.wav\n"
        "#00011:01\n#00012:01\n#00013:01\n#00014:01\n#00015:01\n#00016:01\n#04011:01\n";
    auto parsed = parse_bms_bytes(std::as_bytes(std::span(source.data(), source.size())),
        {.file_extension = ".bms"});
    expect_compose_test(parsed.song.has_value());
    compose_test_fixture_t fixture(std::move(*parsed.song));
    fixture.binding.workspace->selected_chart = fixture.history.document.charts.front().id;
    fixture.project();
    const auto before = fixture.interaction->cursor;
    const auto status = seek_stopped_compose_transport(fixture.history, *fixture.interaction,
        fixture.cache, 20 * 44'100, sink);
    expect_compose_test(status == "Compose cursor moved to the seek position.");
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
    (void)seek_stopped_compose_transport(fixture.history, *fixture.interaction, fixture.cache, 0, sink);
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
    fixture.application.editor_preferences.audition_on_click = enabled;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.note_endpoint_count > 0);
    const auto& bounds = fixture.flattened.view.compose.note_endpoints[0].bounds;
    WormUiWorkspaceAction action{};
    action.shortcut_window_focused = true;
    action.compose_input_chart_id = fixture.interaction->chart.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    action.compose_input_count = 1;
    action.compose_inputs[0] = {.kind = WORM_UI_COMPOSE_INPUT_POINTER_CLICK,
        .x_pixels = (bounds.left + bounds.right) / 2,
        .y_pixels = (bounds.top + bounds.bottom) / 2};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0.016);
    expect_compose_test(!fixture.interaction->selected_notes.empty());
    expect_compose_test(fixture.cache.audition_request.has_value() == enabled);
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
    fixture.application.note_paint_mode_active = true;
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto lane = view.lanes[0];
    const auto x = (lane.bounds.left + lane.bounds.right) * .5;
    const auto source_y = view.pixels_per_standard_measure * .375 - fixture.interaction->viewport_scroll_pixels;
    const auto y = view.downscroll ? view.viewport_height_pixels - source_y : source_y;
    fixture.cache.paint_pointer = std::pair{x, y};
    fixture.project();
    REQUIRE(fixture.flattened.view.compose.paint_ghost_visible);
    const auto builds = fixture.cache.static_scene_rebuild_count;
    fixture.cache.paint_pointer = std::pair{x, y + 30};
    fixture.project();
    CHECK(fixture.flattened.view.compose.paint_ghost_visible);
    CHECK(fixture.cache.static_scene_rebuild_count == builds);
    CHECK(fixture.history.undo.empty());
    const auto endpoint = fixture.flattened.view.compose.note_endpoints[0];
    fixture.cache.paint_pointer = std::pair{(endpoint.bounds.left + endpoint.bounds.right) * .5,
                                           (endpoint.bounds.top + endpoint.bounds.bottom) * .5};
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost_visible);
    const auto ghost = fixture.flattened.view.compose.paint_ghost;
    expect_compose_test(ghost.paint_erase_preview);
    expect_compose_test(std::abs(ghost.bounds.top - endpoint.bounds.top) < 1e-9);
    expect_compose_test(std::abs(ghost.bounds.bottom - endpoint.bounds.bottom) < 1e-9);
    fixture.interaction->armed_note_kind = note_kind_t::hold;
    fixture.project();
    expect_compose_test(fixture.flattened.view.compose.paint_ghost.kind == 1);
    expect_compose_test(fixture.flattened.view.compose.hold_body_count == 0);
    fixture.cache.paint_pointer.reset();
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
    fixture.cache.paint_pointer = std::pair{x, y};
    fixture.application.note_paint_mode_active = false;
    fixture.project();
    CHECK_FALSE(fixture.flattened.view.compose.paint_ghost_visible);
}

TEST_CASE("Lane profile switcher hides itself for layouts it cannot represent",
          "[sys][lane-profile-switch][regression]") {
    // A BMS DP7 chart has 16 lanes, the same count as osu!mania 16K. The switcher only lists
    // osu!mania/StepMania key modes, so it must not present DP7 as its first entry.
    compose_test_fixture_t custom{compose_test_song()};
    custom.project();
    CHECK(custom.flattened.view.compose.lane_profile_choice_count == 0);

    auto song = compose_test_song();
    song.charts.front().lane_layouts.front().layout.profile_id = "osu-mania-1k";
    compose_test_fixture_t mania{std::move(song)};
    mania.project();
    CHECK(mania.flattened.view.compose.lane_profile_choice_count >= 1);
    CHECK(mania.flattened.view.compose.lane_profile_current == 0);
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
    expect_compose_test(view.lane_profile_choice_count == 41);
    expect_compose_test(view.lane_profile_choice_count < WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY);
    expect_compose_test(view.lane_profile_current == -1);
    std::vector<std::string_view> names;
    for (std::size_t index = 0; index < view.lane_profile_choice_count; ++index)
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
        compose.project();
        compose.interaction->active_snap_denominator = 4;
    }
    ~lane_key_callback_fixture_t() { placed_resource_notifications.clear(); }
    auto dispatch(WormUiComposeInputKind kind, std::uint64_t lane = 1,
                  std::uint64_t sound = 7, bool successive = false) -> std::string {
        auto action = cursor_placement_action(compose);
        action.compose_input_project_id = compose.history.document.id.value;
        action.compose_input_count = 1;
        action.compose_inputs[0] = {.kind = kind, .lane_id = lane,
                                    .successive = successive, .resource_id = sound};
        return dispatch_compose_input_action(action, compose.history, *compose.binding.workspace,
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
    expect_compose_test(f.compose.interaction->pending_hold.has_value());
    expect_compose_test(f.compose.interaction->pending_hold->sound == resource_id_t{7});
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
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_PRESS, 1, 8);
        (void)f.dispatch(WORM_UI_COMPOSE_INPUT_LANE_KEY_RELEASE, 1, 8);
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
    expect_compose_test(f.compose.interaction->pending_hold.has_value());
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

#include "sys/stem_waveform_window.h"
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
    const auto packed_source = fixture.sources.register_source(
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
    fixture.flattened.view.compose.selected_bmson_bgm_event_id = 74000;
    auto packed_analysis = std::make_shared<audio::waveform_analysis_result_t>(*fixture.channel_analysis);
    packed_analysis->waveform->key.source = packed_source.binding->source;
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
        fixture.workspace, &fixture.interaction, fixture.cache, fixture.sources,
        {std::nullopt, nullptr}, {asset_id_t{8}, packed_analysis}, 1,
        [&](asset_id_t) { ++requests; return fixture.channel_analysis; }, visible};
    populate_bmson_waveform_projection(context);
    INFO("requests=" << requests << " cache entries=" << fixture.cache.stem_waveforms.size()
         << " projected views=" << fixture.flattened.view.compose.stem_waveform_count);
    expect_gpu_work(requests == 3 && fixture.cache.stem_waveforms.size() == 3);
    expect_gpu_work(fixture.flattened.view.compose.stem_waveform_count == 3);
    expect_gpu_work(fixture.cache.channel_waveform_rows.empty());
    expect_gpu_work(!fixture.cache.channel_waveform_slices);
    const std::vector<std::uint64_t> expected{71001, 71500, 72001};
    std::vector<std::uint64_t> keys;
    for (std::size_t index = 0; index < fixture.flattened.view.compose.stem_waveform_count; ++index)
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
    WormUiTimingLaneKind lane{};
    std::string label, detail;
    bool fixed{}, selected{};
    auto operator==(const workspace_timing_snapshot_t&) const -> bool = default;
};
auto workspace_timing_snapshot(const compose_test_fixture_t& fixture)
    -> std::vector<workspace_timing_snapshot_t> {
    std::vector<workspace_timing_snapshot_t> result;
    for (const auto& marker : fixture.flattened.compose.timing_marker)
        result.push_back({marker.object_id, marker.source_order, marker.storage_order,
            marker.chart_row, marker.marker_y_pixels, marker.marker_left_fraction,
            marker.marker_right_fraction, marker.lane, marker.short_label, marker.detail,
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
    const auto selected = std::ranges::find_if(cached.flattened.compose.timing_marker,
        [](const auto& marker) { return marker.object_id == 10005; });
    if (selected == cached.flattened.compose.timing_marker.end() || !selected->selected ||
        original == workspace_timing_snapshot(cached)) return false;
    for (auto* fixture : {&cached, &reference}) {
        fixture->interaction->selected_objects.clear();
        fixture->project();
    }
    return workspace_timing_snapshot(cached) == workspace_timing_snapshot(reference) &&
        std::ranges::none_of(cached.flattened.compose.timing_marker,
                            [](const auto& marker) { return marker.selected; }) &&
        cached.cache.timing_payload_cache.build_count == 1 &&
        reference.cache.timing_payload_cache.build_count == 0;
}
auto workspace_timing_cache_invalidates_revision() -> bool {
    compose_test_fixture_t cached(workspace_timing_cache_song());
    cached.project();
    auto& document = cached.history.document;
    document.charts.front().bpms[5].beats_per_minute = {999, 1};
    ++document.revision;
    cached.binding.workspace->document_revision = document.revision;
    cached.project();
    compose_test_fixture_t reference(document);
    reference.cache.timing_payload_cache_enabled = false;
    reference.project();
    return cached.cache.timing_payload_cache.build_count == 2 &&
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
    const auto& payloads = cached.cache.timing_payload_cache.markers;
    const auto changed = std::ranges::find(payloads, std::uint64_t{10069},
                                          &compose_timing_marker_payload_t::id);
    return changed != payloads.end() && changed->row == 1920 &&
        cached.cache.timing_payload_cache.build_count == 2 &&
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
    const auto generation = fixture.cache.waveform_row_table.generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform_row_table.generation == generation);
    expect_gpu_work(fixture.cache.waveform_row_table.built_rows == 202);
    fixture.interaction.viewport_scroll_pixels += 1;
    fixture.project();
    expect_gpu_work(fixture.cache.waveform_row_table.built_rows == 1);
    expect_gpu_work(fixture.cache.waveform_row_table.reused_rows == 201);
    const auto scrolled_generation = fixture.cache.waveform_row_table.generation;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform_row_table.generation == scrolled_generation);
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 1);
    expect_gpu_work(fixture.cache.channel_envelope_ring.reused_rows == 201);
    fixture.flattened.view.compose.pixels_per_standard_measure *= 2;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform_row_table.built_rows == 202);
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 202);
}

TEST_CASE("Stem row table retires display rings and native cut derivation when timing identity changes",
          "[sys][waveform-projection][bmson][stem][row-table]") {
    channel_projection_fixture_t fixture;
    fixture.project_channel();
    const auto generation = fixture.cache.waveform_row_table.clock_generation;
    auto& chart = fixture.history.document.charts.front();
    chart.bpms.front().beats_per_minute = {240, 1};
    fixture.cache.marker_timing_index = build_timing_index(chart).index;
    REQUIRE(fixture.cache.marker_timing_index);
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform_row_table.clock_generation == generation + 1);
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == 202);
    expect_gpu_work(fixture.cache.channel_waveform_slices.has_value());
    const auto unchanged = fixture.cache.waveform_row_table.generation;
    const auto projected = fixture.cache.channel_envelope_ring.projected_rows;
    const auto calls = fixture.cache.channel_envelope_ring.projection_calls;
    fixture.project_channel();
    expect_gpu_work(fixture.cache.waveform_row_table.generation == unchanged);
    // A cache hit leaves the last projection counters intact.
    expect_gpu_work(fixture.cache.channel_envelope_ring.projected_rows == projected);
    expect_gpu_work(fixture.cache.channel_envelope_ring.projection_calls == calls);
}
#endif

namespace {
void t475_check(bool value, std::source_location source = std::source_location::current()) {
    INFO("dense projection assertion at " << source.line());
    CHECK(value);
}
void t475_require(const auto& value) { REQUIRE(static_cast<bool>(value)); }
void t475_project(compose_test_fixture_t& fixture) {
    t475_require(rebind_workspace(*fixture.binding.workspace, fixture.history));
    const auto model = make_workspace_view(*fixture.binding.workspace, fixture.history.document);
    t475_require(model.view.has_value());
    fixture.flattened = flatten_workspace(*model.view, fixture.history.document, "", {}, false, {},
        nullptr, dock_sort_t::natural_name, {}, {}, {}, &fixture.cache.keysound_list_cache);
    fixture.flattened.rebind_view_pointers();
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
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
    fresh.binding.workspace->selected_chart = retained.binding.workspace->selected_chart;
    fresh.interaction = retained.interaction;
    fresh.application = retained.application;
    t475_project(fresh); // Establish the source before copying transient pointer state.
    fresh.interaction = retained.interaction;
    fresh.cache.paint_pointer = retained.cache.paint_pointer;
    t475_project(fresh);
    const auto& a = retained.flattened.view.compose;
    const auto& b = fresh.flattened.view.compose;
    t475_check(a.selected_note_label_note_id == b.selected_note_label_note_id);
    t475_check(std::string_view(a.selected_note_label ? a.selected_note_label : "") ==
          std::string_view(b.selected_note_label ? b.selected_note_label : ""));
    t475_check(a.selected_bgm_event_id == b.selected_bgm_event_id);
    t475_check(a.selected_assigned_resource_id == b.selected_assigned_resource_id);
    t475_check(a.selected_bmson_note_event_id == b.selected_bmson_note_event_id);
    t475_check(a.selected_bmson_source_lane_id == b.selected_bmson_source_lane_id);
    std::string labels;
    for (std::size_t i = 0; i < std::min(a.note_endpoint_count, b.note_endpoint_count); ++i) {
        labels += std::string{"note "} + std::to_string(a.note_endpoints[i].note_id) +
            " actual=" + a.note_endpoints[i].sound_label + " reference=" + b.note_endpoints[i].sound_label +
            " detail actual=" + a.note_endpoints[i].sound_detail +
            " reference=" + b.note_endpoints[i].sound_detail + "; ";
    }
    INFO(labels);
    std::string sources = "doc revision=" + std::to_string(retained.history.document.revision) +
        " scene revision=" + std::to_string(retained.cache.index->document_revision);
    for (const auto& note : retained.cache.index->notes)
        sources += " id=" + std::to_string(note.note.value) + " sound=" +
            std::to_string(note.sound.value_or(resource_id_t{}).value);
    if (retained.cache.static_scene_storage) {
        for (const auto& [id, label] : retained.cache.static_scene_storage->compose.sound_labels)
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
    fixture.application.editor_preferences.scroll_directions.fill(downscroll ? 2 : 1);
    t475_project(fixture);
    fixture.interaction->viewport_scroll_pixels = -20; // Keep row zero inside both hit-test directions.
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.downscroll == downscroll);
    const auto lane = view.lanes[0];
    const auto source_y = row * view.pixels_per_standard_measure / 960.0 -
                          fixture.interaction->viewport_scroll_pixels;
    fixture.cache.paint_pointer = std::pair{(lane.bounds.left + lane.bounds.right) / 2,
        downscroll ? view.viewport_height_pixels - source_y : source_y};
    t475_project(fixture);
    INFO("paint target row=" << row << " downscroll=" << downscroll
        << " x=" << fixture.cache.paint_pointer->first << " y=" << fixture.cache.paint_pointer->second
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
        const auto scanned = fixture.cache.selected_source_scanned;
        fixture.interaction->pixels_per_standard_measure = 250;
        fixture.interaction->viewport_scroll_pixels = 15;
        fixture.interaction->cursor = {measure_id_t{2}, 120};
        t475_project(fixture);
        t475_check(fixture.cache.selected_source_scanned == scanned);
        t475_check(fixture.cache.selected_source_reused > 0);
        t475_compare_fresh(fixture);
    }
    fixture.interaction->selected_notes.clear();
    fixture.interaction->selected_objects = {{editor_selection_object_kind_t::bgm_event, object_id_t{700}}};
    t475_project(fixture);
    const auto scanned = fixture.cache.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_bgm_event_id == 700);
    t475_compare_fresh(fixture);
}

TEST_CASE("Dense inspector locations invalidate after label edit delete and undo",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{600});
    t475_require(apply_note_label_edit(fixture.history, fixture.history.document.revision,
        {.chart = chart_id_t{1}, .notes = {object_id_t{600}}, .label = "edited"}));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "edited");
    t475_compare_fresh(fixture);
    t475_require(dispatch_editor_action(fixture.history, *fixture.interaction,
        delete_editor_selection_action_t{}));
    t475_select_note(fixture, object_id_t{600});
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id == 0);
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
    t475_check(replacement.cache.scene_document_identity == &replacement.history.document);
    t475_compare_fresh(replacement);
    auto& charts = replacement.history.document.charts;
    auto chart = charts.front();
    chart.id = chart_id_t{2};
    chart.measures.back().notes.front().label = "other chart";
    charts.push_back(std::move(chart));
    replacement.binding.workspace->selected_chart = chart_id_t{2};
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
        fixture.application.note_paint_mode_active = true;
        for (const auto row : {0U, 240U, 480U, 720U}) {
            const auto scanned = fixture.cache.paint_occupancy_scanned;
            t475_project_at_row(fixture, row, downscroll);
            t475_check(fixture.flattened.view.compose.paint_ghost.paint_erase_preview == (row == 0));
            t475_check(fixture.cache.paint_occupancy_scanned - scanned <= 3);
            t475_compare_fresh(fixture);
        }
    }
}

TEST_CASE("Dense inspector missing locations recover on insertion and read current resource values",
          "[sys][frame-timing][dense-projection]") {
    compose_test_fixture_t fixture{t475_inspector_song()};
    t475_project(fixture);
    t475_select_note(fixture, object_id_t{999});
    const auto scanned = fixture.cache.selected_source_scanned;
    t475_project(fixture);
    t475_check(fixture.cache.selected_source_scanned == scanned);
    t475_check(fixture.flattened.view.compose.selected_note_label_note_id == 0);
    auto inserted = fixture.history.document;
    auto& notes = inserted.charts.front().measures.back().notes;
    notes.insert(notes.begin(), {.id = object_id_t{999}, .position = {measure_id_t{2}, 0},
        .lane = lane_id_t{1}, .label = "inserted"});
    t475_require(commit_document_command(fixture.history, std::move(inserted), "Insert memo target"));
    t475_project(fixture);
    t475_check(std::string_view(fixture.flattened.view.compose.selected_note_label) == "inserted");
    t475_require(apply_resource_assignment(fixture.history, fixture.history.document.revision,
        {.chart = chart_id_t{1}, .resource = resource_id_t{901},
         .objects = {{editor_selection_object_kind_t::note, object_id_t{999}}}}));
    t475_project(fixture);
    t475_check(fixture.flattened.view.compose.selected_assigned_resource_id == 901);
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
        flattened.view.compose.selected_bmson_bgm_event_id = 0;
        interaction.chart = chart.id;
        interaction.pixels_per_standard_measure = interaction.viewport_height_pixels = 200;
        cache.marker_timing_index = build_timing_index(chart).index;
        expect_gpu_work(cache.marker_timing_index.has_value());
    }
    void project_packed(bool ready = true) {
        const std::optional<asset_id_t> absent;
        const std::shared_ptr<const audio::waveform_analysis_result_t> missing;
        const bmson_waveform_projection_context_t context{flattened, history, workspace,
            &interaction, cache, sources, {absent, missing}, {absent, missing}, 1,
            [&](asset_id_t) { return ready ? channel_analysis : nullptr; }};
        populate_bmson_waveform_projection(context);
    }
    auto piece() const -> const WormUiComposeStemWaveformView& {
        const auto views = std::span(flattened.view.compose.stem_waveforms,
                                     flattened.view.compose.stem_waveform_count);
        const auto found = std::ranges::find(views, std::uint64_t{74000},
                                             &WormUiComposeStemWaveformView::stem_key);
        expect_gpu_work(found != views.end());
        return *found;
    }
};
void check_packed_clip_rows(const packed_projection_fixture_t& fixture) {
    const auto& piece = fixture.piece();
    expect_gpu_work(piece.piece_kind == WORM_UI_WAVEFORM_PIECE_PACKED_EVENT);
    expect_gpu_work(piece.source_lane_id == 3 && !piece.pending && piece.row_count > 0);
    const auto downscroll = compose_field_downscroll(fixture.history.document.charts.front());
    for (const auto& row : std::span(piece.rows, piece.row_count)) {
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
    const auto builds = fixture.cache.packed_waveform_index.builds;
    fixture.interaction.viewport_scroll_pixels = 1;
    fixture.project_packed();
    expect_gpu_work(fixture.piece().stem_key == key);
    expect_gpu_work(fixture.cache.packed_waveform_index.builds == builds);
    INFO("Scrolled packed rows: " << fixture.cache.packed_waveforms.front().ring.projected_rows);
    expect_gpu_work(fixture.cache.packed_waveforms.front().ring.projected_rows == 1);
    const auto projected = fixture.cache.packed_waveforms.front().ring.projected_rows;
    const auto calls = fixture.cache.packed_waveforms.front().ring.projection_calls;
    const auto rows = fixture.cache.packed_waveforms.front().rows.data();
    const auto source = fixture.cache.packed_waveforms.front().source;
    fixture.project_packed();
    expect_gpu_work(fixture.cache.packed_waveforms.front().ring.projected_rows == projected);
    expect_gpu_work(fixture.cache.packed_waveforms.front().ring.projection_calls == calls);
    expect_gpu_work(fixture.cache.packed_waveforms.front().rows.data() == rows);
    expect_gpu_work(fixture.cache.packed_waveforms.front().source == source);
    expect_gpu_work(fixture.cache.packed_waveform_index.builds == builds);
}
TEST_CASE("BMSON packed waveform pending band stays inside its event interval",
          "[sys][frame-timing][stem][bmson][packed-waveform]") {
    packed_projection_fixture_t fixture;
    fixture.project_packed(false);
    const auto piece = fixture.piece();
    expect_gpu_work(piece.pending && piece.row_count == 0 && piece.bounded);
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
    fixture.cache.marker_timing_index = build_timing_index(chart).index;
    const auto authored_rows = fixture.cache.marker_timing_index->total_rows;
    fixture.interaction.viewport_scroll_pixels = 220;
    fixture.project_packed();
    const auto& piece = fixture.piece();
    expect_gpu_work(!piece.pending && piece.row_count > 0);
    expect_gpu_work(fixture.cache.packed_waveform_row_table.total_rows > authored_rows);
    expect_gpu_work(fixture.cache.waveform_row_table.total_rows == authored_rows);
    expect_gpu_work(fixture.cache.marker_timing_index->total_rows == authored_rows);
    expect_gpu_work(chart.measures.size() == 1);
    const auto previous_peak = piece.rows[0].absolute_peak[0];
    chart.bpms.front().beats_per_minute = {4800, 1};
    ++fixture.history.document.revision;
    fixture.cache.marker_timing_index = build_timing_index(chart).index;
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
    action.compose_viewport_input_count = 1;
    action.compose_viewport_inputs[0] = {.kind = WORM_UI_COMPOSE_VIEWPORT_INPUT_COARSE_WHEEL,
        .delta_x = delta,
        .cursor_x_pixels = fixture.flattened.view.compose.playable_field_right_pixels + 20,
        .cursor_y_pixels = 300};
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    (void)dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
}
TEST_CASE("Compose stem width changes reproject same revision and preserve packed width",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    double width = 36;
    std::size_t calls{};
    fixture.application.compose_bgm_lane_width = [&](auto, auto, auto) {
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
    expect_compose_test(fixture.cache.bgm_lane_widths_pixels == std::vector<double>{300, 36, 300});
    expect_compose_test(fixture.flattened.view.compose.static_scene_source_generation != generation);
    expect_compose_test(fixture.flattened.view.compose.bgm_lanes[0].width_pixels == 300);
    expect_compose_test(std::string(fixture.flattened.view.compose.bgm_lanes[0].filename) == "音楽-long-stem.ogg");
    expect_compose_test(fixture.history.document.revision == revision && fixture.history.undo.empty());
}
TEST_CASE("Compose BGM dispatcher reaches last widened lane and clamps resized content",
          "[sys][compose][stem-lane-width]") {
    compose_test_fixture_t fixture(resizable_stem_song());
    fixture.application.compose_bgm_lane_width = [](auto, auto, auto) { return 480; };
    fixture.project();
    scroll_resized_bgm(fixture, 10000);
    fixture.project();
    const auto& view = fixture.flattened.view.compose;
    const auto maximum = 996.0 - fixture.cache.preview_scene_request.bgm_pane_width_pixels;
    expect_compose_test(std::abs(fixture.application.bgm_horizontal_scroll_pixels[1] - maximum) < 1e-6);
    expect_compose_test(view.bgm_lanes[view.bgm_lane_count - 1].source_lane_id == 905);
    scroll_resized_bgm(fixture, -10000);
    expect_compose_test(fixture.application.bgm_horizontal_scroll_pixels[1] == 0);
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
        const auto rows = std::span(view.keysounds, view.keysound_count);
        const auto row = std::ranges::find(rows, 100 + index, &WormUiComposeKeysoundRowView::resource_id);
        t475_require(row != rows.end());
        t475_check(row->stem_source_lane_id == (index < 2 ? 600 + index : 0));
    }
}
}
TEST_CASE("BMSON Draw projection keeps duplicate asset stem and packed resource ownership distinct",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.draw_source_kind = 1;
    fixture.application.draw_source_id = 101;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id == 601);
    const auto builds = fixture.cache.draw_stem_sources.build_count;
    fixture.application.draw_source_id = 102;
    t475_project(fixture);
    t463_check_projected_rows(fixture);
    t475_check(fixture.flattened.view.compose.draw_source_stem_lane_id == 0);
    t475_check(fixture.cache.draw_stem_sources.build_count == builds);
}
TEST_CASE("BMSON Draw resource ownership cache invalidates revision and replacement identity",
          "[sys][compose][bmson][stem][draw-source]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    t475_project(fixture);
    const auto builds = fixture.cache.draw_stem_sources.build_count;
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].stem_channel.reset();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Unlink stem"));
    t475_project(fixture);
    t475_check(fixture.cache.draw_stem_sources.build_count == builds + 1);
    t475_check(!fixture.cache.draw_stem_sources.resource_lanes.contains(101));
    compose_test_fixture_t replacement(t463_shared_asset_song());
    replacement.history.document.revision = fixture.history.document.revision;
    replacement.cache = std::move(fixture.cache);
    t475_project(replacement);
    t463_check_projected_rows(replacement);
    t475_check(replacement.cache.draw_stem_sources.build_count == builds + 2);
}

TEST_CASE("BMSON Draw source names remain frame owned after document edits and frame moves",
          "[sys][compose][bmson][stem][draw-source][frame-arena]") {
    compose_test_fixture_t fixture(t463_shared_asset_song());
    fixture.application.draw_source_kind = 1;
    fixture.application.draw_source_id = 101;
    t475_project(fixture);
    const std::string original = fixture.flattened.view.compose.draw_source_stem_lane_name;
    auto retained = std::move(fixture.flattened);
    auto proposed = fixture.history.document;
    proposed.charts.front().bgm_source_lanes[1].name = std::string(256, 'v');
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Rename stem"));
    t475_check(std::string(retained.view.compose.draw_source_stem_lane_name) == original);
    const auto rows = std::span(retained.view.compose.keysounds,
                               retained.view.compose.keysound_count);
    const auto row = std::ranges::find(rows, uint64_t{101}, &WormUiComposeKeysoundRowView::resource_id);
    t475_require(row != rows.end());
    t475_check(std::string(row->stem_lane_name) == original);
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
void project_chip_request(compose_test_fixture_t& fixture, WormUiKeysoundDockRequest request) {
    t475_require(rebind_workspace(*fixture.binding.workspace, fixture.history));
    const auto model = make_workspace_view(*fixture.binding.workspace, fixture.history.document);
    t475_require(model.view.has_value());
    fixture.flattened = flatten_workspace(*model.view, fixture.history.document, "", {}, false, {},
        nullptr, dock_sort_t::natural_name, request, {}, {}, &fixture.cache.keysound_list_cache);
    fixture.flattened.rebind_view_pointers();
    populate_compose_view(fixture.flattened, fixture.history, *fixture.binding.workspace,
        fixture.cache, 1280, 980, fixture.interaction, fixture.application, nullptr);
    t475_require(fixture.flattened.view.compose.scene_available);
}
auto dispatch_chip_input(compose_test_fixture_t& fixture, WormUiComposeInputAction input) -> std::string {
    WormUiWorkspaceAction action{};
    action.compose_input_chart_id = 1;
    action.compose_input_project_id = fixture.history.document.id.value;
    action.compose_input_document_revision = fixture.history.document.revision;
    action.compose_input_count = 1;
    action.compose_inputs[0] = input;
    action.compose_input_viewport_width_pixels = fixture.flattened.view.compose.viewport_width_pixels;
    viewport_input_controller_t viewport;
    editor_navigation_input_controller_t navigation;
    return dispatch_compose_input_action(action, fixture.history, *fixture.binding.workspace,
        *fixture.interaction, viewport, navigation, fixture.application, fixture.cache, 0);
}
}

TEST_CASE("Tool chip projects the actual off-page filtered sound with a retained source cache",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.draw_source_kind = 1;
    fixture.application.draw_source_id = 179;
    WormUiKeysoundDockRequest request{};
    request.include_ungrouped = true;
    request.primary_resource_id = 100;
    std::snprintf(request.automatic_search, sizeof(request.automatic_search), "not this source");
    project_chip_request(fixture, request);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "0A 音楽.wav");
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(!fixture.flattened.view.compose.keysound_projected_filters_unrestricted);
    const auto builds = fixture.cache.draw_stem_sources.build_count;
    request.automatic_search[0] = '\0';
    request.primary_resource_id = 179;
    request.automatic_row_limit = 8;
    project_chip_request(fixture, request);
    const auto& view = fixture.flattened.view.compose;
    t475_check(view.keysound_projected_primary_resource_id == 179);
    t475_check(view.keysound_projected_filters_unrestricted && view.keysound_primary_display_index_valid);
    t475_check(view.keysound_primary_display_index == 80);
    t475_check(view.keysound_display_total_count == 81);
    t475_check(fixture.cache.draw_stem_sources.build_count == builds);
}

TEST_CASE("Tool chip names are bounded UTF-8 and survive a moved frame and source rename",
          "[sys][compose][source-chip][frame-arena]") {
    compose_test_fixture_t fixture(chip_source_song());
    fixture.application.draw_source_kind = 1;
    fixture.application.draw_source_id = 179;
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
    fixture.application.note_paint_mode_active = true;
    fixture.application.draw_source_kind = 1;
    fixture.application.draw_source_id = 179;
    t475_project(fixture);
    auto proposed = fixture.history.document;
    proposed.resources.pop_back();
    t475_require(commit_document_command(fixture.history, std::move(proposed), "Delete chip source"));
    t475_project(fixture);
    t475_check(fixture.application.draw_source_kind == 1 && fixture.application.draw_source_id == 179);
    t475_check(fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "missing source");
    const auto revision = fixture.history.document.revision;
    const auto undos = fixture.history.undo.size();
    const auto attempted = paint_stroke_inputs(fixture, 179);
    const auto rejected = dispatch_chip_input(fixture, attempted.compose_inputs[0]);
    t475_check(rejected.find("paint sound is not an existing sample resource") != std::string::npos);
    t475_check(fixture.history.document.revision == revision && fixture.history.undo.size() == undos);
    t475_check(!fixture.interaction->pending_note_paint_gesture);
    const auto repicked = dispatch_chip_input(fixture, {.kind = WORM_UI_COMPOSE_INPUT_DRAW_SOURCE});
    t475_check(repicked == "Draw source selected.");
    t475_project(fixture);
    t475_check(!fixture.flattened.view.compose.draw_source_missing);
    t475_check(std::string_view(fixture.flattened.view.compose.draw_source_label) == "silent note");
}

TEST_CASE("Tool chip BGA uses resource identity and Layer reveals the off-page target ordinal",
          "[sys][compose][source-chip][bga][semantic-layer]") {
    compose_test_fixture_t fixture(bga_virtual_preview_song());
    fixture.application.draw_source_kind = 2;
    fixture.application.draw_source_id = 501;
    for (std::uint64_t index = 0; index < 100; ++index)
        fixture.history.document.charts.front().semantic_layers.push_back({
            .id = layer_id_t{index + 1}, .name = "Drums " + std::to_string(index)});
    fixture.application.semantic_layer_target = layer_id_t{100};
    t475_project(fixture);
    const auto& view = fixture.flattened.view.compose;
    t475_check(std::string_view(view.draw_source_label) == "501 preview image");
    t475_check(!view.draw_source_missing && view.draw_source_id != fixture.history.document.resources.front().asset.value);
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
        compose.application.edit_snap_enabled = false;
        compose.project();
        expect_compose_test(static_cast<bool>(
            dispatch_editor_action(compose.history, *compose.interaction,
                                   select_editor_notes_action_t{{object_id_t{101}}})));
        compose.project();
        const auto& view = compose.flattened.view.compose;
        const auto endpoints = std::span(view.note_endpoints, view.note_endpoint_count);
        const auto source = std::ranges::find_if(
            endpoints, [](const auto& note) { return note.note_id == 101 && !note.tail; });
        expect_compose_test(source != endpoints.end());
        x = (source->bounds.left + source->bounds.right) * .5;
        pixels_per_row = view.pixels_per_standard_measure / 960;
        (void)dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_BEGIN, 240);
        expect_compose_test(compose.interaction->pending_note_move_gesture.has_value());
    }
    auto dispatch(WormUiComposeInputKind kind, double row) -> std::string {
        WormUiWorkspaceAction action{};
        action.compose_input_project_id = compose.history.document.id.value;
        action.compose_input_chart_id = 1;
        action.compose_input_document_revision = compose.history.document.revision;
        action.compose_input_count = 1;
        const auto& view = compose.flattened.view.compose;
        const auto downscroll = compose.cache.preview_scene_request.scroll_direction ==
            editor_field_scroll_direction_t::down;
        action.compose_inputs[0] = {
            .kind = kind,
            .x_pixels = x,
            .y_pixels = compose_project_y(row + .000001, pixels_per_row,
                compose.interaction->viewport_scroll_pixels, view.viewport_height_pixels,
                downscroll)};
        action.compose_input_viewport_width_pixels =
            compose.flattened.view.compose.viewport_width_pixels;
        viewport_input_controller_t viewport;
        editor_navigation_input_controller_t navigation;
        return dispatch_compose_input_action(action, compose.history, *compose.binding.workspace,
                                             *compose.interaction, viewport, navigation,
                                             compose.application, compose.cache, 0, &sink);
    }
};
void check_projected_move_conflicts(move_feedback_dispatch_fixture_t& fixture) {
    fixture.compose.project();
    const auto& view = fixture.compose.flattened.view.compose;
    expect_compose_test(view.move_conflict_count == 1);
    const auto endpoints = std::span(view.note_endpoints, view.note_endpoint_count);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.proposal_ghost && note.move_conflict != 0;
                        }) == 2);
    expect_compose_test(std::ranges::count_if(endpoints, [](const auto& note) {
                            return note.move_conflict_occupant;
                        }) == 3);
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies, view.hold_body_count),
        [](const auto& hold) { return hold.proposal_ghost && hold.move_conflict != 0; }));
    expect_compose_test(std::ranges::any_of(
        std::span(view.hold_bodies, view.hold_body_count),
        [](const auto& hold) { return hold.note_id == 201 && hold.move_conflict_occupant; }));
}
void check_move_release_warning(move_feedback_dispatch_fixture_t& fixture) {
    fixture.center.drain_inbox(notification_time_t{0});
    const auto messages = fixture.center.snapshot(notification_capacity);
    expect_compose_test(messages.size() == 1);
    expect_compose_test(messages.front().severity == notification_severity_t::warning);
    expect_compose_test(messages.front().message == "1 notes would overlap");
    expect_compose_test(!fixture.compose.interaction->pending_note_move_gesture);
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
    INFO("Actual move target " << fixture.compose.interaction->pending_note_move_gesture->preview_position.measure.value
         << ":" << fixture.compose.interaction->pending_note_move_gesture->preview_position.row
         << " snap=" << fixture.compose.interaction->snap_enabled
         << " scroll=" << fixture.compose.interaction->viewport_scroll_pixels);
    expect_compose_test(fixture.compose.interaction->pending_note_move_gesture->preview_position ==
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
        std::span(view.note_endpoints, view.note_endpoint_count),
        [](const auto& note) { return note.move_conflict || note.move_conflict_occupant; }));
}

TEST_CASE("Compose move release outside actual chart geometry cancels without editing",
          "[sys][compose][move][t420]") {
    move_feedback_dispatch_fixture_t fixture;
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_UPDATE, 120);
    (void)fixture.dispatch(WORM_UI_COMPOSE_INPUT_NOTE_MOVE_RELEASE, 100000);
    expect_compose_test(!fixture.compose.interaction->pending_note_move_gesture);
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
    expect_compose_test(fixture.compose.interaction->pending_note_move_gesture.has_value());
    const auto& pending = *fixture.compose.interaction->pending_note_move_gesture;
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
    expect_compose_test(!fixture.compose.interaction->pending_note_move_gesture);
    expect_compose_test(fixture.compose.history.document.revision == 1);
    expect_compose_test(fixture.compose.history.undo.empty());
}
