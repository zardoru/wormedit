#include <wormedit/core/bmson_lane_mode.h>
#include "sys/workspace_view.h"
#include "sys/catalog_view.h"
#include <charconv>
#include <tuple>
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "sys/compose_resource_presenter.h"
#include "sys/sound_list_view.h"
#include "sys/sound_usage_view.h"
#include "sys/bookmark_navigation.h"
#include "sys/sound_picker_view.h"
#include "wormedit/config.h"

#include <wormedit/core/resource_file_copy.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
auto frontend_workspace_view_t::arena() noexcept -> frame_arena_t* {
    if (arena_) return arena_;
    if (!owned_arena_) {
        try {
            owned_arena_ = std::make_shared<frame_arena_t>(arena_options_t{
                .chunk_bytes = 1024 * 1024, .maximum_bytes = 16 * 1024 * 1024});
        } catch (const std::bad_alloc&) {
            return nullptr;
        }
    }
    return owned_arena_.get();
}


auto arena_string(frame_arena_t& arena, std::string_view value) noexcept -> const char* {
    if (value.size() == std::numeric_limits<std::size_t>::max()) return nullptr;
    const auto allocation = arena.allocate(value.size() + 1, alignof(char));
    if (!allocation) return nullptr;
    auto* text = static_cast<char*>(*allocation);
    if (!value.empty()) std::memcpy(text, value.data(), value.size());
    text[value.size()] = '\0';
    return text;
}

auto arena_format(frame_arena_t& arena, const char* format, ...) noexcept -> const char* {
    if (!format) return nullptr;
    std::va_list arguments;
    va_start(arguments, format);
    std::va_list sizing;
    va_copy(sizing, arguments);
    const auto length = std::vsnprintf(nullptr, 0, format, sizing);
    va_end(sizing);
    if (length < 0) {
        va_end(arguments);
        return nullptr;
    }
    const auto allocation = arena.allocate(static_cast<std::size_t>(length) + 1, alignof(char));
    if (!allocation) {
        va_end(arguments);
        return nullptr;
    }
    auto* text = static_cast<char*>(*allocation);
    const auto written = std::vsnprintf(text, static_cast<std::size_t>(length) + 1,
                                         format, arguments);
    va_end(arguments);
    return written == length ? text : nullptr;
}
namespace {

static_assert(static_cast<unsigned>(workspace_tab_t::setup) == WORM_UI_WORKSPACE_TAB_SETUP);
static_assert(static_cast<unsigned>(workspace_tab_t::metadata) == WORM_UI_WORKSPACE_TAB_METADATA);
static_assert(static_cast<unsigned>(workspace_tab_t::timing) == WORM_UI_WORKSPACE_TAB_TIMING);
static_assert(static_cast<unsigned>(workspace_tab_t::compose) == WORM_UI_WORKSPACE_TAB_COMPOSE);
static_assert(static_cast<unsigned>(workspace_tab_t::storyboard) == WORM_UI_WORKSPACE_TAB_STORYBOARD);
static_assert(static_cast<unsigned>(workspace_tab_t::audio_slices) == WORM_UI_WORKSPACE_TAB_AUDIO_SLICES);
static_assert(static_cast<unsigned>(workspace_tab_t::compare_validate) == WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE);

auto recovery_status_name(wormedit::native_project_recovery_candidate_status_t status)
    -> std::string_view {
    using status_t = wormedit::native_project_recovery_candidate_status_t;
    switch (status) {
    case status_t::missing:
        return "missing";
    case status_t::valid:
        return "valid";
    case status_t::invalid:
        return "invalid";
    case status_t::unreadable:
        return "unreadable";
    case status_t::resource_limited:
        return "resource limited";
    }
    return "unknown";
}

auto recovery_comparison_name(wormedit::native_project_recovery_comparison_t comparison)
    -> std::string_view {
    using comparison_t = wormedit::native_project_recovery_comparison_t;
    switch (comparison) {
    case comparison_t::not_comparable:
        return "not compared";
    case comparison_t::no_valid_primary:
        return "no valid primary";
    case comparison_t::different_project:
        return "different project";
    case comparison_t::older_revision:
        return "older";
    case comparison_t::equal_revision:
        return "same revision";
    case comparison_t::newer_revision:
        return "newer";
    }
    return "unknown comparison";
}


auto recovery_number_suffix(const char* label, std::optional<std::uint64_t> value)
    -> std::array<char, 64> {
    std::array<char, 64> suffix{};
    if (value) std::snprintf(suffix.data(), suffix.size(), " · %s %llu", label,
                             static_cast<unsigned long long>(*value));
    return suffix;
}

void populate_recovery_candidate(
    frontend_workspace_view_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery,
    std::size_t source_index, std::size_t index) {
    const auto& row = recovery.rows[source_index];
    auto& projected = result.view.recovery_candidates[index];
    projected.path_label = row.path_label;
    const auto revision = recovery_number_suffix("revision", row.version
        ? std::optional<std::uint64_t>{row.version->revision} : std::nullopt);
    const auto session = recovery_number_suffix("session", row.recovery_session_sequence);
    projected.summary = recovery_status_name(row.status);
    projected.summary += " · ";
    projected.summary += recovery_comparison_name(row.comparison);
    projected.summary += revision.data();
    projected.summary += session.data();
    if (recovery.preferred_recovery_row == source_index) projected.summary += " · recommended recovery";
    projected.disabled_reason = {};
    if (row.available_actions.empty())
        projected.disabled_reason = "No safe action is available for this candidate.";
    else if (row.available_actions.size() != 5)
        projected.disabled_reason = "Unavailable actions are disabled by the recovery controller's safety checks.";
    projected.actions.assign(row.available_actions.begin(), row.available_actions.end());
    projected.scan_generation = row.target.scan_generation;
    projected.row_index = row.target.row_index;
    projected.candidate_index = row.target.candidate_index;
}

void populate_recovery_candidates(
    frontend_workspace_view_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery) {
    result.view.recovery_candidates.resize(recovery.rows.size());
    std::size_t output_index{};
    // Keep actionable rows ahead of inactive rows without changing action tokens.
    for (const bool actionable : {true, false}) {
        for (std::size_t source_index = 0; source_index < recovery.rows.size(); ++source_index) {
            const bool row_actionable = !recovery.rows[source_index].available_actions.empty();
            if (row_actionable == actionable)
                populate_recovery_candidate(result, recovery, source_index, output_index++);
        }
    }
}

void populate_recovery_view(
    frontend_workspace_view_t& result,
    const wormedit::native_project_recovery_controller_view_t& recovery,
    bool recovery_scan_configured) {
    result.view.recovery_has_actionable_candidate =
        std::any_of(recovery.rows.begin(), recovery.rows.end(), [](const auto& row) {
            return !row.available_actions.empty();
        });
    result.view.recovery_panel_visible = result.view.recovery_has_actionable_candidate;
    result.view.recovery_heading = "Project recovery";
    result.view.recovery_rows_truncated = recovery.rows_truncated;
    if (recovery_scan_configured) {
        result.view.recovery_notice = result.view.recovery_has_actionable_candidate
                                          ? "Review candidates before choosing an action."
                                          : "No recovery candidates currently require action.";
    } else {
        result.view.recovery_notice =
            "Unavailable: this starter workspace has no opened project path or configured "
            "recovery directory.";
    }
    populate_recovery_candidates(result, recovery);
    result.view.recovery_diagnostics.resize(recovery.diagnostics.size());
    result.recovery_diagnostic_paths.resize(recovery.diagnostics.size());
    for (std::size_t index = 0; index < recovery.diagnostics.size(); ++index) {
        const auto& diagnostic = recovery.diagnostics[index];
        auto& text = result.view.recovery_diagnostics[index];
        text = diagnostic.message;
        if (!diagnostic.path.empty()) {
            text += " (";
            auto& [path, generic_path] = result.recovery_diagnostic_paths[index];
            if (path != diagnostic.path) {
                path = diagnostic.path;
                generic_path = path.generic_string();
            }
            text += generic_path;
            text += ')';
        }
    }
}

struct workspace_projection_context_t {
    frontend_workspace_view_t& result;
    const wormedit::workspace_view_t& source;
    const wormedit::song_document_t& document;
    dock_sort_t keysound_sort;
    WormUiKeysoundDockRequest keysound_request;
    wormedit::ui::sound_usage_request_t sound_usage_request;
    WormUiBookmarkChooserRequest bookmark_chooser_request;
    keysound_list_cache_t* keysound_cache{};
    bookmark_chooser_indices_t bookmark_indices;
    compose_resource_catalog_cache_t* resource_catalog_cache{};
};

auto workspace_summary_line(frontend_workspace_view_t& result) -> std::string& {
    if (result.summary_size == result.view.summary_lines.size())
        result.view.summary_lines.emplace_back();
    return result.view.summary_lines[result.summary_size++];
}
void append_workspace_summary(const workspace_projection_context_t& projection,
                              std::string_view line) {
    workspace_summary_line(projection.result) = line;
}
void append_workspace_count(const workspace_projection_context_t& projection,
                            std::string_view label, std::size_t count) {
    auto& line = workspace_summary_line(projection.result);
    line = label;
    std::array<char, std::numeric_limits<std::size_t>::digits10 + 1> digits{};
    const auto converted = std::to_chars(digits.data(), digits.data() + digits.size(), count);
    line.append(digits.data(), converted.ptr);
}

void populate_setup_tab(const workspace_projection_context_t& projection, const wormedit::workspace_setup_view_t& tab) {
    auto& result = projection.result;
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                auto& setup = result.view.setup;
                setup.visible = true;
                setup.chart_count = tab.charts.size();
                setup.audio_asset_count = tab.audio_asset_count;
                setup.visual_asset_count = tab.visual_asset_count;

}

void populate_metadata_search_tags(frontend_workspace_view_t&,
                                   const wormedit::workspace_metadata_view_t& tab,
                                   wormedit::ui::metadata_view_t& metadata) {
    metadata.search_tags.resize(tab.search_tags.size());
    for (std::size_t i = 0; i < tab.search_tags.size(); ++i)
        metadata.search_tags[i] = tab.search_tags[i];
    metadata.search_tags_editable = metadata.search_tags_editable &&
        std::ranges::none_of(tab.search_tags, [](const auto& tag) {
            return tag.size() > wormedit::default_metadata_edit_limits.maximum_search_tag_bytes;
        });
}

void populate_metadata_format_fields(frontend_workspace_view_t&,
                                     const wormedit::workspace_metadata_view_t& tab,
                                     wormedit::ui::metadata_view_t& metadata) {
    metadata.format_field_total_count = tab.format_fields.size();
    metadata.format_fields.resize(tab.format_fields.size());
    for (std::size_t i = 0; i < tab.format_fields.size(); ++i) {
        const auto& field = tab.format_fields[i];
        auto& row = metadata.format_fields[i];
        row.chart_scoped = field.chart_scoped;
        row.family = field.family;
        row.first_group_for_family = field.first_group_for_family;
        row.group_index = field.group_index;
        row.field_index = field.field_index;
        row.group_field_count = field.group_field_count;
        row.name = field.name;
        row.value = field.value;
        row.has_source_order = field.source_order.has_value();
        row.source_order = field.source_order.value_or(0);
    }
}

void append_metadata_format_targets(wormedit::ui::metadata_view_t& metadata,
                                    const std::vector<wormedit::format_metadata_t>& groups,
                                    bool chart_scoped) {
    std::array<const wormedit::format_metadata_t*, wormedit::format_families.size()> first_groups{};
    std::array<std::size_t, wormedit::format_families.size()> first_indices{};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto family = static_cast<std::size_t>(groups[i].family);
        if (family < first_groups.size() && first_groups[family] == nullptr) {
            first_groups[family] = &groups[i];
            first_indices[family] = i;
        }
    }
    for (const auto& descriptor : wormedit::format_families) {
        if (!descriptor.offered_as_metadata) continue;
        const auto i = static_cast<std::size_t>(descriptor.kind);
        const auto* found = first_groups[i];
        metadata.format_targets.push_back({
            .chart_scoped = chart_scoped, .family = descriptor.kind,
            .group_exists = found != nullptr, .group_index = found ? first_indices[i] : 0,
            .field_count = found ? found->ordered_fields.size() : 0});
    }
}

