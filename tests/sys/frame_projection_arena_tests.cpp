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

TEST_CASE("Timing status text reuses warmed arena bytes without C++ new",
          "[sys][frame-arena][timing-text]") {
    const auto history = arena_timing_history();
    const wormedit::workspace_controller_t workspace;
    timing_scene_cache_t cache;
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 4096});
    auto project = [&] {
        arena.reset();
        flattened_workspace_t frame(&arena);
        fill_arena_timing(frame, history, workspace, cache);
        return frame;
    };
    const auto warm = project();
    const auto used = arena.used_bytes();
    const auto before = frame_heap_allocation_snapshot();
    bool exact = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const auto frame = project();
        exact = exact && std::strcmp(frame.view.timing.status_text,
            "Choose a difficulty to display its timing lanes.") == 0;
    }
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(exact);
    check_frame_storage(after.count == before.count);
    check_frame_storage(arena.used_bytes() == used);
}

TEST_CASE("Standalone Timing owns copied cache diagnostics through source mutation and move",
          "[sys][frame-arena][timing-text]") {
    const auto history = arena_timing_history();
    auto binding = wormedit::make_workspace_controller(history);
    REQUIRE(binding.workspace);
    binding.workspace->selected_chart = wormedit::chart_id_t{1};
    const std::string diagnostic = "A long retained timing diagnostic that must not borrow its source cache storage.";
    timing_scene_cache_t cache;
    cache.chart = wormedit::chart_id_t{1};
    cache.revision = history.document.revision;
    cache.attempted = true;
    cache.status = diagnostic;
    flattened_workspace_t frame;
    fill_arena_timing(frame, history, *binding.workspace, cache);
    cache.status.assign(200, 'x');
    auto moved = std::move(frame);
    check_frame_storage(std::strcmp(moved.view.timing.status_text, diagnostic.c_str()) == 0);
}

TEST_CASE("Timing text exhaustion publishes an immutable diagnostic",
          "[sys][frame-arena][timing-text]") {
    const auto history = arena_timing_history();
    const wormedit::workspace_controller_t workspace;
    timing_scene_cache_t cache;
    frame_arena_t arena({.chunk_bytes = 8, .maximum_bytes = 8});
    flattened_workspace_t frame(&arena);
    fill_arena_timing(frame, history, workspace, cache);
    check_frame_storage(frame.view.timing.truncated);
    check_frame_storage(std::strcmp(frame.view.timing.status_text,
        "Timing display text truncated (frame storage limit).") == 0);
    arena.reset();
    check_frame_storage(std::strcmp(frame.view.timing.status_text,
        "Timing display text truncated (frame storage limit).") == 0);
}

TEST_CASE("Timing recipe owns diagnostic text independently of the preview",
          "[sys][frame-arena][timing-text]") {
    const auto history = arena_timing_history();
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
    const std::string diagnostic = "A long transient recipe diagnostic whose bytes belong to the frame projection.";
    wormedit::sv_recipe_draft_preview_t preview;
    preview.diagnostics.push_back({.message = diagnostic});
    flattened_workspace_t frame;
    frame.view.timing.visible = true;
    populate_timing_view(frame, history, *binding.workspace, cache, 1280, 980, grid,
                         nullptr, &draft, &preview, false);
    preview.diagnostics.front().message.assign(200, 'x');
    auto retained = std::move(frame);
    check_frame_storage(retained.view.timing.sv_recipe.active);
    check_frame_storage(!retained.view.timing.sv_recipe.valid);
    check_frame_storage(std::strcmp(retained.view.timing.sv_recipe.status_text,
                                   diagnostic.c_str()) == 0);
}

