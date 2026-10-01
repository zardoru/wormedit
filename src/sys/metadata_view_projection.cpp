#include "sys/metadata_view_projection.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <ranges>

namespace wormedit::sys {
namespace {
auto metadata_frame_text(flattened_workspace_t& flattened, std::string_view text)
    -> const char* {
    auto* arena = flattened.arena();
    const auto* copied = arena ? arena_string(*arena, text) : nullptr;
    if (!copied) flattened.metadata.text_truncated = true;
    return copied;
}

void populate_metadata_music(flattened_workspace_t& flattened,
                             const wormedit::document_history_t& history,
                             const wormedit::workspace_controller_t& workspace) {
    if (!workspace.transport.audio_asset) return;
    const auto audio = std::ranges::find(history.document.audio_assets,
        *workspace.transport.audio_asset, &wormedit::audio_asset_t::id);
    if (audio == history.document.audio_assets.end()) return;
    auto& metadata = flattened.view.metadata;
    metadata.selected_music_asset_id = audio->id.value;
    metadata.selected_music_source = metadata_frame_text(flattened,
                                                        audio->source_path.generic_string());
    metadata.selected_music_resource_count = std::ranges::count(
        history.document.resources, audio->id, &wormedit::resource_entry_t::asset);
    for (const auto& chart : history.document.charts) {
        metadata.selected_music_channel_count += std::ranges::count_if(
            chart.sound_channels, [&](const auto& channel) {
                return channel.source_clip.source == audio->id;
            });
    }
}

auto metadata_copy_preview_matches(const WormUiMetadataView& metadata,
                                   const wormedit::document_history_t& history,
                                   const metadata_copy_preview_key_t& key) -> bool {
    if (key.song != history.document.id || key.revision != history.document.revision ||
        key.chart.value != metadata.chart_id) return false;
    if (key.import_copy) return key.asset.value == metadata.next_import_asset_id;
    return key.asset.value == metadata.selected_music_asset_id &&
        metadata.selected_music_source &&
        key.source_path.generic_string() == metadata.selected_music_source;
}

void populate_metadata_copy_preview(flattened_workspace_t& flattened,
                                    const wormedit::document_history_t& history,
                                    const runtime::metadata_asset_t& runtime) {
    auto& metadata = flattened.view.metadata;
    const auto& key = runtime.copy_preview_key();
    if (!key || !metadata_copy_preview_matches(metadata, history, *key)) return;
    const auto* message = metadata_frame_text(flattened, runtime.copy_preview_message());
    metadata.copy_preview_text = message ? message :
        "Metadata display text truncated (frame storage limit).";
    metadata.copy_preview_pending = runtime.copy_preview_pending();
    metadata.copy_preview_export = key->export_copy;
    metadata.copy_preview_import = key->import_copy;
    metadata.copy_preview_group_index = key->output_group_index;
    metadata.copy_preview_asset_id = key->asset.value;
    metadata.copy_preview_source = metadata_frame_text(flattened, key->source_path.generic_string());
}

void populate_metadata_output_groups(flattened_workspace_t& flattened,
                                     const wormedit::document_history_t& history,
                                     const wormedit::workspace_controller_t& workspace) {
    auto& metadata = flattened.view.metadata;
    const auto selected_count = std::ranges::count_if(history.document.output_groups,
        [&](const auto& group) {
            return workspace.selected_chart &&
                std::ranges::find(group.charts, *workspace.selected_chart) != group.charts.end();
        });
    const auto count = std::min(static_cast<std::size_t>(selected_count),
                                std::size_t{WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY});
    auto* arena = flattened.arena();
    flattened.metadata.output_group.view = arena
        ? arena_array<WormUiComposeOutputGroupView>(*arena, count)
        : frame_span<WormUiComposeOutputGroupView>{};
    metadata.output_groups = flattened.metadata.output_group.view.data();
    if (flattened.metadata.output_group.view.capacity() != count) {
        metadata.output_groups_truncated = true;
        return;
    }
    for (std::size_t index = 0; index < history.document.output_groups.size(); ++index) {
        const auto& group = history.document.output_groups[index];
        if (!workspace.selected_chart ||
            std::ranges::find(group.charts, *workspace.selected_chart) == group.charts.end()) continue;
        if (metadata.output_group_count >= WORM_UI_COMPOSE_OUTPUT_GROUP_CAPACITY) {
            metadata.output_groups_truncated = true;
            break;
        }
        const auto* name = metadata_frame_text(flattened, group.name);
        const auto* path = metadata_frame_text(flattened, group.relative_output_path.generic_string());
        const auto directory = wormedit::export_asset_directory(group.relative_output_path);
        const auto* assets = metadata_frame_text(flattened,
            directory ? directory->generic_string() : std::string{});
        if (!name || !path || !assets) {
            metadata.output_groups_truncated = true;
            break;
        }
        (void)flattened.metadata.output_group.view.push_back({
            .document_index = index, .name = name, .relative_output_path = path,
            .relative_asset_directory = assets});
        metadata.output_group_count = flattened.metadata.output_group.view.size();
    }
}
} // namespace


void populate_metadata_view_projection(
    flattened_workspace_t& flattened, const wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    const std::filesystem::path& asset_base, metadata_drop_state_t& metadata_drop,
    runtime::metadata_asset_t& metadata_asset_runtime) {
    if (!flattened.view.metadata.visible)
        return;

    auto& metadata = flattened.view.metadata;
    metadata_drop.discard_if_stale(history.document.revision, metadata.chart_id);
    metadata.project_directory = metadata_frame_text(flattened, asset_base.generic_string());
    metadata.import_copy_pending = metadata_asset_runtime.import_pending();
    if (!asset_base.empty()) {
        if (const auto next = wormedit::next_resource_asset_id(history.document))
            metadata.next_import_asset_id = next->value;
    }
    metadata_drop.publish(metadata);
    metadata.dropped_music_path = metadata_frame_text(flattened, metadata_drop.music_path);
    metadata.dropped_output_relative_path = metadata_frame_text(flattened, metadata_drop.output_path);

    populate_metadata_music(flattened, history, workspace);
    populate_metadata_copy_preview(flattened, history, metadata_asset_runtime);
    populate_metadata_output_groups(flattened, history, workspace);

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
