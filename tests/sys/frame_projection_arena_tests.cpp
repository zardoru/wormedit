#include "sys/catalog_view.h"
#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/catalogs.h>
#include <wormedit/formats/bms.h>
#include "sys/frame_timing.h"
#include "sys/workspace_view.h"
#include <wormedit/core/lane_profiles.h>
#include <wormedit/audio/playback_rate.h>
#include "sys/timing_view.h"
#include "sys/metadata_view.h"
#include <array>
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
    auto project(frame_arena_t* arena = nullptr) -> flattened_workspace_t {
        flattened_workspace_t frame(arena);
        frame.view.metadata.visible = true;
        frame.view.metadata.chart_id = 1;
        populate_metadata_view_projection(frame, history, workspace, base, drop, runtime);
        return frame;
    }
    void start_preview() {
        WormUiWorkspaceAction action{};
        action.compose_input_chart_id = 1;
        action.compose_input_document_revision = history.document.revision;
        action.compose_resource_asset_id = 8;
        action.metadata_inspect_import = true;
        std::strcpy(action.compose_import_path, "/tmp/arena-copy-preview-source.wav");
        const auto status = runtime.start_copy_preview(action, history, workspace, base);
        INFO(status);
        REQUIRE(runtime.copy_preview_key());
    }
};
void check_metadata_text(const WormUiMetadataView& view) {
    check_frame_storage(std::strcmp(view.project_directory, "/tmp/metadata-arena-project-directory") == 0);
    check_frame_storage(std::strcmp(view.selected_music_source, "audio/a-long-selected-music-source.wav") == 0);
    check_frame_storage(std::strcmp(view.dropped_music_path, "/tmp/a-long-dropped-audio-source.wav") == 0);
    check_frame_storage(std::strcmp(view.dropped_output_relative_path, "exports/a-long-dropped-output.bms") == 0);
    check_frame_storage(view.selected_music_asset_id == 7);
    REQUIRE(view.output_group_count == 1);
    check_frame_storage(std::strcmp(view.output_groups[0].name, "Long retained output group name") == 0);
    check_frame_storage(std::strcmp(view.output_groups[0].relative_output_path, "exports/chart.bms") == 0);
    check_frame_storage(std::strcmp(view.output_groups[0].relative_asset_directory, "exports/assets") == 0);
    check_frame_storage(view.output_groups[0].document_index == 0);
}
}

TEST_CASE("Metadata frame text and output groups survive source mutation and projection moves",
          "[sys][frame-arena][metadata-text]") {
    flattened_workspace_t retained;
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
    check_frame_storage(!moved.metadata.text_truncated);
    check_frame_storage(std::strcmp(moved.view.metadata.copy_preview_text,
        "Inspecting destination bytes in the background...") == 0);
    check_frame_storage(std::strcmp(moved.view.metadata.copy_preview_source,
        "/tmp/arena-copy-preview-source.wav") == 0);
}

TEST_CASE("Metadata text exhaustion publishes null paths and complete output group prefixes",
          "[sys][frame-arena][metadata-text]") {
    metadata_text_fixture_t fixture;
    fixture.start_preview();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    auto failed = fixture.project(&tiny);
    check_frame_storage(failed.metadata.text_truncated);
    check_frame_storage(failed.view.metadata.project_directory == nullptr);
    check_frame_storage(failed.view.metadata.selected_music_source == nullptr);
    check_frame_storage(failed.view.metadata.copy_preview_source == nullptr);
    check_frame_storage(failed.view.metadata.output_group_count == 0);
    check_frame_storage(failed.view.metadata.output_groups_truncated);
    const auto* diagnostic = failed.view.metadata.copy_preview_text;
    REQUIRE(diagnostic);
    tiny.reset();
    check_frame_storage(std::strcmp(diagnostic,
        "Metadata display text truncated (frame storage limit).") == 0);
    fixture.runtime.clear_copy_preview_key();
    auto second = fixture.history.document.output_groups.front();
    second.name.assign(4096, 'x');
    fixture.history.document.output_groups.push_back(std::move(second));
    frame_arena_t limited({.chunk_bytes = 1024, .maximum_bytes = 1024});
    auto prefix = fixture.project(&limited);
    check_metadata_text(prefix.view.metadata);
    check_frame_storage(prefix.view.metadata.output_groups_truncated);
}

