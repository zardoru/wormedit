#include "sys/metadata_actions.h"

#include "sys/application_edit_helpers.h"
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
    return {severity == diagnostic_severity_t::error ? notification_severity_t::error
            : severity == diagnostic_severity_t::warning ? notification_severity_t::warning
                                                         : notification_severity_t::info,
            diagnostic.message};
}

auto parse_unsigned(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0')
        return std::nullopt;
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto publish_metadata_edit(document_command_result_t result,
                           workspace_controller_t& workspace,
                           document_history_t& history) -> metadata_edit_feedback_t {
    if (!result.applied) {
        if (!result.diagnostics.empty()) return diagnostic_feedback(result.diagnostics.front());
        return {notification_severity_t::info, "No metadata changes."};
    }
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        if (!rebound.diagnostics.empty()) return diagnostic_feedback(rebound.diagnostics.front());
        return {notification_severity_t::error, "Metadata changed, but workspace rebinding failed."};
    }
    return {notification_severity_t::success, "Metadata updated."};
}

auto dispatch_song_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                        workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (!action.metadata_title || !action.metadata_artist ||
        action.metadata_search_tag_count > WORM_UI_METADATA_SEARCH_TAG_CAPACITY ||
        (action.metadata_search_tag_count != 0 && !action.metadata_search_tags)) {
        return {notification_severity_t::warning, "Metadata edit payload is incomplete or exceeds the tag-count cap."};
    }
    std::vector<std::string> tags;
    tags.reserve(action.metadata_search_tag_count);
    for (std::size_t index = 0; index < action.metadata_search_tag_count; ++index) {
        if (!action.metadata_search_tags[index])
            return {notification_severity_t::warning, "Metadata edit contains an empty tag pointer."};
        tags.emplace_back(action.metadata_search_tags[index]);
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, action.metadata_document_revision,
                            set_song_metadata_action_t{action.metadata_title, action.metadata_artist,
                                                       std::move(tags)}),
        workspace, history);
}

auto dispatch_chart_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (action.metadata_chart_id == 0 || !action.metadata_chart_name ||
        !action.metadata_chart_author ||
        (action.metadata_has_chart_rating &&
         (!action.metadata_chart_rating_numerator_text ||
          !action.metadata_chart_rating_denominator_text))) {
        return {notification_severity_t::warning, "Chart metadata edit payload is incomplete."};
    }
    const auto rating = make_chart_rating_metadata_edit(
        chart_id_t{action.metadata_chart_id}, !action.metadata_has_chart_rating,
        action.metadata_chart_rating_numerator_text ? action.metadata_chart_rating_numerator_text
                                                    : "",
        action.metadata_chart_rating_denominator_text ? action.metadata_chart_rating_denominator_text
                                                      : "");
    if (!rating)
        return {notification_severity_t::warning, "Rating must use a signed 64-bit numerator and positive 64-bit denominator."};
    return publish_metadata_edit(
        apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{chart_id_t{action.metadata_chart_id},
                                        action.metadata_chart_name, action.metadata_chart_author,
                                        rating->rating}),
        workspace, history);
}

