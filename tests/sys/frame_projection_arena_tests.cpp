#include "sys/catalog_view.h"
#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/catalogs.h>
#include <wormedit/formats/bms.h>
#include "sys/frame_timing.h"
#include "sys/notification_history_runtime.h"
#include "sys/workspace_view.h"
#include <wormedit/core/lane_profiles.h>
#include <wormedit/audio/playback_rate.h>
#include "sys/timing_view.h"
#include "sys/metadata_view.h"
#include <array>
#include <cstring>
#include <limits>
#include <memory>
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

TEST_CASE("Workspace summary retains owned text and zero warmed allocations",
          "[sys][frame-arena][projection]") {
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto model = frame_summary_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frontend_workspace_view_t second(&arena);
    second.refill(model, document, status, {}, false, &arena);
    const auto used = arena.used_bytes();
    const auto* first_text = second.view.summary_lines[0].data();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    second.refill(model, document, status, {}, false, &arena);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(arena.used_bytes() == used);
    check_frame_storage(second.view.summary_lines.size() == 5);
    check_frame_storage(std::strcmp(second.view.summary_lines[0].c_str(), "Measures: 1042") == 0);
    check_frame_storage(std::strcmp(second.view.summary_lines[1].c_str(), "Notes: 50000") == 0);
    check_frame_storage(std::strcmp(second.view.summary_lines[3].c_str(), "Visible lane definitions: 4") == 0);
    check_frame_storage(first_text == second.view.summary_lines[0].data());
}

TEST_CASE("Persistent workspace refill retains converted capacity without warmed allocations",
          "[sys][projection][steady-state]") {
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto model = frame_summary_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frontend_workspace_view_t projection(&arena);
    projection.refill(model, document, status, {}, false, &arena);
    projection.view.compose.measure_lane.selected_bar_lines.reserve(8);
    projection.view.compose.measure_lane.selected_bar_lines.push_back(wormedit::measure_id_t{5});
    const auto capacity = projection.view.compose.measure_lane.selected_bar_lines.capacity();
    projection.view.compare.visible = true;
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    projection.refill(model, document, status, {}, false, &arena);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(projection.view.compose.measure_lane.selected_bar_lines.capacity() == capacity);
    check_frame_storage(projection.view.compose.measure_lane.selected_bar_lines.empty());
    check_frame_storage(!projection.view.compare.visible);
    check_frame_storage(projection.view.summary_lines.size() == 5);
}

TEST_CASE("Standalone workspace summary owns text independently of arena storage after a move",
          "[sys][frame-arena][projection]") {
    const auto model = frame_summary_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    auto projection = frontend_workspace_view_t::from(model, document, status, {}, false);
    auto retained = std::move(projection);
    retained.rebind_view_pointers();
    check_frame_storage(std::strcmp(retained.view.summary_lines[1].c_str(), "Notes: 50000") == 0);
    frame_arena_t frame({.chunk_bytes = 64, .maximum_bytes = 64});
    const auto limited = frontend_workspace_view_t::from(model, document, status, {}, false, &frame);
    check_frame_storage(limited.view.summary_lines.size() == 5);
    check_frame_storage(std::strcmp(retained.view.summary_lines[1].c_str(), "Notes: 50000") == 0);
}

TEST_CASE("Audio asset owned IDs remain complete independently of frame arena exhaustion",
          "[sys][frame-arena][projection]") {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    model.tab_view = wormedit::workspace_audio_slices_view_t{
        .audio_assets = {wormedit::asset_id_t{7}, wormedit::asset_id_t{9}}};
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 4096});
    auto projection = frontend_workspace_view_t::from(model, document, status, {}, false, &arena);
    projection.rebind_view_pointers();
    REQUIRE(!projection.view.audio_slices.asset_ids.empty());
    check_frame_storage(projection.view.audio_slices.asset_ids.size() == 2);
    check_frame_storage(projection.view.audio_slices.asset_ids[0] == wormedit::asset_id_t{7});
    check_frame_storage(projection.view.audio_slices.asset_ids[1] == wormedit::asset_id_t{9});
    frame_arena_t exhausted({.chunk_bytes = 8, .maximum_bytes = 8});
    const auto limited = frontend_workspace_view_t::from(model, document, status, {}, false, &exhausted);
    check_frame_storage(limited.view.audio_slices.asset_ids.size() == 2);
    check_frame_storage(limited.view.audio_slices.visible);
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
                            frame_arena_t* arena = nullptr) -> frontend_workspace_view_t {
    return frontend_workspace_view_t::from(model, document, "ready", {}, false, arena);
}
void check_metadata_fields(const wormedit::ui::metadata_format_field_view_t& field) {
    check_frame_storage(field.chart_scoped);
    check_frame_storage(field.family == wormedit::format_family_t::bms);
    check_frame_storage(field.first_group_for_family);
    check_frame_storage(field.group_index == 2);
    check_frame_storage(field.field_index == 3);
    check_frame_storage(field.group_field_count == 4);
    check_frame_storage(std::strcmp(field.name.c_str(), "field-name") == 0);
    check_frame_storage(std::strcmp(field.value.c_str(), "field-value") == 0);
    check_frame_storage(field.name.size() == 10);
    check_frame_storage(field.value.size() == 11);
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
    REQUIRE(view.search_tags.size() == 2);
    check_frame_storage(std::strcmp(view.search_tags[0].c_str(), "tag") == 0);
    check_frame_storage(view.search_tags[0].size() == 3);
    REQUIRE(view.format_fields.size() == 1);
    check_metadata_fields(view.format_fields[0]);
    REQUIRE(view.format_targets.size() == 14);
    check_frame_storage(view.format_targets[1].family == wormedit::format_family_t::bms);
    check_frame_storage(view.format_targets[1].group_exists);
    check_frame_storage(!view.format_targets[1].chart_scoped);
    check_frame_storage(view.format_targets[11].group_exists);
    check_frame_storage(view.format_targets[11].chart_scoped);
}

TEST_CASE("Metadata tags retain exact count and byte rejection boundaries",
          "[sys][frame-arena][metadata]") {
    const auto document = metadata_arena_document();
    auto model = metadata_arena_model(wormedit::default_metadata_edit_limits.maximum_search_tags);
    auto accepted = project_metadata_arena(model, document);
    check_frame_storage(accepted.view.metadata.search_tags_editable);
    check_frame_storage(accepted.view.metadata.search_tags.size() == wormedit::default_metadata_edit_limits.maximum_search_tags);
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.search_tags.push_back("overflow");
    auto rejected_count = project_metadata_arena(model, document);
    check_frame_storage(!rejected_count.view.metadata.search_tags_editable);
    check_frame_storage(!rejected_count.view.metadata.song_fields_editable);
    check_frame_storage(rejected_count.view.metadata.search_tags.size() == wormedit::default_metadata_edit_limits.maximum_search_tags + 1);
    tab.search_tags = {std::string(wormedit::default_metadata_edit_limits.maximum_search_tag_bytes, 'a')};
    auto accepted_bytes = project_metadata_arena(model, document);
    check_frame_storage(accepted_bytes.view.metadata.search_tags_editable);
    check_frame_storage(accepted_bytes.view.metadata.search_tags[0].size() == wormedit::default_metadata_edit_limits.maximum_search_tag_bytes);
    tab.search_tags.front().push_back('a');
    auto rejected_bytes = project_metadata_arena(model, document);
    check_frame_storage(!rejected_bytes.view.metadata.search_tags_editable);

}

TEST_CASE("Metadata owned lists remain complete independently of frame arena exhaustion",
          "[sys][frame-arena][metadata]") {
    auto model = metadata_arena_model();
    const auto document = metadata_arena_document();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    auto rejected = project_metadata_arena(model, document, &tiny);
    check_frame_storage(rejected.view.metadata.search_tags_editable);


    check_frame_storage(rejected.view.metadata.format_fields.size() == 1);
    check_frame_storage(rejected.view.metadata.selected_chart_source_family ==
        wormedit::format_family_t::bms);
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.search_tags.clear();
    auto second = tab.format_fields.front();
    second.value.assign(1024, 'v');
    tab.format_fields.push_back(std::move(second));
    frame_arena_t limited({.chunk_bytes = 512, .maximum_bytes = 512});
    auto prefix = project_metadata_arena(model, document, &limited);
    REQUIRE(prefix.view.metadata.format_fields.size() == 2);
    check_frame_storage(prefix.view.metadata.format_field_total_count == 2);

    check_metadata_fields(prefix.view.metadata.format_fields[0]);
}

TEST_CASE("Metadata fields include rows beyond the former storage cap",
          "[sys][frame-arena][metadata]") {
    auto model = metadata_arena_model();
    const auto document = metadata_arena_document();
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.format_fields.resize(128U, tab.format_fields.front());
    auto accepted = project_metadata_arena(model, document);

    check_frame_storage(accepted.view.metadata.format_fields.size() == 128U);
    tab.format_fields.push_back(tab.format_fields.front());
    auto capped = project_metadata_arena(model, document);

    check_frame_storage(capped.view.metadata.format_fields.size() == 129U);
    check_frame_storage(capped.view.metadata.format_field_total_count == 128U + 1);
    check_metadata_fields(capped.view.metadata.format_fields[128U - 1]);
}

TEST_CASE("Metadata owned strings retain capacity across warmed refills",
          "[sys][metadata][steady-state]") {
    auto model = metadata_arena_model();
    auto& tab = std::get<wormedit::workspace_metadata_view_t>(model.tab_view);
    tab.title.assign(80, 't');
    tab.search_tags[0].assign(300, 's');
    tab.format_fields[0].name.assign(80, 'n');
    tab.format_fields[0].value.assign(300, 'v');
    const auto document = metadata_arena_document();
    const std::string status = "ready";
    frame_arena_t arena;
    frontend_workspace_view_t frame(&arena);
    frame.refill(model, document, status, {}, false, &arena);
    const auto* tag = frame.view.metadata.search_tags[0].data();
    const auto* value = frame.view.metadata.format_fields[0].value.data();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    frame.refill(model, document, status, {}, false, &arena);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(frame.view.metadata.search_tags[0] == tab.search_tags[0]);
    check_frame_storage(frame.view.metadata.search_tags[0].data() == tag);
    check_frame_storage(frame.view.metadata.format_fields[0].value.data() == value);
}