TEST_CASE("Metadata output groups retain their capacity and selected chart filtering",
          "[sys][frame-arena][metadata-text]") {
    metadata_text_fixture_t fixture;
    auto& groups = fixture.history.document.output_groups;
    groups.resize(WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY, groups.front());
    groups.front().charts = {wormedit::chart_id_t{2}};
    auto accepted = fixture.project();
    check_frame_storage(accepted.view.metadata.output_group_count == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY - 1);
    check_frame_storage(accepted.view.metadata.output_groups[0].document_index == 1);
    check_frame_storage(!accepted.view.metadata.output_groups_truncated);
    groups.push_back(groups.back());
    auto exact = fixture.project();
    check_frame_storage(exact.view.metadata.output_group_count == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY);
    check_frame_storage(!exact.view.metadata.output_groups_truncated);
    groups.push_back(groups.back());
    auto truncated = fixture.project();
    check_frame_storage(truncated.view.metadata.output_group_count == WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY);
    check_frame_storage(truncated.view.metadata.output_groups_truncated);
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
            !frame.metadata.text_truncated && frame.view.metadata.output_group_count == 1;
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
                            frame_arena_t* arena = nullptr,
                            const wormedit::stepmania_export_policy_t& policy = {})
    -> flattened_workspace_t {
    return flatten_workspace(setup_arena_model(), {}, "ready", recovery, true, policy, arena);
}
void check_recovery_arena_rows(const WormUiWorkspaceView& view) {
    REQUIRE(view.recovery_candidate_count == 2);
    const auto& active = view.recovery_candidates[0];
    check_frame_storage(std::strcmp(active.path_label, "/tmp/a-long-actionable-recovery-path.wormedit") == 0);
    check_frame_storage(std::strcmp(active.summary, "valid · newer") == 0);
    check_frame_storage(active.disabled_reason == nullptr);
    check_frame_storage(active.action_mask == (WORM_UI_RECOVERY_ACTION_INSPECT |
        WORM_UI_RECOVERY_ACTION_RECOVER | WORM_UI_RECOVERY_ACTION_SAVE_AS |
        WORM_UI_RECOVERY_ACTION_DISCARD | WORM_UI_RECOVERY_ACTION_QUARANTINE));
    check_frame_storage(active.scan_generation == 9 && active.row_index == 2 && active.candidate_index == 8);
    const auto& inactive = view.recovery_candidates[1];
    check_frame_storage(std::strcmp(inactive.summary,
        "invalid · older · revision 42 · session 0 · recommended recovery") == 0);
    check_frame_storage(std::strcmp(inactive.disabled_reason,
        "No safe action is available for this candidate.") == 0);
    check_frame_storage(inactive.action_mask == 0);
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
    REQUIRE(moved.view.recovery_diagnostic_count == 2);
    check_frame_storage(std::strcmp(moved.view.recovery_diagnostics[0],
        "First retained recovery diagnostic (recovery/candidate.wormedit)") == 0);
    check_frame_storage(std::strcmp(moved.view.recovery_diagnostics[1],
        "Second retained recovery diagnostic") == 0);
    check_frame_storage(moved.view.setup.stepmania_policy_valid);
    check_frame_storage(std::strcmp(moved.view.setup.stepmania_policy_status,
        "Compatible StepMania export policy.") == 0);
}

TEST_CASE("Setup arena text preserves warnings and rejected policy semantics",
          "[sys][frame-arena][setup-recovery]") {
    flattened_workspace_t hidden;
    hidden.rebind_view_pointers();
    auto moved_hidden = std::move(hidden);
    moved_hidden.rebind_view_pointers();
    REQUIRE(moved_hidden.view.setup.stepmania_policy_status);
    check_frame_storage(!moved_hidden.view.setup.visible);
    check_frame_storage(moved_hidden.view.setup.stepmania_policy_status[0] == '\0');
    wormedit::stepmania_export_policy_t policy;
    policy.file_profile = wormedit::stepmania_profile_t::sm;
    auto warning = project_setup_recovery({}, nullptr, policy);
    check_frame_storage(warning.view.setup.stepmania_policy_valid);
    check_frame_storage(std::strcmp(warning.view.setup.stepmania_policy_status,
        "Valid syntax policy; SM charts containing warps require a legacy encoding instead.") == 0);
    policy.timing_syntax = wormedit::stepmania_timing_syntax_t::unspecified;
    auto rejected = project_setup_recovery({}, nullptr, policy);
    auto moved = std::move(rejected);
    moved.rebind_view_pointers();
    check_frame_storage(!moved.view.setup.stepmania_policy_valid);
    check_frame_storage(std::strcmp(moved.view.setup.stepmania_policy_status,
        "Select a StepMania timing syntax explicitly") == 0);
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
    check_frame_storage(std::strcmp(frame.view.recovery_candidates[0].summary,
        "valid · newer · revision 18446744073709551615 · session 18446744073709551615") == 0);
    check_frame_storage(std::strcmp(frame.view.recovery_candidates[0].disabled_reason,
        "Unavailable actions are disabled by the recovery controller's safety checks.") == 0);
    check_frame_storage(frame.view.recovery_candidates[0].action_mask == WORM_UI_RECOVERY_ACTION_INSPECT);
    recovery.rows.resize(WORM_UI_RECOVERY_ROW_CAPACITY + 1, active);
    recovery.diagnostics.resize(WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY + 1, recovery.diagnostics.front());
    const auto bounded = project_setup_recovery(recovery);
    check_frame_storage(bounded.view.recovery_candidate_count == WORM_UI_RECOVERY_ROW_CAPACITY);
    check_frame_storage(bounded.view.recovery_rows_truncated);
    check_frame_storage(bounded.view.recovery_diagnostic_count == WORM_UI_RECOVERY_DIAGNOSTIC_CAPACITY);
}

