#include "sys/metadata/metadata_commands.h"
#include "sys/commands/document_edit_feedback.h"
#include "sys/metadata/metadata_actions.h"
#include "sys/application_edit_inputs.h"
#include "wormedit/core/numeric_text.h"

#include "ui/frontend.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/channel_map_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/lane_migration.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/workspace.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {
namespace {

struct metadata_edit_feedback_t {
    notification_severity_t severity;
    std::string message;
};

auto diagnostic_feedback(const diagnostic_t& diagnostic) -> metadata_edit_feedback_t {
    const auto severity = diagnostic.severity;
    return {to_notification_severity(severity, notification_severity_t::info), diagnostic.message};
}

auto publish_metadata_edit(document_command_result_t result,
                           workspace_controller_t& workspace,
                           document_history_t& history) -> metadata_edit_feedback_t {
    if (!result.applied) {
        if (!result.diagnostics.empty()) return diagnostic_feedback(result.diagnostics.front());
        return {notification_severity_t::info, "No metadata changes."};
    }
    auto feedback = rebind_document_edit(
        workspace, history,
        {.success = "Metadata updated.",
         .rebind_failure = "Metadata changed, but workspace rebinding failed.",
         .unknown_severity = notification_severity_t::info,
         .source = notification_source_t::edit});
    return {feedback.severity, std::string{feedback.message_text()}};
}

auto dispatch_song_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                        workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!action.metadata_title || !action.metadata_artist ||
        action.metadata_search_tags.size() > wormedit::default_metadata_edit_limits.maximum_search_tags) {
        return {notification_severity_t::warning, "Metadata edit payload is incomplete or exceeds the tag-count cap."};
    }
    std::vector<std::string> tags;
    tags.reserve(action.metadata_search_tags.size());
    for (std::size_t index = 0; index < action.metadata_search_tags.size(); ++index) {
        if (!action.metadata_search_tags[index])
            return {notification_severity_t::warning, "Metadata edit contains an empty tag pointer."};
        tags.emplace_back(*action.metadata_search_tags[index]);
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, action.metadata_document_revision,
                            set_song_metadata_action_t{*action.metadata_title, *action.metadata_artist,
                                                       std::move(tags)}),
        workspace, history);
}

auto dispatch_chart_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                         workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!action.metadata_chart_id || !action.metadata_chart_name ||
        !action.metadata_chart_author ||
        (action.metadata_has_chart_rating &&
         (!action.metadata_chart_rating_numerator_text ||
          !action.metadata_chart_rating_denominator_text))) {
        return {notification_severity_t::warning, "Chart metadata edit payload is incomplete."};
    }
    const auto rating = wormedit::chart_rating_edit_t::from(
        action.metadata_chart_id, !action.metadata_has_chart_rating,
        action.metadata_chart_rating_numerator_text ? *action.metadata_chart_rating_numerator_text
                                                    : "",
        action.metadata_chart_rating_denominator_text
            ? *action.metadata_chart_rating_denominator_text
            : "");
    if (!rating)
        return {notification_severity_t::warning, "Rating must use a signed 64-bit numerator and positive 64-bit denominator."};
    return publish_metadata_edit(
        apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{action.metadata_chart_id,
                                        *action.metadata_chart_name, *action.metadata_chart_author,
                                        rating->rating}),
        workspace, history);
}

auto dispatch_batch_chart_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                               workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    const auto count = action.metadata_batch_chart_ids.size();
    if (count == 0 || count > wormedit::maximum_batch_chart_metadata_charts ||
        (action.metadata_batch_edit_author && !action.metadata_chart_author) ||
        (action.metadata_batch_edit_rating && action.metadata_has_chart_rating &&
         (!action.metadata_chart_rating_numerator_text ||
          !action.metadata_chart_rating_denominator_text)))
        return {notification_severity_t::warning, "Batch metadata edit payload is incomplete."};
    batch_chart_metadata_action_t batch;
    batch.charts = action.metadata_batch_chart_ids;
    if (action.metadata_batch_edit_author) batch.author = *action.metadata_chart_author;
    batch.edit_rating = action.metadata_batch_edit_rating;
    if (batch.edit_rating) {
        const auto rating = wormedit::chart_rating_edit_t::from(
            batch.charts.front(), !action.metadata_has_chart_rating,
            action.metadata_chart_rating_numerator_text
                ? *action.metadata_chart_rating_numerator_text
                : "",
            action.metadata_chart_rating_denominator_text
                ? *action.metadata_chart_rating_denominator_text
                : "");
        if (!rating)
            return {notification_severity_t::warning, "Rating must use a signed 64-bit numerator and positive 64-bit denominator."};
        batch.rating = rating->rating;
    }
    auto feedback = publish_metadata_edit(
        apply_batch_chart_metadata_edit(history, action.metadata_document_revision, batch),
        workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Metadata updated for " + std::to_string(count) + " difficulties.";
    return feedback;
}

