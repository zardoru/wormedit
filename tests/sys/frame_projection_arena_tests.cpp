#include "sys/frame_timing.h"
#include "sys/workspace_projection.h"
#include "sys/timing_view.h"
#include <cstring>
#include <limits>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <new>
#include <source_location>

using namespace wormedit::sys;
using wormedit::frame_arena_t;

TEST_CASE("Production debug heap hook counts scalar array aligned and nothrow new",
          "[sys][frame-arena][allocation]") {
#ifndef NDEBUG
    const auto before = frame_heap_allocation_snapshot();
    auto* scalar = ::operator new(17);
    auto* array = ::operator new[](31);
    auto* aligned = ::operator new(128, std::align_val_t{64});
    auto* optional = ::operator new(13, std::nothrow);
    auto* aligned_optional = ::operator new[](64, std::align_val_t{64}, std::nothrow);
    const auto after = frame_heap_allocation_snapshot();
    const auto address = reinterpret_cast<std::uintptr_t>(aligned);
    const auto optional_address = reinterpret_cast<std::uintptr_t>(aligned_optional);
    ::operator delete(scalar);
    ::operator delete[](array);
    ::operator delete(aligned, std::align_val_t{64});
    ::operator delete(optional, std::nothrow);
    ::operator delete[](aligned_optional, std::align_val_t{64}, std::nothrow);
    CHECK(after.count - before.count == 5);
    CHECK(after.bytes - before.bytes == 253);
    CHECK(address % 64 == 0);
    CHECK(optional_address % 64 == 0);
#else
    CHECK(frame_heap_allocation_snapshot().count == 0);
#endif
}

namespace {
template<class Condition>
void check_frame_storage(const Condition& condition,
                         std::source_location source = std::source_location::current()) {
    INFO("Frame storage expectation at line " << source.line());
    CHECK(condition);
}
}

TEST_CASE("Frame storage arrays retain fill order and reject capacity overflow",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 4096});
    auto rows = arena_array<std::uint64_t>(arena, 2);
    REQUIRE(rows.capacity() == 2);
    check_frame_storage(rows.push_back(7));
    check_frame_storage(rows.push_back(11));
    check_frame_storage(!(rows.push_back(13)));
    check_frame_storage(rows.size() == 2);
    check_frame_storage(rows[0] == 7);
    check_frame_storage(rows[1] == 11);
    check_frame_storage(rows.end() - rows.begin() == 2);
    rows.clear();
    check_frame_storage(rows.push_back(19));
    check_frame_storage(rows[0] == 19);
    check_frame_storage(arena_array<std::uint64_t>(arena, std::numeric_limits<std::size_t>::max()).empty());
}

TEST_CASE("Arena strings own formatted bytes and fail without partial publication",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 64, .maximum_bytes = 128});
    const auto* copied = arena_string(arena, "plain");
    const auto* formatted = arena_format(arena, "row %d:%.2f", 7, 1.25);
    REQUIRE(copied);
    REQUIRE(formatted);
    check_frame_storage(std::strcmp(copied, "plain") == 0);
    check_frame_storage(std::strcmp(formatted, "row 7:1.25") == 0);
    check_frame_storage(arena_format(arena, nullptr) == nullptr);
    check_frame_storage(arena_array<std::uint64_t>(arena, 128).capacity() == 0);
    check_frame_storage(std::strcmp(copied, "plain") == 0);
}

TEST_CASE("Warmed frame storage reuses chunks with stable bytes and no heap allocation",
          "[sys][frame-arena]") {
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    auto fill = [&] {
        arena.reset();
        auto rows = arena_array<std::uint64_t>(arena, 32);
        for (std::uint64_t row = 0; row < 32; ++row) (void)rows.push_back(row);
        (void)arena_format(arena, "position %d", 42);
        return arena.used_bytes();
    };
    const auto used = fill();
    const auto committed = arena.committed_bytes();
    const auto before = frame_heap_allocation_snapshot();
    bool stable = true;
    for (int frame = 0; frame < 1000; ++frame)
        stable = stable && fill() == used && arena.committed_bytes() == committed;
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(stable);
    check_frame_storage(after.count == before.count);
}

namespace {
auto frame_summary_model() -> wormedit::workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{
        .chart = wormedit::chart_id_t{1}, .measure_count = 1042, .note_count = 50000,
        .layout_segment_count = 1, .lane_count = 4, .layer_count = 0};
    return model;
}
}

TEST_CASE("Workspace summary uses the application arena with stable contents and zero warmed new",
          "[sys][frame-arena][projection]") {
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto model = frame_summary_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    auto fill = [&] {
        arena.reset();
        return flatten_workspace(model, document, status, {}, false, {}, &arena);
    };
    const auto first = fill();
    const auto used = arena.used_bytes();
    const auto before = frame_heap_allocation_snapshot();
    auto second = fill();
    second.rebind_view_pointers();
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(arena.used_bytes() == used);
    check_frame_storage(second.view.summary_line_count == 5);
    check_frame_storage(std::strcmp(second.view.summary_lines[0], "Measures: 1042") == 0);
    check_frame_storage(std::strcmp(second.view.summary_lines[1], "Notes: 50000") == 0);
    check_frame_storage(std::strcmp(second.view.summary_lines[3], "Visible lane definitions: 4") == 0);
    check_frame_storage(first.summary.lines[0] == second.summary.lines[0]);
}

