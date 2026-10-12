#pragma once

#include "ui/frontend.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <wormedit/core/channel_map_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/lane_migration.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/workspace.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct metadata_drop_state_t {
    std::string music_path;
    WormUiMetadataDropTarget music_target{};
    std::string output_path;
    std::size_t output_group_index{};
    std::uint64_t project_id{};
    std::uint64_t music_revision{};
    std::uint64_t chart_id{};
    // Sealed channel-map preview from the last Preview action. Apply consumes exactly this proposal.
    std::optional<channel_map_proposal_t> channel_map_preview;
    // Sealed key-count preview from the last Preview action; Apply consumes exactly this proposal.
    std::optional<lane_migration_proposal_t> lane_profile_preview;

    void clear_paths();
    void clear_channel_map_preview();
    void clear_lane_profile_preview();
    void clear_music();
    void clear_output();
    void discard_if_stale(std::uint64_t current_project, std::uint64_t current_revision,
                          std::uint64_t current_chart_id);
    void publish(WormUiMetadataView& view) const;
    void handle_file_drop(const SDL_Event& event, SDL_Window* window_handle,
                          const workspace_controller_t& workspace,
                          const document_history_t& history,
                          const std::filesystem::path& asset_base, const notification_sink_t& notifications);
};

} // namespace wormedit::sys