void populate_metadata_format_targets(const workspace_projection_context_t& projection,
                                      const wormedit::workspace_metadata_view_t& tab,
                                      wormedit::ui::metadata_view_t& metadata) {
    const auto* selected = tab.selected_difficulty
        ? projection.document.find_chart(tab.selected_difficulty->id) : nullptr;
    if (selected) metadata.selected_chart_source_family = selected->source_family;
    metadata.format_targets.clear();
    append_metadata_format_targets(metadata, projection.document.metadata.format_groups, false);
    if (selected) append_metadata_format_targets(metadata, selected->metadata.format_groups, true);
}

void populate_metadata_summary(const workspace_projection_context_t& projection,
                               const workspace_metadata_view_t& tab) {
    auto& result = projection.result;
    auto& title_line = workspace_summary_line(result);
    title_line = "Song title: "; title_line += tab.title;
    auto& artist_line = workspace_summary_line(result);
    artist_line = "Artist: "; artist_line += tab.artist;
    append_workspace_count(projection, "Search tags: ", tab.search_tags.size());
    auto& author_line = workspace_summary_line(result);
    author_line = "Chart author: ";
    author_line += tab.chart_author.empty() ? "(unset)" : tab.chart_author;
    append_workspace_count(projection, "Format-specific fields: ", tab.format_fields.size());
    for (const auto& field : tab.format_fields) {
        auto& line = workspace_summary_line(result);
        line = field.chart_scoped ? "Difficulty · " : "Song · ";
        line += format_family_label(field.family);
        line += " · "; line += field.name; line += ": ";
        if (field.value.size() <= 96) line += field.value;
        else { line += '<'; line += std::to_string(field.value.size()); line += " UTF-8 bytes>"; }
    }
}

void populate_metadata_tab(const workspace_projection_context_t& projection, const wormedit::workspace_metadata_view_t& tab) {
    auto& result = projection.result;
    const auto& source = projection.source;
                result.view.project_title = tab.title.c_str();
                result.view.artist = tab.artist.c_str();
                auto& metadata = result.view.metadata;
                metadata.visible = true;
                metadata.document_revision = source.document_revision;
                metadata.title = tab.title.c_str();
                metadata.artist = tab.artist.c_str();
                metadata.song_fields_editable =
                    tab.title.size() <= wormedit::default_metadata_edit_limits.maximum_title_bytes &&
                    tab.artist.size() <= wormedit::default_metadata_edit_limits.maximum_title_bytes &&
                    tab.search_tags.size() <= wormedit::default_metadata_edit_limits.maximum_search_tags;
                metadata.search_tags_editable =
                    tab.search_tags.size() <= wormedit::default_metadata_edit_limits.maximum_search_tags;
                populate_metadata_search_tags(result, tab, metadata);
                metadata.song_fields_editable =
                    metadata.song_fields_editable && metadata.search_tags_editable;
                metadata.has_chart = tab.selected_difficulty.has_value();
                metadata.audio_offset_editable = metadata.has_chart;
                if (tab.selected_difficulty) {
                    metadata.chart_id = tab.selected_difficulty->id;
                    metadata.chart_name = tab.selected_difficulty->difficulty_name.c_str();
                    metadata.chart_author = tab.chart_author.c_str();
                    metadata.chart_fields_editable =
                        tab.selected_difficulty->difficulty_name.size() <=
                            wormedit::default_metadata_edit_limits.maximum_chart_text_bytes &&
                        tab.chart_author.size() <= wormedit::default_metadata_edit_limits.maximum_chart_text_bytes;
                    metadata.has_chart_rating = tab.selected_difficulty->rating.has_value();
                    if (tab.selected_difficulty->rating) {
                        metadata.chart_rating_numerator =
                            tab.selected_difficulty->rating->numerator;
                        metadata.chart_rating_denominator =
                            tab.selected_difficulty->rating->denominator;
                    }
                    metadata.audio_offset_numerator = tab.beat_zero_audio_offset_seconds.numerator;
                    metadata.audio_offset_denominator =
                        tab.beat_zero_audio_offset_seconds.denominator;
                }
                populate_metadata_format_fields(result, tab, metadata);
                populate_metadata_format_targets(projection, tab, metadata);
    populate_metadata_summary(projection, tab);

}
void populate_timing_tab(const workspace_projection_context_t& projection, const wormedit::workspace_timing_view_t& tab) {
    auto& result = projection.result;
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                result.view.timing.visible = true;
                result.view.timing.measure_count = tab.measure_count;
                result.view.timing.bpm_count = tab.bpm_count;
                result.view.timing.pause_count = tab.pause_count;
                result.view.timing.warp_count = tab.warp_count;
                result.view.timing.bookmark_count = tab.bookmark_count;
                result.view.timing.section_count = tab.section_count;
                result.view.timing.anchor_count = tab.anchor_count;
                append("Measures: " + std::to_string(tab.measure_count));
                append("BPM events: " + std::to_string(tab.bpm_count));
                append("Stops / delays: " + std::to_string(tab.pause_count));
                append("Warps: " + std::to_string(tab.warp_count));
                append("Anchors: " + std::to_string(tab.anchor_count));
                append("Bookmarks / sections: " +
                       std::to_string(tab.bookmark_count + tab.section_count));

}
auto append_compose_output_group(frontend_workspace_view_t& result, frame_arena_t& arena,
                                 const wormedit::output_group_t& group,
                                 std::size_t document_index) -> bool {
    const auto* name = arena_string(arena, group.name);
    const auto* path = arena_string(arena, group.relative_output_path.generic_string());
    const auto directory = wormedit::export_asset_directory(group.relative_output_path,
                                                             group.destination_kind);
    const auto* assets = arena_string(arena,
        directory ? directory->generic_string() : std::string{});
    if (!name || !path || !assets) return false;
    if (!result.compose.output_group.view.push_back(
            {.document_index = document_index,
             .name = name,
             .relative_output_path = path,
             .relative_asset_directory = assets,
             .existing_files = static_cast<WormUiExistingFilesChoice>(group.existing_files)}))
        return false;
    result.view.compose.output_group_count = result.compose.output_group.view.size();
    return true;
}