TEST_CASE("Standalone workspace summary owns persistent arena storage after a move",
          "[sys][frame-arena][projection]") {
    const auto model = frame_summary_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    auto projection = flatten_workspace(model, document, status, {}, false, {});
    auto retained = std::move(projection);
    retained.rebind_view_pointers();
    check_frame_storage(std::strcmp(retained.view.summary_lines[1], "Notes: 50000") == 0);
    frame_arena_t frame({.chunk_bytes = 64, .maximum_bytes = 64});
    const auto limited = flatten_workspace(model, document, status, {}, false, {}, &frame);
    check_frame_storage(limited.summary.truncated);
    check_frame_storage(limited.view.summary_line_count < 5);
    check_frame_storage(std::strcmp(retained.view.summary_lines[1], "Notes: 50000") == 0);
}

TEST_CASE("Audio asset view publishes arena IDs and truncates only its exhausted unit",
          "[sys][frame-arena][projection]") {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    model.tab_view = wormedit::workspace_audio_slices_view_t{
        .audio_assets = {wormedit::asset_id_t{7}, wormedit::asset_id_t{9}}};
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 4096});
    auto projection = flatten_workspace(model, document, status, {}, false, {}, &arena);
    projection.rebind_view_pointers();
    REQUIRE(projection.view.audio_slices.asset_ids);
    check_frame_storage(projection.view.audio_slices.asset_count == 2);
    check_frame_storage(projection.view.audio_slices.asset_ids[0] == 7);
    check_frame_storage(projection.view.audio_slices.asset_ids[1] == 9);
    frame_arena_t exhausted({.chunk_bytes = 8, .maximum_bytes = 8});
    const auto limited = flatten_workspace(model, document, status, {}, false, {}, &exhausted);
    check_frame_storage(limited.view.audio_slices.assets_truncated);
    check_frame_storage(limited.view.audio_slices.asset_count == 0);
    check_frame_storage(limited.view.audio_slices.asset_ids == nullptr);
    check_frame_storage(limited.view.audio_slices.visible);
}