auto dispatch_batch_chart_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                               workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    const auto count = action.metadata_batch_chart_count;
    if (count == 0 || count > WORM_UI_METADATA_BATCH_CHART_CAPACITY ||
        !action.metadata_batch_chart_ids ||
        (action.metadata_batch_edit_author && !action.metadata_chart_author) ||
        (action.metadata_batch_edit_rating && action.metadata_has_chart_rating &&
         (!action.metadata_chart_rating_numerator_text ||
          !action.metadata_chart_rating_denominator_text)))
        return {notification_severity_t::warning, "Batch metadata edit payload is incomplete."};
    batch_chart_metadata_action_t batch;
    for (std::size_t index = 0; index < count; ++index)
        batch.charts.push_back(chart_id_t{action.metadata_batch_chart_ids[index]});
    if (action.metadata_batch_edit_author) batch.author = action.metadata_chart_author;
    batch.edit_rating = action.metadata_batch_edit_rating;
    if (batch.edit_rating) {
        const auto rating = make_chart_rating_metadata_edit(
            batch.charts.front(), !action.metadata_has_chart_rating,
            action.metadata_chart_rating_numerator_text ? action.metadata_chart_rating_numerator_text : "",
            action.metadata_chart_rating_denominator_text ? action.metadata_chart_rating_denominator_text : "");
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

auto dispatch_audio_offset_edit(const WormUiWorkspaceAction& action,
                                document_history_t& history,
                                workspace_controller_t& workspace,
                                bool timing_locked) -> metadata_edit_feedback_t {
    if (action.metadata_chart_id == 0 || !action.metadata_audio_offset_numerator_text ||
        !action.metadata_audio_offset_denominator_text)
        return {notification_severity_t::warning, "Audio offset edit payload is incomplete."};
    const auto numerator = parse_timing_signed(action.metadata_audio_offset_numerator_text);
    const auto denominator = parse_unsigned(action.metadata_audio_offset_denominator_text);
    if (!numerator || !denominator || *denominator == 0) {
        return {notification_severity_t::warning, "Audio offset must use a signed 64-bit numerator and positive 64-bit denominator."};
    }
    return publish_metadata_edit(
        apply_audio_offset_edit(history, action.metadata_document_revision,
                               chart_id_t{action.metadata_chart_id}, {*numerator, *denominator},
                               timing_locked),
        workspace, history);
}

auto dispatch_format_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                          workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (action.metadata_format_family > static_cast<std::uint8_t>(format_family_t::quaver))
        return {notification_severity_t::warning, "Format metadata edit refers to an unknown format family."};
    const format_metadata_target_t target{
        action.metadata_format_chart_scoped
            ? std::optional<chart_id_t>{chart_id_t{action.metadata_chart_id}}
            : std::nullopt,
        static_cast<format_family_t>(action.metadata_format_family)};
    metadata_edit_action_t edit;
    if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return {notification_severity_t::warning, "Format metadata replacement payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = action.metadata_format_index,
            .name = action.metadata_format_name,
            .value = action.metadata_format_value,
            .mode = wormedit::format_metadata_field_mode_t::replace};
    } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
        edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
    } else {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return {notification_severity_t::warning, "Format metadata insertion payload is incomplete."};
        edit = edit_format_metadata_field_action_t{
            .target = target,
            .index = action.metadata_format_index,
            .name = action.metadata_format_name,
            .value = action.metadata_format_value,
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

auto dispatch_redivision_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                              workspace_controller_t& workspace) -> metadata_edit_feedback_t {
    if (action.metadata_chart_id == 0 || action.metadata_redivision_resolution == 0)
        return {notification_severity_t::warning, "Resolution change payload is incomplete."};
    auto redivision = apply_redivision_command(history, action.metadata_document_revision,
                                               chart_id_t{action.metadata_chart_id},
                                               action.metadata_redivision_resolution);
    if (!redivision.command.applied && !redivision.command.diagnostics.empty())
        return {notification_severity_t::error,
                redivision_rejection_message(redivision.command.diagnostics.front())};
    const auto moved = redivision.moves.size();
    auto feedback = publish_metadata_edit(std::move(redivision.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Redivided " + std::to_string(moved) + " positions to " +
                           std::to_string(action.metadata_redivision_resolution) +
                           " divisions per measure.";
    return feedback;
}

auto optional_channel_text(const char* text) -> std::optional<std::string> {
    if (!text || text[0] == '\0') return std::nullopt;
    return std::string{text};
}

// Lane ids and four texts per lane; an empty text means the channel is unset.
auto channel_map_replacements(const WormUiWorkspaceAction& action)
    -> std::optional<std::vector<channel_map_replacement_t>> {
    const auto count = action.metadata_channel_map_lane_count;
    if (count > WORM_UI_METADATA_CHANNEL_MAP_LANE_CAPACITY ||
        (count != 0 && (!action.metadata_channel_map_lane_ids || !action.metadata_channel_map_texts)))
        return std::nullopt;
    std::vector<channel_map_replacement_t> replacements;
    replacements.reserve(count);
    for (std::size_t lane = 0; lane < count; ++lane) {
        const auto* texts = action.metadata_channel_map_texts + lane * 4;
        if (!texts[0] || !texts[1] || !texts[2] || !texts[3]) return std::nullopt;
        replacements.push_back({lane_id_t{action.metadata_channel_map_lane_ids[lane]},
                                {optional_channel_text(texts[0]), optional_channel_text(texts[1]),
                                 optional_channel_text(texts[2]), optional_channel_text(texts[3])}});
    }
    return replacements;
}

auto dispatch_channel_map_preview(const WormUiWorkspaceAction& action, const document_history_t& history,
                                  metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto replacements = channel_map_replacements(action);
    if (action.metadata_chart_id == 0 || !replacements)
        return {notification_severity_t::warning, "Channel-map preview payload is incomplete."};
    auto preview = propose_bms_channel_map_edit(
        history.document, action.metadata_document_revision, chart_id_t{action.metadata_chart_id},
        object_id_t{action.metadata_channel_map_layout_segment_id}, *replacements);
    auto feedback = metadata_edit_feedback_t{notification_severity_t::info, preview.message};
    if (!preview) {
        feedback.severity = notification_severity_t::error;
    } else if (preview.conflict_count != 0) {
        feedback = {notification_severity_t::warning, "Channel-map preview has " +
                    std::to_string(preview.conflict_count) + " conflicts; Apply is disabled."};
    } else {
        feedback.message = "Channel-map preview: " + std::to_string(preview.affected_note_count) +
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
    if (!preview || preview.conflict_count != 0)
        return {notification_severity_t::warning, "The channel-map preview is not applicable."};
    auto result = apply_bms_channel_map_edit(history, preview);
    metadata_drop.clear_channel_map_preview();
    if (!result) return {notification_severity_t::error, result.message};
    auto feedback = publish_metadata_edit(std::move(result.command), workspace, history);
    if (feedback.severity == notification_severity_t::success)
        feedback.message = "Channel maps updated.";
    return feedback;
}

auto lane_profile_chart(const song_document_t& song, std::uint64_t id) -> const chart_document_t* {
    const auto found = std::ranges::find(song.charts, id, [](const chart_document_t& chart) {
        return chart.id.value;
    });
    return found == song.charts.end() ? nullptr : &*found;
}

// Previews the chosen family profile as a sealed proposal. The index must name a family choice:
// Custom and stale indices never reach the core.
auto dispatch_lane_profile_preview(const WormUiWorkspaceAction& action, const document_history_t& history,
                                   metadata_drop_state_t& metadata_drop) -> metadata_edit_feedback_t {
    const auto* chart = lane_profile_chart(history.document, action.metadata_chart_id);
    if (!chart) return {notification_severity_t::warning, "Choose a chart before previewing its key count."};
    const auto targets = lane_profile_family_targets(*chart);
    if (action.metadata_lane_profile_index >= targets.size())
        return {notification_severity_t::warning,
                "Choose a built-in key-count profile of this chart's family to preview."};
    auto proposal = propose_lane_migration(history.document, action.metadata_document_revision,
                                           chart->id, targets[action.metadata_lane_profile_index]);
    if (proposal.issue != lane_migration_issue_t::none) {
        metadata_drop.clear_lane_profile_preview();
        return {notification_severity_t::error, proposal.message};
    }
    const auto message = "Key-count preview: " + std::to_string(proposal.mapped_note_count) + " notes map, " +
                         std::to_string(proposal.dropped_notes.size()) + " dropped, " +
                         std::to_string(proposal.collision_count) + " collisions.";
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
    auto result = apply_lane_migration(history, proposal, true);
    if (!result) return {notification_severity_t::error, result.message};
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

auto dispatch_metadata_edit(const WormUiWorkspaceAction& action, document_history_t& history,
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
            std::strlen(action.metadata_output_path) > WORM_UI_METADATA_OUTPUT_PATH_BYTES)
            return {notification_severity_t::warning, "Output path edit payload is incomplete or too long."};
        return publish_metadata_edit(
            apply_metadata_edit(history, action.metadata_document_revision,
                                set_output_group_path_action_t{action.metadata_output_group_index,
                                                               action.metadata_output_path}),
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

auto dispatch_metadata_action(
    const WormUiWorkspaceAction& action, document_history_t& history,
    workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy,
    std::optional<notification_sink_t> notifications) -> std::optional<std::string> {
    if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
        auto feedback = dispatch_metadata_edit(action, history, workspace, timing_locked, metadata_drop);
        if (notifications) notifications->post(feedback.severity, notification_source_t::edit, feedback.message);
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
