#include "sys/metadata_view_projection.h"

#include "sys/metadata_actions.h"

#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/resource_assignment.h>

#include <algorithm>
#include <ranges>
#include <string>

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
    const auto audio = history.document.find_audio_asset(*workspace.transport.audio_asset);
    if (audio == nullptr) return;
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

void populate_metadata_chart_resolution(flattened_workspace_t& flattened,
                                       const wormedit::document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    const auto chart = history.document.find_chart(wormedit::chart_id_t{metadata.chart_id});
    if (chart == nullptr || !metadata.has_chart) return;
    metadata.chart_divisions_per_standard_measure = chart->divisions_per_standard_measure;
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

void populate_metadata_channel_map_lanes(flattened_workspace_t& flattened,
                                        const wormedit::document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    const auto chart = history.document.find_chart(wormedit::chart_id_t{metadata.chart_id});
    if (chart == nullptr || !metadata.has_chart ||
        chart->source_family != wormedit::format_family_t::bms || chart->lane_layouts.empty())
        return;
    // The first layout segment is the one the table edits; its id travels back with the action.
    const auto& segment = chart->lane_layouts.front();
    const auto lane_count = segment.layout.lanes.size();
    const auto count = std::min(lane_count, std::size_t{WORM_UI_METADATA_CHANNEL_MAP_LANE_CAPACITY});
    metadata.channel_map_layout_segment_id = segment.id.value;
    metadata.channel_map_lanes_truncated = count != lane_count;
    auto* arena = flattened.arena();
    flattened.metadata.channel_map_lane = arena
        ? arena_array<WormUiMetadataChannelMapLaneView>(*arena, count)
        : frame_span<WormUiMetadataChannelMapLaneView>{};
    metadata.channel_map_lanes = flattened.metadata.channel_map_lane.data();
    if (flattened.metadata.channel_map_lane.capacity() != count) {
        metadata.channel_map_lanes_truncated = true;
        return;
    }
    for (const auto& lane : segment.layout.lanes) {
        if (flattened.metadata.channel_map_lane.size() >= count) break;
        const auto& channels = lane.bms_channels;
        const auto* name = metadata_frame_text(flattened, lane.name);
        const auto* visible = metadata_frame_text(flattened, channels.visible.value_or(""));
        const auto* invisible = metadata_frame_text(flattened, channels.invisible.value_or(""));
        const auto* long_note = metadata_frame_text(flattened, channels.long_note.value_or(""));
        const auto* mine = metadata_frame_text(flattened, channels.mine.value_or(""));
        if (!name || !visible || !invisible || !long_note || !mine) {
            metadata.channel_map_lanes_truncated = true;
            break;
        }
        (void)flattened.metadata.channel_map_lane.push_back({
            .lane_id = lane.id.value, .name = name,
            .channels = {visible, invisible, long_note, mine}});
    }
    metadata.channel_map_lane_count = flattened.metadata.channel_map_lane.size();
}

auto channel_map_preview_summary(const wormedit::channel_map_proposal_t& preview) -> std::string {
    if (preview.issue != wormedit::channel_map_issue_t::none) return preview.message;
    if (!preview.conflicts.empty()) {
        const auto& first = preview.conflicts.front();
        return "Channel " + first.channel + " would be shared by " + first.first_lane_name +
               " and " + first.second_lane_name + ".";
    }
    return "Preview: " + std::to_string(preview.affected_note_count) + " notes change channel.";
}

void populate_metadata_channel_map_preview(flattened_workspace_t& flattened,
                                           const metadata_drop_state_t& metadata_drop) {
    auto& metadata = flattened.view.metadata;
    const auto& preview = metadata_drop.channel_map_preview;
    if (!preview) return;
    const auto valid = static_cast<bool>(*preview);
    metadata.channel_map_preview_present = true;
    metadata.channel_map_preview_valid = valid;
    metadata.channel_map_preview_affected_note_count = preview->affected_note_count;
    metadata.channel_map_preview_conflict_count = preview->conflict_count;
    const auto* message = metadata_frame_text(flattened, channel_map_preview_summary(*preview));
    metadata.channel_map_preview_message = message ? message : "";
    metadata.channel_map_apply_enabled = valid && preview->conflict_count == 0 &&
                                         !metadata.channel_map_lanes_truncated;
}

constexpr const char* custom_lane_profile_reason =
    "Custom layouts need the per-lane mapping editor, which is not available here yet.";

// The chart's key-count family, then a disabled Custom entry. Labels live in the frame arena.
void populate_metadata_lane_profile(flattened_workspace_t& flattened,
                                    const wormedit::document_history_t& history,
                                    const metadata_drop_state_t& metadata_drop) {
    auto& metadata = flattened.view.metadata;
    const auto chart = history.document.find_chart(wormedit::chart_id_t{metadata.chart_id});
    if (chart == nullptr || !metadata.has_chart) return;
    const auto targets = lane_profile_family_targets(*chart);
    if (targets.empty()) return;
    const auto count = targets.size() + 1;
    auto* arena = flattened.arena();
    auto choices = arena ? arena_array<WormUiMetadataLaneProfileChoice>(*arena, count)
                         : frame_span<WormUiMetadataLaneProfileChoice>{};
    if (choices.capacity() != count) return;
    const auto& current_id = chart->lane_layouts.front().layout.profile_id;
    metadata.lane_profile_current = count - 1;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto& layout = targets[index].layout;
        const auto* label = metadata_frame_text(flattened, layout.display_name.empty() ? layout.profile_id
                                                                                      : layout.display_name);
        if (!label) return;
        if (layout.profile_id == current_id) metadata.lane_profile_current = index;
        (void)choices.push_back({.label = label, .enabled = true, .reason = ""});
    }
    const auto* custom = metadata_frame_text(flattened, "Custom");
    if (!custom) return;
    (void)choices.push_back({.label = custom, .enabled = false, .reason = custom_lane_profile_reason});
    metadata.lane_profile_choices = choices.data();
    metadata.lane_profile_choice_count = choices.size();
    if (!metadata_drop.lane_profile_preview) return;
    const auto& preview = *metadata_drop.lane_profile_preview;
    metadata.lane_profile_preview_present = true;
    metadata.lane_profile_preview_valid = static_cast<bool>(preview);
    metadata.lane_profile_mapped_note_count = preview.mapped_note_count;
    metadata.lane_profile_dropped_note_count = preview.dropped_notes.size();
    metadata.lane_profile_collision_count = preview.collision_count;
    metadata.lane_profile_apply_enabled = static_cast<bool>(preview);
    const auto message = "Key-count preview: " + std::to_string(preview.mapped_note_count) + " notes map, " +
                         std::to_string(preview.dropped_notes.size()) + " dropped, " +
                         std::to_string(preview.collision_count) + " collisions.";
    const auto* text = metadata_frame_text(flattened, message);
    metadata.lane_profile_preview_message = text ? text : "";
}