TEST_CASE("Metadata arena list storage stays bounded through 1000 dense resets",
          "[sys][frame-arena][metadata]") {
    const auto document = metadata_arena_document();
    const auto smaller = metadata_arena_model(1000);
    const auto dense = metadata_arena_model(wormedit::default_metadata_edit_limits.maximum_search_tags);
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
            frame.view.metadata.search_tags.size() == wormedit::default_metadata_edit_limits.maximum_search_tags;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed;
        allocation_count_independent = allocation_count_independent && after.count - before.count == small_count;
    }
    check_frame_storage(exact);
    check_frame_storage(stable);
    check_frame_storage(allocation_count_independent);
    INFO("Metadata projection still allocates " << small_count << " outside the converted lists");
}

namespace {
struct metadata_text_fixture_t {
    wormedit::document_history_t history;
    wormedit::workspace_controller_t workspace;
    metadata_drop_state_t drop;
    runtime::metadata_asset_t runtime;
    std::filesystem::path base = "/tmp/metadata-arena-project-directory";
    metadata_text_fixture_t() {
        history.document.id = wormedit::song_id_t{1};
        history.document.charts.push_back({.id = wormedit::chart_id_t{1}});
        history.document.audio_assets.push_back({.id = wormedit::asset_id_t{7},
            .source_path = "audio/a-long-selected-music-source.wav"});
        history.document.output_groups.push_back({.name = "Long retained output group name",
            .charts = {wormedit::chart_id_t{1}}, .relative_output_path = "exports/chart.bms"});
        workspace.active_tab = wormedit::workspace_tab_t::metadata;
        workspace.selected_chart = wormedit::chart_id_t{1};
        workspace.transport.audio_asset = wormedit::asset_id_t{7};
        drop.project_id = history.document.id.value;
        drop.chart_id = 1;
        drop.music_revision = history.document.revision;
        drop.music_path = "/tmp/a-long-dropped-audio-source.wav";
        drop.output_path = "exports/a-long-dropped-output.bms";
    }
    auto project(frame_arena_t* arena = nullptr) -> frontend_workspace_view_t {
        frontend_workspace_view_t frame(arena);
        frame.view.metadata.visible = true;
        frame.view.metadata.chart_id = wormedit::chart_id_t{1};
        populate_metadata_view_projection(frame, history, workspace, base, drop, runtime);
        return frame;
    }
    void start_preview() {
        wormedit::ui::workspace_action_t action{};
        action.compose_input_chart_id.value = 1;
        action.compose_input_document_revision = history.document.revision;
        action.compose_resource_asset_id.value = 8;
        action.metadata_inspect_import = true;
        action.compose_import_path = "/tmp/arena-copy-preview-source.wav";
        const auto status = runtime.start_copy_preview(action, history, workspace, base);
        INFO(status);
        REQUIRE(runtime.copy_preview_key());
    }
};
void check_metadata_text(const wormedit::ui::metadata_view_t& view) {
    check_frame_storage(std::strcmp(view.project_directory.c_str(), "/tmp/metadata-arena-project-directory") == 0);
    check_frame_storage(std::strcmp(view.selected_music_source.c_str(), "audio/a-long-selected-music-source.wav") == 0);
    check_frame_storage(std::strcmp(view.dropped_music_path.c_str(), "/tmp/a-long-dropped-audio-source.wav") == 0);
    check_frame_storage(std::strcmp(view.dropped_output_relative_path.c_str(), "exports/a-long-dropped-output.bms") == 0);
    check_frame_storage(view.selected_music_asset_id == wormedit::asset_id_t{7});
    REQUIRE(view.output_groups.size() == 1);
    check_frame_storage(std::strcmp(view.output_groups[0].name, "Long retained output group name") == 0);
    check_frame_storage(std::strcmp(view.output_groups[0].relative_output_path, "exports/chart.bms") == 0);
    check_frame_storage(std::strcmp(view.output_groups[0].relative_asset_directory,
                                    "exports/chart.bms/assets") == 0);
    check_frame_storage(view.output_groups[0].document_index == 0);
}
}

TEST_CASE("Metadata frame text and output groups survive source mutation and projection moves",
          "[sys][frame-arena][metadata-text]") {
    frontend_workspace_view_t retained;
    {
        metadata_text_fixture_t fixture;
        fixture.start_preview();
        retained = fixture.project();
        fixture.drop.clear_paths();
        fixture.history.document.audio_assets.clear();
        fixture.history.document.output_groups.clear();
        fixture.runtime.clear_copy_preview_key();
    }
    auto moved = std::move(retained);
    moved.rebind_view_pointers();
    check_metadata_text(moved.view.metadata);
    check_frame_storage(std::strcmp(moved.view.metadata.copy_preview_text.c_str(),
        "Inspecting destination bytes in the background...") == 0);
    check_frame_storage(std::strcmp(moved.view.metadata.copy_preview_source.c_str(),
        "/tmp/arena-copy-preview-source.wav") == 0);
}

TEST_CASE("Metadata scalar text is owned while unconverted output rows preserve complete prefixes",
          "[sys][frame-arena][metadata-text]") {
    metadata_text_fixture_t fixture;
    fixture.start_preview();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    auto failed = fixture.project(&tiny);
    check_frame_storage(!failed.view.metadata.project_directory.empty());
    check_frame_storage(!failed.view.metadata.selected_music_source.empty());
    check_frame_storage(!failed.view.metadata.copy_preview_source.empty());
    check_frame_storage(failed.view.metadata.output_groups.size() == 0);
    check_frame_storage(failed.view.metadata.output_group_text_truncated);
    const auto diagnostic = failed.view.metadata.copy_preview_text;
    REQUIRE(!diagnostic.empty());
    tiny.reset();
    check_frame_storage(std::strcmp(diagnostic.c_str(),
        "Inspecting destination bytes in the background...") == 0);
    fixture.runtime.clear_copy_preview_key();
    auto second = fixture.history.document.output_groups.front();
    second.name.assign(4096, 'x');
    fixture.history.document.output_groups.push_back(std::move(second));
    frame_arena_t limited({.chunk_bytes = 1024, .maximum_bytes = 1024});
    auto prefix = fixture.project(&limited);
    check_metadata_text(prefix.view.metadata);
    check_frame_storage(prefix.view.metadata.output_group_text_truncated);
}

TEST_CASE("Metadata output groups retain selected chart filtering beyond the former capacity",
          "[sys][frame-arena][metadata-text]") {
    metadata_text_fixture_t fixture;
    auto& groups = fixture.history.document.output_groups;
    groups.resize(WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY, groups.front());
    groups.front().charts = {wormedit::chart_id_t{2}};
    auto accepted = fixture.project();
    check_frame_storage(accepted.view.metadata.output_groups.size() == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY - 1);
    check_frame_storage(accepted.view.metadata.output_groups[0].document_index == 1);
    check_frame_storage(!accepted.view.metadata.output_group_text_truncated);
    groups.push_back(groups.back());
    auto exact = fixture.project();
    check_frame_storage(exact.view.metadata.output_groups.size() == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY);
    check_frame_storage(!exact.view.metadata.output_group_text_truncated);
    groups.push_back(groups.back());
    auto truncated = fixture.project();
    check_frame_storage(truncated.view.metadata.output_groups.size() == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY + 1);
    check_frame_storage(!truncated.view.metadata.output_group_text_truncated);
}

TEST_CASE("Metadata frame text keeps stable warmed storage across 1000 projections",
          "[sys][frame-arena][metadata-text]") {
    metadata_text_fixture_t fixture;
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    (void)fixture.project(&arena);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    (void)fixture.project(&arena);
    const auto allocations = frame_heap_allocation_snapshot().count - before.count;
    bool stable = true;
    bool consistent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        const auto frame = fixture.project(&arena);
        const auto finish = frame_heap_allocation_snapshot();
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            !frame.view.metadata.output_group_text_truncated && frame.view.metadata.output_groups.size() == 1;
        consistent = consistent && finish.count - start.count == allocations;
    }
    check_frame_storage(stable);
    check_frame_storage(consistent);
    INFO("Filesystem generic formatting still allocates " << allocations << " per projection");
}

namespace {
auto setup_arena_model() -> wormedit::workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::setup;
    model.tab_view = wormedit::workspace_setup_view_t{};
    return model;
}
auto recovery_arena_model() -> wormedit::native_project_recovery_controller_view_t {
    using action = wormedit::native_project_recovery_action_t;
    wormedit::native_project_recovery_controller_view_t recovery;
    recovery.rows.push_back({.target = {.scan_generation = 9, .row_index = 4, .candidate_index = 3},
        .path_label = "/tmp/a-long-inactive-recovery-path.wormedit",
        .status = wormedit::native_project_recovery_candidate_status_t::invalid,
        .comparison = wormedit::native_project_recovery_comparison_t::older_revision,
        .version = wormedit::native_project_document_version_t{.revision = 42},
        .recovery_session_sequence = 0});
    recovery.rows.push_back({.target = {.scan_generation = 9, .row_index = 2, .candidate_index = 8},
        .path_label = "/tmp/a-long-actionable-recovery-path.wormedit",
        .status = wormedit::native_project_recovery_candidate_status_t::valid,
        .comparison = wormedit::native_project_recovery_comparison_t::newer_revision,
        .available_actions = {action::inspect_read_only, action::recover_into_session,
            action::save_as_new_project, action::discard, action::quarantine}});
    recovery.preferred_recovery_row = 0;
    recovery.diagnostics.push_back({.severity = wormedit::diagnostic_severity_t::warning,
        .message = "First retained recovery diagnostic", .path = "recovery/candidate.wormedit"});
    recovery.diagnostics.push_back({.message = "Second retained recovery diagnostic"});
    return recovery;
}
auto project_setup_recovery(const wormedit::native_project_recovery_controller_view_t& recovery,
                            frame_arena_t* arena = nullptr) -> frontend_workspace_view_t {
    return frontend_workspace_view_t::from(setup_arena_model(), {}, "ready", recovery, true, arena);
}
void check_recovery_arena_rows(const wormedit::ui::workspace_view_t& view) {
    REQUIRE(view.recovery_candidates.size() == 2);
    const auto& active = view.recovery_candidates[0];
    check_frame_storage(active.path_label == "/tmp/a-long-actionable-recovery-path.wormedit");
    check_frame_storage(active.summary == "valid · newer");
    check_frame_storage(active.disabled_reason.empty());
    using action_t = wormedit::native_project_recovery_action_t;
    for (const auto action : {action_t::inspect_read_only, action_t::recover_into_session,
                              action_t::save_as_new_project, action_t::discard, action_t::quarantine})
        check_frame_storage(active.can(action));
    check_frame_storage(active.scan_generation == 9 && active.row_index == 2 && active.candidate_index == 8);
    const auto& inactive = view.recovery_candidates[1];
    check_frame_storage(inactive.summary ==
        "invalid · older · revision 42 · session 0 · recommended recovery");
    check_frame_storage(inactive.disabled_reason == "No safe action is available for this candidate.");
    check_frame_storage(inactive.actions.empty());
    check_frame_storage(inactive.scan_generation == 9 && inactive.row_index == 4 && inactive.candidate_index == 3);
}
}