namespace {
auto arena_timing_history() -> wormedit::document_history_t {
    wormedit::song_document_t song;
    song.id = wormedit::song_id_t{1};
    song.revision = 1;
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.lane_layouts = {{.id = wormedit::object_id_t{1},
        .position = {wormedit::measure_id_t{1}, 0},
        .layout = {.profile_id = "arena-timing", .display_name = "Arena Timing",
                   .lanes = {{.id = wormedit::lane_id_t{1}, .name = "Key",
                              .role = wormedit::lane_role_t::playable}}}}};
    chart.measures.push_back({.id = wormedit::measure_id_t{1}});
    chart.bpms = {{.id = wormedit::object_id_t{2},
                   .position = {wormedit::measure_id_t{1}, 0}, .beats_per_minute = {120, 1}},
                  {.id = wormedit::object_id_t{3},
                   .position = {wormedit::measure_id_t{1}, 480}, .beats_per_minute = {150, 1}}};
    chart.speeds.push_back({.id = wormedit::object_id_t{4},
                           .position = {wormedit::measure_id_t{1}, 0},
                           .multiplier = {2, 1}, .transition_beats = {1, 1}});
    song.charts.push_back(std::move(chart));
    return wormedit::make_document_history(std::move(song));
}
void fill_arena_timing(flattened_workspace_t& projection,
                       const wormedit::document_history_t& history,
                       const wormedit::workspace_controller_t& workspace,
                       timing_scene_cache_t& cache) {
    projection.view.timing.visible = true;
    timing_grid_state_t grid;
    grid.active_denominator = 4;
    populate_timing_view(projection, history, workspace, cache, 1280, 980, grid,
                         nullptr, nullptr, nullptr, false);
}
void check_timing_projection(const WormUiTimingView& actual, const WormUiTimingView& expected) {
    check_frame_storage(actual.scene_available == expected.scene_available);
    check_frame_storage(actual.grid_line_count == expected.grid_line_count);
    check_frame_storage(actual.marker_count == expected.marker_count);
    check_frame_storage(actual.speed_count == expected.speed_count);
    for (std::size_t index = 0; index < actual.speed_count; ++index) {
        check_frame_storage(actual.speeds[index].object_id == expected.speeds[index].object_id);
        check_frame_storage(actual.speeds[index].multiplier == expected.speeds[index].multiplier);
        check_frame_storage(actual.speeds[index].affected_end_y_pixels == expected.speeds[index].affected_end_y_pixels);
    }
    for (std::size_t index = 0; index < actual.marker_count; ++index) {
        check_frame_storage(actual.markers[index].object_id == expected.markers[index].object_id);
        check_frame_storage(actual.markers[index].marker_y_pixels == expected.markers[index].marker_y_pixels);
        check_frame_storage(actual.markers[index].marker_left_fraction == expected.markers[index].marker_left_fraction);
        check_frame_storage(actual.markers[index].marker_right_fraction == expected.markers[index].marker_right_fraction);
    }
    for (std::size_t index = 0; index < actual.grid_line_count; ++index) {
        check_frame_storage(actual.grid_lines[index].y_pixels == expected.grid_lines[index].y_pixels);
        check_frame_storage(actual.grid_lines[index].kind == expected.grid_lines[index].kind);
    }
}
}
TEST_CASE("Timing arena projection preserves markers and grid through 1000 resets",
          "[sys][frame-arena][timing]") {
    auto history = arena_timing_history();
    auto binding = wormedit::make_workspace_controller(history);
    REQUIRE(binding.workspace);
    binding.workspace->selected_chart = wormedit::chart_id_t{1};
    timing_scene_cache_t cache;
    flattened_workspace_t owned;
    fill_arena_timing(owned, history, *binding.workspace, cache);
    REQUIRE(owned.view.timing.scene_available);
    REQUIRE(owned.view.timing.marker_count == 2);
    REQUIRE(owned.view.timing.speed_count == 1);
    REQUIRE(owned.view.timing.speeds[0].multiplier == 2.0);
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    std::size_t used = 0;
    std::size_t committed = 0;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        flattened_workspace_t frame(&arena);
        fill_arena_timing(frame, history, *binding.workspace, cache);
        check_timing_projection(frame.view.timing, owned.view.timing);
        if (iteration == 0) { used = arena.used_bytes(); committed = arena.committed_bytes(); }
        check_frame_storage(arena.used_bytes() == used);
        check_frame_storage(arena.committed_bytes() == committed);
    }
}
TEST_CASE("Timing arena exhaustion publishes no partial geometry",
          "[sys][frame-arena][timing]") {
    auto history = arena_timing_history();
    auto binding = wormedit::make_workspace_controller(history);
    REQUIRE(binding.workspace);
    binding.workspace->selected_chart = wormedit::chart_id_t{1};
    timing_scene_cache_t cache;
    frame_arena_t arena({.chunk_bytes = 8, .maximum_bytes = 8});
    flattened_workspace_t frame(&arena);
    fill_arena_timing(frame, history, *binding.workspace, cache);
    check_frame_storage(frame.view.timing.truncated);
    check_frame_storage(!frame.view.timing.scene_available);
    check_frame_storage(frame.view.timing.grid_line_count == 0);
    check_frame_storage(frame.view.timing.grid_lines == nullptr);
    check_frame_storage(frame.view.timing.marker_count == 0);
    check_frame_storage(frame.view.timing.speed_count == 0);
    check_frame_storage(frame.view.timing.status_text != nullptr);
}

TEST_CASE("Timing recipe arena arrays publish exact normalized candidate and output points",
          "[sys][frame-arena][timing]") {
    auto history = arena_timing_history();
    auto binding = wormedit::make_workspace_controller(history);
    REQUIRE(binding.workspace);
    binding.workspace->selected_chart = wormedit::chart_id_t{1};
    timing_scene_cache_t cache;
    timing_grid_state_t grid;
    grid.active_denominator = 4;
    wormedit::sv_recipe_draft_t draft;
    draft.status = wormedit::sv_recipe_draft_status_t::editing;
    draft.chart = wormedit::chart_id_t{1};
    draft.document_revision = history.document.revision;
    draft.candidate.range = {{wormedit::measure_id_t{1}, 0}, {wormedit::measure_id_t{1}, 960}};
    wormedit::sv_recipe_draft_preview_t preview;
    preview.candidate_points.points = {{.absolute_row = 0, .value = 1},
                                       {.absolute_row = 480, .value = 3}};
    preview.effective_output.points = {{.absolute_row = 0, .value = 2},
                                       {.absolute_row = 960, .value = 5}};
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    flattened_workspace_t frame(&arena);
    frame.view.timing.visible = true;
    populate_timing_view(frame, history, *binding.workspace, cache, 1280, 980, grid,
                         nullptr, &draft, &preview, false);
    const auto& recipe = frame.view.timing.sv_recipe;
    REQUIRE(recipe.candidate_point_count == 2);
    REQUIRE(recipe.composed_point_count == 2);
    INFO("total rows=" << frame.view.timing.total_rows << " candidate rows="
         << recipe.candidate_points[0].row_fraction << "," << recipe.candidate_points[1].row_fraction
         << " output rows=" << recipe.composed_points[0].row_fraction << ","
         << recipe.composed_points[1].row_fraction);
    check_frame_storage(recipe.candidate_points[0].row_fraction == 0.0);
    check_frame_storage(recipe.candidate_points[1].row_fraction == 0.5);
    check_frame_storage(recipe.candidate_points[1].value_fraction == 0.6);
    check_frame_storage(recipe.composed_points[0].value_fraction == 0.4);
    check_frame_storage(recipe.composed_points[1].row_fraction == 1.0);
    check_frame_storage(recipe.composed_points[1].value_fraction == 1.0);
}