void populate_metadata_chart_choices(flattened_workspace_t& flattened,
                                     const wormedit::document_history_t& history) {
    auto& metadata = flattened.view.metadata;
    constexpr std::size_t batch_limit = WORM_UI_METADATA_BATCH_CHART_CAPACITY;
    const auto total = history.document.charts.size();
    const auto count = std::min(total, batch_limit);
    metadata.chart_choices_truncated = total > batch_limit;
    auto* arena = flattened.arena();
    auto choices = arena ? arena_array<WormUiMetadataChartChoice>(*arena, count)
                         : frame_span<WormUiMetadataChartChoice>{};
    for (std::size_t index = 0; index < count && choices.capacity() == count; ++index) {
        const auto& chart = history.document.charts[index];
        const auto* name = metadata_frame_text(flattened, chart.metadata.name);
        if (!name) {
            metadata.chart_choices_truncated = true;
            break;
        }
        (void)choices.push_back({.chart_id = chart.id.value, .name = name});
    }
    if (choices.capacity() != count) metadata.chart_choices_truncated = true;
    metadata.chart_choices = choices.data();
    metadata.chart_choice_count = choices.size();
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
    metadata_drop.discard_if_stale(history.document.id.value, history.document.revision,
        workspace.selected_chart ? workspace.selected_chart->value : 0);
    if (!flattened.view.metadata.visible)
        return;

    auto& metadata = flattened.view.metadata;
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
    populate_metadata_chart_resolution(flattened, history);
    populate_metadata_copy_preview(flattened, history, metadata_asset_runtime);
    populate_metadata_output_groups(flattened, history, workspace);
    populate_metadata_channel_map_lanes(flattened, history);
    populate_metadata_channel_map_preview(flattened, metadata_drop);
    populate_metadata_lane_profile(flattened, history, metadata_drop);
    populate_metadata_chart_choices(flattened, history);

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