TEST_CASE("Setup and Recovery arena text preserves source-independent ordering and action tokens",
          "[sys][frame-arena][setup-recovery]") {
    auto recovery = recovery_arena_model();
    auto frame = project_setup_recovery(recovery);
    recovery.rows.clear();
    recovery.diagnostics.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    check_recovery_arena_rows(moved.view);
    check_frame_storage(moved.view.recovery_has_actionable_candidate);
    check_frame_storage(moved.view.recovery_panel_visible);
    check_frame_storage(!moved.view.recovery_rows_truncated);
    REQUIRE(moved.view.recovery_diagnostics.size() == 2);
    check_frame_storage(std::strcmp(moved.view.recovery_diagnostics[0].c_str(),
        "First retained recovery diagnostic (recovery/candidate.wormedit)") == 0);
    check_frame_storage(std::strcmp(moved.view.recovery_diagnostics[1].c_str(),
        "Second retained recovery diagnostic") == 0);
}

TEST_CASE("Recovery arena summaries retain maximum numeric values and disabled action reasons",
          "[sys][frame-arena][setup-recovery]") {
    auto recovery = recovery_arena_model();
    auto& active = recovery.rows[1];
    active.version = wormedit::native_project_document_version_t{
        .revision = std::numeric_limits<std::uint64_t>::max()};
    active.recovery_session_sequence = std::numeric_limits<std::uint64_t>::max();
    active.available_actions = {wormedit::native_project_recovery_action_t::inspect_read_only};
    const auto frame = project_setup_recovery(recovery);
    check_frame_storage(frame.view.recovery_candidates[0].summary ==
        "valid · newer · revision 18446744073709551615 · session 18446744073709551615");
    check_frame_storage(frame.view.recovery_candidates[0].disabled_reason ==
        "Unavailable actions are disabled by the recovery controller's safety checks.");
    check_frame_storage(frame.view.recovery_candidates[0].can(
        wormedit::native_project_recovery_action_t::inspect_read_only));
    recovery.rows.resize(5, active);
    recovery.diagnostics.resize(5, recovery.diagnostics.front());
    const auto bounded = project_setup_recovery(recovery);
    check_frame_storage(bounded.view.recovery_candidates.size() == 5);
    check_frame_storage(!bounded.view.recovery_rows_truncated);
    check_frame_storage(bounded.view.recovery_diagnostics.size() == 5);
}

TEST_CASE("Recovery owns complete rows independently of frame arena exhaustion",
          "[sys][frame-arena][setup-recovery]") {
    const auto recovery = recovery_arena_model();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    const auto complete = project_setup_recovery(recovery, &tiny);
    check_recovery_arena_rows(complete.view);
    check_frame_storage(!complete.view.recovery_rows_truncated);
    auto long_model = recovery_arena_model();
    long_model.rows[0].path_label.assign(4096, 'p');
    const auto long_rows = project_setup_recovery(long_model, &tiny);
    REQUIRE(long_rows.view.recovery_candidates.size() == 2);
    check_frame_storage(!long_rows.view.recovery_rows_truncated);
    check_frame_storage(long_rows.view.recovery_candidates[1].path_label.size() == 4096);
}

TEST_CASE("Warmed Setup and Recovery refill allocates no heap across 1000 projections",
          "[sys][frame-arena][setup-recovery]") {
    auto recovery = recovery_arena_model();
    const auto model = setup_arena_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    frontend_workspace_view_t frame(&arena);
    auto project = [&] { frame.refill(model, document, status, recovery, true, &arena); };
    project();
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    bool stable = true;
    bool zero_new = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        project();
        const auto finish = frame_heap_allocation_snapshot();
        zero_new = zero_new && finish.count == start.count;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            frame.view.recovery_candidates.size() == 2;
    }
    check_frame_storage(zero_new);
    check_frame_storage(stable);
}

namespace {
auto compose_output_group_document() -> wormedit::song_document_t {
    wormedit::song_document_t document;
    document.output_groups.push_back({.name = "Excluded chart output",
        .charts = {wormedit::chart_id_t{2}}, .relative_output_path = "ignored/chart.bms"});
    document.output_groups.push_back({.name = "Independently owned Compose output group",
        .charts = {wormedit::chart_id_t{1}}, .relative_output_path = "exports/chart.bms"});
    return document;
}
auto project_compose_output_groups(const wormedit::song_document_t& document,
                                   frame_arena_t* arena = nullptr) -> frontend_workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{1}};
    return frontend_workspace_view_t::from(model, document, "ready", {}, false, arena);
}
void check_compose_output_group(const WormUiComposeOutputGroupView& group) {
    check_frame_storage(group.document_index == 1);
    check_frame_storage(std::strcmp(group.name, "Independently owned Compose output group") == 0);
    check_frame_storage(std::strcmp(group.relative_output_path, "exports/chart.bms") == 0);
    check_frame_storage(std::strcmp(group.relative_asset_directory,
                                    "exports/chart.bms/assets") == 0);
}
}

TEST_CASE("Compose output group arena text retains source-independent moves and chart filtering",
          "[sys][frame-arena][compose-output-group]") {
    auto document = compose_output_group_document();
    auto frame = project_compose_output_groups(document);
    document.output_groups.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    REQUIRE(moved.view.compose.output_group_count == 1);
    check_frame_storage(!moved.view.compose.output_groups_truncated);
    check_compose_output_group(moved.view.compose.output_groups[0]);
    check_frame_storage(moved.view.compose.output_groups == moved.compose.output_group.view.data());
}

TEST_CASE("Compose output group arena retains count limits and existing path display semantics",
          "[sys][frame-arena][compose-output-group]") {
    auto document = compose_output_group_document();
    const auto selected = document.output_groups[1];
    document.output_groups.resize(WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY + 1, selected);
    document.output_groups.insert(document.output_groups.begin() + 2, document.output_groups.front());
    auto exact = project_compose_output_groups(document);
    check_frame_storage(exact.view.compose.output_group_count == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY);
    check_frame_storage(!exact.view.compose.output_groups_truncated);
    check_frame_storage(exact.view.compose.output_groups[WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY - 1].document_index == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY + 1);
    document.output_groups.push_back(selected);
    auto truncated = project_compose_output_groups(document);
    check_frame_storage(truncated.view.compose.output_group_count == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY);
    check_frame_storage(truncated.view.compose.output_groups_truncated);
    document.output_groups.resize(2);
    document.output_groups[1].relative_output_path = "/absolute/chart.bms";
    auto absolute = project_compose_output_groups(document);
    REQUIRE(absolute.view.compose.output_group_count == 1);
    check_frame_storage(std::strcmp(absolute.view.compose.output_groups[0].relative_output_path,
                                   "/absolute/chart.bms") == 0);
    check_frame_storage(absolute.view.compose.output_groups[0].relative_asset_directory[0] == '\0');
    document.output_groups[1].charts = {wormedit::chart_id_t{2}};
    auto unmatched = project_compose_output_groups(document);
    check_frame_storage(unmatched.view.compose.output_group_count == 0);
    check_frame_storage(!unmatched.view.compose.output_groups_truncated);
    document.output_groups.clear();
    auto empty = project_compose_output_groups(document);
    check_frame_storage(empty.view.compose.output_group_count == 0);
    check_frame_storage(!empty.view.compose.output_groups_truncated);
}

TEST_CASE("Compose output group exhaustion publishes no partial row or fake path",
          "[sys][frame-arena][compose-output-group]") {
    auto document = compose_output_group_document();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    auto failed = project_compose_output_groups(document, &tiny);
    check_frame_storage(failed.view.compose.output_group_count == 0);
    check_frame_storage(failed.view.compose.output_groups_truncated);
    check_frame_storage(failed.view.compose.output_groups == nullptr);
    auto second = document.output_groups[1];
    second.name.assign(4096, 'x');
    document.output_groups.push_back(std::move(second));
    frame_arena_t limited({.chunk_bytes = 512, .maximum_bytes = 512});
    auto prefix = project_compose_output_groups(document, &limited);
    REQUIRE(prefix.view.compose.output_group_count == 1);
    check_frame_storage(prefix.view.compose.output_groups_truncated);
    check_compose_output_group(prefix.view.compose.output_groups[0]);
}

TEST_CASE("Compose output group arena keeps stable storage across 1000 warmed frames",
          "[sys][frame-arena][compose-output-group]") {
    const auto document = compose_output_group_document();
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    (void)project_compose_output_groups(document, &arena);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    (void)project_compose_output_groups(document, &arena);
    const auto allocations = frame_heap_allocation_snapshot().count - before.count;
    bool stable = true;
    bool consistent = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        const auto frame = project_compose_output_groups(document, &arena);
        const auto finish = frame_heap_allocation_snapshot();
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            frame.view.compose.output_group_count == 1 && !frame.view.compose.output_groups_truncated;
        consistent = consistent && finish.count - start.count == allocations;
    }
    INFO("Unconverted catalog/branch and generic path formatting allocations: " << allocations);
    check_frame_storage(stable);
    check_frame_storage(consistent);
}