TEST_CASE("Setup and Recovery exhaustion retains immutable notices and only complete rows",
          "[sys][frame-arena][setup-recovery]") {
    const auto recovery = recovery_arena_model();
    frame_arena_t tiny({.chunk_bytes = 8, .maximum_bytes = 8});
    const auto failed = project_setup_recovery(recovery, &tiny);
    check_frame_storage(failed.view.recovery_candidate_count == 0);
    check_frame_storage(failed.view.recovery_rows_truncated);
    check_frame_storage(failed.setup.text_truncated);
    check_frame_storage(failed.view.setup.stepmania_policy_valid);
    const auto* notice = failed.view.recovery_notice;
    const auto* diagnostic = failed.view.recovery_diagnostics[0];
    const auto* setup = failed.view.setup.stepmania_policy_status;
    tiny.reset();
    check_frame_storage(std::strcmp(notice, "Recovery display text truncated (frame storage limit).") == 0);
    check_frame_storage(std::strcmp(diagnostic, "Recovery display text truncated (frame storage limit).") == 0);
    check_frame_storage(std::strcmp(setup, "Setup display text truncated (frame storage limit).") == 0);
    auto prefix_model = recovery_arena_model();
    prefix_model.rows[0].path_label.assign(4096, 'p');
    frame_arena_t limited({.chunk_bytes = 512, .maximum_bytes = 512});
    const auto prefix = project_setup_recovery(prefix_model, &limited);
    check_frame_storage(prefix.view.recovery_candidate_count == 1);
    check_frame_storage(prefix.view.recovery_rows_truncated);
    check_frame_storage(prefix.view.recovery_candidates[0].candidate_index == 8);
}

