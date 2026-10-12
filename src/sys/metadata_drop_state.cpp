#include "sys/metadata_drop_state.h"

#include <wormedit/core/metadata_editor.h>

#include <string_view>

namespace wormedit::sys {

void metadata_drop_state_t::clear_paths() {
    music_path.clear();
    output_path.clear();
    music_target = {};
}

void metadata_drop_state_t::clear_music() {
    music_path.clear();
    music_target = {};
}

void metadata_drop_state_t::clear_output() { output_path.clear(); }

void metadata_drop_state_t::clear_channel_map_preview() {
    channel_map_preview.reset();
}

void metadata_drop_state_t::clear_lane_profile_preview() {
    lane_profile_preview.reset();
}

void metadata_drop_state_t::discard_if_stale(std::uint64_t current_project, std::uint64_t current_revision,
                                             std::uint64_t current_chart_id) {
    if ((!music_path.empty() || !output_path.empty()) &&
        (project_id != current_project || music_revision != current_revision || chart_id != current_chart_id))
        clear_paths();
    if (channel_map_preview && (channel_map_preview->song.value != current_project ||
                                channel_map_preview->revision != current_revision ||
                                channel_map_preview->chart.value != current_chart_id))
        clear_channel_map_preview();
    if (lane_profile_preview && (lane_profile_preview->song.value != current_project ||
                                 lane_profile_preview->revision != current_revision ||
                                 lane_profile_preview->chart.value != current_chart_id))
        clear_lane_profile_preview();
}

void metadata_drop_state_t::publish(WormUiMetadataView& view) const {
    view.dropped_music_path = music_path.c_str();
    view.dropped_music_target = music_target;
    view.dropped_output_relative_path = output_path.c_str();
    view.dropped_output_group_index = output_group_index;
}

void metadata_drop_state_t::handle_file_drop(
    const SDL_Event& event, SDL_Window* window_handle,
    const workspace_controller_t& workspace, const document_history_t& history,
    const std::filesystem::path& asset_base, const notification_sink_t& notifications) {
    if (event.type != SDL_EVENT_DROP_FILE || !event.drop.data ||
        (event.drop.windowID != 0 &&
         event.drop.windowID != SDL_GetWindowID(window_handle)) ||
        workspace.active_tab != workspace_tab_t::metadata)
        return;

    const std::string_view dropped{event.drop.data};
    if (dropped.size() > WORM_UI_COMPOSE_IMPORT_PATH_BYTES ||
        !std::filesystem::path{dropped}.is_absolute()) {
        clear_paths();
        notifications.post(notification_severity_t::warning, notification_source_t::import_,
                           "Drop an absolute local file path within the import limit.");
        return;
    }

    const auto target = worm_ui_frontend_metadata_drop_target(
        event.drop.windowID, event.drop.x, event.drop.y);
    clear_paths();
    if (target.destination == WORM_UI_METADATA_DROP_OUTPUT_PATH) {
        const auto relative = output_path_from_drop(asset_base, std::filesystem::path{dropped});
        if (relative) {
            output_path = relative->generic_string();
            output_group_index = target.output_group_index;
        } else {
            notifications.post(notification_severity_t::warning, notification_source_t::import_,
                "Drop an existing output file or directory inside the saved "
                "project; directories need extensionless names.");
        }
    } else {
        music_path.assign(dropped);
        music_target = target;
    }
    project_id = history.document.id.value;
    music_revision = history.document.revision;
    chart_id = workspace.selected_chart ? workspace.selected_chart->value : 0;
}

} // namespace wormedit::sys