TEST_CASE("Compose output group external arena reset republishes current document text",
          "[sys][frame-arena][compose-output-group]") {
    auto document = compose_output_group_document();
    frame_arena_t arena({.chunk_bytes = 1024, .maximum_bytes = 1024});
    {
        auto frame = project_compose_output_groups(document, &arena);
        auto moved = std::move(frame);
        moved.rebind_view_pointers();
        REQUIRE(moved.view.compose.output_group_count == 1);
        check_compose_output_group(moved.view.compose.output_groups[0]);
    }
    arena.reset();
    document.output_groups[1].name = "Updated second frame group";
    auto current = project_compose_output_groups(document, &arena);
    REQUIRE(current.view.compose.output_group_count == 1);
    check_frame_storage(std::strcmp(current.view.compose.output_groups[0].name,
                                   "Updated second frame group") == 0);
    check_frame_storage(!current.view.compose.output_groups_truncated);
}

namespace {
auto compose_resource_document(std::size_t count) -> wormedit::song_document_t {
    wormedit::song_document_t document;
    for (std::size_t index = 0; index < count; ++index) {
        const auto id = index + 1;
        document.audio_assets.push_back({.id = wormedit::asset_id_t{id},
            .source_path = std::string(80, 'p') + "/" + std::string(80, 'f') + ".wav"});
        document.resources.push_back({.id = wormedit::resource_id_t{id},
            .name = std::string(80, 'n') + std::to_string(id),
            .asset = wormedit::asset_id_t{id},
            .roles = {wormedit::resource_role_t::sample, wormedit::resource_role_t::bgm},
            .target_identities = {{.family = wormedit::format_family_t::bms,
                .identifier = "01", .namespace_name = "WAV"}}});
    }
    return document;
}
auto project_resource_rows(const wormedit::song_document_t& document,
                           frame_arena_t* arena = nullptr,
                           wormedit::ui::keysound_dock_request_t request = {}) -> frontend_workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{1}};
    model.transport.audio_asset = wormedit::asset_id_t{1};
    model.selected_visual_asset = wormedit::asset_id_t{7};
    return frontend_workspace_view_t::from(model, document, "ready", {}, false, arena,
                             dock_sort_t::natural_name, request);
}
}
TEST_CASE("Compose resource arena owns complete rows after source mutation and move",
          "[sys][frame-arena][compose-resource]") {
    auto document = compose_resource_document(2);
    document.resources.push_back({.id = wormedit::resource_id_t{9},
        .roles = {wormedit::resource_role_t::preview}});
    auto frame = project_resource_rows(document);
    REQUIRE(frame.view.compose.resources.size() == 2);
    check_frame_storage(frame.view.compose.resource_total_count == 2);
    document.resources.clear();
    document.audio_assets.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& row = moved.view.compose.resources[0];
    check_frame_storage(row.resource_id.value == 1 && row.asset_id.value == 1);
    check_frame_storage(row.audio_asset && row.selected_audio_asset && !row.missing);
    check_frame_storage(row.roles == (WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE | WORM_UI_COMPOSE_RESOURCE_ROLE_BGM));
    check_frame_storage(std::strcmp(row.target_index.c_str(), "#WAV01") == 0);
    check_frame_storage(std::strlen(row.name.c_str()) == 81);
    check_frame_storage(std::strlen(row.source_name.c_str()) == 84);
}
TEST_CASE("Compose resource arena preserves fallback labels visual flags and missing assets",
          "[sys][frame-arena][compose-resource]") {
    wormedit::song_document_t document;
    document.visual_assets.push_back({.id = wormedit::asset_id_t{7},
        .kind = wormedit::asset_kind_t::image, .source_path = "images/banner.png"});
    document.resources.push_back({.id = wormedit::resource_id_t{17},
        .asset = wormedit::asset_id_t{7}, .roles = {wormedit::resource_role_t::bga}});
    document.resources.push_back({.id = wormedit::resource_id_t{18},
        .asset = wormedit::asset_id_t{8}, .roles = {wormedit::resource_role_t::sample}});
    auto frame = project_resource_rows(document);
    REQUIRE(frame.view.compose.resources.size() == 2);
    const auto& visual = frame.view.compose.resources[0];
    check_frame_storage(visual.image_asset && visual.selected_visual_asset && !visual.audio_asset && !visual.missing);
    check_frame_storage(std::strcmp(visual.target_index.c_str(), "#17") == 0);
    check_frame_storage(std::strcmp(visual.name.c_str(), "Resource 17") == 0);
    check_frame_storage(std::strcmp(visual.source_name.c_str(), "banner.png") == 0);
    const auto& missing = frame.view.compose.resources[1];
    check_frame_storage(missing.missing && !missing.image_asset && !missing.audio_asset);
    check_frame_storage(std::strcmp(missing.source_name.c_str(), "") == 0);
}
TEST_CASE("Compose resource arena empty catalogs do not report truncation",
          "[sys][frame-arena][compose-resource]") {
    wormedit::song_document_t document;
    auto empty = project_resource_rows(document);
    check_frame_storage(empty.view.compose.resources.size() == 0);
    check_frame_storage(empty.view.compose.resource_total_count == 0);
    document.resources.push_back({.id = wormedit::resource_id_t{9},
        .roles = {wormedit::resource_role_t::preview}});
    auto hidden = project_resource_rows(document);
    check_frame_storage(hidden.view.compose.resources.size() == 0);
    check_frame_storage(hidden.view.compose.resource_total_count == 0);
}
TEST_CASE("Compose resource external arena reset republishes updated text",
          "[sys][frame-arena][compose-resource]") {
    auto document = compose_resource_document(1);
    frame_arena_t arena({.chunk_bytes = 4096, .maximum_bytes = 4096});
    {
        auto first = project_resource_rows(document, &arena);
        auto moved = std::move(first);
        moved.rebind_view_pointers();
        REQUIRE(moved.view.compose.resources.size() == 1);
        check_frame_storage(std::strlen(moved.view.compose.resources[0].name.c_str()) == 81);
    }
    arena.reset();
    document.resources[0].name = "Updated second frame resource label";
    document.audio_assets[0].source_path = "new/path/updated.wav";
    auto second = project_resource_rows(document, &arena);
    REQUIRE(second.view.compose.resources.size() == 1);
    check_frame_storage(std::strcmp(second.view.compose.resources[0].name.c_str(),
        "Updated second frame resource label") == 0);
    check_frame_storage(std::strcmp(second.view.compose.resources[0].source_name.c_str(),
        "updated.wav") == 0);
}

namespace {
auto branch_arena_model(std::size_t count = 3) -> wormedit::workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    wormedit::workspace_compose_view_t tab{.chart = wormedit::chart_id_t{1}};
    auto shared = std::make_shared<wormedit::workspace_bms_branch_outline_view_t>();
    tab.shared_bms_branch_outline = shared;
    auto& outline = *shared;
    outline.available = true;
    for (std::size_t index = 0; index < count; ++index) {
        outline.rows.push_back({.depth = index, .source_order = index == 0
            ? std::optional<std::size_t>{} : std::optional<std::size_t>{index + 7},
            .active = index % 2 == 0, .selected = index == 1,
            .display_text = std::string(128, char('a' + index % 26)),
            .display_text_truncated = index == 2});
        outline.choices.push_back({.id = wormedit::branch_id_t{index + 11},
            .kind = index % 2 == 0 ? wormedit::bms_control_kind_t::random
                                  : wormedit::bms_control_kind_t::switch_block,
            .source_order = index == 0 ? std::optional<std::size_t>{}
                                      : std::optional<std::size_t>{index + 30},
            .active = index % 2 == 0, .editable = index != 2,
            .selected_value = index == 0 ? std::optional<std::int64_t>{}
                                        : std::optional<std::int64_t>{-7},
            .previous_value = -9, .next_value = index == 2
                ? std::optional<std::int64_t>{} : std::optional<std::int64_t>{-3},
            .can_clear = index == 1});
    }
    outline.diagnostics.push_back({.message = std::string(128, 'd')});
    outline.diagnostics.push_back({.message = std::string(128, 'e')});
    model.tab_view = std::move(tab);
    return model;
}
auto project_branch_arena(const wormedit::workspace_view_t& model, frame_arena_t* arena = nullptr)
    -> frontend_workspace_view_t {
    return frontend_workspace_view_t::from(model, {}, "ready", {}, false, arena);
}
auto branch_outline(wormedit::workspace_view_t& model)
    -> wormedit::workspace_bms_branch_outline_view_t& {
    // branch_arena_model creates the outline non-const, so tests may edit it in place.
    return const_cast<wormedit::workspace_bms_branch_outline_view_t&>(
        std::get<wormedit::workspace_compose_view_t>(model.tab_view).bms_branch_outline());
}
void check_branch_choice(const WormUiComposeBranchChoiceView& view,
                         const wormedit::workspace_bms_branch_choice_t& source) {
    check_frame_storage(view.branch_id == source.id.value);
    check_frame_storage(view.kind == (source.kind == wormedit::bms_control_kind_t::switch_block
        ? WORM_UI_COMPOSE_BRANCH_SWITCH : WORM_UI_COMPOSE_BRANCH_RANDOM));
    check_frame_storage(view.has_source_order == source.source_order.has_value());
    check_frame_storage(view.source_order == source.source_order.value_or(0));
    check_frame_storage(view.active == source.active && view.editable == source.editable);
    check_frame_storage(view.has_selected_value == source.selected_value.has_value());
    check_frame_storage(view.selected_value == source.selected_value.value_or(0));
    check_frame_storage(view.has_previous_value == source.previous_value.has_value());
    check_frame_storage(view.previous_value == source.previous_value.value_or(0));
    check_frame_storage(view.has_next_value == source.next_value.has_value());
    check_frame_storage(view.next_value == source.next_value.value_or(0));
    check_frame_storage(view.can_clear == source.can_clear);
}
void check_branch_row(const WormUiComposeBranchRowView& view,
                      const wormedit::workspace_bms_branch_row_t& source) {
    REQUIRE(view.display_text);
    check_frame_storage(std::strcmp(view.display_text, source.display_text.c_str()) == 0);
    check_frame_storage(view.depth == source.depth && view.active == source.active && view.selected == source.selected);
    check_frame_storage(view.has_source_order == source.source_order.has_value());
    check_frame_storage(view.source_order == source.source_order.value_or(0));
    check_frame_storage(view.display_text_truncated == source.display_text_truncated);
}
void check_branch_prefix(const WormUiComposeView& view,
                         const wormedit::workspace_bms_branch_outline_view_t& source) {
    for (std::size_t index = 0; index < view.branch_row_count; ++index)
        check_branch_row(view.branch_rows[index], source.rows[index]);
    for (std::size_t index = 0; index < view.branch_choice_count; ++index)
        check_branch_choice(view.branch_choices[index], source.choices[index]);
    for (std::size_t index = 0; index < view.branch_diagnostic_count; ++index) {
        REQUIRE(view.branch_diagnostics[index]);
        check_frame_storage(std::strcmp(view.branch_diagnostics[index], source.diagnostics[index].message.c_str()) == 0);
    }
}
void check_branch_in_arena(const frontend_workspace_view_t& frame, std::uintptr_t begin, std::uintptr_t end) {
    const auto contains = [&](const void* data, std::size_t bytes) {
        const auto address = reinterpret_cast<std::uintptr_t>(data);
        return address >= begin && address + bytes <= end;
    };
    const auto& view = frame.view.compose;
    check_frame_storage(contains(view.branch_rows, view.branch_row_count * sizeof(*view.branch_rows)));
    check_frame_storage(contains(view.branch_choices, view.branch_choice_count * sizeof(*view.branch_choices)));
    for (std::size_t index = 0; index < view.branch_row_count; ++index)
        check_frame_storage(contains(view.branch_rows[index].display_text, std::strlen(view.branch_rows[index].display_text) + 1));
    for (std::size_t index = 0; index < view.branch_diagnostic_count; ++index)
        check_frame_storage(contains(view.branch_diagnostics[index], std::strlen(view.branch_diagnostics[index]) + 1));
}
}

