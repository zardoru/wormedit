#include "sys/metadata_view_projection.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <ranges>

namespace wormedit::sys {

void populate_metadata_view_projection(
    flattened_workspace_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime) {
    if (!flattened.view.metadata.visible)
        return;

    auto& metadata = flattened.view.metadata;
    metadata_drop.discard_if_stale(history.document.revision, metadata.chart_id);
    flattened.metadata.project_directory = asset_base.generic_string();
    metadata.project_directory = flattened.metadata.project_directory.c_str();
    metadata.import_copy_pending = metadata_asset_runtime.import_pending();
    if (!asset_base.empty()) {
        if (const auto next = wormedit::next_resource_asset_id(history.document))
            metadata.next_import_asset_id = next->value;
    }
    metadata_drop.publish(metadata);

    if (workspace.transport.audio_asset) {
        const auto audio = std::ranges::find(history.document.audio_assets,
                                             *workspace.transport.audio_asset,
                                             &wormedit::audio_asset_t::id);
        if (audio != history.document.audio_assets.end()) {
            metadata.selected_music_asset_id = audio->id.value;
            flattened.metadata.music_source = audio->source_path.generic_string();
            metadata.selected_music_source = flattened.metadata.music_source.c_str();
            metadata.selected_music_resource_count =
                std::ranges::count(history.document.resources, audio->id,
                                   &wormedit::resource_entry_t::asset);
            for (const auto& chart : history.document.charts) {
                metadata.selected_music_channel_count += std::ranges::count_if(
                    chart.sound_channels, [&](const auto& channel) {
                        return channel.source_clip.source == audio->id;
                    });
            }
        }
    }

    metadata.output_groups = flattened.metadata.output_group.view.data();
    const auto& copy_key = metadata_asset_runtime.copy_preview_key();
    if (copy_key && copy_key->song == history.document.id &&
        copy_key->revision == history.document.revision &&
        copy_key->chart.value == metadata.chart_id &&
        (copy_key->import_copy
             ? copy_key->asset.value == metadata.next_import_asset_id
             : copy_key->asset.value == metadata.selected_music_asset_id &&
                   metadata.selected_music_source &&
                   copy_key->source_path.generic_string() == metadata.selected_music_source)) {
        metadata.copy_preview_text = metadata_asset_runtime.copy_preview_message().c_str();
        metadata.copy_preview_pending = metadata_asset_runtime.copy_preview_pending();
        metadata.copy_preview_export = copy_key->export_copy;
        metadata.copy_preview_import = copy_key->import_copy;
        metadata.copy_preview_group_index = copy_key->output_group_index;
        metadata.copy_preview_asset_id = copy_key->asset.value;
        flattened.metadata.copy_preview_source = copy_key->source_path.generic_string();
        metadata.copy_preview_source = flattened.metadata.copy_preview_source.c_str();
    }

    for (std::size_t group_index = 0;
         group_index < history.document.output_groups.size(); ++group_index) {
        const auto& group = history.document.output_groups[group_index];
        if (!workspace.selected_chart ||
            std::ranges::find(group.charts, *workspace.selected_chart) == group.charts.end())
            continue;
        if (metadata.output_group_count >= WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY) {
            metadata.output_groups_truncated = true;
            continue;
        }
        const auto slot = metadata.output_group_count++;
        flattened.metadata.output_group.name[slot] = group.name;
        flattened.metadata.output_group.path[slot] =
            group.relative_output_path.generic_string();
        const auto asset_directory =
            wormedit::export_asset_directory(group.relative_output_path);
        flattened.metadata.output_group.asset_directory[slot] =
            asset_directory ? asset_directory->generic_string() : std::string{};
        flattened.metadata.output_group.view[slot] = {
            .document_index = group_index,
            .name = flattened.metadata.output_group.name[slot].c_str(),
            .relative_output_path = flattened.metadata.output_group.path[slot].c_str(),
            .relative_asset_directory =
                flattened.metadata.output_group.asset_directory[slot].c_str()};
    }

    metadata.search_tags = metadata.search_tags_editable
                               ? flattened.metadata.search_tag.data()
                               : nullptr;
    flattened.view.metadata.search_tag_bytes =
        flattened.view.metadata.search_tags_editable
            ? flattened.metadata.search_tag_length.data()
            : nullptr;
    flattened.view.metadata.format_fields = flattened.metadata.format_field.data();
    flattened.view.metadata.format_targets = flattened.metadata.format_target.data();
}

} // namespace wormedit::sys
