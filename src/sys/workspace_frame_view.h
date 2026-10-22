#pragma once

#include "sys/audio/player_preview_presenter.h"
#include "sys/workspace_view.h"

#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/player_preview.h>
#include <wormedit/core/workspace.h>

#include <cstdint>
#include <filesystem>
#include <optional>

#if defined(WORMEDIT_HAS_IMAGE_DECODER)
#include <wormedit/visual/image_job.h>
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
#include <wormedit/visual/video_job.h>
#endif

namespace wormedit::sys {

struct workspace_frame_transport_t {
    bool available{};
    bool playing{};
    bool paused{};
    std::uint64_t frame{};
    std::uint64_t end_frame{};
    std::uint32_t sample_rate{44'100};
};

struct workspace_frame_view_args_t {
    const document_history_t& history;
    const workspace_view_t& workspace;
    std::optional<chart_id_t> selected_chart;
    bool timing_locked{};
    const std::optional<proposal_t<audio_offset_drag_t>>& audio_offset_drag;
    workspace_frame_transport_t transport;
    player_preview_presenter_t& player_preview_presenter;
    bool player_preview_visible{};
    player_preview_skin_t player_preview_skin{};
    const std::filesystem::path& asset_base;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_job_t& player_image_job;
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_job_t& player_video_job;
#endif
};

void populate_workspace_frame_view(frontend_workspace_view_t& flattened,
                                   const workspace_frame_view_args_t& args);

} // namespace wormedit::sys
