#pragma once

#include "sys/compose_view.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/waveform.h>
#include <wormedit/core/audio_offset_editor.h>

namespace wormedit::sys {

// Adds the audio-offset and beat-fit drag previews to an already projected
// Compose waveform overview.
void populate_compose_waveform_overlays(
    bool overview_available, frontend_workspace_view_t& flattened,
    document_history_t& history, const workspace_controller_t& workspace,
    compose_scene_cache_t& cache, editor_interaction_state_t* interaction,
    const std::optional<proposal_t<audio_offset_drag_t>>& audio_offset_drag,
    const std::optional<workspace_tab_t>& audio_offset_drag_tab,
    const audio::waveform_analysis_result_t* waveform_analysis);

} // namespace wormedit::sys
#endif