TEST_CASE("Compose branch arena preserves source values independently through move", "[sys][frame-arena][compose-branch]") {
    auto model = branch_arena_model();
    const auto expected = branch_outline(model);
    auto frame = project_branch_arena(model);
    REQUIRE(frame.view.compose.branch_row_count == 3);
    REQUIRE(frame.view.compose.branch_choice_count == 3);
    REQUIRE(frame.view.compose.branch_diagnostic_count == 2);
    check_frame_storage(frame.view.compose.branch_outline_available);
    branch_outline(model).rows[0].display_text.assign(256, 'x');
    branch_outline(model).diagnostics[0].message.assign(256, 'y');
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    check_branch_prefix(moved.view.compose, expected);
    check_frame_storage(!moved.view.compose.branch_rows_truncated && !moved.view.compose.branch_choices_truncated);
    check_frame_storage(!moved.view.compose.branch_diagnostics_truncated && !moved.view.compose.branch_display_text_truncated);
}

TEST_CASE("Compose branch spans and diagnostic strings belong to supplied arena", "[sys][frame-arena][compose-branch]") {
    auto model = branch_arena_model();
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const auto first = arena.allocate(1, 1);
    auto frame = project_branch_arena(model, &arena);
    const auto last = arena.allocate(1, 1);
    REQUIRE((first && last));
    check_branch_in_arena(frame, reinterpret_cast<std::uintptr_t>(*first), reinterpret_cast<std::uintptr_t>(*last));
    check_frame_storage(frame.view.compose.branch_rows == frame.compose.branch.row.data());
    check_frame_storage(frame.view.compose.branch_choices == frame.compose.branch.choice.data());
    frame = {};
    arena.reset();
    auto& outline = branch_outline(model);
    outline.rows[0].display_text = "updated row";
    outline.diagnostics[0].message = "updated diagnostic";
    const auto next = project_branch_arena(model, &arena);
    check_branch_prefix(next.view.compose, outline);
}

TEST_CASE("Compose branch caps retain document order and existing truncation flags", "[sys][frame-arena][compose-branch]") {
    auto model = branch_arena_model(97);
    auto& outline = branch_outline(model);
    outline.display_text_truncated = true;
    outline.diagnostics.resize(5, {.message = "extra diagnostic"});
    const auto frame = project_branch_arena(model);
    REQUIRE(frame.view.compose.branch_row_count == 96);
    REQUIRE(frame.view.compose.branch_choice_count == 96);
    REQUIRE(frame.view.compose.branch_diagnostic_count == 4);
    check_branch_prefix(frame.view.compose, outline);
    check_frame_storage(frame.view.compose.branch_rows_truncated && frame.view.compose.branch_choices_truncated);
    check_frame_storage(frame.view.compose.branch_display_text_truncated && frame.view.compose.branch_diagnostics_truncated);
    check_frame_storage(outline.rows.size() == 97 && outline.choices.size() == 97 && outline.diagnostics.size() == 5);
}

TEST_CASE("Compose empty branch outlines preserve availability and source flags", "[sys][frame-arena][compose-branch]") {
    auto model = branch_arena_model(0);
    auto& outline = branch_outline(model);
    outline.diagnostics.clear();
    for (const bool marked : {false, true}) {
        outline.available = marked;
        outline.rows_truncated = marked; outline.choices_truncated = marked;
        outline.display_text_truncated = marked; outline.diagnostics_truncated = marked;
        const auto frame = project_branch_arena(model);
        const auto& view = frame.view.compose;
        check_frame_storage(view.branch_row_count == 0 && view.branch_choice_count == 0 && view.branch_diagnostic_count == 0);
        check_frame_storage(view.branch_rows == nullptr && view.branch_choices == nullptr);
        check_frame_storage(view.branch_outline_available == marked && view.branch_rows_truncated == marked);
        check_frame_storage(view.branch_choices_truncated == marked && view.branch_display_text_truncated == marked && view.branch_diagnostics_truncated == marked);
    }
}

TEST_CASE("Compose branch storage exhaustion retains complete row and diagnostic prefixes", "[sys][frame-arena][compose-branch]") {
    const auto model = branch_arena_model();
    const auto& source = std::get<wormedit::workspace_compose_view_t>(model.tab_view).bms_branch_outline();
    bool row_prefix = false, diagnostic_prefix = false, zero_choice = false;
    for (std::size_t limit = 32; limit <= 1400; limit += 8) {
        frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        const auto frame = project_branch_arena(model, &arena);
        const auto& view = frame.view.compose;
        check_branch_prefix(view, source);
        if (view.branch_row_count != 3) check_frame_storage(view.branch_rows_truncated);
        if (view.branch_choice_count != 3) check_frame_storage(view.branch_choices_truncated);
        if (view.branch_diagnostic_count != 2) check_frame_storage(view.branch_diagnostics_truncated);
        row_prefix = row_prefix || (view.branch_row_count == 1 && view.branch_display_text_truncated);
        diagnostic_prefix = diagnostic_prefix || view.branch_diagnostic_count == 1;
        zero_choice = zero_choice || view.branch_choice_count == 0;
    }
    check_frame_storage(row_prefix && diagnostic_prefix && zero_choice);
    check_frame_storage(source.rows[0].display_text == std::string(128, 'a'));
}

TEST_CASE("Warmed Compose branch projection remains stable for 1000 arena resets", "[sys][frame-arena][compose-branch]") {
    const auto model = branch_arena_model();
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frontend_workspace_view_t frame(&arena);
    auto refill = [&] { frame.refill(model, document, status, {}, false, &arena); };
    refill();
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    refill();
    const auto remaining = frame_heap_allocation_snapshot().count - before.count;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        refill();
        stable = stable && frame_heap_allocation_snapshot().count - start.count == remaining &&
            arena.used_bytes() == used && arena.committed_bytes() == committed &&
            frame.view.compose.branch_row_count == 3 && frame.view.compose.branch_choice_count == 3 &&
            frame.view.compose.branch_diagnostic_count == 2 && !frame.view.compose.branch_rows_truncated;
    }
    INFO("Remaining actual flatten allocations=" << remaining << ", used=" << used << ", committed=" << committed);
    check_frame_storage(stable);
    check_frame_storage(remaining == 0);
}

TEST_CASE("Compose sound group choices retain names and stable resource membership",
          "[sys][frame-arena][sound-group]") {
    auto document = compose_resource_document(2);
    document.charts.push_back({.id = wormedit::chart_id_t{1}});
    document.sound_groups.push_back({.id = wormedit::sound_group_id_t{9}, .name = "Piano"});
    document.resources.front().sound_group = wormedit::sound_group_id_t{9};
    auto frame = project_resource_rows(document);
    REQUIRE(frame.view.compose.compose_sound_groups.size() == 1);
    REQUIRE(frame.view.compose.keysounds.size() == 2);
    document.sound_groups.front().name = "Changed";
    document.resources.front().sound_group.reset();
    auto moved = std::move(frame);
    check_frame_storage(moved.view.compose.compose_sound_groups[0].group_id.value == 9);
    check_frame_storage(std::strcmp(moved.view.compose.compose_sound_groups[0].name.c_str(), "Piano") == 0);
    const auto& rows = moved.view.compose.keysounds;
    const auto grouped = rows[0].resource_id.value == 1 ? 0 : 1;
    check_frame_storage(rows[grouped].sound_group_id.value == 9);
    check_frame_storage(rows[1 - grouped].sound_group_id.value == 0);
    auto current = project_resource_rows(document);
    check_frame_storage(current.view.compose.keysounds[grouped].sound_group_id.value == 0);
    check_frame_storage(std::strcmp(current.view.compose.compose_sound_groups[0].name.c_str(), "Changed") == 0);
}