TEST_CASE("Warmed Setup and Recovery arena copies allocate no heap across 1000 projections",
          "[sys][frame-arena][setup-recovery]") {
    auto recovery = recovery_arena_model();
    recovery.diagnostics[0].path.clear();
    const auto model = setup_arena_model();
    const wormedit::song_document_t document;
    const std::string status = "ready";
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 2 * 1024 * 1024});
    auto project = [&] { return flatten_workspace(model, document, status, recovery, true, {}, &arena); };
    (void)project();
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    bool stable = true;
    bool zero_new = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        const auto frame = project();
        const auto finish = frame_heap_allocation_snapshot();
        zero_new = zero_new && finish.count == start.count;
        stable = stable && arena.used_bytes() == used && arena.committed_bytes() == committed &&
            frame.view.recovery_candidate_count == 2 && !frame.setup.text_truncated;
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
                                   frame_arena_t* arena = nullptr) -> flattened_workspace_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{1}};
    return flatten_workspace(model, document, "ready", {}, false, {}, arena);
}
void check_compose_output_group(const WormUiComposeOutputGroupView& group) {
    check_frame_storage(group.document_index == 1);
    check_frame_storage(std::strcmp(group.name, "Independently owned Compose output group") == 0);
    check_frame_storage(std::strcmp(group.relative_output_path, "exports/chart.bms") == 0);
    check_frame_storage(std::strcmp(group.relative_asset_directory, "exports/assets") == 0);
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
                           WormUiKeysoundDockRequest request = {}) -> flattened_workspace_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{1}};
    model.transport.audio_asset = wormedit::asset_id_t{1};
    model.selected_visual_asset = wormedit::asset_id_t{7};
    return flatten_workspace(model, document, "ready", {}, false, {}, arena,
                             dock_sort_t::natural_name, request);
}
}
TEST_CASE("Compose resource arena owns complete rows after source mutation and move",
          "[sys][frame-arena][compose-resource]") {
    auto document = compose_resource_document(2);
    document.resources.push_back({.id = wormedit::resource_id_t{9},
        .roles = {wormedit::resource_role_t::preview}});
    auto frame = project_resource_rows(document);
    REQUIRE(frame.view.compose.resource_count == 2);
    check_frame_storage(frame.view.compose.resource_total_count == 2);
    document.resources.clear();
    document.audio_assets.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& row = moved.view.compose.resources[0];
    check_frame_storage(row.resource_id == 1 && row.asset_id == 1);
    check_frame_storage(row.audio_asset && row.selected_audio_asset && !row.missing);
    check_frame_storage(row.roles == (WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE | WORM_UI_COMPOSE_RESOURCE_ROLE_BGM));
    check_frame_storage(std::strcmp(row.target_index, "#WAV01") == 0);
    check_frame_storage(std::strlen(row.name) == 81);
    check_frame_storage(std::strlen(row.source_name) == 84);
}
TEST_CASE("Compose resource arena preserves capacity and degrades storage exhaustion",
          "[sys][frame-arena][compose-resource]") {
    auto document = compose_resource_document(129);
    auto frame = project_resource_rows(document);
    REQUIRE(frame.view.compose.resource_count == 128);
    check_frame_storage(frame.view.compose.resource_total_count == 129);
    check_frame_storage(frame.view.compose.resources_truncated);
    check_frame_storage(frame.view.compose.resources[127].resource_id == 128);
    frame_arena_t tiny({.chunk_bytes = 64, .maximum_bytes = 64});
    auto failed = project_resource_rows(document, &tiny);
    check_frame_storage(failed.view.compose.resource_count == 0);
    check_frame_storage(failed.view.compose.resource_total_count == 129);
    check_frame_storage(failed.view.compose.resources_truncated);
    check_frame_storage(failed.view.compose.resources == nullptr);
}
TEST_CASE("Compose resource arena warmed dense projections retain stable storage",
          "[sys][frame-arena][compose-resource]") {
    const auto document = compose_resource_document(128);
    frame_arena_t arena({.chunk_bytes = 1024 * 1024, .maximum_bytes = 1024 * 1024});
    (void)project_resource_rows(document, &arena);
    const auto used = arena.used_bytes();
    const auto committed = arena.committed_bytes();
    bool stable = true;
    std::uint64_t allocations{};
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto before = frame_heap_allocation_snapshot();
        const auto frame = project_resource_rows(document, &arena);
        const auto after = frame_heap_allocation_snapshot();
        allocations = after.count - before.count;
        stable = stable && frame.view.compose.resource_count == 128 &&
            !frame.view.compose.resources_truncated && arena.used_bytes() == used &&
            arena.committed_bytes() == committed;
    }
    INFO("Remaining temporary catalog/path allocations per projection: " << allocations);
    check_frame_storage(stable);
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
    REQUIRE(frame.view.compose.resource_count == 2);
    const auto& visual = frame.view.compose.resources[0];
    check_frame_storage(visual.image_asset && visual.selected_visual_asset && !visual.audio_asset && !visual.missing);
    check_frame_storage(std::strcmp(visual.target_index, "#17") == 0);
    check_frame_storage(std::strcmp(visual.name, "Resource 17") == 0);
    check_frame_storage(std::strcmp(visual.source_name, "banner.png") == 0);
    const auto& missing = frame.view.compose.resources[1];
    check_frame_storage(missing.missing && !missing.image_asset && !missing.audio_asset);
    check_frame_storage(std::strcmp(missing.source_name, "") == 0);
}
TEST_CASE("Compose resource arena exhaustion publishes only complete string prefixes",
          "[sys][frame-arena][compose-resource]") {
    const auto document = compose_resource_document(2);
    bool found_prefix = false;
    for (std::size_t limit = 128; limit <= 1024; ++limit) {
        frame_arena_t arena({.chunk_bytes = limit, .maximum_bytes = limit});
        const auto frame = project_resource_rows(document, &arena);
        if (frame.view.compose.resource_count != 1) continue;
        REQUIRE(frame.view.compose.resources_truncated);
        check_frame_storage(frame.view.compose.resource_total_count == 2);
        const auto& first = frame.view.compose.resources[0];
        check_frame_storage(first.resource_id == 1 && std::strlen(first.name) == 81);
        check_frame_storage(std::strcmp(first.target_index, "#WAV01") == 0);
        check_frame_storage(std::strlen(first.source_name) == 84);
        found_prefix = true;
        break;
    }
    check_frame_storage(found_prefix);
}
TEST_CASE("Compose resource arena empty catalogs do not report truncation",
          "[sys][frame-arena][compose-resource]") {
    wormedit::song_document_t document;
    auto empty = project_resource_rows(document);
    check_frame_storage(empty.view.compose.resource_count == 0);
    check_frame_storage(empty.view.compose.resource_total_count == 0);
    check_frame_storage(!empty.view.compose.resources_truncated);
    document.resources.push_back({.id = wormedit::resource_id_t{9},
        .roles = {wormedit::resource_role_t::preview}});
    auto hidden = project_resource_rows(document);
    check_frame_storage(hidden.view.compose.resource_count == 0);
    check_frame_storage(hidden.view.compose.resource_total_count == 0);
    check_frame_storage(!hidden.view.compose.resources_truncated);
}
TEST_CASE("Compose resource external arena reset republishes updated text",
          "[sys][frame-arena][compose-resource]") {
    auto document = compose_resource_document(1);
    frame_arena_t arena({.chunk_bytes = 4096, .maximum_bytes = 4096});
    {
        auto first = project_resource_rows(document, &arena);
        auto moved = std::move(first);
        moved.rebind_view_pointers();
        REQUIRE(moved.view.compose.resource_count == 1);
        check_frame_storage(std::strlen(moved.view.compose.resources[0].name) == 81);
    }
    arena.reset();
    document.resources[0].name = "Updated second frame resource label";
    document.audio_assets[0].source_path = "new/path/updated.wav";
    auto second = project_resource_rows(document, &arena);
    REQUIRE(second.view.compose.resource_count == 1);
    check_frame_storage(std::strcmp(second.view.compose.resources[0].name,
        "Updated second frame resource label") == 0);
    check_frame_storage(std::strcmp(second.view.compose.resources[0].source_name,
        "updated.wav") == 0);
    check_frame_storage(!second.view.compose.resources_truncated);
}