auto dispatch_audio_offset_edit(const wormedit::ui::workspace_action_t& action,
                                document_history_t& history,
                                workspace_controller_t& workspace,
                                bool timing_locked) -> metadata_edit_feedback_t {
    if (!action.metadata_chart_id || !action.metadata_audio_offset_numerator_text ||
        !action.metadata_audio_offset_denominator_text)
        return {notification_severity_t::warning, "Audio offset edit payload is incomplete."};
    std::int64_t numerator{};
    std::uint64_t denominator{};
    if (!parse_integer(*action.metadata_audio_offset_numerator_text, numerator) ||
        !parse_integer(*action.metadata_audio_offset_denominator_text, denominator) || denominator == 0) {
        return {notification_severity_t::warning, "Audio offset must use a signed 64-bit numerator and positive 64-bit denominator."};
    }
    return publish_metadata_edit(
        apply_audio_offset_edit(history, action.metadata_document_revision,
                               action.metadata_chart_id, {numerator, denominator},
                               timing_locked),
        workspace, history);
}

auto dispatch_format_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                          workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (action.metadata_format_family < format_family_t::native_project ||
        action.metadata_format_family > format_family_t::quaver)
        return {notification_severity_t::warning, "Format metadata edit refers to an unknown format family."};
    const format_metadata_target_t target{
        action.metadata_format_chart_scoped
            ? std::optional<chart_id_t>{action.metadata_chart_id}
            : std::nullopt,
        action.metadata_format_family};
    metadata_edit_action_t edit;
    if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return {notification_severity_t::warning, "Format metadata replacement payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = action.metadata_format_index,
            .name = *action.metadata_format_name,
            .value = *action.metadata_format_value,
            .mode = wormedit::format_metadata_field_mode_t::replace};
    } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
        edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
    } else {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return {notification_severity_t::warning, "Format metadata insertion payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = action.metadata_format_index,
            .name = *action.metadata_format_name,
            .value = *action.metadata_format_value,
            .mode = wormedit::format_metadata_field_mode_t::insert};
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, action.metadata_document_revision, edit), workspace, history);
}

auto redivision_rejection_message(const diagnostic_t& diagnostic) -> std::string {
    auto message = "Resolution change rejected: " + diagnostic.message;
    if (diagnostic.object) message += " (object " + std::to_string(diagnostic.object->value) + ")";
    return message;
}

