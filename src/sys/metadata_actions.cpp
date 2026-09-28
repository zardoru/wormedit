#include "sys/metadata_actions.h"

#include "sys/application_edit_helpers.h"
#include "ui/frontend.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/resource_file_copy.h>
#include <wormedit/core/workspace.h>

#include <charconv>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {
namespace {

auto parse_unsigned(const char* text) -> std::optional<std::uint64_t> {
    if (!text || text[0] == '\0')
        return std::nullopt;
    const auto* end = text + std::strlen(text);
    std::uint64_t value{};
    const auto parsed = std::from_chars(text, end, value);
    return parsed.ec == std::errc{} && parsed.ptr == end ? std::optional{value} : std::nullopt;
}

auto failed(const document_command_result_t& result) -> std::string {
    return result.diagnostics.empty() ? std::string{"Metadata edit was not applied."}
                                      : result.diagnostics.front().message;
}

auto publish_metadata_edit(document_command_result_t result,
                           workspace_controller_t& workspace,
                           document_history_t& history) -> std::string {
    if (!result.applied) {
        return result.diagnostics.empty() ? std::string{"No metadata changes."} : failed(result);
    }
    const auto rebound = rebind_workspace(workspace, history);
    if (!rebound) {
        return rebound.diagnostics.empty()
                   ? std::string{"Metadata changed, but workspace rebinding failed."}
                   : rebound.diagnostics.front().message;
    }
    return "Metadata updated.";
}

auto dispatch_song_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                        workspace_controller_t& workspace) -> std::string {
    if (!action.metadata_title || !action.metadata_artist ||
        action.metadata_search_tag_count > WORM_UI_METADATA_SEARCH_TAG_CAPACITY ||
        (action.metadata_search_tag_count != 0 && !action.metadata_search_tags)) {
        return "Metadata edit payload is incomplete or exceeds the tag-count cap.";
    }
    std::vector<std::string> tags;
    tags.reserve(action.metadata_search_tag_count);
    for (std::size_t index = 0; index < action.metadata_search_tag_count; ++index) {
        if (!action.metadata_search_tags[index])
            return "Metadata edit contains an empty tag pointer.";
        tags.emplace_back(action.metadata_search_tags[index]);
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, action.metadata_document_revision,
                            set_song_metadata_action_t{action.metadata_title, action.metadata_artist,
                                                       std::move(tags)}),
        workspace, history);
}

auto dispatch_chart_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                         workspace_controller_t& workspace) -> std::string {
    if (action.metadata_chart_id == 0 || !action.metadata_chart_name ||
        !action.metadata_chart_author ||
        (action.metadata_has_chart_rating &&
         (!action.metadata_chart_rating_numerator_text ||
          !action.metadata_chart_rating_denominator_text))) {
        return "Chart metadata edit payload is incomplete.";
    }
    const auto rating = make_chart_rating_metadata_edit(
        chart_id_t{action.metadata_chart_id}, !action.metadata_has_chart_rating,
        action.metadata_chart_rating_numerator_text ? action.metadata_chart_rating_numerator_text
                                                    : "",
        action.metadata_chart_rating_denominator_text ? action.metadata_chart_rating_denominator_text
                                                      : "");
    if (!rating)
        return "Rating must use a signed 64-bit numerator and positive 64-bit denominator.";
    return publish_metadata_edit(
        apply_metadata_edit(
            history, action.metadata_document_revision,
            set_chart_metadata_action_t{chart_id_t{action.metadata_chart_id},
                                        action.metadata_chart_name, action.metadata_chart_author,
                                        rating->rating}),
        workspace, history);
}