void populate_compose_output_groups(const workspace_projection_context_t& projection,
                                    wormedit::chart_id_t chart) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    const auto& groups = projection.document.output_groups;
    const bool project = projection.source.active_tab == wormedit::workspace_tab_t::setup;
    const auto selected_count = std::ranges::count_if(groups, [&](const auto& group) {
        return project || std::ranges::find(group.charts, chart) != group.charts.end();
    });
    const auto count = std::min(static_cast<std::size_t>(selected_count),
                                std::size_t{WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY});
    auto* arena = result.arena();
    result.compose.output_group.view = arena
        ? arena_array<WormUiComposeOutputGroupView>(*arena, count)
        : frame_span<WormUiComposeOutputGroupView>{};
    view.output_groups = result.compose.output_group.view.data();
    view.output_group_count = 0;
    view.output_groups_truncated = static_cast<std::size_t>(selected_count) > count;
    if (result.compose.output_group.view.capacity() != count) {
        view.output_groups_truncated = true;
        return;
    }
    if (!arena) return;
    for (std::size_t index = 0; index < groups.size(); ++index) {
        if (!project && std::ranges::find(groups[index].charts, chart) == groups[index].charts.end()) continue;
        if (view.output_group_count == count) break;
        if (!append_compose_output_group(result, *arena, groups[index], index)) {
            view.output_groups_truncated = true;
            break;
        }
    }
}

auto append_compose_resource(frontend_workspace_view_t& result, frame_arena_t& arena,
                             const compose_resource_entry_t& resource) -> bool {
    const auto* target = arena_string(arena, resource.target_index);
    const auto* name = arena_string(arena, resource.name);
    const auto* source = arena_string(arena, resource.source_name);
    const auto* path = arena_string(arena, resource.source_path);
    if (!target || !name || !source || !path) return false;
    std::uint8_t roles{};
    roles |= resource.sample ? WORM_UI_COMPOSE_RESOURCE_ROLE_SAMPLE : 0;
    roles |= resource.bgm ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGM : 0;
    roles |= resource.bga ? WORM_UI_COMPOSE_RESOURCE_ROLE_BGA : 0;
    return result.compose.resource.view.push_back({
        .resource_id = resource.resource.value, .asset_id = resource.asset.value,
        .roles = roles, .audio_asset = resource.audio_asset,
        .selected_audio_asset = resource.selected_audio_asset,
        .image_asset = resource.image_asset, .selected_visual_asset = resource.selected_visual_asset,
        .missing = resource.missing, .target_index = target, .name = name, .source_name = source,
        .source_path = path, .use_count = resource.use_count});
}
void populate_compose_resources(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    compose_resource_catalog_t uncached;
    const auto& catalog = projection.resource_catalog_cache
        ? compose_resource_catalog_t::from(*projection.resource_catalog_cache, projection.document,
              projection.source.transport.audio_asset, WORM_UI_COMPOSE_RESOURCE_CAPACITY,
              projection.source.selected_visual_asset)
        : (uncached = compose_resource_catalog_t::from(projection.document,
              projection.source.transport.audio_asset, WORM_UI_COMPOSE_RESOURCE_CAPACITY,
              projection.source.selected_visual_asset));
    auto* arena = result.arena();
    result.compose.resource.view = arena
        ? arena_array<WormUiComposeResourceView>(*arena, catalog.entries.size())
        : frame_span<WormUiComposeResourceView>{};
    view.resources = result.compose.resource.view.data();
    view.resource_count = 0;
    view.resource_total_count = catalog.total_count;
    view.resources_truncated = catalog.truncated;
    if (result.compose.resource.view.capacity() != catalog.entries.size()) {
        view.resources_truncated = true;
        return;
    }
    for (const auto& resource : catalog.entries) {
        if (!append_compose_resource(result, *arena, resource)) {
            view.resources_truncated = true;
            break;
        }
        view.resource_count = result.compose.resource.view.size();
    }
}

auto append_keysound_row(frontend_workspace_view_t& result, frame_arena_t& arena,
                         const wormedit::sys::keysound_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    const auto* name = arena_string(arena, row.name);
    const auto* path = arena_string(arena, row.source_path);
    if (!slot || !name || !path)
        return false;
    return result.compose.keysound.row.push_back(
        {.resource_id = row.resource.value,
         .slot = slot,
         .name = name,
         .missing = row.missing,
         .layer_mask = row.layer_mask,
         .note_layer_conflict_mask = row.note_layer_conflict_mask,
         .sound_group_id = row.sound_group_id,
         .use_count = row.use_count, .source_path = path});
}

auto append_keysound_selected_row(frontend_workspace_view_t& result, frame_arena_t& arena,
                                  const wormedit::sys::keysound_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    const auto* name = arena_string(arena, row.name);
    const auto* path = arena_string(arena, row.source_path);
    if (!slot || !name || !path) return false;
    return result.compose.keysound.selected_row.push_back({.resource_id = row.resource.value,
        .slot = slot, .name = name, .missing = row.missing, .layer_mask = row.layer_mask,
        .note_layer_conflict_mask = row.note_layer_conflict_mask,
        .sound_group_id = row.sound_group_id,
         .use_count = row.use_count, .source_path = path});
}

auto append_keysound_selection_row(frontend_workspace_view_t& result, frame_arena_t& arena,
                                   const wormedit::sys::keysound_selection_row_t& row) -> bool {
    const auto* slot = arena_string(arena, row.slot);
    if (!slot) return false;
    return result.compose.keysound.selection_row.push_back({.resource_id = row.resource.value,
        .group_id = row.group_id, .layer_mask = row.layer_mask,
        .note_layer_conflict_mask = row.note_layer_conflict_mask,
        .slot = slot, .missing = row.missing});
}

auto append_keysound_display_row(frontend_workspace_view_t& result, frame_arena_t& arena,
                                 const wormedit::sys::keysound_list_view_t::display_row_t& row)
    -> bool {
    const auto* name = row.kind == wormedit::sys::keysound_list_view_t::display_row_t::kind_t::group_header
        ? arena_string(arena, row.group_name) : "";
    if (!name) return false;
    return result.compose.keysound.display_row.push_back({
        .kind = row.kind == wormedit::sys::keysound_list_view_t::display_row_t::kind_t::group_header
            ? WORM_UI_COMPOSE_KEYSOUND_GROUP_HEADER : WORM_UI_COMPOSE_KEYSOUND_SOUND_ROW,
        .group_id = row.group_id, .group_name = name,
        .group_sound_count = row.group_sound_count, .expanded = row.expanded,
        .resource_id = row.resource.value});
}