auto dispatch_redivision_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                              workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!action.metadata_chart_id || action.metadata_redivision_resolution == 0)
        return {notification_severity_t::warning, "Resolution change payload is incomplete."};
    auto redivision = apply_redivision_command(history, action.metadata_document_revision,
                                               action.metadata_chart_id,
                                               action.metadata_redivision_resolution);
    if (!redivision.command.applied && !redivision.command.diagnostics.empty())
        return {notification_severity_t::error,
                redivision_rejection_message(redivision.command.diagnostics.front())};
    const auto moved = redivision.payload.moves.size();
    auto feedback = publish_metadata_edit(std::move(redivision.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Redivided " + std::to_string(moved) + " positions to " +
                           std::to_string(action.metadata_redivision_resolution) +
                           " divisions per measure.";
    return feedback;
}

auto optional_channel_text(const std::optional<std::string>& text) -> std::optional<std::string> {
    if (!text || text->empty()) return std::nullopt;
    return *text;
}

// Lane ids and four texts per lane; an empty text means the channel is unset.
auto channel_map_replacements(const wormedit::ui::workspace_action_t& action)
    -> std::optional<std::vector<channel_map_replacement_t>> {
    const auto count = action.metadata_channel_map_lanes.size();
    if (count > wormedit::ui::metadata_channel_map_lane_limit)
        return std::nullopt;
    std::vector<channel_map_replacement_t> replacements;
    replacements.reserve(count);
    for (const auto& lane : action.metadata_channel_map_lanes) {
        if (!lane.visible || !lane.invisible || !lane.long_note || !lane.mine) return std::nullopt;
        replacements.push_back({.lane = lane.lane,
            .channels = {optional_channel_text(lane.visible), optional_channel_text(lane.invisible),
                         optional_channel_text(lane.long_note), optional_channel_text(lane.mine)}});
    }
    return replacements;
}

auto dispatch_channel_map_preview(const wormedit::ui::workspace_action_t& action, const document_history_t& history,
                                  metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto replacements = channel_map_replacements(action);
    if (!action.metadata_chart_id || !replacements)
        return {notification_severity_t::warning, "Channel-map preview payload is incomplete."};
    auto preview = channel_map_proposal_t::propose(
        history.document, action.metadata_document_revision, action.metadata_chart_id,
        action.metadata_channel_map_layout_segment_id, *replacements);
    const auto view = channel_map_preview_view_t::from(preview);
    auto feedback = metadata_edit_feedback_t{notification_severity_t::info,
        view.diagnostic ? view.diagnostic->message : std::string{}};
    if (!view.proposal) {
        feedback.severity = notification_severity_t::error;
    } else if (view.report->conflict_count != 0) {
        feedback = {notification_severity_t::warning, "Channel-map preview has " +
                    std::to_string(view.report->conflict_count) + " conflicts; Apply is disabled."};
    } else {
        feedback.message = "Channel-map preview: " + std::to_string(view.report->affected_note_count) +
                           " notes change channel.";
    }
    metadata_drop.channel_map_preview = std::move(preview);
    return feedback;
}

auto dispatch_channel_map_apply(document_history_t& history, workspace_controller_t& workspace,
                                metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    if (!metadata_drop.channel_map_preview)
        return {notification_severity_t::warning, "Preview the channel map before applying it."};
    const auto& preview = *metadata_drop.channel_map_preview;
    const auto view = channel_map_preview_view_t::from(preview);
    if (!view.proposal || view.report->conflict_count != 0)
        return {notification_severity_t::warning, "The channel-map preview is not applicable."};
    auto result = apply_proposal(history, *view.proposal);
    metadata_drop.clear_channel_map_preview();
    if (!result) return {notification_severity_t::error, result.payload.message};
    auto feedback = publish_metadata_edit(std::move(result.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Channel maps updated.";
    return feedback;
}

// Previews the chosen family profile as a sealed proposal. The index must name a family choice:
// Custom and stale indices never reach the core.
auto dispatch_lane_profile_preview(const wormedit::ui::workspace_action_t& action, const document_history_t& history,
                                   metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto* chart = history.document.find_chart(action.metadata_chart_id);
    if (!chart) return {notification_severity_t::warning, "Choose a chart before previewing its key count."};
    const auto targets = lane_profile_family_targets(*chart);
    if (action.metadata_lane_profile_index >= targets.size())
        return {notification_severity_t::warning,
                "Choose a built-in key-count profile of this chart's family to preview."};
    auto proposal = lane_migration_proposal_t::propose(history.document, action.metadata_document_revision,
                                           chart->id, targets[action.metadata_lane_profile_index]);
    if (!proposal.value) {
        metadata_drop.clear_lane_profile_preview();
        return {notification_severity_t::error, proposal.diagnostics.empty() ? std::string{} : proposal.diagnostics.front().message};
    }
    const auto& report = proposal.value->payload.report;
    const auto message = "Key-count preview: " + std::to_string(report.mapped_note_count) + " notes map, " +
                         std::to_string(report.dropped_notes.size()) + " dropped, " +
                         std::to_string(report.collision_count) + " collisions.";
    metadata_drop.lane_profile_preview = std::move(proposal);
    return {notification_severity_t::info, message};
}

// Apply consumes the sealed preview. The button press is the confirmation for any dropped notes.
auto dispatch_lane_profile_apply(document_history_t& history, workspace_controller_t& workspace,
                                 metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    if (!metadata_drop.lane_profile_preview)
        return {notification_severity_t::warning, "Preview the key-count change before applying it."};
    const auto proposal = std::move(*metadata_drop.lane_profile_preview);
    metadata_drop.clear_lane_profile_preview();
    if (!proposal.value)
        return {notification_severity_t::error, "Lane migration preview data was changed after proposal."};
    auto result = apply_proposal(history, *proposal.value, true);
    if (!result) return {notification_severity_t::error, result.payload.message};
    auto feedback = publish_metadata_edit(std::move(result.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Key-count profile changed.";
    return feedback;
}

auto lane_profile_family_targets_impl(const chart_document_t& chart) -> std::vector<lane_input_profile_t> {
    std::vector<lane_input_profile_t> targets;
    if (chart.lane_layouts.empty()) return targets;
    const auto& current_id = chart.lane_layouts.front().layout.profile_id;
    const auto catalog = built_in_bms_lane_profiles();
    const auto current = std::ranges::find_if(catalog, [&](const bms_lane_profile_descriptor_t& item) {
        return item.profile.layout.profile_id == current_id;
    });
    if (current != catalog.end()) {
        for (const auto& item : catalog)
            if (item.family == current->family) targets.push_back(item.profile);
        return targets;
    }
    // osu!mania charts: the 1-18 key factory is the family's supported counts.
    for (std::uint32_t keys = 1; keys <= 18; ++keys) {
        auto profile = make_osu_mania_profile(keys);
        if (profile) targets.push_back(*profile);
    }
    return targets;
}

auto dispatch_metadata_edit(const wormedit::ui::workspace_action_t& action, document_history_t& history,
                            workspace_controller_t& workspace, bool timing_locked,
                            metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    switch (action.metadata_edit) {
    case WORM_UI_METADATA_EDIT_SONG:
        return dispatch_song_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_CHART:
        return dispatch_chart_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_BATCH_CHART:
        return dispatch_batch_chart_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_AUDIO_OFFSET:
        return dispatch_audio_offset_edit(action, history, workspace, timing_locked);
    case WORM_UI_METADATA_EDIT_OUTPUT_PATH:
        if (!action.metadata_output_path ||
            action.metadata_output_path->size() > wormedit::default_metadata_edit_limits.maximum_output_path_bytes)
            return {notification_severity_t::warning, "Output path edit payload is incomplete or too long."};
        return publish_metadata_edit(
            apply_metadata_edit(history, action.metadata_document_revision,
                                set_output_group_path_action_t{action.metadata_output_group_index,
                                                               *action.metadata_output_path}),
            workspace, history);
    case WORM_UI_METADATA_EDIT_REDIVIDE_CHART:
        return dispatch_redivision_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_PREVIEW_CHANNEL_MAP:
        return dispatch_channel_map_preview(action, history, metadata_drop);
    case WORM_UI_METADATA_EDIT_APPLY_CHANNEL_MAP:
        return dispatch_channel_map_apply(history, workspace, metadata_drop);
    case WORM_UI_METADATA_EDIT_PREVIEW_LANE_PROFILE:
        return dispatch_lane_profile_preview(action, history, metadata_drop);
    case WORM_UI_METADATA_EDIT_APPLY_LANE_PROFILE:
        return dispatch_lane_profile_apply(history, workspace, metadata_drop);
    case WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD:
        return dispatch_format_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_NONE:
        break;
    }
    return {notification_severity_t::warning, "Invalid metadata edit action."};
}

} // namespace

auto lane_profile_family_targets(const chart_document_t& chart) -> std::vector<lane_input_profile_t> {
    return lane_profile_family_targets_impl(chart);
}

auto dispatch_metadata_action(const wormedit::ui::workspace_action_t& action,
                              const metadata_action_inputs_t& environment, bool timing_locked,
                              bool resource_copy_busy, bool export_busy, bool post_notifications)
    -> std::optional<std::string> {
    auto& history = environment.document.history;
    auto& workspace = environment.document.workspace;
    auto& metadata_drop = environment.import.drop;
    auto& metadata_asset_runtime = environment.import.runtime;
    auto& asset_base = environment.asset_base;
    auto& resource_copy_scheduler = environment.import.scheduler;
    const notification_sink_t notifications =
        post_notifications ? environment.document.notifications : notification_sink_t::muted();

    if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
        auto feedback = dispatch_metadata_edit(action, history, workspace, timing_locked, metadata_drop);
        notifications.post(feedback.severity, notification_source_t::edit, feedback.message);
        if (action.metadata_edit == WORM_UI_METADATA_EDIT_OUTPUT_PATH)
            metadata_drop.clear_output();
        return std::move(feedback.message);
    }
    if (action.metadata_import_copy_project || action.metadata_import_copy_export) {
        auto status = metadata_asset_runtime.start_import_copy(
            action, history, workspace, asset_base, resource_copy_scheduler, resource_copy_busy,
            export_busy);
        if (metadata_asset_runtime.import_pending())
            metadata_drop.clear_music();
        return status;
    }
    if (action.metadata_inspect_copy)
        return metadata_asset_runtime.start_copy_preview(action, history, workspace, asset_base);
    return std::nullopt;
}

} // namespace wormedit::sys