TEST_CASE("Windowed sound sections retain arena labels and complete selection orders",
          "[sys][frame-arena][sound-group][sound-filter]") {
    auto document = compose_resource_document(5);
    document.charts.push_back({.id = wormedit::chart_id_t{1}});
    document.sound_groups = {{.id = wormedit::sound_group_id_t{9}, .name = "Piano"},
                             {.id = wormedit::sound_group_id_t{10}, .name = "Percussion"}};
    for (std::size_t index = 0; index < document.resources.size(); ++index) {
        auto& resource = document.resources[index];
        resource.name = "Sample " + std::to_string(index + 1);
        resource.target_identities.front().identifier = "0" + std::to_string(index + 1);
        if (index < 4)
            resource.sound_group = wormedit::sound_group_id_t{index < 2 ? 9U : 10U};
    }
    std::array<std::uint64_t, 1> collapsed{9}, selected{1};
    wormedit::ui::keysound_dock_request_t request{};
    for (const auto id : collapsed) request.collapsed_group_ids.push_back(wormedit::sound_group_id_t{id});
    for (const auto id : selected) request.selected_resource_ids.push_back(wormedit::resource_id_t{id});
    request.primary_resource_id = wormedit::resource_id_t{4};
    request.automatic_first_row = 2;
    request.automatic_row_limit = 3;
    auto frame = project_resource_rows(document, nullptr, request);
    REQUIRE(frame.view.compose.keysound_display_rows.size() == 3);
    REQUIRE(frame.view.compose.keysounds.size() == 1);
    REQUIRE(frame.view.compose.keysound_selection_rows.size() == 1);
    document.sound_groups.clear();
    document.resources.clear();
    collapsed[0] = 0;
    selected[0] = 0;
    auto retained = std::move(frame);
    retained.rebind_view_pointers();
    const auto& view = retained.view.compose;
    check_frame_storage(view.keysound_section_projection_available);
    check_frame_storage(view.keysound_display_total_count == 6);
    check_frame_storage(view.keysound_display_first_row == 2);
    check_frame_storage(view.keysound_primary_display_index_valid);
    check_frame_storage(view.keysound_primary_display_index == 2);
    check_frame_storage(view.keysound_display_rows[0].kind == WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW);
    check_frame_storage(view.keysound_display_rows[0].resource_id.value == 4);
    check_frame_storage(view.keysound_display_rows[1].group_id.value == 9);
    check_frame_storage(std::strcmp(view.keysound_display_rows[1].group_name.c_str(), "Piano") == 0);
    check_frame_storage(!view.keysound_display_rows[1].expanded);
    check_frame_storage(view.keysound_display_rows[1].group_sound_count == 2);
    check_frame_storage(std::strcmp(view.keysound_display_rows[2].group_name.c_str(), "Ungrouped") == 0);
    check_frame_storage(std::strcmp(view.keysounds[0].name.c_str(), "Sample 4") == 0);
    check_frame_storage(view.keysound_selected_rows.size() == 0);
    check_frame_storage(view.keysound_selection_rows[0].resource_id.value == 1);
    check_frame_storage(view.keysound_selection_rows[0].group_id.value == 9);
    check_frame_storage(std::strcmp(view.keysound_selection_rows[0].slot.c_str(), "01") == 0);
    check_frame_storage(view.keysound_all_resource_ids.size() == 5);
    check_frame_storage(view.keysound_advance_rows.size() == 5);
    check_frame_storage(view.keysound_advance_rows[0].resource_id.value == 3);
    check_frame_storage(view.keysound_advance_rows[2].resource_id.value == 1);
    check_frame_storage(view.keysound_listed_resource_ids.size() == 3);
    check_frame_storage(view.keysound_listed_resource_ids[0] == 3);
    check_frame_storage(view.keysound_listed_resource_ids[2] == 5);
}

TEST_CASE("Audio sound rows reuse query and text storage until the document changes",
          "[sys][audio][steady-state]") {
    auto document = compose_resource_document(1);
    document.resources[0].name.assign(80, 'n');
    document.resources[0].tags = {std::string(80, 't')};
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    model.tab_view = wormedit::workspace_audio_slices_view_t{};
    const std::string status = "ready";
    frame_arena_t arena;
    frontend_workspace_view_t frame(&arena);
    frame.refill(model, document, status, {}, false, &arena);
    check_frame_storage(!frame.view.audio_slices.sound_rows.empty());
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    frame.refill(model, document, status, {}, false, &arena);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(frame.view.audio_slices.sound_rows[0].name == document.resources[0].name);
    document.resources[0].name = "Changed";
    ++document.revision;
    frame.refill(model, document, status, {}, false, &arena);
    check_frame_storage(frame.view.audio_slices.sound_rows[0].name == "Changed");
    document.resources[0].name = "Reopened";
    ++frame.query_session_generation;
    frame.refill(model, document, status, {}, false, &arena);
    check_frame_storage(frame.view.audio_slices.sound_rows[0].name == "Reopened");
}

TEST_CASE("Sound usage rows and labels remain arena owned after source changes and move",
          "[sys][frame-arena][sound-usage]") {
    auto document = compose_resource_document(1);
    document.id = wormedit::song_id_t{8};
    document.revision = 11;
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{12};
    chart.metadata.name = "Difficulty";
    chart.measures.push_back({.id = wormedit::measure_id_t{50}});
    chart.measures.front().notes.push_back({.id = wormedit::object_id_t{60},
        .position = {wormedit::measure_id_t{50}, 120}, .sound = wormedit::resource_id_t{1}});
    document.charts.push_back(chart);
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    model.tab_view = wormedit::workspace_audio_slices_view_t{};
    auto frame = frontend_workspace_view_t::from(model, document, "ready", {}, false, nullptr, dock_sort_t::natural_name, {}, {.selected_project_id = wormedit::song_id_t{8}, .selected_resource_id = wormedit::resource_id_t{1}, .find_uses_open = true});
    REQUIRE(frame.view.audio_slices.usage_rows.size() == 1);
    document.charts.front().metadata.name = "Changed";
    document.charts.front().measures.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& view = moved.view.audio_slices;
    check_frame_storage(view.usage_valid);
    check_frame_storage(view.usage_song_id == wormedit::song_id_t{8} && view.usage_document_revision == 11);
    check_frame_storage(view.usage_total_count == 1 && !view.usage_rows_truncated);
    check_frame_storage(view.usage_rows[0].object_id == wormedit::object_id_t{60});
    check_frame_storage(std::strcmp(view.usage_rows[0].chart_label.c_str(), "Difficulty") == 0);
    check_frame_storage(std::strcmp(view.usage_rows[0].position_label.c_str(), "Measure 1, row 120") == 0);
}

namespace {
auto bookmark_arena_document() -> wormedit::song_document_t {
    using namespace wormedit;
    chart_document_t chart{.id = chart_id_t{2}, .divisions_per_standard_measure = 960,
        .measures = {{.id = measure_id_t{10}, .definition = {{1, 1}, meter_t{4, 4}}}},
        .bpms = {{.id = object_id_t{50}, .position = {measure_id_t{10}, 0}, .beats_per_minute = {120, 1}}}};
    chart.bookmarks.push_back({.id = object_id_t{101}, .label = "Original name",
        .anchor = marker_anchor_t::beat, .position = local_position_t{measure_id_t{10}, 240}});
    return {.id = song_id_t{1}, .revision = 7, .charts = {std::move(chart)}};
}
auto project_bookmark_arena(const wormedit::song_document_t& document, frame_arena_t* arena = nullptr)
    -> frontend_workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{2}};
    return frontend_workspace_view_t::from(model, document, "ready", {}, false, arena,
        dock_sort_t::natural_name, {}, {}, {.first_row = 0, .row_limit = 12, .open = true});
}
}

TEST_CASE("Bookmark chooser rows and text survive document mutation and projection move",
          "[sys][frame-arena][bookmark]") {
    auto document = bookmark_arena_document();
    auto frame = project_bookmark_arena(document);
    REQUIRE(frame.view.compose.bookmark_chooser.row_count == 1);
    document.charts.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& view = moved.view.compose.bookmark_chooser;
    check_frame_storage(view.project_id == 1);
    check_frame_storage(view.chart_id == 2);
    check_frame_storage(view.document_revision == 7);
    check_frame_storage(view.rows[0].bookmark_id == 101);
    check_frame_storage(std::strcmp(view.rows[0].name, "Original name") == 0);
    check_frame_storage(std::strstr(view.rows[0].display_label, "Original name") != nullptr);
}

TEST_CASE("Bookmark chooser arena exhaustion publishes a consistent bounded page",
          "[sys][frame-arena][bookmark]") {
    auto document = bookmark_arena_document();
    frame_arena_t arena({.chunk_bytes = 64, .maximum_bytes = 64});
    auto frame = project_bookmark_arena(document, &arena);
    frame.rebind_view_pointers();
    const auto& view = frame.view.compose.bookmark_chooser;
    CHECK(view.total_count == 1);
    CHECK(view.truncated);
    CHECK(view.row_count == 0);
}

TEST_CASE("Go to sound frame searches beyond dock filters and owns moved display labels",
          "[sys][frame-arena][sound-picker]") {
    auto document = compose_resource_document(300);
    document.charts.push_back({.id = wormedit::chart_id_t{1}});
    document.resources.back().name = "Super Snare Roll";
    wormedit::ui::keysound_dock_request_t request{};
    request.sound_picker_open = true;
    request.sound_picker_query = "SSR";
    request.automatic_search = "no dock matches";
    request.automatic_first_row = 100;
    request.automatic_row_limit = 1;
    auto frame = project_resource_rows(document, nullptr, request);
    check_frame_storage(frame.view.compose.keysounds.size() == 0);
    check_frame_storage(frame.view.compose.sound_picker.ready);
    check_frame_storage(frame.view.compose.sound_picker.total_count == 1);
    REQUIRE(frame.view.compose.sound_picker.rows.size() == 1);
    check_frame_storage(frame.view.compose.sound_picker.rows[0].resource_id.value == 300);
    document.resources.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    check_frame_storage(std::strstr(moved.view.compose.sound_picker.rows[0].label.c_str(), "Super Snare Roll") != nullptr);
    request.sound_picker_open = false;
    auto closed = project_resource_rows(document, nullptr, request);
    check_frame_storage(!closed.view.compose.sound_picker.ready);
    check_frame_storage(closed.view.compose.sound_picker.rows.size() == 0);
    request.sound_picker_open = true;
    const std::string query(96, 'x');
    request.sound_picker_query = query;
    const auto bounded = project_resource_rows(document, nullptr, request);
    check_frame_storage(bounded.view.compose.sound_picker.ready);
    check_frame_storage(bounded.view.compose.sound_picker.query.size() == 95);
}

TEST_CASE("Project export catalog includes groups outside the selected Compose difficulty", "[sys][frame-arena][output-groups]") {
    auto document = compose_output_group_document();
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::setup;
    auto frame = frontend_workspace_view_t::from(model, document, "ready", {}, false, nullptr);
    REQUIRE(frame.view.compose.output_group_count == document.output_groups.size());
    check_frame_storage(frame.view.compose.output_groups[0].document_index == 0);
    check_frame_storage(frame.view.compose.output_groups[1].document_index == 1);
    check_frame_storage(!frame.view.compose.output_groups_truncated);
}

TEST_CASE("Workspace projection uses the canonical Audio Slices tab label", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    CHECK(std::string_view{frame.view.tabs[WORM_UI_WORKSPACE_TAB_AUDIO_SLICES].label} == "Audio Slices");
    CHECK(std::string_view{frame.view.tab_heading} == "Audio Slices");
}