auto bounded_text(const char* text, std::size_t size) -> std::string_view {
    if (!text) return {};
    const auto end = std::find(text, text + size, '\0');
    return {text, static_cast<std::size_t>(end - text)};
}

template<class T>
auto request_span(const T* data, std::size_t count) -> std::span<const T> {
    return data && count > 0 ? std::span<const T>{data, count} : std::span<const T>{};
}

auto select_compose_keysounds(const workspace_projection_context_t& projection,
                               const wormedit::chart_document_t& chart,
                               keysound_list_view_t& uncached)
    -> const keysound_list_view_t& {
    const auto& request = projection.keysound_request;
    if (request.slots_mode) {
        const auto sort = chart.source_family == wormedit::format_family_t::bms &&
                (request.sort_by_id || projection.keysound_sort == dock_sort_t::bms_id)
            ? dock_sort_t::bms_id : dock_sort_t::natural_name;
        const bool ids = chart.source_family == wormedit::format_family_t::bms;
        if (projection.keysound_cache)
            return keysound_list_view_t::from(*projection.keysound_cache,
                projection.document, &chart, WORM_UI_COMPOSE_KEYSOUND_CAPACITY, sort, ids);
        uncached = keysound_list_view_t::from(projection.document, &chart,
            WORM_UI_COMPOSE_KEYSOUND_CAPACITY, sort, ids);
        return uncached;
    }
    auto layer_filter = wormedit::sys::keysound_layer_filter_kind_t::any;
    if (request.layer_filter_kind == 1)
        layer_filter = wormedit::sys::keysound_layer_filter_kind_t::no_layer;
    else if (request.layer_filter_kind == 2)
        layer_filter = wormedit::sys::keysound_layer_filter_kind_t::specific;
    const keysound_list_request_t projected_request{
            .search = bounded_text(request.automatic_search, sizeof(request.automatic_search)),
            .use_filter = static_cast<resource_use_filter_t>(request.use_filter),
            .selected_resource_ids = request_span(request.selected_resource_ids,
                                                   request.selected_resource_count),
            .include_selected_metadata = request.include_selected_metadata,
            .group_ids = request_span(request.group_filter_ids, request.group_filter_count),
            .group_filter_active = request.group_filter_active,
            .include_ungrouped = request.include_ungrouped,
            .layer_filter = layer_filter, .layer_id = request.layer_filter_id,
            .collapsed_group_ids = request_span(request.collapsed_group_ids,
                                                request.collapsed_group_count),
            .ungrouped_collapsed = request.ungrouped_collapsed,
            .first_row = request.automatic_first_row,
            .row_limit = request.automatic_row_limit == 0
                ? 64 : request.automatic_row_limit,
            .primary_resource_id = request.primary_resource_id,
            .filter_layer_first = request.filter_layer_first_row,
            .filter_layer_limit = request.filter_layer_row_limit == 0
                ? 64 : request.filter_layer_row_limit,
            .sort = request.sort_by_id || projection.keysound_sort == dock_sort_t::bms_id
                ? wormedit::sys::dock_sort_t::bms_id
                : wormedit::sys::dock_sort_t::natural_name,
            .ids_available = chart.source_family == wormedit::format_family_t::bms};
    if (projection.keysound_cache)
        return keysound_list_view_t::from(*projection.keysound_cache,
            projection.document, &chart, projected_request);
    uncached = keysound_list_view_t::from(projection.document, &chart, projected_request);
    return uncached;
}

auto append_keysound_layer(frontend_workspace_view_t& result, frame_arena_t& arena,
                           const wormedit::sys::keysound_layer_t& layer) -> bool {
    const auto* name = arena_string(arena, layer.name);
    if (!name)
        return false;
    return result.compose.keysound.layer.push_back({.layer_id = layer.id.value,
                                                    .name = name,
                                                    .red = layer.color.red,
                                                    .green = layer.color.green,
                                                    .blue = layer.color.blue,
                                                    .alpha = layer.color.alpha,
                                                    .locked = layer.locked});
}

auto append_keysound_filter_layer(frontend_workspace_view_t& result, frame_arena_t& arena,
                                  const wormedit::sys::keysound_layer_t& layer) -> bool {
    const auto* name = arena_string(arena, layer.name);
    if (!name) return false;
    return result.compose.keysound.filter_layer.push_back({.layer_id = layer.id.value,
        .name = name, .red = layer.color.red, .green = layer.color.green,
        .blue = layer.color.blue, .alpha = layer.color.alpha, .locked = layer.locked});
}

void populate_compose_sound_groups(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    auto* arena = result.arena();
    if (!arena)
        return;
    auto& groups = result.compose.keysound.group;
    groups =
        arena_array<WormUiComposeSoundGroupView>(*arena, projection.document.sound_groups.size());
    for (const auto& group : projection.document.sound_groups) {
        const auto* name = arena_string(*arena, group.name);
        if (!name || !groups.push_back({.group_id = group.id.value, .name = name}))
            break;
    }
    view.compose_sound_groups = groups.data();
    view.compose_sound_group_count = groups.size();
}

void populate_compose_sound_slots(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    const auto& request = projection.keysound_request;
    view.sound_slots_mode = view.keysound_ids_available && request.slots_mode;
    if (!view.sound_slots_mode || !result.arena()) return;
    const auto list = wormedit::sys::sound_slots_view_t::from(projection.document,
        {.search = std::string{std::begin(request.search),
             std::find(std::begin(request.search), std::end(request.search), '\0')},
         .selected_slot = request.selected_slot,
         .first_row = request.first_row, .row_limit = request.row_limit,
         .use_filter = static_cast<resource_use_filter_t>(request.use_filter)});
    view.sound_slot_selected_number = request.selected_slot;
    view.sound_slot_selected_row = list.selected_row;
    view.sound_slot_first_row = list.first_row;
    view.sound_slot_total_matching = list.total_matching;
    auto& rows = result.compose.keysound.slot;
    auto& arena = *result.arena();
    rows = arena_array<WormUiComposeSoundSlotRowView>(arena, list.rows.size());
    for (const auto& row : list.rows) {
        const auto* slot = arena_string(arena, row.identifier);
        const auto* name = arena_string(arena, row.name);
        const auto* path = arena_string(arena, row.source_path);
        if (!slot || !name || !path || !rows.push_back({.number = row.number, .slot = slot,
            .resource_id = row.resource.value, .name = name, .use_count = row.use_count,
            .missing = row.missing, .placeable = row.placeable, .source_path = path})) break;
    }
    view.sound_slots = rows.data();
    view.sound_slot_count = rows.size();
}

auto reserve_compose_keysound_arrays(frontend_workspace_view_t& result, frame_arena_t& arena,
                                     const wormedit::sys::keysound_list_view_t& list)
    -> bool {
    auto& rows = result.compose.keysound;
    rows.row = arena_array<WormUiComposeKeysoundRowView>(arena, list.rows.size());
    rows.selected_row = arena_array<WormUiComposeKeysoundRowView>(arena, list.selected_rows.size());
    rows.selection_row = arena_array<WormUiComposeKeysoundSelectionView>(arena,
                                                                          list.selection_rows.size());
    rows.display_row = arena_array<WormUiComposeKeysoundDisplayRowView>(arena,
                                                                         list.display_rows.size());
    rows.advance_row = arena_array<WormUiComposeKeysoundAdvanceRowView>(arena,
                                                                        list.advance_rows.size());
    rows.listed_resource_id = arena_array<std::uint64_t>(arena, list.listed_resources.size());
    rows.all_resource_id = arena_array<std::uint64_t>(arena, list.all_resources.size());
    rows.layer = arena_array<WormUiComposeKeysoundLayerView>(arena, list.layers.size());
    rows.filter_layer = arena_array<WormUiComposeKeysoundLayerView>(arena,
                                                                     list.filter_layers.size());
    return rows.row.capacity() == list.rows.size() &&
        rows.selected_row.capacity() == list.selected_rows.size() &&
        rows.selection_row.capacity() == list.selection_rows.size() &&
        rows.display_row.capacity() == list.display_rows.size() &&
        rows.advance_row.capacity() == list.advance_rows.size() &&
        rows.listed_resource_id.capacity() == list.listed_resources.size() &&
        rows.all_resource_id.capacity() == list.all_resources.size() &&
        rows.layer.capacity() == list.layers.size() &&
        rows.filter_layer.capacity() == list.filter_layers.size();
}