namespace {
auto metadata_arena_model(std::size_t tags = 2) -> wormedit::workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::metadata;
    wormedit::workspace_metadata_view_t tab;
    tab.title = "Title";
    tab.artist = "Artist";
    tab.search_tags.assign(tags, "tag");
    tab.selected_difficulty = wormedit::workspace_chart_option_t{
        .id = wormedit::chart_id_t{1}, .difficulty_name = "Hard"};
    tab.format_fields.push_back({.chart_scoped = true,
        .family = wormedit::format_family_t::bms, .first_group_for_family = true,
        .group_index = 2, .field_index = 3, .group_field_count = 4,
        .name = "field-name", .value = "field-value", .source_order = 17});
    model.tab_view = std::move(tab);
    return model;
}
auto metadata_arena_document() -> wormedit::song_document_t {
    wormedit::song_document_t document;
    document.charts.push_back({.id = wormedit::chart_id_t{1}});
    document.charts.front().source_family = wormedit::format_family_t::bms;
    document.metadata.format_groups.push_back({.family = wormedit::format_family_t::bms,
                                               .ordered_fields = {{"song", "value"}}});
    document.charts.front().metadata.format_groups.push_back({
        .family = wormedit::format_family_t::bmson, .ordered_fields = {{"chart", "value"}}});
    return document;
}
auto project_metadata_arena(const wormedit::workspace_view_t& model,
                            const wormedit::song_document_t& document,
                            frame_arena_t* arena = nullptr) -> flattened_workspace_t {
    return flatten_workspace(model, document, "ready", {}, false, {}, arena);
}
void check_metadata_fields(const WormUiMetadataFormatFieldView& field) {
    check_frame_storage(field.chart_scoped);
    check_frame_storage(field.family == static_cast<std::uint8_t>(wormedit::format_family_t::bms));
    check_frame_storage(field.first_group_for_family);
    check_frame_storage(field.group_index == 2);
    check_frame_storage(field.field_index == 3);
    check_frame_storage(field.group_field_count == 4);
    check_frame_storage(std::strcmp(field.name, "field-name") == 0);
    check_frame_storage(std::strcmp(field.value, "field-value") == 0);
    check_frame_storage(field.name_bytes == 10);
    check_frame_storage(field.value_bytes == 11);
    check_frame_storage(field.has_source_order && field.source_order == 17);
}
}

TEST_CASE("Metadata arena lists own source text and preserve field and target identities",
          "[sys][frame-arena][metadata]") {
    auto model = metadata_arena_model();
    const auto document = metadata_arena_document();
    auto frame = project_metadata_arena(model, document);
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.search_tags[0].assign(100, 'x');
    tab.format_fields[0].name.assign(100, 'n');
    tab.format_fields[0].value.assign(100, 'v');
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& view = moved.view.metadata;
    REQUIRE(view.search_tags_editable);
    REQUIRE(view.search_tag_count == 2);
    check_frame_storage(std::strcmp(view.search_tags[0], "tag") == 0);
    check_frame_storage(view.search_tag_bytes[0] == 3);
    REQUIRE(view.format_field_count == 1);
    check_metadata_fields(view.format_fields[0]);
    REQUIRE(view.format_target_count == 14);
    check_frame_storage(view.format_targets[1].family == static_cast<std::uint8_t>(wormedit::format_family_t::bms));
    check_frame_storage(view.format_targets[1].group_exists);
    check_frame_storage(!view.format_targets[1].chart_scoped);
    check_frame_storage(view.format_targets[11].group_exists);
    check_frame_storage(view.format_targets[11].chart_scoped);
}

TEST_CASE("Metadata tags retain exact count and byte rejection boundaries",
          "[sys][frame-arena][metadata]") {
    const auto document = metadata_arena_document();
    auto model = metadata_arena_model(WORM_UI_METADATA_SEARCH_TAG_CAPACITY);
    auto accepted = project_metadata_arena(model, document);
    check_frame_storage(accepted.view.metadata.search_tags_editable);
    check_frame_storage(accepted.view.metadata.search_tag_count == WORM_UI_METADATA_SEARCH_TAG_CAPACITY);
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.search_tags.push_back("overflow");
    auto rejected_count = project_metadata_arena(model, document);
    check_frame_storage(!rejected_count.view.metadata.search_tags_editable);
    check_frame_storage(!rejected_count.view.metadata.song_fields_editable);
    check_frame_storage(rejected_count.view.metadata.search_tags == nullptr);
    check_frame_storage(rejected_count.view.metadata.search_tag_count == WORM_UI_METADATA_SEARCH_TAG_CAPACITY + 1);
    tab.search_tags = {std::string(WORM_UI_METADATA_SEARCH_TAG_BYTES, 'a')};
    auto accepted_bytes = project_metadata_arena(model, document);
    check_frame_storage(accepted_bytes.view.metadata.search_tags_editable);
    check_frame_storage(accepted_bytes.view.metadata.search_tag_bytes[0] == WORM_UI_METADATA_SEARCH_TAG_BYTES);
    tab.search_tags.front().push_back('a');
    auto rejected_bytes = project_metadata_arena(model, document);
    check_frame_storage(!rejected_bytes.view.metadata.search_tags_editable);
    check_frame_storage(rejected_bytes.view.metadata.search_tags == nullptr);
}

