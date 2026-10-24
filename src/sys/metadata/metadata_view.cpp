#include "sys/metadata/metadata_view.h"

#include "sys/metadata/metadata_actions.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/timing_links.h>

#include <algorithm>
#include <ranges>
#include <string>

namespace wormedit::sys {
auto metadata_output_path_key_t::operator==(const metadata_output_path_query_t& query) const -> bool {
    return path == query.path && destination == query.destination;
}
auto metadata_output_path_key_t::from(const metadata_output_path_query_t& query)
    -> metadata_output_path_key_t {
    return {query.path, query.destination};
}
void metadata_output_path_key_t::assign(const metadata_output_path_query_t& query) {
    path = query.path;
    destination = query.destination;
}
auto metadata_channel_summary_key_t::operator==(const channel_map_preview_view_t& preview) const -> bool {
    return report.has_value() == (preview.report != nullptr) &&
        diagnostic.has_value() == (preview.diagnostic != nullptr) &&
        (!report || *report == *preview.report) &&
        (!diagnostic || *diagnostic == *preview.diagnostic);
}
auto metadata_channel_summary_key_t::from(const channel_map_preview_view_t& preview)
    -> metadata_channel_summary_key_t {
    metadata_channel_summary_key_t key;
    key.assign(preview);
    return key;
}
void metadata_channel_summary_key_t::assign(const channel_map_preview_view_t& preview) {
    if (preview.report) report = *preview.report;
    else report.reset();
    if (preview.diagnostic) diagnostic = *preview.diagnostic;
    else diagnostic.reset();
}
namespace {
auto metadata_path_text(keyed_cache_t<std::filesystem::path, std::string>& cache,
                        const std::filesystem::path& path) -> const std::string& {
    return cache.get_or_build(path, [&] { return path.generic_string(); });
}


void populate_metadata_music(frontend_workspace_view_t& flattened,
                             const wormedit::document_history_t& history,
                             const wormedit::workspace_controller_t& workspace) {
    if (!workspace.transport.audio_asset) return;
    const auto audio = history.document.find_audio_asset(*workspace.transport.audio_asset);
    if (audio == nullptr) return;
    auto& metadata = flattened.view.metadata;
    metadata.selected_music_asset_id = audio->id;
    metadata.selected_music_source = metadata_path_text(
        flattened.metadata_projection_cache.selected_music_source, audio->source_path);
    metadata.selected_music_resource_count = std::ranges::count(
        history.document.resources, audio->id, &wormedit::resource_entry_t::asset);
    for (const auto* chart : history.document.read_charts()) {
        metadata.selected_music_channel_count += std::ranges::count_if(
            chart->sound_channels, [&](const auto& channel) {
                return channel.source_clip.source == audio->id;
            });
    }
}

auto metadata_copy_preview_matches(const wormedit::ui::metadata_view_t& metadata,
                                   const wormedit::document_history_t& history,
                                   const metadata_copy_preview_key_t& key) -> bool {
    if (key.song != history.document.id || key.revision != history.document.revision ||
        key.chart != metadata.chart_id) return false;
    if (key.import_copy) return key.asset == metadata.next_import_asset_id;
    const auto* asset = history.document.find_audio_asset(metadata.selected_music_asset_id);
    return asset && key.asset == asset->id && !asset->source_path.empty() &&
        key.source_path == asset->source_path;
}

void populate_metadata_chart_resolution(frontend_workspace_view_t& flattened,
                                       const wormedit::document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    const auto chart = history.read_chart(wormedit::chart_id_t{metadata.chart_id});
    if (chart == nullptr || !metadata.has_chart) return;
    metadata.chart_divisions_per_standard_measure = chart->divisions_per_standard_measure;
}

void populate_metadata_copy_preview(frontend_workspace_view_t& flattened,
                                    const wormedit::document_history_t& history,
                                    const runtime::metadata_asset_t& runtime) {
    auto& metadata = flattened.view.metadata;
    const auto& key = runtime.copy_preview_key();
    if (!key || !metadata_copy_preview_matches(metadata, history, *key)) return;
    metadata.copy_preview_text = runtime.copy_preview_message();
    metadata.copy_preview_pending = runtime.copy_preview_pending();
    metadata.copy_preview_export = key->export_copy;
    metadata.copy_preview_import = key->import_copy;
    metadata.copy_preview_group_index = key->output_group_index;
    metadata.copy_preview_asset_id = key->asset;
    metadata.copy_preview_source = metadata_path_text(
        flattened.metadata_projection_cache.copy_preview_source, key->source_path);
}

void populate_metadata_channel_map_lanes(frontend_workspace_view_t& flattened,
                                        const document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    const auto* chart = history.read_chart(metadata.chart_id);
    if (!chart || !metadata.has_chart || chart->source_family != format_family_t::bms ||
        chart->lane_layouts.empty()) { metadata.channel_map_lanes.clear(); return; }
    const auto& segment = chart->lane_layouts.front();
    metadata.channel_map_layout_segment_id = segment.id;
    metadata.channel_map_editable = segment.layout.lanes.size() <= ui::metadata_channel_map_lane_limit;
    metadata.channel_map_lanes.resize(segment.layout.lanes.size());
    for (std::size_t i = 0; i < segment.layout.lanes.size(); ++i) {
        const auto& lane = segment.layout.lanes[i];
        auto& row = metadata.channel_map_lanes[i];
        row.lane_id = lane.id;
        row.name = lane.name;
        row.channels[0] = lane.bms_channels.visible.value_or("");
        row.channels[1] = lane.bms_channels.invisible.value_or("");
        row.channels[2] = lane.bms_channels.long_note.value_or("");
        row.channels[3] = lane.bms_channels.mine.value_or("");
    }
}

auto channel_map_preview_summary(const wormedit::channel_map_preview_view_t& preview) -> std::string {
    if (preview.diagnostic && preview.diagnostic->issue != wormedit::channel_map_issue_t::none)
        return preview.diagnostic->message;
    if (preview.report && !preview.report->conflicts.empty()) {
        const auto& first = preview.report->conflicts.front();
        return "Channel " + first.channel + " would be shared by " + first.first_lane_name +
               " and " + first.second_lane_name + ".";
    }
    return "Preview: " + std::to_string(preview.report ? preview.report->affected_note_count : 0) +
           " notes change channel.";
}

void populate_metadata_channel_map_preview(frontend_workspace_view_t& flattened,
                                           const metadata_drop_state_t& metadata_drop) {
    auto& metadata = flattened.view.metadata;
    const auto& outcome = metadata_drop.channel_map_preview;
    if (!outcome) return;
    const auto preview = wormedit::channel_map_preview_view_t::from(*outcome);
    const auto valid = preview.proposal != nullptr;
    metadata.channel_map_preview_present = true;
    metadata.channel_map_preview_valid = valid;
    metadata.channel_map_preview_affected_note_count = preview.report ? preview.report->affected_note_count : 0;
    metadata.channel_map_preview_conflict_count = preview.report ? preview.report->conflict_count : 0;
    metadata.channel_map_preview_message = flattened.metadata_projection_cache.channel_summary.get_or_build(
        preview, [&] { return channel_map_preview_summary(preview); });
    metadata.channel_map_apply_enabled = valid && preview.report->conflict_count == 0 &&
                                         metadata.channel_map_editable;
}

constexpr const char* custom_lane_profile_reason =
    "Custom layouts need the per-lane mapping editor, which is not available here yet.";

// The chart's key-count family, then a disabled Custom entry.
void populate_metadata_lane_profile(frontend_workspace_view_t& flattened,
                                    const wormedit::document_history_t& history,
                                    const metadata_drop_state_t& metadata_drop) {
    auto& metadata = flattened.view.metadata;
    const auto chart = history.read_chart(wormedit::chart_id_t{metadata.chart_id});
    if (chart == nullptr || !metadata.has_chart) { metadata.lane_profile_choices.clear(); return; }
    const auto& targets = flattened.metadata_projection_cache.lane_targets.get_or_build(
        std::tuple{document_stamp_t::from(history.document), chart->id,
                   flattened.query_session_generation},
        [&] { return lane_profile_family_targets(*chart); });
    if (targets.empty()) { metadata.lane_profile_choices.clear(); return; }
    auto& choices = metadata.lane_profile_choices;
    choices.resize(targets.size() + 1);
    const auto& current_id = chart->lane_layouts.front().layout.profile_id;
    metadata.lane_profile_current = targets.size();
    for (std::size_t i = 0; i < targets.size(); ++i) {
        const auto& layout = targets[i].layout;
        choices[i].label = layout.display_name.empty() ? layout.profile_id : layout.display_name;
        choices[i].enabled = true;
        choices[i].reason.clear();
        if (layout.profile_id == current_id) metadata.lane_profile_current = i;
    }
    choices.back().label = "Custom";
    choices.back().enabled = false;
    choices.back().reason = custom_lane_profile_reason;
    if (!metadata_drop.lane_profile_preview) return;
    const auto preview = wormedit::lane_migration_preview_view_t::from(*metadata_drop.lane_profile_preview);
    const auto mapped_count = preview.report ? preview.report->mapped_note_count : 0;
    const auto dropped_count = preview.report ? preview.report->dropped_notes.size() : 0;
    const auto collision_count = preview.report ? preview.report->collision_count : 0;
    metadata.lane_profile_preview_present = true;
    metadata.lane_profile_preview_valid = preview.proposal != nullptr;
    metadata.lane_profile_mapped_note_count = mapped_count;
    metadata.lane_profile_dropped_note_count = dropped_count;
    metadata.lane_profile_collision_count = collision_count;
    metadata.lane_profile_apply_enabled = preview.proposal != nullptr;
    metadata.lane_profile_preview_message = flattened.metadata_projection_cache.lane_summary.get_or_build(
        std::array{mapped_count, dropped_count, collision_count}, [&] {
            return "Key-count preview: " + std::to_string(mapped_count) + " notes map, " +
                std::to_string(dropped_count) + " dropped, " +
                std::to_string(collision_count) + " collisions.";
        });
}

void populate_metadata_inheritance(frontend_workspace_view_t& flattened, const document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    const auto* chart = metadata.has_chart ? history.read_chart(metadata.chart_id) : nullptr;
    if (!chart) { metadata.inheritance.clear(); return; }
    const auto& resolved = flattened.metadata_projection_cache.inheritance.get_or_build(
        std::tuple{document_stamp_t::from(history.document), chart->id, flattened.query_session_generation},
        [&] { return resolved_metadata_t::from(history.document, *chart); });
    metadata.inheritance.resize(inherited_metadata_fields.size());
    for (std::size_t index = 0; index < inherited_metadata_fields.size(); ++index) {
        const auto& field = inherited_metadata_fields[index];
        auto& row = metadata.inheritance[index];
        row.field = field.field;
        row.label = field.label;
        row.inherited = resolved.inherited[index];
        row.override_value = chart->metadata.overrides.value(field.field);
    }
}

void populate_metadata_chart_choices(frontend_workspace_view_t& flattened,
                                     const document_history_t& history) {
    auto& choices = flattened.view.metadata.chart_choices;
    const auto charts = history.document.read_charts();
    choices.resize(charts.size());
    for (std::size_t i = 0; i < choices.size(); ++i) {
        choices[i].chart_id = charts[i]->id;
        choices[i].name = charts[i]->metadata.name;
    }
}

void populate_metadata_timing_links(frontend_workspace_view_t& flattened, const document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    metadata.timing_link_context = {.project = history.document.id, .chart = metadata.chart_id,
        .revision = history.document.revision, .session_generation = flattened.query_session_generation};
    const auto* chart = metadata.has_chart ? history.read_chart(metadata.chart_id) : nullptr;
    if (!chart) { metadata.timing_links.clear(); return; }
    metadata.timing_links.resize(timing_categories.size());
    for (std::size_t index = 0; index < timing_categories.size(); ++index) {
        const auto& category = timing_categories[index];
        const auto& link = chart->timing_links.value(category.kind);
        auto& row = metadata.timing_links[index];
        row.category = category.kind;
        row.label = category.label;
        row.source = link ? link->source : chart_id_t{};
        row.is_local_override = link && link->is_local_override;
        row.requires_confirmation = category.requires_link_confirmation(*chart);
        row.status = link ? link->is_local_override ? "Local override: " : "Linked: " : "Local";
        if (link) {
            const auto* source = history.read_chart(link->source);
            row.status += source ? source->metadata.name : "Missing source";
        }
    }
}

auto metadata_output_path_text(frontend_workspace_view_t& flattened,
                               const output_group_t& group, std::size_t index)
    -> const metadata_output_path_text_t& {
    auto& caches = flattened.metadata_projection_cache.output_paths;
    return caches[index].get_or_build(
        metadata_output_path_query_t{group.relative_output_path, group.destination_kind}, [&] {
            const auto directory = export_asset_directory(group.relative_output_path, group.destination_kind);
            return metadata_output_path_text_t{group.relative_output_path.generic_string(),
                directory ? directory->generic_string() : std::string{}};
        });
}

void populate_metadata_output_groups(frontend_workspace_view_t& flattened,
                                     const document_history_t& history,
                                     const workspace_controller_t& workspace) {
    auto& rows = flattened.view.metadata.output_groups;
    if (!workspace.selected_chart) { rows.clear(); return; }
    const auto& groups = history.document.output_groups;
    rows.resize(static_cast<std::size_t>(std::ranges::count_if(groups, [&](const auto& group) {
        return std::ranges::find(group.charts, *workspace.selected_chart) != group.charts.end();
    })));
    flattened.metadata_projection_cache.output_paths.resize(groups.size());
    std::size_t next{};
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& group = groups[i];
        if (std::ranges::find(group.charts, *workspace.selected_chart) == group.charts.end()) continue;
        const auto& text = metadata_output_path_text(flattened, group, i);
        auto& row = rows[next++];
        row.document_index = i;
        row.name = group.name;
        row.relative_output_path = text.path;
        row.relative_asset_directory = text.asset_directory;
    }

}
} // namespace