void clear_compose_keysound_counts(WormUiComposeView& view) {
    view.keysound_count = 0;
    view.keysound_selected_row_count = 0;
    view.keysound_selection_row_count = 0;
    view.keysound_display_row_count = 0;
    view.keysound_advance_row_count = 0;
    view.keysound_listed_resource_count = 0;
    view.keysound_all_resource_count = 0;
    view.keysound_layer_count = 0;
    view.keysound_filter_layer_count = 0;
}

template<class KeysoundArrays>
void bind_compose_keysound_arrays(WormUiComposeView& view, KeysoundArrays& rows) {
    view.keysounds = rows.row.data();
    view.keysound_selected_rows = rows.selected_row.data();
    view.keysound_selection_rows = rows.selection_row.data();
    view.keysound_display_rows = rows.display_row.data();
    view.keysound_advance_rows = rows.advance_row.data();
    view.keysound_listed_resource_ids = rows.listed_resource_id.data();
    view.keysound_all_resource_ids = rows.all_resource_id.data();
    view.keysound_layers = rows.layer.data();
    view.keysound_filter_layers = rows.filter_layer.data();
}

void append_compose_keysound_payload(frontend_workspace_view_t& result, frame_arena_t& arena,
                                     const wormedit::sys::keysound_list_view_t& list,
                                     WormUiComposeView& view) {
    for (const auto& layer : list.layers)
        if (!append_keysound_layer(result, arena, layer)) break;
    view.keysound_layer_count = result.compose.keysound.layer.size();
    for (const auto& layer : list.filter_layers)
        if (!append_keysound_filter_layer(result, arena, layer)) break;
    view.keysound_filter_layer_count = result.compose.keysound.filter_layer.size();
    for (const auto& row : list.rows) {
        if (!append_keysound_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    }
    view.keysound_count = result.compose.keysound.row.size();
    for (const auto& row : list.selected_rows) {
        if (!append_keysound_selected_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    }
    view.keysound_selected_row_count = result.compose.keysound.selected_row.size();
    for (const auto& row : list.selection_rows)
        if (!append_keysound_selection_row(result, arena, row)) {
            view.keysounds_truncated = true;
            break;
        }
    view.keysound_selection_row_count = result.compose.keysound.selection_row.size();
    for (const auto& row : list.display_rows) {
        if (!append_keysound_display_row(result, arena, row)) {
            view.keysound_display_rows_truncated = true;
            break;
        }
    }
    view.keysound_display_row_count = result.compose.keysound.display_row.size();
}

void append_compose_keysound_indices(frontend_workspace_view_t& result,
                                     const wormedit::sys::keysound_list_view_t& list,
                                     WormUiComposeView& view) {
    for (const auto& row : list.advance_rows)
        if (!result.compose.keysound.advance_row.push_back({.resource_id = row.resource.value,
                                                             .missing = row.missing})) {
            view.keysounds_truncated = true;
            break;
        }
    for (const auto id : list.listed_resources)
        if (!result.compose.keysound.listed_resource_id.push_back(id.value)) {
            view.keysounds_truncated = true;
            break;
        }
    for (const auto id : list.all_resources)
        if (!result.compose.keysound.all_resource_id.push_back(id.value)) {
            view.keysounds_truncated = true;
            break;
        }
    view.keysound_advance_row_count = result.compose.keysound.advance_row.size();
    view.keysound_listed_resource_count = result.compose.keysound.listed_resource_id.size();
    view.keysound_all_resource_count = result.compose.keysound.all_resource_id.size();
}

void populate_sound_picker(const workspace_projection_context_t& projection,
                           const wormedit::chart_document_t& chart) {
    auto& view = projection.result.view.compose.sound_picker;
    const auto& request = projection.keysound_request;
    if (!request.sound_picker_open) return;
    auto* arena = projection.result.arena();
    if (!arena) return;
    sound_picker_cache_t uncached;
    auto* cache = &uncached;
    if (projection.keysound_cache) {
        auto& stored = projection.keysound_cache->sound_picker;
        if (!stored) stored = std::make_shared<sound_picker_cache_t>();
        cache = stored.get();
    }
    const auto query = bounded_text(request.sound_picker_query, sizeof(request.sound_picker_query) - 1);
    const auto& result = sound_picker_view_result_t::from(*cache, projection.document, chart, query);
    std::copy(query.begin(), query.end(), view.query);
    view.total_count = result.total;
    for (const auto& row : result.rows) {
        const auto* label = arena_format(*arena, "%s%s%s%s  (#%llu)",
            row.slot.empty() ? "" : row.slot.c_str(), row.slot.empty() ? "" : " · ",
            row.name.empty() ? "(unnamed sound)" : row.name.c_str(),
            row.missing ? " · Missing" : "", static_cast<unsigned long long>(row.resource.value));
        if (!label) return;
        view.rows[view.row_count++] = {row.resource.value, label, row.missing};
    }
    view.ready = true;
}

void populate_compose_keysounds(const workspace_projection_context_t& projection,
                                wormedit::chart_id_t chart_id) {
    auto& result = projection.result;
    auto& view = result.view.compose;
    auto* arena = result.arena();
    const auto chart = projection.document.find_chart(chart_id);
    view.keysounds_available = chart != nullptr;
    if (!arena || !view.keysounds_available) return;
    populate_sound_picker(projection, *chart);
    keysound_list_view_t uncached;
    const auto& list = select_compose_keysounds(projection, *chart, uncached);
    clear_compose_keysound_counts(view);
    view.keysound_section_projection_available = !projection.keysound_request.slots_mode;
    view.keysound_total_count = list.total;
    view.keysounds_truncated = list.truncated;
    view.keysound_display_total_count = list.display_total;
    view.keysound_display_first_row = list.display_first_row;
    view.keysound_display_rows_truncated = list.display_truncated;
    view.keysound_primary_display_index_valid = list.primary_display_index.has_value();
    view.keysound_primary_display_index = list.primary_display_index.value_or(0);
    const auto& request = projection.keysound_request;
    view.keysound_projected_primary_resource_id = request.primary_resource_id;
    view.keysound_projected_filters_unrestricted = !request.slots_mode &&
        !request.automatic_search[0] && !request.group_filter_active &&
        request.layer_filter_kind == WORM_UI_KEYSOUND_LAYER_FILTER_ANY &&
        !request.collapsed_group_count && !request.ungrouped_collapsed && request.include_ungrouped;
    view.keysound_filter_layer_first = list.filter_layer_first;
    view.keysound_filter_layer_total = list.filter_layer_total;
    view.keysound_filter_selected_layer_valid = list.selected_filter_layer_valid;
    view.keysound_filter_selected_layer_name = arena_string(
        *arena, list.selected_filter_layer_name);
    view.keysound_ids_available = chart->source_family == wormedit::format_family_t::bms;
    view.keysound_ids_used = list.bms_capacity.used;
    view.keysound_ids_total = list.bms_capacity.total;
    view.keysound_largest_free_run = list.bms_capacity.largest_free_run;
    if (!reserve_compose_keysound_arrays(result, *arena, list)) {
        bind_compose_keysound_arrays(view, result.compose.keysound);
        view.keysounds_truncated = true;
        return;
    }
    bind_compose_keysound_arrays(view, result.compose.keysound);
    append_compose_keysound_payload(result, *arena, list, view);
    append_compose_keysound_indices(result, list, view);
    populate_compose_sound_groups(projection);
    populate_compose_sound_slots(projection);
}

void populate_compose_branch_rows(frontend_workspace_view_t& result,
                                  const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.rows.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_ROW_CAPACITY));
    view.branch_rows_truncated = outline.rows_truncated || count < outline.rows.size();
    if (count == 0) return;
    auto* arena = result.arena();
    result.compose.branch.row = arena ? arena_array<WormUiComposeBranchRowView>(*arena, count)
                                     : frame_span<WormUiComposeBranchRowView>{};
    view.branch_rows = result.compose.branch.row.data();
    if (result.compose.branch.row.capacity() != count) {
        view.branch_rows_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto& row = outline.rows[index];
        const auto* text = arena_string(*arena, row.display_text);
        if (!text) {
            view.branch_rows_truncated = true;
            view.branch_display_text_truncated = true;
            break;
        }
        if (!result.compose.branch.row.push_back({.display_text = text, .depth = row.depth,
                .active = row.active, .selected = row.selected,
                .display_text_truncated = row.display_text_truncated,
                .has_source_order = row.source_order.has_value(),
                .source_order = row.source_order.value_or(0),
                .node_id = row.id.value,
                .raw_statement = row.kind == wormedit::bms_control_kind_t::raw_statement,
                .choice_block = row.kind == wormedit::bms_control_kind_t::random ||
                                row.kind == wormedit::bms_control_kind_t::switch_block})) {
            view.branch_rows_truncated = true;
            break;
        }
        view.branch_row_count = result.compose.branch.row.size();
    }
}
auto compose_branch_choice_view(const wormedit::workspace_bms_branch_choice_t& choice)
    -> WormUiComposeBranchChoiceView {
    return {.branch_id = choice.id.value,
        .kind = choice.kind == wormedit::bms_control_kind_t::switch_block
            ? WORM_UI_COMPOSE_BRANCH_SWITCH : WORM_UI_COMPOSE_BRANCH_RANDOM,
        .has_source_order = choice.source_order.has_value(),
        .source_order = choice.source_order.value_or(0), .active = choice.active,
        .editable = choice.editable, .has_selected_value = choice.selected_value.has_value(),
        .selected_value = choice.selected_value.value_or(0),
        .has_previous_value = choice.previous_value.has_value(),
        .previous_value = choice.previous_value.value_or(0),
        .has_next_value = choice.next_value.has_value(), .next_value = choice.next_value.value_or(0),
        .can_clear = choice.can_clear};
}
void populate_compose_branch_choices(frontend_workspace_view_t& result,
                                     const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.choices.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_CHOICE_CAPACITY));
    view.branch_choices_truncated = outline.choices_truncated || count < outline.choices.size();
    if (count == 0) return;
    auto* arena = result.arena();
    result.compose.branch.choice = arena ? arena_array<WormUiComposeBranchChoiceView>(*arena, count)
                                        : frame_span<WormUiComposeBranchChoiceView>{};
    view.branch_choices = result.compose.branch.choice.data();
    if (result.compose.branch.choice.capacity() != count) {
        view.branch_choices_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < count; ++index) {
        if (!result.compose.branch.choice.push_back(compose_branch_choice_view(outline.choices[index]))) {
            view.branch_choices_truncated = true;
            break;
        }
        view.branch_choice_count = result.compose.branch.choice.size();
    }
}
void populate_compose_branch_diagnostics(
    frontend_workspace_view_t& result, const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    const auto count = std::min(outline.diagnostics.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY));
    view.branch_diagnostics_truncated = outline.diagnostics_truncated || count < outline.diagnostics.size();
    if (count == 0) return;
    auto* arena = result.arena();
    for (std::size_t index = 0; index < count; ++index) {
        const auto* text = arena ? arena_string(*arena, outline.diagnostics[index].message) : nullptr;
        if (!text) {
            view.branch_diagnostics_truncated = true;
            break;
        }
        view.branch_diagnostics[view.branch_diagnostic_count++] = text;
    }
}
void populate_compose_branch_outline(frontend_workspace_view_t& result,
                                     const wormedit::workspace_bms_branch_outline_view_t& outline) {
    auto& view = result.view.compose;
    view.branch_outline_available = outline.available;
    view.branch_display_text_truncated = outline.display_text_truncated;
    populate_compose_branch_rows(result, outline);
    populate_compose_branch_choices(result, outline);
    populate_compose_branch_diagnostics(result, outline);
}