auto dispatch_audio_offset_edit(const WormUiWorkspaceAction& action,
                                document_history_t& history,
                                workspace_controller_t& workspace,
                                bool timing_locked) -> std::string {
    if (action.metadata_chart_id == 0 || !action.metadata_audio_offset_numerator_text ||
        !action.metadata_audio_offset_denominator_text)
        return "Audio offset edit payload is incomplete.";
    const auto numerator = parse_timing_signed(action.metadata_audio_offset_numerator_text);
    const auto denominator = parse_unsigned(action.metadata_audio_offset_denominator_text);
    if (!numerator || !denominator || *denominator == 0) {
        return "Audio offset must use a signed 64-bit numerator and positive 64-bit denominator.";
    }
    return publish_metadata_edit(
        apply_audio_offset_edit(history, action.metadata_document_revision,
                               chart_id_t{action.metadata_chart_id}, {*numerator, *denominator},
                               timing_locked),
        workspace, history);
}

auto dispatch_format_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                          workspace_controller_t& workspace) -> std::string {
    if (action.metadata_format_family > static_cast<std::uint8_t>(format_family_t::quaver))
        return "Format metadata edit refers to an unknown format family.";
    const format_metadata_target_t target{
        action.metadata_format_chart_scoped
            ? std::optional<chart_id_t>{chart_id_t{action.metadata_chart_id}}
            : std::nullopt,
        static_cast<format_family_t>(action.metadata_format_family)};
    metadata_edit_action_t edit;
    if (action.metadata_edit == WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD) {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return "Format metadata replacement payload is incomplete.";
        edit = replace_format_metadata_field_action_t{target, action.metadata_format_index,
                                                      action.metadata_format_name,
                                                      action.metadata_format_value};
    } else if (action.metadata_edit == WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD) {
        edit = erase_format_metadata_field_action_t{target, action.metadata_format_index};
    } else {
        if (!action.metadata_format_name || !action.metadata_format_value)
            return "Format metadata insertion payload is incomplete.";
        edit = insert_format_metadata_field_action_t{target, action.metadata_format_index,
                                                     action.metadata_format_name,
                                                     action.metadata_format_value};
    }
    return publish_metadata_edit(
        apply_metadata_edit(history, action.metadata_document_revision, edit), workspace, history);
}

auto dispatch_metadata_edit(const WormUiWorkspaceAction& action, document_history_t& history,
                            workspace_controller_t& workspace,
                            bool timing_locked) -> std::string {
    switch (action.metadata_edit) {
    case WORM_UI_METADATA_EDIT_SONG:
        return dispatch_song_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_CHART:
        return dispatch_chart_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_AUDIO_OFFSET:
        return dispatch_audio_offset_edit(action, history, workspace, timing_locked);
    case WORM_UI_METADATA_EDIT_OUTPUT_PATH:
        if (!action.metadata_output_path ||
            std::strlen(action.metadata_output_path) > WORM_UI_METADATA_OUTPUT_PATH_BYTES)
            return "Output path edit payload is incomplete or too long.";
        return publish_metadata_edit(
            apply_metadata_edit(history, action.metadata_document_revision,
                                set_output_group_path_action_t{action.metadata_output_group_index,
                                                               action.metadata_output_path}),
            workspace, history);
    case WORM_UI_METADATA_EDIT_REPLACE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_ERASE_FORMAT_FIELD:
    case WORM_UI_METADATA_EDIT_INSERT_FORMAT_FIELD:
        return dispatch_format_edit(action, history, workspace);
    case WORM_UI_METADATA_EDIT_NONE:
        break;
    }
    return "Invalid metadata edit action.";
}

} // namespace

auto dispatch_metadata_action(
    const WormUiWorkspaceAction& action, document_history_t& history,
    workspace_controller_t& workspace, bool timing_locked,
    metadata_drop_state_t& metadata_drop, runtime::metadata_asset_t& metadata_asset_runtime,
    const std::filesystem::path& asset_base, task_scheduler_t& resource_copy_scheduler,
    bool resource_copy_busy, bool export_busy) -> std::optional<std::string> {
    if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) {
        auto status = dispatch_metadata_edit(action, history, workspace, timing_locked);
        if (action.metadata_edit == WORM_UI_METADATA_EDIT_OUTPUT_PATH)
            metadata_drop.clear_output();
        return status;
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