namespace {
void check_new_project_catalog_entry(const new_project_layout_entry_view_t& actual,
                                     const wormedit::new_project_layout_descriptor_t& expected) {
    CHECK(actual.id == expected.id);
    CHECK(actual.label == expected.label);
    CHECK(actual.game == expected.game);
    CHECK(actual.lane_shape == expected.lane_shape);
    CHECK(actual.variant == expected.variant);
    CHECK(actual.preview_lane_roles.size() == expected.profile().layout.lanes.size());
}
} // namespace

TEST_CASE("Workspace projects every stable new-project catalog identity", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    const auto& view = frame.view.new_project_layouts;
    REQUIRE(view.entries.size() == wormedit::new_project_layouts().size());
    for (std::size_t i = 0; i < view.entries.size(); ++i)
        check_new_project_catalog_entry(view.entries[i], wormedit::new_project_layouts()[i]);
}

namespace {
void check_playback_rate_catalog_entry(const wormedit::ui::kind_descriptor_view_t<wormedit::audio::playback_rate_id_t>& actual,
                                       const wormedit::audio::playback_rate_descriptor_t& expected) {
    CHECK(actual.id == expected.id);
    CHECK(std::string_view{actual.label} == expected.label);
}
} // namespace

TEST_CASE("Workspace projects playback rate ids and labels from the catalog", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    REQUIRE(frame.view.playback_rates.entries.size() == wormedit::audio::playback_rate_catalog.size());
    for (std::size_t i = 0; i < frame.view.playback_rates.entries.size(); ++i)
        check_playback_rate_catalog_entry(frame.view.playback_rates.entries[i], wormedit::audio::playback_rate_catalog[i]);
}

TEST_CASE("Workspace offers supported BMS encodings with stable ids and language hints", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    std::size_t index = 0;
    for (const auto& encoding : wormedit::bms_text_encodings) {
        if (!encoding.offered || !wormedit::is_bms_text_encoding_supported(encoding.kind)) continue;
        REQUIRE(index < frame.view.bms_encodings.entries.size());
        check_frame_storage(frame.view.bms_encodings.entries[index].id == encoding.kind);
        check_frame_storage(std::string_view{frame.view.bms_encodings.entries[index].label} == encoding.choice_label);
        ++index;
    }
    check_frame_storage(index == frame.view.bms_encodings.entries.size());
}

TEST_CASE("Workspace output families preserve SM SSC presentation and D9 exclusions", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    const auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    const auto& output = frame.view.setup.output_families;
    std::size_t index = 0;
    for (const auto& family : wormedit::format_families) {
        if (!family.offered_as_output) continue;
        REQUIRE(index < output.entries.size());
        check_frame_storage(output.entries[index].id == family.kind);
        check_frame_storage(std::string_view{output.entries[index].label} == family.output_label);
        ++index;
    }
    check_frame_storage(index == output.entries.size());
    check_frame_storage(output.entries.size() == 5);
    check_frame_storage(output.entries[2].id == wormedit::format_family_t::stepmania);
    check_frame_storage(std::string_view{output.entries[2].label} == "SM/SSC");
    check_frame_storage(frame.view.metadata.format_families.entries.size() == wormedit::format_families.size());
}

namespace {
void check_timing_catalog_entry(const WormUiComposeView& view, std::size_t index) {
    const auto& expected = wormedit::timing_lanes[index];
    check_frame_storage(view.timing_lanes.entries[index].id == expected.kind);
    check_frame_storage(std::string_view{view.timing_lanes.entries[index].label} == expected.label);
    check_frame_storage(view.timing_lane_captions.entries[index].id == expected.kind);
    check_frame_storage(std::string_view{view.timing_lane_captions.entries[index].label} == expected.short_label);
}
}
TEST_CASE("Workspace projects timing and note presentation with catalog identities", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    const auto frame = frontend_workspace_view_t::from(model, {}, "ready", {}, false, nullptr);
    const auto& view = frame.view.compose;
    REQUIRE(view.timing_lanes.entries.size() == wormedit::timing_lanes.size());
    REQUIRE(view.timing_lane_captions.entries.size() == wormedit::timing_lanes.size());
    for (std::size_t index = 0; index < view.timing_lanes.entries.size(); ++index)
        check_timing_catalog_entry(view, index);
    REQUIRE(view.note_palette.entries.size() == wormedit::note_palette_kinds.size());
    for (std::size_t index = 0; index < view.note_palette.entries.size(); ++index) {
        const auto kind = wormedit::note_palette_kinds[index];
        check_frame_storage(view.note_palette.entries[index].id == kind);
        check_frame_storage(std::string_view{view.note_palette.entries[index].label} ==
              wormedit::find_catalog_entry(wormedit::note_kinds, kind)->palette_label);
    }
}

TEST_CASE("BMS action identities reject unoffered and invalid encodings", "[sys][catalog-wiring]") {
    check_frame_storage(is_bms_encoding_offered(wormedit::text_encoding_t::utf8));
    check_frame_storage(!is_bms_encoding_offered(wormedit::text_encoding_t::unknown));
    check_frame_storage(!is_bms_encoding_offered(wormedit::text_encoding_t::ascii));
    check_frame_storage(!is_bms_encoding_offered(static_cast<wormedit::text_encoding_t>(-1)));
}

TEST_CASE("notification views refill retained storage without warm allocations",
          "[notifications][frontend][allocation]") {
    wormedit::notification_center_t center;
    center.post(wormedit::notification_severity_t::error, wormedit::notification_source_t::system,
                std::string(180, 'E'));
    center.drain_inbox(std::chrono::nanoseconds{0});
    wormedit::sys::notification_history_runtime_t runtime;
    runtime.visible = true;
    wormedit::song_document_t document;
    wormedit::ui::notification_view_t view;
    view.refill(center, std::chrono::nanoseconds{0});
    view.history.refill(runtime, center, document, std::chrono::nanoseconds{0});
    const auto before = frame_heap_allocation_snapshot();
    view.reset();
    view.refill(center, std::chrono::nanoseconds{0});
    view.history.refill(runtime, center, document, std::chrono::nanoseconds{0});
    const auto after = frame_heap_allocation_snapshot();
    CHECK(after.count == before.count);
    CHECK(after.bytes == before.bytes);
    REQUIRE(view.toasts.size() == 1);
    REQUIRE(view.history.rows.size() == 1);
    CHECK(view.toasts.front().message == view.history.rows.front().message);
}

TEST_CASE("Warmed retained timing projection preserves storage without heap allocations",
          "[sys][timing-projection][allocation]") {
    auto document = metadata_arena_document();
    document.id = wormedit::song_id_t{1};
    auto& chart = document.charts.front();
    chart.divisions_per_standard_measure = 960;
    chart.measures.push_back({.id = wormedit::measure_id_t{1},
                              .definition = {{1, 1}, wormedit::meter_t{4, 4}}});
    auto history = wormedit::document_history_t::from(std::move(document));
    wormedit::workspace_controller_t workspace;
    workspace.selected_chart = wormedit::chart_id_t{1};
    frontend_workspace_view_t flattened;
    timing_scene_cache_t cache;
    timing_grid_state_t grid{.supported_denominators = {1, 2, 4, 8}, .active_denominator = 4};
    populate_timing_view_for_compose(flattened, history, workspace, cache,
                                    1280, 980, grid, nullptr);
    const auto* denominators = flattened.view.timing.supported_snap_denominators.data();
    const auto before = frame_heap_allocation_snapshot();
    flattened.view.timing.reset();
    populate_timing_view_for_compose(flattened, history, workspace, cache,
                                    1280, 980, grid, nullptr);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(after.bytes == before.bytes);
    check_frame_storage(flattened.view.timing.supported_snap_denominators.data() == denominators);
    check_frame_storage(flattened.view.timing.supported_snap_denominators == grid.supported_denominators);
}

#include "sys/options_runtime.h"
#include "sys/project_view.h"
#include "sys/shortcut_rows.h"