void populate_bookmark_chooser(const workspace_projection_context_t& projection, chart_id_t chart) {
    auto& result = projection.result;
    auto& view = result.view.compose.bookmark_chooser;
    view.project_id = projection.document.id.value;
    view.chart_id = chart.value;
    view.document_revision = projection.document.revision;
    const auto& request = projection.bookmark_chooser_request;
    if (!request.open) return;
    const auto page = bookmark_chooser_view_t::from(projection.document, chart, request.first_row,
                                               std::clamp(request.row_limit, std::size_t{1}, std::size_t{64}), request.query,
                                               projection.bookmark_indices);
    view.total_count = page.total_count;
    view.first_row = page.first_row;
    auto* arena = result.arena();
    if (!arena) { view.truncated = true; return; }
    view.query = arena_string(*arena, request.query);
    result.compose.bookmark_chooser_row = arena_array<WormUiBookmarkChooserRowView>(*arena, page.rows.size());
    for (const auto& row : page.rows) {
        WormUiBookmarkChooserRowView item{};
        item.bookmark_id = row.bookmark_id.value;
        item.anchor = static_cast<std::uint8_t>(row.anchor);
        item.position_label = arena_string(*arena, row.position_label);
        item.name = arena_string(*arena, row.name);
        item.display_label = arena_string(*arena, row.display_label);
        item.navigation_available = row.navigation_available;
        if (!item.position_label || !item.name || !item.display_label ||
            !result.compose.bookmark_chooser_row.push_back(item)) {
            view.truncated = true;
            break;
        }
    }
    view.rows = result.compose.bookmark_chooser_row.data();
    view.row_count = result.compose.bookmark_chooser_row.size();
}

void populate_compose_tab(const workspace_projection_context_t& projection, const wormedit::workspace_compose_view_t& tab) {
    auto& result = projection.result;
    const auto& source = projection.source;
                result.view.compose.visible = true;
                const auto& outline = tab.bms_branch_outline();
                auto& compose = result.view.compose;
                compose.selected_audio_asset_id =
                    source.transport.audio_asset.value_or(wormedit::asset_id_t{}).value;
                compose.selected_visual_asset_id =
                    source.selected_visual_asset.value_or(wormedit::asset_id_t{}).value;
                populate_compose_resources(projection);
                populate_compose_keysounds(projection, tab.chart);
                populate_bookmark_chooser(projection, tab.chart);
                populate_compose_branch_outline(result, outline);
                append_workspace_count(projection, "Measures: ", static_cast<std::size_t>(tab.measure_count));
                append_workspace_count(projection, "Notes: ", static_cast<std::size_t>(tab.note_count));
                append_workspace_count(projection, "Layout segments: ", static_cast<std::size_t>(tab.layout_segment_count));
                append_workspace_count(projection, "Visible lane definitions: ", static_cast<std::size_t>(tab.lane_count));
                append_workspace_count(projection, "Semantic layers: ", static_cast<std::size_t>(tab.layer_count));

}
void populate_storyboard_tab(const workspace_projection_context_t& projection, const wormedit::workspace_storyboard_view_t& tab) {
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                append(tab.placeholder);
                append("Storyboard documents: " + std::to_string(tab.storyboard_count));

}
void populate_sound_lane_modes(const workspace_projection_context_t& projection) {
    const auto* chart = projection.document.find_chart(projection.result.view.selected_chart_id);
    if (!chart || chart->source_family != format_family_t::bmson) return;
    const auto& bindings = projection.result.sound_lane_modes_cache.get_or_build(
        std::tuple{document_stamp_t::from(projection.document), chart->id, projection.result.query_session_generation}, [&] {
            return bmson_lane_mode_resource_bindings(projection.document, chart->id);
        });
    for (auto& row : projection.result.view.audio_slices.sound_rows) {
        const auto binding = std::ranges::find(bindings, row.resource_id,
                                               &bmson_channel_resource_binding_t::resource);
        row.bmson_channel_id = {};
        row.bmson_channel_continues = false;
        if (binding == bindings.end() || binding->ambiguous || !binding->channel) continue;
        row.bmson_channel_id = *binding->channel;
        const auto channel = std::ranges::find(chart->sound_channels, *binding->channel, &sound_channel_t::id);
        if (channel != chart->sound_channels.end())
            row.bmson_channel_continues = std::ranges::any_of(channel->events,
                [](const auto& event) { return event.continuation; });
    }
}

