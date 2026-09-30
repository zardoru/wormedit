#include "sys/waveform_analysis_runtime.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/compose_input.h"

#include <algorithm>

namespace wormedit::sys::runtime {

waveform_analysis_t::waveform_analysis_t(audio::decoded_source_registry_t& sources)
    : sources_(sources),
      waveform_job_(sources_, decoded_blocks_, waveform_cache_, scheduler_),
      channel_waveform_job_(sources_, decoded_blocks_, waveform_cache_, scheduler_),
      detail_job_(sources_, decoded_blocks_, waveform_cache_, scheduler_) {}

void waveform_analysis_t::reset_sources() {
    waveform_job_.cancel();
    waveform_job_asset_.reset();
    waveform_analysis_.reset();
    channel_waveform_job_.cancel();
    channel_waveform_job_asset_.reset();
    channel_waveform_analysis_.reset();
    detail_job_.cancel();
    detail_asset_.reset();
    detail_key_.reset();
    detail_analysis_.reset();
    decoded_blocks_.clear();
    waveform_cache_.clear();
}

void waveform_analysis_t::service(const song_document_t& document,
                                  const workspace_controller_t& workspace,
                                  compose_scene_cache_t& compose_scene_cache,
                                  const editor_interaction_state_t* compose_interaction_state,
                                  std::string& status) {
    service_workspace_waveform(workspace, status);
    service_channel_waveform(document, workspace, compose_scene_cache,
                             compose_interaction_state, status);
}

void waveform_analysis_t::service_workspace_waveform(
    const workspace_controller_t& workspace, std::string& status) {
    if (workspace.transport.audio_asset != waveform_job_asset_) {
        waveform_job_.cancel();
        waveform_job_asset_.reset();
        waveform_analysis_.reset();
        if (workspace.transport.audio_asset) {
            const auto started = waveform_job_.request(*workspace.transport.audio_asset, 256);
            if (started) {
                waveform_job_asset_ = workspace.transport.audio_asset;
            } else if (started.status != audio::waveform_job_start_status_t::capacity_exceeded) {
                status = started.message.empty() ? "Waveform analysis could not start."
                                                 : started.message;
            }
        }
    }
    if (const auto completed = waveform_job_.poll();
        completed && waveform_job_asset_ == completed->asset) {
        if (*completed) {
            waveform_analysis_ = completed->analysis;
        } else if (completed->task_error) {
            status = completed->task_error->message;
        }
    }

}

void waveform_analysis_t::service_compose_detail(
    std::optional<asset_id_t> asset, std::optional<audio::aligned_waveform_window_t> window) {
    const auto binding = asset ? sources_.binding(*asset) : std::nullopt;
    if (!binding || !window || window->frames_per_pixel >= 256.0 ||
        window->end_frame - window->begin_frame > audio::maximum_waveform_projection_columns) {
        detail_job_.cancel();
        detail_asset_.reset();
        detail_key_.reset();
        detail_analysis_.reset();
        return;
    }
    const audio::waveform_key_t key{binding->source, 32, window->begin_frame, window->end_frame};
    if (detail_key_ != key || detail_asset_ != asset) {
        detail_job_.cancel();
        detail_analysis_.reset();
        detail_key_ = key;
        detail_asset_ = asset;
    }
    if (const auto completed = detail_job_.poll(); completed && completed->asset == asset &&
        completed->analysis && completed->analysis->waveform &&
        completed->analysis->waveform->key == key) {
        detail_analysis_ = completed->analysis;
    }
    const bool pcm_missing = window->frames_per_pixel < 1.0 &&
        !audio::project_cached_waveform_samples(decoded_blocks_, key.source,
            key.begin_frame, key.end_frame, audio::maximum_aligned_waveform_rows);
    if (detail_job_.busy() || (detail_analysis_ && !pcm_missing)) return;
    const auto started = detail_job_.request(*asset, 32,
        {.begin_frame = key.begin_frame, .end_frame = key.end_frame,
         .require_cached_pcm = window->frames_per_pixel < 1.0});
    (void)started; // A busy bounded scheduler retries next frame and retains coarse rendering.
}

auto waveform_analysis_t::selected_channel_asset(
    const song_document_t& document, const workspace_controller_t& workspace,
    compose_scene_cache_t& compose_scene_cache,
    const editor_interaction_state_t* compose_interaction_state) const
    -> std::optional<asset_id_t> {
    std::optional<asset_id_t> selected_channel_waveform_asset;
    if (workspace.active_tab == workspace_tab_t::compose && workspace.selected_chart &&
        compose_interaction_state &&
        compose_interaction_state->chart == *workspace.selected_chart &&
        compose_interaction_state->selected_objects.size() == 1 &&
        compose_interaction_state->selected_objects.front().kind ==
            editor_selection_object_kind_t::bgm_event) {
        const auto chart = std::ranges::find(document.charts, *workspace.selected_chart,
                                             &chart_document_t::id);
        if (chart != document.charts.end()) {
            const auto selection = selected_bmson_waveform_source(
                compose_scene_cache, *chart, document.revision,
                compose_interaction_state->selected_objects.front().id);
            if (selection)
                selected_channel_waveform_asset = selection->asset;
        }
    }
    if (selected_channel_waveform_asset == workspace.transport.audio_asset)
        return std::nullopt;
    return selected_channel_waveform_asset;
}

void waveform_analysis_t::service_channel_waveform(
    const song_document_t& document, const workspace_controller_t& workspace,
    compose_scene_cache_t& compose_scene_cache,
    const editor_interaction_state_t* compose_interaction_state, std::string& status) {
    const auto selected_channel_waveform_asset = selected_channel_asset(
        document, workspace, compose_scene_cache, compose_interaction_state);
    if (selected_channel_waveform_asset != channel_waveform_job_asset_) {
        channel_waveform_job_.cancel();
        channel_waveform_job_asset_.reset();
        channel_waveform_analysis_.reset();
        if (selected_channel_waveform_asset) {
            const auto started = channel_waveform_job_.request(*selected_channel_waveform_asset, 256);
            if (started)
                channel_waveform_job_asset_ = selected_channel_waveform_asset;
            else if (started.status != audio::waveform_job_start_status_t::capacity_exceeded)
                status = started.message.empty() ? "Channel waveform analysis could not start."
                                                 : started.message;
        }
    }
    if (const auto completed = channel_waveform_job_.poll();
        completed && channel_waveform_job_asset_ == completed->asset) {
        if (*completed) {
            channel_waveform_analysis_ = completed->analysis;
            if (!channel_waveform_analysis_->waveform)
                status = "Selected BMSON channel waveform could not be analyzed.";
        } else if (completed->task_error) {
            status = completed->task_error->message;
        }
    }
}

} // namespace wormedit::sys::runtime

#endif