namespace {
auto branch_arena_model(std::size_t count = 3) -> wormedit::workspace_view_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    wormedit::workspace_compose_view_t tab{.chart = wormedit::chart_id_t{1}};
    auto& outline = tab.bms_branch_outline;
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
    -> flattened_workspace_t {
    return flatten_workspace(model, {}, "ready", {}, false, {}, arena);
}
auto branch_outline(wormedit::workspace_view_t& model)
    -> wormedit::workspace_bms_branch_outline_view_t& {
    return std::get<wormedit::workspace_compose_view_t>(model.tab_view).bms_branch_outline;
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
void check_branch_in_arena(const flattened_workspace_t& frame, std::uintptr_t begin, std::uintptr_t end) {
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
    const auto first = arena.allocate_bytes(1, 1);
    auto frame = project_branch_arena(model, &arena);
    const auto last = arena.allocate_bytes(1, 1);
    REQUIRE((first && last));
    check_branch_in_arena(frame, reinterpret_cast<std::uintptr_t>(first.data), reinterpret_cast<std::uintptr_t>(last.data));
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
    const auto& source = std::get<wormedit::workspace_compose_view_t>(model.tab_view).bms_branch_outline;
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
    (void)project_branch_arena(model, &arena);
    const auto used = arena.used_bytes(), committed = arena.committed_bytes();
    arena.reset();
    const auto before = frame_heap_allocation_snapshot();
    (void)project_branch_arena(model, &arena);
    const auto remaining = frame_heap_allocation_snapshot().count - before.count;
    bool stable = true;
    for (int iteration = 0; iteration < 1000; ++iteration) {
        arena.reset();
        const auto start = frame_heap_allocation_snapshot();
        const auto frame = project_branch_arena(model, &arena);
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
    REQUIRE(frame.view.compose.compose_sound_group_count == 1);
    REQUIRE(frame.view.compose.keysound_count == 2);
    document.sound_groups.front().name = "Changed";
    document.resources.front().sound_group.reset();
    auto moved = std::move(frame);
    check_frame_storage(moved.view.compose.compose_sound_groups[0].group_id == 9);
    check_frame_storage(std::strcmp(moved.view.compose.compose_sound_groups[0].name, "Piano") == 0);
    const auto* rows = moved.view.compose.keysounds;
    const auto grouped = rows[0].resource_id == 1 ? 0 : 1;
    check_frame_storage(rows[grouped].sound_group_id == 9);
    check_frame_storage(rows[1 - grouped].sound_group_id == 0);
    auto current = project_resource_rows(document);
    check_frame_storage(current.view.compose.keysounds[grouped].sound_group_id == 0);
    check_frame_storage(std::strcmp(current.view.compose.compose_sound_groups[0].name, "Changed") == 0);
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
    WormUiKeysoundDockRequest request{};
    request.collapsed_group_ids = collapsed.data();
    request.collapsed_group_count = collapsed.size();
    request.selected_resource_ids = selected.data();
    request.selected_resource_count = selected.size();
    request.primary_resource_id = 4;
    request.automatic_first_row = 2;
    request.automatic_row_limit = 3;
    auto frame = project_resource_rows(document, nullptr, request);
    REQUIRE(frame.view.compose.keysound_display_row_count == 3);
    REQUIRE(frame.view.compose.keysound_count == 1);
    REQUIRE(frame.view.compose.keysound_selection_row_count == 1);
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
    check_frame_storage(view.keysound_display_rows[0].resource_id == 4);
    check_frame_storage(view.keysound_display_rows[1].group_id == 9);
    check_frame_storage(std::strcmp(view.keysound_display_rows[1].group_name, "Piano") == 0);
    check_frame_storage(!view.keysound_display_rows[1].expanded);
    check_frame_storage(view.keysound_display_rows[1].group_sound_count == 2);
    check_frame_storage(std::strcmp(view.keysound_display_rows[2].group_name, "Ungrouped") == 0);
    check_frame_storage(std::strcmp(view.keysounds[0].name, "Sample 4") == 0);
    check_frame_storage(view.keysound_selected_row_count == 0);
    check_frame_storage(view.keysound_selection_rows[0].resource_id == 1);
    check_frame_storage(view.keysound_selection_rows[0].group_id == 9);
    check_frame_storage(std::strcmp(view.keysound_selection_rows[0].slot, "01") == 0);
    check_frame_storage(view.keysound_all_resource_count == 5);
    check_frame_storage(view.keysound_advance_row_count == 5);
    check_frame_storage(view.keysound_advance_rows[0].resource_id == 3);
    check_frame_storage(view.keysound_advance_rows[2].resource_id == 1);
    check_frame_storage(view.keysound_listed_resource_count == 3);
    check_frame_storage(view.keysound_listed_resource_ids[0] == 3);
    check_frame_storage(view.keysound_listed_resource_ids[2] == 5);
}

namespace {
auto keysound_arrays_are_consistent(const WormUiComposeView& view) -> bool {
    return (view.keysound_count == 0 || view.keysounds) &&
           (view.keysound_display_row_count == 0 || view.keysound_display_rows) &&
           (view.keysound_selected_row_count == 0 || view.keysound_selected_rows) &&
           (view.keysound_selection_row_count == 0 || view.keysound_selection_rows) &&
           (view.keysound_filter_layer_count == 0 || view.keysound_filter_layers) &&
           (view.keysound_advance_row_count == 0 || view.keysound_advance_rows) &&
           (view.keysound_listed_resource_count == 0 || view.keysound_listed_resource_ids) &&
           (view.keysound_all_resource_count == 0 || view.keysound_all_resource_ids);
}
}

TEST_CASE("Sound section arena exhaustion preserves count and pointer consistency",
          "[sys][frame-arena][sound-group][sound-filter]") {
    auto document = compose_resource_document(5);
    document.charts.push_back({.id = wormedit::chart_id_t{1}});
    document.charts.front().semantic_layers.push_back(
        {.id = wormedit::layer_id_t{7}, .name = "Blue"});
    document.sound_groups = {{.id = wormedit::sound_group_id_t{9}, .name = "Piano"}};
    for (auto& resource : document.resources)
        resource.sound_group = wormedit::sound_group_id_t{9};
    const std::array<std::uint64_t, 5> selected{1, 2, 3, 4, 5};
    WormUiKeysoundDockRequest request{};
    request.selected_resource_ids = selected.data();
    request.selected_resource_count = selected.size();
    request.include_selected_metadata = true;
    request.automatic_row_limit = 3;
    request.filter_layer_row_limit = 64;
    bool saw_rows = false, saw_truncation = false;
    for (const auto budget : {64U, 128U, 256U, 512U, 1024U, 2048U, 4096U, 8192U}) {
        frame_arena_t arena({.chunk_bytes = budget, .maximum_bytes = budget});
        auto frame = project_resource_rows(document, &arena, request);
        frame.rebind_view_pointers();
        INFO("Arena budget " << budget);
        check_frame_storage(keysound_arrays_are_consistent(frame.view.compose));
        saw_rows = saw_rows || frame.view.compose.keysound_display_row_count != 0;
        saw_truncation = saw_truncation || frame.view.compose.keysounds_truncated;
    }
    check_frame_storage(saw_rows && saw_truncation);
    check_frame_storage(document.resources.size() == 5);
    check_frame_storage(document.resources.front().sound_group == wormedit::sound_group_id_t{9});
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
    auto frame = flatten_workspace(model, document, "ready", {}, false, {}, nullptr,
        dock_sort_t::natural_name, {}, {.selected_project_id = 8, .selected_resource_id = 1, .find_uses_open = true});
    REQUIRE(frame.view.audio_slices.usage_row_count == 1);
    document.charts.front().metadata.name = "Changed";
    document.charts.front().measures.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    const auto& view = moved.view.audio_slices;
    check_frame_storage(view.usage_valid);
    check_frame_storage(view.usage_song_id == 8 && view.usage_document_revision == 11);
    check_frame_storage(view.usage_total_count == 1 && !view.usage_rows_truncated);
    check_frame_storage(view.usage_rows[0].object_id == 60);
    check_frame_storage(std::strcmp(view.usage_rows[0].chart_label, "Difficulty") == 0);
    check_frame_storage(std::strcmp(view.usage_rows[0].position_label, "Measure 1, row 120") == 0);
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
    -> flattened_workspace_t {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::compose;
    model.tab_view = wormedit::workspace_compose_view_t{.chart = wormedit::chart_id_t{2}};
    return flatten_workspace(model, document, "ready", {}, false, {}, arena,
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
    WormUiKeysoundDockRequest request{};
    request.sound_picker_open = true;
    std::strcpy(request.sound_picker_query, "SSR");
    std::strcpy(request.automatic_search, "no dock matches");
    request.automatic_first_row = 100;
    request.automatic_row_limit = 1;
    auto frame = project_resource_rows(document, nullptr, request);
    check_frame_storage(frame.view.compose.keysound_count == 0);
    check_frame_storage(frame.view.compose.sound_picker.ready);
    check_frame_storage(frame.view.compose.sound_picker.total_count == 1);
    REQUIRE(frame.view.compose.sound_picker.row_count == 1);
    check_frame_storage(frame.view.compose.sound_picker.rows[0].resource_id == 300);
    document.resources.clear();
    auto moved = std::move(frame);
    moved.rebind_view_pointers();
    check_frame_storage(std::strstr(moved.view.compose.sound_picker.rows[0].label, "Super Snare Roll") != nullptr);
    request.sound_picker_open = false;
    auto closed = project_resource_rows(document, nullptr, request);
    check_frame_storage(!closed.view.compose.sound_picker.ready);
    check_frame_storage(closed.view.compose.sound_picker.row_count == 0);
    request.sound_picker_open = true;
    std::fill(std::begin(request.sound_picker_query), std::end(request.sound_picker_query), 'x');
    const auto bounded = project_resource_rows(document, nullptr, request);
    check_frame_storage(bounded.view.compose.sound_picker.ready);
    check_frame_storage(std::strlen(bounded.view.compose.sound_picker.query) == 95);
}

TEST_CASE("Project export catalog includes groups outside the selected Compose difficulty", "[sys][frame-arena][output-groups]") {
    auto document = compose_output_group_document();
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::setup;
    auto frame = flatten_workspace(model, document, "ready", {}, false, {}, nullptr);
    REQUIRE(frame.view.compose.output_group_count == document.output_groups.size());
    check_frame_storage(frame.view.compose.output_groups[0].document_index == 0);
    check_frame_storage(frame.view.compose.output_groups[1].document_index == 1);
    check_frame_storage(!frame.view.compose.output_groups_truncated);
}

TEST_CASE("Workspace projection uses the canonical Audio Slices tab label", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    model.active_tab = wormedit::workspace_tab_t::audio_slices;
    auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    CHECK(std::string_view{frame.view.tabs[WORM_UI_WORKSPACE_TAB_AUDIO_SLICES].label} == "Audio Slices");
    CHECK(std::string_view{frame.view.tab_heading} == "Audio Slices");
}

namespace {
void check_new_project_catalog_entry(const WormUiStringDescriptorView& actual,
                                     const wormedit::new_project_layout_descriptor_t& expected) {
    CHECK(std::string_view{actual.id} == expected.id);
    CHECK(std::string_view{actual.label} == expected.label);
}
} // namespace

TEST_CASE("Workspace projects every stable new-project catalog identity", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    const auto& view = frame.view.new_project_layouts;
    REQUIRE(view.count == wormedit::new_project_layouts().size());
    for (std::size_t i = 0; i < view.count; ++i)
        check_new_project_catalog_entry(view.entries[i], wormedit::new_project_layouts()[i]);
}

namespace {
void check_playback_rate_catalog_entry(const WormUiKindDescriptorView& actual,
                                       const wormedit::audio::playback_rate_descriptor_t& expected) {
    CHECK(actual.id == static_cast<std::uint32_t>(expected.id));
    CHECK(std::string_view{actual.label} == expected.label);
}
} // namespace

TEST_CASE("Workspace projects playback rate ids and labels from the catalog", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    REQUIRE(frame.view.playback_rates.count == wormedit::audio::playback_rate_catalog.size());
    for (std::size_t i = 0; i < frame.view.playback_rates.count; ++i)
        check_playback_rate_catalog_entry(frame.view.playback_rates.entries[i], wormedit::audio::playback_rate_catalog[i]);
}

TEST_CASE("Workspace offers supported BMS encodings with stable ids and language hints", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    std::size_t index = 0;
    for (const auto& encoding : wormedit::bms_text_encodings) {
        if (!encoding.offered || !wormedit::is_bms_text_encoding_supported(encoding.kind)) continue;
        REQUIRE(index < frame.view.bms_encodings.count);
        check_frame_storage(frame.view.bms_encodings.entries[index].id == static_cast<std::uint32_t>(encoding.kind));
        check_frame_storage(std::string_view{frame.view.bms_encodings.entries[index].label} == encoding.choice_label);
        ++index;
    }
    check_frame_storage(index == frame.view.bms_encodings.count);
}

TEST_CASE("Workspace output families preserve SM SSC presentation and D9 exclusions", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    const auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    const auto& output = frame.view.setup.output_families;
    std::size_t index = 0;
    for (const auto& family : wormedit::format_families) {
        if (!family.offered_as_output) continue;
        REQUIRE(index < output.count);
        check_frame_storage(output.entries[index].id == static_cast<std::uint32_t>(family.kind));
        check_frame_storage(std::string_view{output.entries[index].label} == family.output_label);
        ++index;
    }
    check_frame_storage(index == output.count);
    check_frame_storage(output.count == 5);
    check_frame_storage(output.entries[2].id == static_cast<std::uint32_t>(wormedit::format_family_t::stepmania));
    check_frame_storage(std::string_view{output.entries[2].label} == "SM/SSC");
    check_frame_storage(frame.view.metadata.format_families.count == wormedit::format_families.size());
}

namespace {
void check_timing_catalog_entry(const WormUiComposeView& view, std::size_t index) {
    const auto& expected = wormedit::timing_lanes[index];
    check_frame_storage(view.timing_lanes.entries[index].id == static_cast<std::uint32_t>(expected.kind));
    check_frame_storage(std::string_view{view.timing_lanes.entries[index].label} == expected.label);
    check_frame_storage(view.timing_lane_captions.entries[index].id == static_cast<std::uint32_t>(expected.kind));
    check_frame_storage(std::string_view{view.timing_lane_captions.entries[index].label} == expected.short_label);
}
}
TEST_CASE("Workspace projects timing and note presentation with catalog identities", "[sys][catalog-wiring]") {
    wormedit::workspace_view_t model;
    const auto frame = flatten_workspace(model, {}, "ready", {}, false, {}, nullptr);
    const auto& view = frame.view.compose;
    REQUIRE(view.timing_lanes.count == wormedit::timing_lanes.size());
    REQUIRE(view.timing_lane_captions.count == wormedit::timing_lanes.size());
    for (std::size_t index = 0; index < view.timing_lanes.count; ++index)
        check_timing_catalog_entry(view, index);
    REQUIRE(view.note_palette.count == wormedit::note_palette_kinds.size());
    for (std::size_t index = 0; index < view.note_palette.count; ++index) {
        const auto kind = wormedit::note_palette_kinds[index];
        check_frame_storage(view.note_palette.entries[index].id == static_cast<std::uint32_t>(kind));
        check_frame_storage(std::string_view{view.note_palette.entries[index].label} ==
              wormedit::find_catalog_entry(wormedit::note_kinds, kind)->palette_label);
    }
}

TEST_CASE("BMS action identities reject unoffered and invalid encodings", "[sys][catalog-wiring]") {
    check_frame_storage(is_bms_encoding_offered(static_cast<std::uint32_t>(wormedit::text_encoding_t::utf8)));
    check_frame_storage(!is_bms_encoding_offered(static_cast<std::uint32_t>(wormedit::text_encoding_t::unknown)));
    check_frame_storage(!is_bms_encoding_offered(static_cast<std::uint32_t>(wormedit::text_encoding_t::ascii)));
    check_frame_storage(!is_bms_encoding_offered(std::numeric_limits<std::uint32_t>::max()));
}
