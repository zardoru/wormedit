#pragma once

#include "sys/compose_input.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/waveform_job.h>

namespace wormedit::sys {

// Projects the selected BMSON BGM channel waveform into the compose view. The
// supplied analyses and job assets are the currently retained results for the
// transport and selected-channel waveform jobs.
void populate_bmson_waveform_projection(
    flattened_workspace_t& flattened, wormedit::document_history_t& history,
    const wormedit::workspace_controller_t& workspace,
    wormedit::editor_interaction_state_t* interaction_state,
    compose_scene_cache_t& cache,
    wormedit::audio::decoded_source_registry_t& audition_sources,
    const std::optional<wormedit::asset_id_t>& waveform_job_asset,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& waveform_analysis,
    const std::optional<wormedit::asset_id_t>& channel_waveform_job_asset,
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>&
        channel_waveform_analysis,
    std::uint64_t audition_source_generation);

} // namespace wormedit::sys
#endif
