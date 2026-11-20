#pragma once

#include "sys/compose/input/compose_input.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include "sys/audio/project_audio_sources.h"
#include <wormedit/audio/waveform_job.h>

#include <functional>
#include <memory>
#include <optional>
#include <span>

namespace wormedit::sys {

// Projects the selected BMSON BGM channel waveform into the compose view. The
// supplied analyses and job assets are the currently retained results for the
// transport and selected-channel waveform jobs.
struct waveform_analysis_slot_t {
    const std::optional<wormedit::asset_id_t>& asset;
    const std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>& analysis;
};

// The detail layer of one audio asset as the analysis runtime holds it.
struct bmson_stem_detail_t {
    std::shared_ptr<const wormedit::audio::waveform_analysis_result_t> analysis;
    wormedit::audio::decoded_block_cache_t* blocks{};
    wormedit::audio::aligned_waveform_style_t style;
    std::uint64_t generation{};
};

struct bmson_waveform_projection_context_t {
    frontend_workspace_view_t& flattened;
    wormedit::document_history_t& history;
    const wormedit::workspace_controller_t& workspace;
    wormedit::editor_interaction_state_t* interaction_state;
    compose_scene_cache_t& cache;
    project_audio_sources_t& audition_sources;
    waveform_analysis_slot_t transport_waveform;
    waveform_analysis_slot_t channel_waveform;
    std::uint64_t audition_source_generation;
    // Finished analysis of a stem asset, from the analysis runtime; empty disables stem drawing.
    std::function<std::shared_ptr<const wormedit::audio::waveform_analysis_result_t>(wormedit::asset_id_t)>
        stem_analysis{};
    std::optional<std::span<const wormedit::sound_channel_t* const>> visible_stems{};
    // Detail layer per asset (32-frame window, cached PCM, style); empty draws the overview only.
    std::function<bmson_stem_detail_t(wormedit::asset_id_t)> stem_detail{};
};

void populate_bmson_waveform_projection(const bmson_waveform_projection_context_t& context);

// Shared by song, selected channel and stems; timing changes retire all display rings.
[[nodiscard]] auto prepare_compose_display_row_table(compose_scene_cache_t& cache,
    audio::aligned_waveform_request_t request, audio::display_row_table_identity_t identity) -> bool;

} // namespace wormedit::sys
#endif