TEST_CASE("Metadata arena exhaustion publishes only complete fields and no partial tag list",
          "[sys][frame-arena][metadata]") {
    auto model = metadata_arena_model();
    const auto document = metadata_arena_document();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    auto rejected = project_metadata_arena(model, document, &tiny);
    check_frame_storage(!rejected.view.metadata.search_tags_editable);
    check_frame_storage(rejected.view.metadata.search_tags == nullptr);
    check_frame_storage(rejected.view.metadata.search_tag_bytes == nullptr);
    check_frame_storage(rejected.view.metadata.format_fields_truncated);
    check_frame_storage(rejected.view.metadata.format_field_count == 0);
    check_frame_storage(rejected.view.metadata.selected_chart_source_family ==
        static_cast<std::uint8_t>(wormedit::format_family_t::bms));
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.search_tags.clear();
    auto second = tab.format_fields.front();
    second.value.assign(1024, 'v');
    tab.format_fields.push_back(std::move(second));
    frame_arena_t limited({.chunk_bytes = 512, .maximum_bytes = 512});
    auto prefix = project_metadata_arena(model, document, &limited);
    REQUIRE(prefix.view.metadata.format_field_count == 1);
    check_frame_storage(prefix.view.metadata.format_field_total_count == 2);
    check_frame_storage(prefix.view.metadata.format_fields_truncated);
    check_metadata_fields(prefix.view.metadata.format_fields[0]);
}

TEST_CASE("Metadata fields retain 128-entry limit and full source total",
          "[sys][frame-arena][metadata]") {
    auto model = metadata_arena_model();
    const auto document = metadata_arena_document();
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.format_fields.resize(WORM_UI_METADATA_FORMAT_FIELD_CAPACITY, tab.format_fields.front());
    auto accepted = project_metadata_arena(model, document);
    check_frame_storage(!accepted.view.metadata.format_fields_truncated);
    check_frame_storage(accepted.view.metadata.format_field_count == WORM_UI_METADATA_FORMAT_FIELD_CAPACITY);
    tab.format_fields.push_back(tab.format_fields.front());
    auto capped = project_metadata_arena(model, document);
    check_frame_storage(capped.view.metadata.format_fields_truncated);
    check_frame_storage(capped.view.metadata.format_field_count == WORM_UI_METADATA_FORMAT_FIELD_CAPACITY);
    check_frame_storage(capped.view.metadata.format_field_total_count == WORM_UI_METADATA_FORMAT_FIELD_CAPACITY + 1);
    check_metadata_fields(capped.view.metadata.format_fields[WORM_UI_METADATA_FORMAT_FIELD_CAPACITY - 1]);
}

TEST_CASE("Metadata arena list storage stays bounded through 1000 dense resets",
          "[sys][frame-arena][metadata]") {
    const auto document = metadata_arena_document();
    const auto smaller = metadata_arena_model(1000);
    const auto dense = metadata_arena_model(WORM_UI_METADATA_SEARCH_TAG_CAPACITY);
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    auto project = [&](const auto& model) {
        arena.reset();
        return project_metadata_arena(model, document, &arena);
    };
    (void)project(dense);
    const auto before_small = frame_heap_allocation_snapshot();
    (void)project(smaller);
    const auto small_count = frame_heap_allocation_snapshot().count - before_small.count;
    (void)project(dense);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    bool exact = true;
    bool stable = true;
    bool allocation_count_independent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        const auto before = frame_heap_allocation_snapshot();
        const auto frame = project(dense);
        const auto after = frame_heap_allocation_snapshot();
        exact = exact && frame.view.metadata.search_tags_editable &&
            frame.view.metadata.search_tag_count == WORM_UI_METADATA_SEARCH_TAG_CAPACITY;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed;
        allocation_count_independent = allocation_count_independent && after.count - before.count == small_count;
    }
    check_frame_storage(exact);
    check_frame_storage(stable);
    check_frame_storage(allocation_count_independent);
    INFO("Metadata projection still allocates " << small_count << " outside the converted lists");
}
