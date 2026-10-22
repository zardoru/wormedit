#pragma once

#include "sys/compose_view.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/waveform.h>
#include <wormedit/audio/aligned_waveform.h>

namespace wormedit::sys {

// Populate the audio-slices and metadata views from the current waveform job result.
void populate_audio_waveform_views(
    frontend_workspace_view_t& flattened,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    int window_width, const wormedit::ui::audio_waveform_viewport_request_t* viewport_request = nullptr);

// Populate the compose waveform and overview rows. The caller handles waveform offset and
// beat-fit interaction overlays separately.
[[nodiscard]] auto populate_compose_waveform_projection(
    frontend_workspace_view_t& flattened, compose_scene_cache_t& cache,
    const wormedit::audio::waveform_analysis_result_t* analysis, bool analysis_pending,
    bool has_selected_audio_asset,
    const wormedit::editor_interaction_state_t* interaction_state,
    const audio::waveform_analysis_result_t* detail_analysis = nullptr,
    audio::decoded_block_cache_t* decoded_blocks = nullptr,
    const audio::aligned_waveform_style_t* stable_style = nullptr,
    bgm_bar_range_t bgm_bar_range = {},
    std::optional<bgm_bar_pan_override_t>* bgm_bar_pan_override = nullptr,
    std::optional<bgm_bar_window_capture_t>* bgm_bar_window_capture = nullptr) -> bool;

} // namespace wormedit::sys
#endif
