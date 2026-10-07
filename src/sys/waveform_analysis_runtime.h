#pragma once

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/compose_view.h"
#include "sys/thread_priority.h"

#include <wormedit/audio/source_registry.h>
#include <wormedit/audio/waveform.h>
#include <wormedit/audio/aligned_waveform.h>
#include <wormedit/audio/waveform_job.h>
#include <wormedit/core/document.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/task_graph.h>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace wormedit::sys::runtime {

// Completion metadata is determined from the native result, never message text.
[[nodiscard]] auto waveform_completion_notification(
    const audio::waveform_job_completion_t& completed, const char* failure_message = nullptr)
    -> std::optional<notification_post_t>;

// Owns the analysis caches and jobs used by the audio and Compose waveform views.
class waveform_analysis_t {
public:
    explicit waveform_analysis_t(audio::decoded_source_registry_t& sources,
        audio::decoded_block_cache_t* preview_blocks = nullptr,
        std::optional<notification_sink_t> sink = std::nullopt);

    ~waveform_analysis_t();

    // Drop job results and cached decoded data after the registered source set changes.
    void reset_sources();

    // Request and collect the workspace and selected BMSON channel analyses.
    void service(const song_document_t& document, const workspace_controller_t& workspace,
                 compose_scene_cache_t& compose_scene_cache,
                 const editor_interaction_state_t* compose_interaction_state);

    void service_compose_detail(std::optional<asset_id_t> asset,
                                std::optional<audio::aligned_waveform_window_t> window,
                                bool playing = false, double playback_rate = 1.0);
    [[nodiscard]] auto playback_style() const noexcept -> const audio::aligned_waveform_style_t* {
        return playing_detail_ ? &detail_style_ : nullptr;
    }
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
    // The finished analysis of a BMSON stem asset other than the selected channel's, if any.
    [[nodiscard]] auto stem_waveform_analysis(asset_id_t asset) const
        -> std::shared_ptr<const audio::waveform_analysis_result_t>;
    // Stem assets analyzed at once (the stems near the cursor; see stem_waveform_window.h).
    static constexpr std::size_t maximum_stem_waveforms = 192;

private:
    void request_selected_waveform(std::optional<asset_id_t> asset,
        audio::waveform_job_t& job, std::optional<asset_id_t>& accepted,
        std::optional<asset_id_t>& attempted, const char* failure_message);
    void report_waveform_completion(const audio::waveform_job_completion_t& completed,
                                    const char* failure_message = nullptr);
    void service_idle_detail(std::optional<asset_id_t> asset,
        std::optional<audio::aligned_waveform_window_t> window);
    [[nodiscard]] auto retained_detail_range() const
        -> std::optional<audio::aligned_waveform_detail_range_t>;
    [[nodiscard]] auto cached_detail_window(const audio::waveform_key_t& key,
        audio::aligned_waveform_window_t window) -> bool;
    void cancel_detail_worker() noexcept;
    [[nodiscard]] auto poll_detail_worker() -> std::optional<audio::waveform_job_completion_t>;
    [[nodiscard]] auto request_detail_worker(asset_id_t asset, audio::waveform_key_t key, bool samples) -> bool;
    void borrow_preview_blocks(const audio::waveform_key_t& key, std::stop_token stop);
    bool playing_detail_{};
    audio::aligned_waveform_style_t detail_style_;
    void service_workspace_waveform(const workspace_controller_t& workspace);
    void service_stem_waveforms(const song_document_t& document,
                                const workspace_controller_t& workspace,
                                compose_scene_cache_t& compose_scene_cache,
                                const editor_interaction_state_t* compose_interaction_state);
    void collect_stem_waveforms(const std::vector<asset_id_t>& wanted);
    void start_stem_waveforms(const std::vector<asset_id_t>& wanted);
    [[nodiscard]] auto stem_needs_analysis(asset_id_t asset) -> bool;
    [[nodiscard]] auto wanted_stem_assets(const song_document_t& document,
                                          const workspace_controller_t& workspace,
                                          std::optional<asset_id_t> selected_channel_asset,
                                          const editor_interaction_state_t* interaction,
                                          double viewport_height_pixels) const
        -> std::vector<asset_id_t>;
    void service_channel_waveform(const song_document_t& document,
                                  const workspace_controller_t& workspace,
                                  compose_scene_cache_t& compose_scene_cache,
                                  const editor_interaction_state_t* compose_interaction_state);
    [[nodiscard]] auto selected_channel_asset(
        const song_document_t& document, const workspace_controller_t& workspace,
        compose_scene_cache_t& compose_scene_cache,
        const editor_interaction_state_t* compose_interaction_state) const
        -> std::optional<asset_id_t>;

    audio::decoded_source_registry_t& sources_;
    audio::decoded_block_cache_t* preview_blocks_{};
    std::optional<notification_sink_t> sink_;
    std::optional<asset_id_t> attempted_waveform_asset_;
    std::optional<asset_id_t> attempted_channel_asset_;
    audio::decoded_block_cache_t coarse_blocks_;
    audio::decoded_block_cache_t decoded_blocks_{
        {.maximum_bytes = 4U * 1024U * 1024U, .maximum_frames = 524288,
         .maximum_bytes_per_asset = 2U * 1024U * 1024U, .maximum_frames_per_asset = 262144}};
    audio::waveform_cache_t waveform_cache_;
    task_scheduler_t scheduler_{2, 4, background_scheduler_options()};
    audio::waveform_job_t waveform_job_;
    audio::waveform_job_t channel_waveform_job_;
    audio::waveform_job_t detail_job_;
    // A waveform job keeps only its latest request, so each stem analysis running at once needs
    // its own job; the slots share the scheduler and decoded-block cache.
    struct stem_slot_t {
        stem_slot_t(audio::decoded_source_registry_t& sources, audio::decoded_block_cache_t& blocks,
                    audio::waveform_cache_t& waveforms, task_scheduler_t& scheduler)
            : job(sources, blocks, waveforms, scheduler, 1) {}
        audio::waveform_job_t job;
        std::optional<asset_id_t> asset;
    };
    static constexpr std::size_t stem_slot_count = 3;
    std::vector<std::unique_ptr<stem_slot_t>> stem_slots_;
    std::map<asset_id_t, std::shared_ptr<const audio::waveform_analysis_result_t>> stem_analyses_;
    std::set<asset_id_t> stem_attempted_;
    task_graph_handle_t detail_worker_;
    std::optional<task_id_t> detail_task_;
    std::uint64_t detail_generation_{};
    std::uint64_t worker_generation_{};
    std::optional<asset_id_t> worker_asset_;
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