void populate_metadata_view_projection(
    frontend_workspace_view_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime) {
    metadata_drop.discard_if_stale(history.document.id.value, history.document.revision,
        workspace.selected_chart ? workspace.selected_chart->value : 0);
    if (!flattened.view.metadata.visible)
        return;

    auto& metadata = flattened.view.metadata;
    metadata.project_directory = metadata_path_text(
        flattened.metadata_projection_cache.project_directory, asset_base);
    metadata.import_copy_pending = metadata_asset_runtime.import_pending();
    if (!asset_base.empty()) {
        if (const auto next = wormedit::next_resource_asset_id(history.document))
            metadata.next_import_asset_id = *next;
    }
    metadata_drop.publish(metadata);
    metadata.dropped_music_path = metadata_drop.music_path;
    metadata.dropped_output_relative_path = metadata_drop.output_path;

    populate_metadata_inheritance(flattened, history);
    populate_metadata_music(flattened, history, workspace);
    populate_metadata_chart_resolution(flattened, history);
    populate_metadata_copy_preview(flattened, history, metadata_asset_runtime);
    populate_metadata_output_groups(flattened, history, workspace);
    populate_metadata_channel_map_lanes(flattened, history);
    populate_metadata_channel_map_preview(flattened, metadata_drop);
    populate_metadata_lane_profile(flattened, history, metadata_drop);
    populate_metadata_chart_choices(flattened, history);
    populate_metadata_timing_links(flattened, history);


}

} // namespace wormedit::sys