void populate_sound_rows(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& audio = result.view.audio_slices;
    const sound_list_request_t request{.row_limit = ui::audio_sound_row_limit};
    const auto& list = result.sound_list_cache.view_for(projection.document, request, result.query_session_generation);
    audio.sound_rows_truncated = list.total_matching > list.rows.size();
    audio.sound_rows.resize(list.rows.size());
    for (std::size_t i = 0; i < list.rows.size(); ++i) {
        const auto& row = list.rows[i];
        const auto resource = std::ranges::find(projection.document.resources, row.resource,
                                                &resource_entry_t::id);
        auto& view = audio.sound_rows[i];
        view.resource_id = row.resource;
        view.asset_id = resource == projection.document.resources.end() ? asset_id_t{} : resource->asset;
        view.name = row.name;
        view.tags_text = row.tags_text;
        if (resource == projection.document.resources.end()) view.tags.clear();
        else view.tags = resource->tags;
        view.source_path = row.source_path;
        view.kind = row.kind;
        view.use_count = row.use_count;
        view.duration_frames = row.duration_frames;
        view.sample_rate = row.sample_rate;
        view.missing = row.missing;
        view.bmson_channel_id = {};
        view.bmson_channel_continues = false;
    }
    populate_sound_lane_modes(projection);
}

void build_sound_usage_position_label(std::string& label, const song_document_t& document,
                                      const sound_usage_row_t& row) {
    if (!row.position) { label = "Source clip"; return; }
    const auto* chart = document.find_chart(row.chart);
    const auto* measure = chart ? chart->find_measure(row.position->measure) : nullptr;
    if (!measure) { label = "Unavailable position"; return; }
    std::array<char, 96> text{};
    std::snprintf(text.data(), text.size(), "Measure %llu, row %llu",
        static_cast<unsigned long long>(measure - chart->measures.data() + 1),
        static_cast<unsigned long long>(row.position->row));
    label = text.data();
}

void populate_sound_usage(const workspace_projection_context_t& projection) {
    auto& result = projection.result;
    auto& view = result.view.audio_slices;
    const auto& request = projection.sound_usage_request;
    view.usage_song_id = projection.document.id;
    view.usage_document_revision = projection.document.revision;
    if (request.selected_project_id != projection.document.id) { view.usage_rows.clear(); return; }
    view.selected_resource_id = request.selected_resource_id;
    view.selected_channel_id = request.selected_channel_id;
    view.selected_clip_id = request.selected_clip_id;
    if (!request.find_uses_open || !request.selected_resource_id) { view.usage_rows.clear(); return; }
    const auto& usage = result.sound_usage_cache.view_for(projection.document, request.selected_resource_id,
                                                       result.query_session_generation);
    view.usage_song_id = usage.song;
    view.usage_document_revision = usage.revision;
    view.usage_resource_id = usage.resource;
    view.usage_asset_id = usage.asset;
    view.usage_valid = usage.valid;
    view.usage_total_count = usage.total_matching;
    view.usage_rows_truncated = usage.truncated;
    view.usage_rows.resize(usage.rows.size());
    for (std::size_t i = 0; i < usage.rows.size(); ++i) {
        const auto& row = usage.rows[i];
        auto& item = view.usage_rows[i];
        item.chart_id = row.chart;
        item.measure_id = row.position ? row.position->measure : measure_id_t{};
        item.row = row.position ? row.position->row : 0;
        item.position_valid = row.position.has_value();
        item.object_id = row.object;
        item.channel_id = row.channel;
        item.clip_id = row.clip;
        item.referenced_resource_id = row.referenced_resource;
        item.referenced_asset_id = row.referenced_asset;
        item.kind = row.kind;
        item.scope = row.scope;
        item.chart_label = row.chart_name;
        build_sound_usage_position_label(item.position_label, projection.document, row);
    }
}

void populate_audio_slices_tab(const workspace_projection_context_t& projection,
                               const workspace_audio_slices_view_t& tab) {
    auto& audio = projection.result.view.audio_slices;
    audio.visible = true;
    populate_sound_rows(projection);
    populate_sound_usage(projection);
    audio.asset_ids.assign(tab.audio_assets.begin(), tab.audio_assets.end());
    audio.has_selected_asset = tab.transport.audio_asset.has_value();
    audio.selected_asset_id = tab.transport.audio_asset.value_or(asset_id_t{});
    append_workspace_count(projection, "Audio assets: ", tab.audio_assets.size());
    append_workspace_count(projection, "Clips: ", tab.clip_count);
    append_workspace_count(projection, "Selected chart sound channels: ", tab.sound_channel_count);
    append_workspace_summary(projection, tab.transport.playing ? "Transport: playing" : "Transport: stopped");
}

void populate_compare_tab(const workspace_projection_context_t& projection, const wormedit::workspace_compare_validate_view_t& tab) {
    const auto append = [&](std::string line) { append_workspace_summary(projection, std::move(line)); };
                append("Compared difficulties: " + std::to_string(tab.comparison_charts.size()));
                append("Validation errors: " + std::to_string(tab.error_count));
                append("Validation warnings: " + std::to_string(tab.warning_count));
                for (const auto& diagnostic : tab.diagnostics) {
                    append(diagnostic.message);
                }

}
struct workspace_tab_visitor {
    const workspace_projection_context_t& projection;
    void operator()(const wormedit::workspace_setup_view_t& tab) const {
        populate_setup_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_metadata_view_t& tab) const {
        populate_metadata_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_timing_view_t& tab) const {
        populate_timing_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_compose_view_t& tab) const {
        populate_compose_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_storyboard_view_t& tab) const {
        populate_storyboard_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_audio_slices_view_t& tab) const {
        populate_audio_slices_tab(projection, tab);
    }
    void operator()(const wormedit::workspace_compare_validate_view_t& tab) const {
        populate_compare_tab(projection, tab);
    }
};

} // namespace

namespace {
template<class View, class... Containers>
void reset_workspace_storage(View& view, Containers&... containers) {
    auto storage = std::tuple{std::move(containers)...};
    view = {};
    std::tie(containers...) = std::move(storage);
}
} // namespace

void frontend_workspace_view_t::reset() {
    if (!arena_ && owned_arena_) owned_arena_->reset();
    projection_feedback.clear();
    summary_size = 0;
    reset_workspace_storage(view,
        view.recent_files, view.menu_commands, view.summary_lines,
        view.recovery_candidates, view.recovery_diagnostics, view.command_menu_rows,
        view.setup, view.options, view.chart_import, view.notifications,
        view.metadata, view.audio_slices, view.timing, view.player_preview, view.external_player,
        view.compare.charts, view.compare.diagnostics,
        view.playback_rates.entries, view.bms_encodings.entries, view.sound_kinds.entries,
        view.compose.timing_lanes.entries,
        view.compose.timing_lane_captions.entries, view.compose.note_palette.entries,
        view.compose.measure_lane.selected_bar_lines);
    view.setup.reset();
    view.options.reset();
    view.chart_import.reset();
    view.notifications.reset();
    view.metadata.reset();
    view.audio_slices.reset();
    view.timing.reset();
    view.player_preview.reset();
    view.external_player.reset();
    view.menu_commands.clear();
    view.recent_files.clear();
    view.command_menu_rows.clear();
    view.compare.charts.clear();
    view.compare.diagnostics.clear();
    view.compose.measure_lane.selected_bar_lines.clear();
}