TEST_CASE("Project options and shortcut projections reuse warmed nested storage",
          "[sys][project-options][allocation]") {
    auto document = metadata_arena_document();
    document.id = wormedit::song_id_t{1};
    document.output_groups.push_back({.name = std::string(180, 'g'),
        .family = wormedit::format_family_t::bms, .charts = {wormedit::chart_id_t{1}},
        .destination_kind = wormedit::output_destination_kind_t::file,
        .relative_output_path = "exports/a-long-retained-output-chart.bms"});
    project_chart_runtime_t project;
    frontend_workspace_view_t frame;
    frame.view.setup.visible = true;
    frame.view.setup.refill(project, document);
    REQUIRE_FALSE(document.output_groups.empty());
    project.output_group_previews[0].emplace();
    project.output_group_previews[0]->files.push_back({
        .path = std::string(180, 'p'), .bytes = 123});
    project.output_group_previews[0]->converted.diagnostics.push_back({
        .severity = wormedit::diagnostic_severity_t::warning, .message = std::string(180, 'w')});
    project.output_group_plan_paths[0].clear();
    frame.view.setup.refill(project, document);
    options_runtime_t options;
    open_options(options, wormedit::program_preferences_t{});
    options.draft.audio.osu_skin_directory = std::string(180, 's');
    frame.view.options.refill(options);
    wormedit::command_registry_t registry;
    for (const auto* id : {"test.first", "test.second"}) {
        REQUIRE(wormedit::register_command(registry, {.definition = {
            .id = {id}, .display_name = id, .localization_key = id,
            .category = wormedit::command_category_t::edit,
            .focus_scope = wormedit::command_focus_scope_t::global,
            .default_shortcuts = {{.key = wormedit::shortcut_key_t::s}},
            .enabled = true,
            .invoke = [](const wormedit::command_invocation_context_t&) {
                return wormedit::dispatch_outcome_t{.value = true};
            }}}).value);
    }
    shortcut_rows_projection_t shortcuts;
    shortcuts.refill(registry, wormedit::shortcut_primary_modifier_t::control);
    frame.view.options.shortcut_rows.assign(shortcuts.rows().begin(), shortcuts.rows().end());
    const auto* plans = frame.view.setup.output_groups.front().plan_rows.data();
    const auto before = frame_heap_allocation_snapshot();
    frame.reset();
    frame.view.setup.visible = true;
    frame.view.setup.refill(project, document);
    frame.view.options.refill(options);
    shortcuts.refill(registry, wormedit::shortcut_primary_modifier_t::control);
    frame.view.options.shortcut_rows.assign(shortcuts.rows().begin(), shortcuts.rows().end());
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(after.bytes == before.bytes);
    REQUIRE(frame.view.setup.output_groups.front().plan_rows.size() == 1);
    check_frame_storage(frame.view.setup.output_groups.front().plan_rows.data() == plans);
    check_frame_storage(frame.view.setup.output_groups.front().plan_rows.front().path.size() == 180);
    check_frame_storage(frame.view.setup.output_groups.front().diagnostics.front().message.size() == 180);
    REQUIRE(frame.view.options.shortcut_rows.size() == 2);
    check_frame_storage(frame.view.options.shortcut_rows.front().conflict);
}
namespace {
struct metadata_supplemental_fixture_t : metadata_text_fixture_t {
    frame_arena_t arena{{.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024}};
    frontend_workspace_view_t frame{&arena};
    metadata_supplemental_fixture_t() {
        base = std::filesystem::path{"/tmp"} / std::string(180, 'p');
        history.document.audio_assets.front().source_path = std::string(180, 'm') + ".wav";
        history.document.output_groups.front().relative_output_path = std::string(180, 'o') + ".bms";
        auto& chart = history.document.charts.front();
        const auto profile = wormedit::make_bms_7k_profile();
        chart.source_family = wormedit::format_family_t::bms;
        chart.lane_layouts.push_back({.id = wormedit::object_id_t{10}, .layout = profile.layout});
        chart.input_profile = profile.input;
        std::vector<wormedit::channel_map_replacement_t> replacements;
        for (const auto& lane : profile.layout.lanes)
            replacements.push_back({lane.id, lane.bms_channels});
        drop.channel_map_preview = wormedit::channel_map_proposal_t::propose(history.document,
            history.document.revision, chart.id, chart.lane_layouts.front().id, replacements);
        drop.lane_profile_preview = wormedit::lane_migration_proposal_t::propose(history.document,
            history.document.revision, chart.id, profile);
        REQUIRE(drop.channel_map_preview->value);
        REQUIRE(drop.lane_profile_preview->value);
        start_preview();
    }
    void refill() {
        arena.reset();
        frame.reset();
        frame.view.metadata.visible = true;
        frame.view.metadata.has_chart = true;
        frame.view.metadata.chart_id = wormedit::chart_id_t{1};
        populate_metadata_view_projection(frame, history, workspace, base, drop, runtime);
    }
    void check_warmed_refill() {
        const auto before = frame_heap_allocation_snapshot();
        refill();
        const auto after = frame_heap_allocation_snapshot();
        check_frame_storage(after.count == before.count);
        check_frame_storage(after.bytes == before.bytes);
    }
    void check_populated() const {
        const auto& metadata = frame.view.metadata;
        check_frame_storage(metadata.project_directory == base.generic_string());
        check_frame_storage(metadata.selected_music_source ==
                            history.document.audio_assets.front().source_path.generic_string());
        check_frame_storage(metadata.output_groups.size() == 1);
        check_frame_storage(!metadata.copy_preview_source.empty());
        check_frame_storage(metadata.channel_map_preview_present);
        check_frame_storage(metadata.lane_profile_preview_present);
        check_frame_storage(metadata.lane_profile_choices.size() > 1);
        check_frame_storage(!metadata.channel_map_preview_message.empty());
        check_frame_storage(!metadata.lane_profile_preview_message.empty());
    }
};
}

TEST_CASE("Populated supplemental metadata reuses warmed paths previews and profile catalog",
          "[sys][frame-arena][metadata-text][allocation]") {
    metadata_supplemental_fixture_t fixture;
    fixture.refill();
    fixture.refill();
    fixture.check_warmed_refill();
    fixture.check_populated();
    check_frame_storage(fixture.frame.view.metadata.channel_map_preview_valid);
    check_frame_storage(fixture.frame.view.metadata.lane_profile_preview_valid);
    check_frame_storage(fixture.frame.view.metadata.channel_map_preview_affected_note_count == 0);
    check_frame_storage(fixture.frame.view.metadata.channel_map_preview_conflict_count == 0);
    check_frame_storage(fixture.frame.view.metadata.lane_profile_mapped_note_count == 0);
    check_frame_storage(fixture.frame.view.metadata.lane_profile_dropped_note_count == 0);
    check_frame_storage(fixture.frame.view.metadata.lane_profile_collision_count == 0);
    check_frame_storage(fixture.frame.metadata_projection_cache.lane_targets.build_count() == 1);
    check_frame_storage(fixture.frame.metadata_projection_cache.channel_summary.build_count() == 1);
}

TEST_CASE("Supplemental metadata caches invalidate changed paths previews and reopened charts",
          "[sys][frame-arena][metadata-text]") {
    metadata_supplemental_fixture_t fixture;
    fixture.refill();
    fixture.base /= "changed";
    fixture.history.document.audio_assets.front().source_path = std::string(190, 'n') + ".wav";
    auto& group = fixture.history.document.output_groups.front();
    group.relative_output_path = "different/changed.bms";
    const auto stamp = wormedit::chart_stamp_t{
        .document = wormedit::document_stamp_t::from(fixture.history.document),
        .chart = wormedit::chart_id_t{1}};
    fixture.drop.channel_map_preview = wormedit::channel_map_proposal_outcome_t{};
    fixture.drop.channel_map_preview->diagnostics.push_back(wormedit::channel_map_diagnostic_t::from(
        wormedit::channel_map_issue_t::duplicate_channel, std::string(160, 'd'), stamp, {}));
    fixture.refill();
    fixture.check_populated();
    check_frame_storage(fixture.frame.view.metadata.channel_map_preview_message == std::string(160, 'd'));
    check_frame_storage(std::string_view{fixture.frame.view.metadata.output_groups.front().relative_output_path}
                        == "different/changed.bms");
    fixture.drop.channel_map_preview->diagnostics.front().message = std::string(170, 'e');
    fixture.history.document.charts.front().lane_layouts.front().layout = wormedit::make_bms_5k_profile().layout;
    ++fixture.frame.query_session_generation;
    fixture.refill();
    check_frame_storage(fixture.frame.view.metadata.channel_map_preview_message == std::string(170, 'e'));
    check_frame_storage(fixture.frame.metadata_projection_cache.channel_summary.build_count() == 3);
    check_frame_storage(fixture.frame.metadata_projection_cache.lane_targets.build_count() == 2);
    check_frame_storage(fixture.frame.view.metadata.lane_profile_current + 1 <
                        fixture.frame.view.metadata.lane_profile_choices.size());
    fixture.check_warmed_refill();
    group.destination_kind = wormedit::output_destination_kind_t::file;
    fixture.refill();
    check_frame_storage(std::string_view{fixture.frame.view.metadata.output_groups.front().relative_asset_directory}
                        == "different/assets");
    ++fixture.history.document.revision;
    fixture.refill();
    check_frame_storage(fixture.frame.metadata_projection_cache.lane_targets.build_count() == 3);
    fixture.check_warmed_refill();
}

TEST_CASE("Output suggestions query long profile identities without copying text",
          "[sys][project-options][allocation]") {
    wormedit::chart_document_t chart;
    chart.lane_layouts.emplace_back();
    chart.lane_layouts.front().layout.profile_id.assign(180, 'p');
    const auto before = frame_heap_allocation_snapshot();
    const auto suggestions = wormedit::suggested_output_groups(chart);
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(suggestions.empty());
    check_frame_storage(after.count == before.count);
    check_frame_storage(after.bytes == before.bytes);
}

TEST_CASE("Compose sound families retain populated row storage without warmed allocations",
          "[sys][compose-resource][sound-group][sound-picker][steady-state]") {
    auto document = compose_resource_document(129);
    document.id = wormedit::song_id_t{4};
    document.revision = 7;
    document.charts.push_back({.id = wormedit::chart_id_t{1},
        .source_family = wormedit::format_family_t::bms});
    document.sound_groups.push_back({.id = wormedit::sound_group_id_t{9},
                                    .name = std::string(80, 'g')});
    document.resources.front().sound_group = wormedit::sound_group_id_t{9};
    document.charts.front().semantic_layers.push_back({.id = wormedit::layer_id_t{3},
                                                      .name = std::string(80, 'l')});
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{1}};
    wormedit::ui::keysound_dock_request_t request{.sound_picker_open = true,
        .include_ungrouped = true, .automatic_row_limit = 64};
    compose_resource_catalog_cache_t resources;
    keysound_list_cache_t keysounds;
    frontend_workspace_view_t frame;
    const std::string status = "ready";
    auto refill = [&] {
        frame.refill(model, document, status, {}, false, nullptr,
            dock_sort_t::natural_name, request, {}, {}, &keysounds, {}, &resources);
    };
    refill();
    REQUIRE(frame.view.compose.resources.size() == 129);
    check_frame_storage(frame.view.compose.resources.back().resource_id == wormedit::resource_id_t{129});
    REQUIRE_FALSE(frame.view.compose.keysounds.empty());
    REQUIRE_FALSE(frame.view.compose.keysound_display_rows.empty());
    REQUIRE(frame.view.compose.sound_picker.rows.size() == 20);
    const auto* rows = frame.view.compose.resources.data();
    const auto before = frame_heap_allocation_snapshot();
    refill();
    const auto after = frame_heap_allocation_snapshot();
    check_frame_storage(after.count == before.count);
    check_frame_storage(after.bytes == before.bytes);
    check_frame_storage(frame.view.compose.resources.data() == rows);
    document.resources.front().name = "Reopened resource";
    document.sound_groups.front().name = "Reopened group";
    ++frame.query_session_generation;
    refill();
    check_frame_storage(frame.view.compose.resources.front().name == "Reopened resource");
    check_frame_storage(frame.view.compose.compose_sound_groups.front().name == "Reopened group");
    request.slots_mode = true;
    request.row_limit = 64;
    refill();
    REQUIRE(frame.view.compose.sound_slots.size() == 64);
    const auto slots_before = frame_heap_allocation_snapshot();
    refill();
    const auto slots_after = frame_heap_allocation_snapshot();
    check_frame_storage(slots_after.count == slots_before.count);
    check_frame_storage(slots_after.bytes == slots_before.bytes);
}
