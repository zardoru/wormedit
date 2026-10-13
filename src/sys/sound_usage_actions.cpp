#include <wormedit/core/rational.h>
#include "sys/sound_usage_actions.h"

#include <wormedit/core/chart_editor.h>

#include <algorithm>
#include <array>
#include <span>
#include <utility>

namespace wormedit::sys {
namespace {

auto row_matches(const WormUiWorkspaceAction& action, const sound_usage_row_t& row) -> bool {
    const auto& position = row.position;
    return action.sound_usage_chart_id == row.chart.value &&
           action.sound_usage_kind == static_cast<int>(row.kind) &&
           action.sound_usage_scope == static_cast<int>(row.scope) &&
           action.sound_usage_object_id == row.object.value &&
           action.sound_usage_channel_id == row.channel.value &&
           action.sound_usage_clip_id == row.clip.value &&
           action.sound_usage_referenced_resource_id == row.referenced_resource.value &&
           action.sound_usage_referenced_asset_id == row.referenced_asset.value &&
           action.sound_usage_position_valid == position.has_value() &&
           action.sound_usage_measure_id == (position ? position->measure.value : 0) &&
           action.sound_usage_row == (position ? position->row : 0);
}

auto interaction_for_jump(const document_history_t& history, const chart_document_t& chart,
                          local_position_t position,
                          const std::optional<editor_interaction_state_t>& previous)
    -> std::optional<editor_interaction_state_t> {
    if (previous && previous->chart == chart.id)
        return previous;
    const auto built = build_measure_index(chart);
    const auto* index = std::get_if<measure_index_t>(&built);
    const auto row = index ? absolute_row(*index, position) : std::nullopt;
    const lane_layout_segment_t* active{};
    std::uint64_t active_row{};
    if (row) {
        for (const auto& segment : chart.lane_layouts) {
            const auto start = absolute_row(*index, segment.position);
            if (start && *start <= *row && (!active || *start >= active_row)) {
                active = &segment;
                active_row = *start;
            }
        }
    }
    if (!active || active->layout.lanes.empty())
        return std::nullopt;
    const auto setup = make_editor_interaction(
        history, chart.id, active->layout.lanes.front().id, position,
        previous ? std::span<const std::uint64_t>{previous->supported_snap_denominators}
                 : std::span<const std::uint64_t>{wormedit::default_snap_denominators},
        previous ? previous->active_snap_denominator : 16,
        previous ? previous->viewport_height_pixels : 1.0);
    return setup.state;
}

auto rejection(std::string message) -> sound_usage_action_result_t {
    return {.accepted = false, .message = std::move(message)};
}

} // namespace

auto dispatch_sound_usage_action(const WormUiWorkspaceAction& action,
                                 const WormUiSoundUsageRequest& request,
                                 document_history_t& history, workspace_controller_t& workspace,
                                 std::optional<editor_interaction_state_t>& interaction)
    -> sound_usage_action_result_t {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE ||
        workspace.active_tab != workspace_tab_t::audio_slices || !request.find_uses_open ||
        request.selected_project_id != history.document.id.value ||
        request.selected_resource_id != action.sound_usage_resource_id ||
        action.sound_usage_song_id != history.document.id.value ||
        action.sound_usage_document_revision != history.document.revision)
        return rejection("Sound usage results changed; refresh Find uses.");

    const auto usage =
        project_sound_usage(history.document, resource_id_t{action.sound_usage_resource_id});
    if (!usage.valid || usage.asset.value != action.sound_usage_asset_id)
        return rejection("The selected sound is no longer available.");
    const auto found =
        std::ranges::find_if(usage.rows, [&](const auto& row) { return row_matches(action, row); });
    if (found == usage.rows.end())
        return rejection("The sound usage is no longer available.");

    auto next_workspace = workspace;
    auto next_interaction = interaction;
    const auto revision = history.document.revision;
    if (!apply_workspace_action(next_workspace, history.document, revision,
                                workspace_select_chart_action_t{found->chart}))
        return rejection("The sound usage chart cannot be opened.");

    if (!found->position) {
        if (!apply_workspace_action(
                next_workspace, history.document, revision,
                workspace_select_asset_action_t{.asset = usage.asset,
                                                .slot = wormedit::workspace_asset_slot_t::audio}))
            return rejection("The sound source is no longer available.");
        workspace = std::move(next_workspace);
        return {.accepted = true,
                .message = "Sound channel source selected.",
                .source_selection =
                    sound_usage_source_selection_t{usage.song.value, usage.resource.value,
                                                   found->channel.value, found->clip.value}};
    }

    const auto chart =
        std::ranges::find(history.document.charts, found->chart, &chart_document_t::id);
    if (chart == history.document.charts.end())
        return rejection("The sound usage chart is unavailable.");
    auto candidate = interaction_for_jump(history, *chart, *found->position, next_interaction);
    if (!candidate ||
        !dispatch_editor_action(history, *candidate,
                                jump_edit_cursor_action_t{*found->position, revision})
             .accepted ||
        !apply_workspace_action(next_workspace, history.document, revision,
                                workspace_switch_tab_action_t{workspace_tab_t::compose}))
        return rejection("The sound usage cannot be opened in Compose.");

    workspace = std::move(next_workspace);
    interaction = std::move(candidate);
    return {.accepted = true, .message = "Moved to sound usage."};
}

} // namespace wormedit::sys