void frontend_workspace_view_t::rebind_view_pointers() {
    auto& compose_view = view.compose;
    compose_view.bookmark_chooser.rows = compose.bookmark_chooser_row.data();
    compose_view.resources = compose.resource.view.data();
    compose_view.keysounds = compose.keysound.row.data();
    compose_view.keysound_selected_rows = compose.keysound.selected_row.data();
    compose_view.keysound_selection_rows = compose.keysound.selection_row.data();
    compose_view.keysound_display_rows = compose.keysound.display_row.data();
    compose_view.keysound_advance_rows = compose.keysound.advance_row.data();
    compose_view.keysound_listed_resource_ids = compose.keysound.listed_resource_id.data();
    compose_view.keysound_all_resource_ids = compose.keysound.all_resource_id.data();
    compose_view.sound_slots = compose.keysound.slot.data();
    compose_view.keysound_layers = compose.keysound.layer.data();
    compose_view.keysound_filter_layers = compose.keysound.filter_layer.data();
    compose_view.output_groups = compose.output_group.view.data();
    compose_view.branch_rows = compose.branch.row.data();
    compose_view.branch_choices = compose.branch.choice.data();

}

} // namespace wormedit::sys

void wormedit::ui::notification_view_t::refill(const notification_center_t& center,
                                             std::chrono::nanoseconds now) {
    current_text = {};
    current_severity = {};
    toasts.clear();
    if (const auto* current = center.current_line()) {
        current_text = current->message;
        current_severity = current->severity;
    }
    const auto unread = center.unread_counts();
    unread_warnings = unread.warnings;
    unread_errors = unread.errors;
    const auto entries = center.borrowed_snapshot(notification_capacity);
    for (auto entry = entries.rbegin(); entry != entries.rend(); ++entry) {
        if (entry->severity != notification_severity_t::warning &&
            entry->severity != notification_severity_t::error)
            continue;
        if (entry->severity == notification_severity_t::warning &&
            now - entry->timestamp >= std::chrono::seconds{8})
            continue;
        toasts.push_back({.id = entry->id, .severity = entry->severity,
                         .message = entry->message, .repeat_count = entry->count});
        if (toasts.size() == maximum_visible_toasts) break;
    }
}

auto wormedit::ui::notification_view_t::from(const notification_center_t& center,
                                           std::chrono::nanoseconds now) -> notification_view_t {
    notification_view_t result;
    result.refill(center, now);
    return result;
}

namespace wormedit::sys {

auto frontend_workspace_view_t::from(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured, frame_arena_t* arena, dock_sort_t keysound_sort,
    WormUiKeysoundDockRequest keysound_request, wormedit::ui::sound_usage_request_t sound_usage_request,
                       WormUiBookmarkChooserRequest bookmark_chooser_request,
                       keysound_list_cache_t* keysound_cache,
                       const bookmark_chooser_indices_t& bookmark_indices,
                       compose_resource_catalog_cache_t* resource_catalog_cache) -> frontend_workspace_view_t {
    frontend_workspace_view_t result(arena);
    result.refill(source, document, status, recovery, recovery_scan_configured, arena,
                  keysound_sort, keysound_request, sound_usage_request, bookmark_chooser_request,
                  keysound_cache, bookmark_indices, resource_catalog_cache);
    return result;
}

void frontend_workspace_view_t::refill(const wormedit::workspace_view_t& source,
                       const wormedit::song_document_t& document, const std::string& status,
                       const wormedit::native_project_recovery_controller_view_t& recovery,
                       bool recovery_scan_configured, frame_arena_t* arena, dock_sort_t keysound_sort,
    WormUiKeysoundDockRequest keysound_request, wormedit::ui::sound_usage_request_t sound_usage_request,
                       WormUiBookmarkChooserRequest bookmark_chooser_request,
                       keysound_list_cache_t* keysound_cache,
                       const bookmark_chooser_indices_t& bookmark_indices,
                       compose_resource_catalog_cache_t* resource_catalog_cache) {
    arena_ = arena;
    reset();
    auto& result = *this;
    result.view.project_title = document.metadata.title.c_str();
    result.view.artist = document.metadata.artist.c_str();
    result.view.project_id = document.id;
    result.view.document_revision = document.revision;
    result.view.selected_chart_id = source.selected_difficulty
        ? source.selected_difficulty->id : chart_id_t{};
    result.view.selected_difficulty = source.selected_difficulty.has_value()
                                          ? source.selected_difficulty->difficulty_name.c_str()
                                          : "No difficulty";
    result.view.status_text = status.c_str();
    result.view.git_hash = WORMEDIT_GIT_HASH;
    result.view.active_tab = static_cast<WormUiWorkspaceTab>(source.active_tab);
    result.view.history_available = source.history.available;
    result.view.dirty = source.history.dirty;
    result.view.undo_depth = source.history.undo_depth;
    result.view.redo_depth = source.history.redo_depth;
    populate_recovery_view(result, recovery, recovery_scan_configured);
    static_assert(workspace_tab_count == WORM_UI_WORKSPACE_TAB_COUNT);
    for (const auto& tab : workspace_tabs) {
        const auto index = static_cast<std::size_t>(tab.kind);
        result.view.tabs[index] = {
            .label = tab.kind == workspace_tab_t::timing ? std::string_view{} : tab.label,
            .enabled = is_workspace_tab_enabled(tab.kind),
            .id = tab.kind,
        };
    }
    result.view.tab_heading = source.active_tab == workspace_tab_t::timing
        ? "Compose" : workspace_tabs[static_cast<std::size_t>(source.active_tab)].label.data();
    result.view.sound_kinds.refill(sound_kind_view);
    result.view.new_project_layouts = new_project_layout_view;
    result.view.playback_rates.refill(playback_rate_view);
    result.view.bms_encodings.refill(bms_encoding_view);
    result.view.compose.timing_lanes.refill(timing_lane_view);
    result.view.compose.timing_lane_captions.refill(timing_caption_view);
    result.view.compose.note_palette.refill(note_palette_view);
    result.view.setup.output_families.refill(output_family_view);
    result.view.metadata.format_families.refill(metadata_family_view);
    result.view.transport_rate_id = audio::playback_rate_id_t::normal;

    const workspace_projection_context_t projection{
        result,        source,           document,
        keysound_sort, keysound_request, sound_usage_request, bookmark_chooser_request,
        keysound_cache, bookmark_indices, resource_catalog_cache};
    std::visit(workspace_tab_visitor{projection}, source.tab_view);
    if (!view.metadata.visible) {
        view.metadata.search_tags.clear();
        view.metadata.format_fields.clear();
        view.metadata.channel_map_lanes.clear();
        view.metadata.lane_profile_choices.clear();
        view.metadata.chart_choices.clear();
    }
    if (!view.audio_slices.visible) {
        view.audio_slices.sound_rows.clear();
        view.audio_slices.usage_rows.clear();
    }
    // Export is project UI and must not depend on building a Compose scene.
    const auto* compose_tab = std::get_if<workspace_compose_view_t>(&source.tab_view);
    const auto selected = source.selected_difficulty ? source.selected_difficulty->id
        : compose_tab ? compose_tab->chart : chart_id_t{};
    result.view.compose.project_id = document.id.value;
    result.view.compose.chart_id = selected.value;
    result.view.compose.document_revision = document.revision;
    populate_compose_output_groups(projection, selected);
    result.view.audio_slices.sound_kinds.refill(sound_kind_view);
    view.summary_lines.resize(summary_size);
    rebind_view_pointers();
}

} // namespace wormedit::sys

namespace wormedit::ui {

void metadata_view_t::reset() {
    sys::reset_workspace_storage(*this,
        title, artist, chart_name,
        chart_author, project_directory, selected_music_source,
        dropped_music_path, dropped_output_relative_path, copy_preview_text,
        copy_preview_source, channel_map_preview_message, lane_profile_preview_message,
        audio_waveform_columns, search_tags, format_fields,
        format_targets, output_groups, channel_map_lanes,
        lane_profile_choices, chart_choices, format_families.entries);
    title.clear();
    artist.clear();
    chart_name.clear();
    chart_author.clear();
    project_directory.clear();
    selected_music_source.clear();
    dropped_music_path.clear();
    dropped_output_relative_path.clear();
    copy_preview_text.clear();
    copy_preview_source.clear();
    channel_map_preview_message.clear();
    lane_profile_preview_message.clear();
    audio_waveform_columns.clear();
    format_targets.clear();
    output_groups.clear();
}

void audio_slices_view_t::reset() {
    sys::reset_workspace_storage(*this,
        sound_rows, usage_rows, asset_ids,
        waveform_columns, sound_kinds.entries);
    asset_ids.clear();
    waveform_columns.clear();
}

} // namespace wormedit::ui
