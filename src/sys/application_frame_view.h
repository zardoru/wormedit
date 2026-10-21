#pragma once

#include "sys/compose_view.h"
#include "sys/metadata_asset_runtime.h"
#include "sys/metadata_drop_state.h"
#include "sys/player_preview_presenter.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/timing/timing_view.h"
#include "sys/workspace_frame_view.h"
#include "sys/workspace_view.h"

#include <wormedit/window.h>

#include <wormedit/core/recovery_controller.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/decoder.h>
#include "sys/chart_preview_state.h"
#include "sys/waveform_analysis_runtime.h"
#include <wormedit/audio/audition_session.h>
#include <wormedit/audio/preview_session.h>
#include "sys/project_audio_sources.h"
#endif

#include <filesystem>
#include <optional>
#include <string>

namespace wormedit::sys {

struct application_frame_projection_context_t {
    const workspace_view_t& model;
    document_history_t& history;
    workspace_controller_t& workspace;
    const native_project_recovery_controller_t& recovery_controller;
    bool recovery_scan_configured{};
    std::uint64_t project_session_generation{};
    const std::filesystem::path& asset_base;
    timing_grid_state_t& timing_grid;
    timing_scene_cache_t& timing_scene_cache;
    runtime::timing_edit_t& timing_edit;
    compose_scene_cache_t& compose_scene_cache;
    player_preview_presenter_t& player_preview_presenter;
    application_command_state_t& application_command_state;
    std::optional<editor_interaction_state_t>& compose_interaction_state;
    compose_bga_move_preview_input_t bga_move_preview{};
    const std::optional<proposal_t<audio_offset_drag_t>>& audio_offset_drag;
    const std::optional<workspace_tab_t>& audio_offset_drag_tab;
    metadata_drop_state_t& metadata_drop;
    runtime::metadata_asset_t& metadata_asset_runtime;
    WormWindow& window;
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
    visual::image_job_t& player_image_job;
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
    visual::video_frame_job_t& player_video_job;
#endif
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    runtime::waveform_analysis_t& waveform_runtime;
    project_audio_sources_t& audition_sources;
    std::uint64_t audition_source_generation{};
    audio::audition_session_t& audition_session;
    audio::preview_session_t& preview_session;
    runtime::chart_preview_t& preview_state;
    bool preview_output_available{};
#endif
};

} // namespace wormedit::sys
