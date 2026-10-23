#include "sys/project/workspace_navigation.h"
#include "sys/resources/sound_usage_actions.h"

#include <wormedit/core/chart_editor.h>
#include <wormedit/core/interaction_dispatch.h>

#include <algorithm>
#include <utility>

namespace wormedit::sys {
namespace {

auto row_matches(const wormedit::ui::workspace_action_t& action, const sound_usage_row_t& row) -> bool {
    const auto& position = row.position;
    return action.sound_usage_chart_id.value == row.chart.value &&
           action.sound_usage_kind == row.kind &&
           action.sound_usage_scope == row.scope &&
           action.sound_usage_object_id.value == row.object.value &&
           action.sound_usage_channel_id.value == row.channel.value &&
           action.sound_usage_clip_id.value == row.clip.value &&
           action.sound_usage_referenced_resource_id.value == row.referenced_resource.value &&
           action.sound_usage_referenced_asset_id.value == row.referenced_asset.value &&
           action.sound_usage_position_valid == position.has_value() &&
           action.sound_usage_measure_id.value == (position ? position->measure.value : 0) &&
           action.sound_usage_row == (position ? position->row : 0);
}

auto rejection(std::string message) -> sound_usage_action_result_t {
    return {.accepted = false, .message = std::move(message)};
}

} // namespace

auto dispatch_sound_usage_action(const wormedit::ui::workspace_action_t& action,
                                 const wormedit::ui::sound_usage_request_t& request,
                                 document_history_t& history, workspace_controller_t& workspace,
                                 std::optional<editor_interaction_state_t>& interaction)
    -> sound_usage_action_result_t {
    if (action.kind != WORM_UI_WORKSPACE_ACTION_SOUND_USAGE_ACTIVATE ||
        workspace.active_tab != workspace_tab_t::audio_slices || !request.find_uses_open ||
        request.selected_project_id != history.document.id ||
        request.selected_resource_id != action.sound_usage_resource_id ||
        action.sound_usage_song_id.value != history.document.id.value ||
        action.sound_usage_document_revision != history.document.revision)
        return rejection("Sound usage results changed; refresh Find uses.");

    const auto usage =
        sound_usage_view_t::from(history.document, action.sound_usage_resource_id);
    if (!usage.valid || usage.asset.value != action.sound_usage_asset_id.value)
        return rejection("The selected sound is no longer available.");
    const auto found =
        std::ranges::find_if(usage.rows, [&](const auto& row) { return row_matches(action, row); });
    if (found == usage.rows.end())
        return rejection("The sound usage is no longer available.");

    if (!found->position) {
        auto next_workspace = workspace;
        if (!apply_workspace_action(next_workspace, history.document,
                                    workspace_select_chart_action_t{found->chart})
                 .is_accepted(diagnostic_acceptance_policy_t::require_empty))
            return rejection("The sound usage chart cannot be opened.");
        if (!apply_workspace_action(
                next_workspace, history.document,
                workspace_select_asset_action_t{.asset = usage.asset,
                                                .slot = wormedit::workspace_asset_slot_t::audio})
                 .is_accepted(diagnostic_acceptance_policy_t::require_empty))
            return rejection("The sound source is no longer available.");
        workspace = std::move(next_workspace);
        return {.accepted = true,
                .message = "Sound channel source selected.",
                .source_selection =
                    sound_usage_source_selection_t{usage.song.value, usage.resource.value,
                                                   found->channel.value, found->clip.value}};
    }

    auto navigation = workspace_navigation_t::from(history, workspace, interaction,
                                                   found->chart, *found->position);
    if (!navigation) return rejection("The sound usage cannot be opened in Compose.");
    std::move(*navigation).apply(workspace, interaction);
    return {.accepted = true, .message = "Moved to sound usage."};
}

} // namespace wormedit::sys
