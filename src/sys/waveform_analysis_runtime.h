#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/compose_view.h"

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/waveform.h>
#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/waveform_job.h>
#include <wormedit/core/document.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/task_graph.h>

#include <memory>
#include <optional>
#include <string>

namespace wormedit::sys::runtime {

// Owns the analysis caches and jobs used by the audio and Compose waveform views.
class waveform_analysis_t {
public:
    explicit waveform_analysis_t(audio::decoded_source_registry_t& sources);

    // Drop job results and cached decoded data after the registered source set changes.
    void reset_sources();

    // Request and collect the workspace and selected BMSON channel analyses.
    void service(const song_document_t& document, const workspace_controller_t& workspace,
                 compose_scene_cache_t& compose_scene_cache,
                 const editor_interaction_state_t* compose_interaction_state,
                 std::string& status);

    void service_compose_detail(std::optional<asset_id_t> asset,
                                std::optional<audio::aligned_waveform_window_t> window);
    [[nodiscard]] auto detail_analysis() const noexcept
        -> const std::shared_ptr<const audio::waveform_analysis_result_t>& {
        return detail_analysis_;
    }
    [[nodiscard]] auto decoded_blocks() noexcept -> audio::decoded_block_cache_t& {
        return decoded_blocks_;
    }

    [[nodiscard]] auto waveform_job() noexcept -> audio::waveform_job_t& { return waveform_job_; }
    [[nodiscard]] auto waveform_job() const noexcept -> const audio::waveform_job_t& {
        return waveform_job_;
    }
    [[nodiscard]] auto waveform_asset() const noexcept -> const std::optional<asset_id_t>& {
        return waveform_job_asset_;
    }
    [[nodiscard]] auto waveform_analysis() const noexcept
        -> const std::shared_ptr<const audio::waveform_analysis_result_t>& {
        return waveform_analysis_;
    }
    [[nodiscard]] auto channel_waveform_job() noexcept -> audio::waveform_job_t& {
        return channel_waveform_job_;
    }
    [[nodiscard]] auto channel_waveform_job() const noexcept -> const audio::waveform_job_t& {
        return channel_waveform_job_;
    }
    [[nodiscard]] auto channel_waveform_asset() const noexcept
        -> const std::optional<asset_id_t>& {
        return channel_waveform_job_asset_;
    }
    [[nodiscard]] auto channel_waveform_analysis() const noexcept
        -> const std::shared_ptr<const audio::waveform_analysis_result_t>& {
        return channel_waveform_analysis_;
    }

private:
    void service_workspace_waveform(const workspace_controller_t& workspace,
                                   std::string& status);
    void service_channel_waveform(const song_document_t& document,
                                  const workspace_controller_t& workspace,
                                  compose_scene_cache_t& compose_scene_cache,
                                  const editor_interaction_state_t* compose_interaction_state,
                                  std::string& status);
    [[nodiscard]] auto selected_channel_asset(
        const song_document_t& document, const workspace_controller_t& workspace,
        compose_scene_cache_t& compose_scene_cache,
        const editor_interaction_state_t* compose_interaction_state) const
        -> std::optional<asset_id_t>;

    audio::decoded_source_registry_t& sources_;
    audio::decoded_block_cache_t decoded_blocks_;
    audio::waveform_cache_t waveform_cache_;
    task_scheduler_t scheduler_{2, 4};
    audio::waveform_job_t waveform_job_;
    audio::waveform_job_t channel_waveform_job_;
    audio::waveform_job_t detail_job_;
    std::optional<asset_id_t> detail_asset_;
    std::optional<audio::waveform_key_t> detail_key_;
    std::shared_ptr<const audio::waveform_analysis_result_t> detail_analysis_;
    std::optional<asset_id_t> waveform_job_asset_;
    std::shared_ptr<const audio::waveform_analysis_result_t> waveform_analysis_;
    std::optional<asset_id_t> channel_waveform_job_asset_;
    std::shared_ptr<const audio::waveform_analysis_result_t> channel_waveform_analysis_;
};

} // namespace wormedit::sys::runtime

#endif
