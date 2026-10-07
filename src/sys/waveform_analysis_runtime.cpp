#include "sys/waveform_analysis_runtime.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/compose_input.h"

#include <algorithm>
#include <span>

namespace wormedit::sys::runtime {

namespace {
auto waveform_failure_message(audio::waveform_analysis_status_t status) -> const char* {
    using enum audio::waveform_analysis_status_t;
    switch (status) {
    case decoder_unavailable: return "Waveform analysis failed: the audio source is unavailable.";
    case source_too_large: return "Waveform analysis failed: the audio exceeds the analysis cache limit.";
    case decode_failed: return "Waveform analysis failed while decoding the audio.";
    case analysis_cache_capacity_exceeded: return "Waveform analysis failed: the waveform cache is full.";
    case invalid_request: return "Waveform analysis failed: the analysis range is invalid.";
    case cancelled: return "";
    default: return "Waveform analysis returned no waveform.";
    }
}
auto waveform_source_recovered(std::optional<asset_id_t> asset,
    const std::shared_ptr<const audio::waveform_analysis_result_t>& analysis,
    const audio::decoded_source_registry_t& sources) -> bool {
    return asset && analysis && !analysis->waveform &&
           analysis->status == audio::waveform_analysis_status_t::decoder_unavailable &&
           sources.binding(*asset).has_value();
}
} // namespace

waveform_analysis_t::waveform_analysis_t(audio::decoded_source_registry_t& sources,
    audio::decoded_block_cache_t* preview_blocks, std::optional<notification_sink_t> sink)
    : sources_(sources), preview_blocks_(preview_blocks), sink_(std::move(sink)),
      waveform_job_(sources_, coarse_blocks_, waveform_cache_, scheduler_),
      channel_waveform_job_(sources_, coarse_blocks_, waveform_cache_, scheduler_),
      detail_job_(sources_, decoded_blocks_, waveform_cache_, scheduler_),
      stem_job_(sources_, coarse_blocks_, waveform_cache_, scheduler_, maximum_stem_waveforms) {}

waveform_analysis_t::~waveform_analysis_t() {
    cancel_detail_worker();
    if (detail_worker_) static_cast<void>(detail_worker_.wait());
}

void waveform_analysis_t::cancel_detail_worker() noexcept {
    if (detail_worker_) detail_worker_.cancel();
    ++detail_generation_;
}

void waveform_analysis_t::borrow_preview_blocks(const audio::waveform_key_t& key,
                                               std::stop_token stop) {
    if (!preview_blocks_ || key.begin_frame >= key.end_frame) return;
    const auto first = key.begin_frame / audio::maximum_read_frames;
    const auto last = (key.end_frame - 1U) / audio::maximum_read_frames;
    for (auto index = first; index <= last && !stop.stop_requested(); ++index) {
        const audio::audio_block_key_t block_key{key.source, index};
        if (decoded_blocks_.contains(block_key)) continue;
        if (const auto resident = preview_blocks_->find(block_key))
            static_cast<void>(decoded_blocks_.store(block_key, resident->frame_count,
                                                   resident->interleaved_stereo));
    }
}

auto waveform_analysis_t::request_detail_worker(asset_id_t asset, audio::waveform_key_t key,
                                               bool samples) -> bool {
    if (detail_task_) return false;
    task_graph_t graph{1};
    const auto generation = detail_generation_;
    const auto task = graph.add_task({}, [this, asset, key, samples](
        std::span<const task_result_t>, std::stop_token stop) {
        borrow_preview_blocks(key, stop);
        auto analysis = sources_.analyze_waveform(asset, decoded_blocks_, waveform_cache_, 32,
            {.begin_frame = key.begin_frame, .end_frame = key.end_frame, .require_cached_pcm = samples}, stop);
        return task_result_t::success(make_task_value(
            std::make_shared<const audio::waveform_analysis_result_t>(std::move(analysis))));
    });
    if (!task) return false;
    auto submitted = scheduler_.submit(std::move(graph));
    if (!submitted) return false;
    detail_worker_ = std::move(submitted.graph);
    detail_task_ = *task.id;
    worker_asset_ = asset;
    // Generation travels in the completion field, and cancelled results are never installed.
    worker_generation_ = generation;
    return true;
}

auto waveform_analysis_t::poll_detail_worker() -> std::optional<audio::waveform_job_completion_t> {
    if (!detail_task_) return std::nullopt;
    const auto completed = detail_worker_.result(*detail_task_);
    if (!completed) return std::nullopt;
    detail_task_.reset();
    detail_worker_ = {};
    if (worker_generation_ != detail_generation_) return std::nullopt;
    return audio::waveform_job_completion_t{.generation = worker_generation_, .asset = *worker_asset_,
        .analysis = task_value_cast<audio::waveform_analysis_result_t>(*completed)};
}

void waveform_analysis_t::reset_sources() {
    cancel_detail_worker();
    waveform_job_.cancel();
    waveform_job_asset_.reset();
    waveform_analysis_.reset();
    channel_waveform_job_.cancel();
    stem_job_.cancel();
    stem_analyses_.clear();
    stem_requested_.clear();
    stem_attempted_.clear();
    attempted_waveform_asset_.reset();
    attempted_channel_asset_.reset();
    channel_waveform_job_asset_.reset();
    channel_waveform_analysis_.reset();
    detail_job_.cancel();
    detail_asset_.reset();
    detail_key_.reset();
    detail_analysis_.reset();
    detail_style_ = {};
    playing_detail_ = false;
    decoded_blocks_.clear();
    coarse_blocks_.clear();
    waveform_cache_.clear();
}

void waveform_analysis_t::service(const song_document_t& document,
                                  const workspace_controller_t& workspace,
                                  compose_scene_cache_t& compose_scene_cache,
                                  const editor_interaction_state_t* compose_interaction_state) {
    service_workspace_waveform(workspace);
    service_channel_waveform(document, workspace, compose_scene_cache,
                             compose_interaction_state);
    service_stem_waveforms(document, workspace, compose_scene_cache, compose_interaction_state);
}

void waveform_analysis_t::request_selected_waveform(std::optional<asset_id_t> asset,
    audio::waveform_job_t& job, std::optional<asset_id_t>& accepted,
    std::optional<asset_id_t>& attempted, const char* failure_message) {
    if (attempted != asset) attempted.reset();
    if (!asset || attempted == asset) return;
    const auto started = job.request(*asset, 256);
    if (started) {
        accepted = asset;
        return;
    }
    if (started.status == audio::waveform_job_start_status_t::capacity_exceeded) return;
    attempted = asset;
    if (sink_) sink_->post(notification_severity_t::error, notification_source_t::audio,
        started.message.empty() ? failure_message : started.message);
}

auto waveform_completion_notification(const audio::waveform_job_completion_t& completed,
                                      const char* failure_message) -> std::optional<notification_post_t> {
    if (completed.task_error) {
        if (completed.task_error->code == task_error_code_t::cancelled) return std::nullopt;
        return notification_post_t{notification_severity_t::error, notification_source_t::audio,
                                   completed.task_error->message};
    }
    if (!completed.analysis || completed.analysis->waveform ||
        completed.analysis->status == audio::waveform_analysis_status_t::cancelled) return std::nullopt;
    return notification_post_t{notification_severity_t::error, notification_source_t::audio,
        failure_message ? failure_message : waveform_failure_message(completed.analysis->status)};
}

void waveform_analysis_t::report_waveform_completion(
    const audio::waveform_job_completion_t& completed, const char* failure_message) {
    if (!sink_) return;
    if (const auto notification = waveform_completion_notification(completed, failure_message))
        sink_->post(*notification);
}

void waveform_analysis_t::service_workspace_waveform(const workspace_controller_t& workspace) {
    if (waveform_source_recovered(workspace.transport.audio_asset, waveform_analysis_, sources_))
        waveform_job_asset_.reset();
    if (attempted_waveform_asset_ != workspace.transport.audio_asset) attempted_waveform_asset_.reset();
    if (workspace.transport.audio_asset != waveform_job_asset_) {
        waveform_job_.cancel();
        waveform_job_asset_.reset();
        waveform_analysis_.reset();
        request_selected_waveform(workspace.transport.audio_asset, waveform_job_,
            waveform_job_asset_, attempted_waveform_asset_, "Waveform analysis could not start.");
    }
    if (const auto completed = waveform_job_.poll();
        completed && waveform_job_asset_ == completed->asset) {
        if (*completed) waveform_analysis_ = completed->analysis;
        report_waveform_completion(*completed);
    }
}

auto waveform_analysis_t::retained_detail_range() const
    -> std::optional<audio::aligned_waveform_detail_range_t> {
    if (!detail_analysis_ || !detail_analysis_->waveform) return std::nullopt;
    const auto& key = detail_analysis_->waveform->key;
    return audio::aligned_waveform_detail_range_t{key.begin_frame, key.end_frame};
}

auto waveform_analysis_t::cached_detail_window(const audio::waveform_key_t& key,
                                              audio::aligned_waveform_window_t window) -> bool {
    if (window.end_frame <= window.begin_frame) return false;
    const auto first = window.begin_frame / audio::maximum_read_frames;
    const auto last = (window.end_frame - 1U) / audio::maximum_read_frames;
    for (auto index = first; index <= last; ++index) {
        const auto frames = decoded_blocks_.cached_frame_count({key.source, index});
        const auto needed = std::min(window.end_frame - index * audio::maximum_read_frames,
                                     static_cast<std::uint64_t>(audio::maximum_read_frames));
        if (!frames || *frames < needed) return false;
    }
    return true;
}

void waveform_analysis_t::service_compose_detail(std::optional<asset_id_t> asset,
    std::optional<audio::aligned_waveform_window_t> window, bool playing, double playback_rate) {
    if (playing_detail_ != playing) {
        detail_job_.cancel();
        cancel_detail_worker();
        detail_key_.reset();
    }
    playing_detail_ = playing;
    const auto completed = poll_detail_worker();
    if (!playing) {
        service_idle_detail(asset, window);
        return;
    }
    const auto binding = asset ? sources_.binding(*asset) : std::nullopt;
    if (!binding || !window) return;
    if (detail_asset_ != asset || (detail_key_ && detail_key_->source != binding->source)) {
        cancel_detail_worker();
        detail_key_.reset();
        detail_analysis_.reset();
        detail_asset_ = asset;
    }
    if (completed && completed->asset == asset &&
        completed->analysis && completed->analysis->waveform) {
        const auto& key = completed->analysis->waveform->key;
        if (key.source == binding->source &&
            audio::aligned_waveform_detail_range_t{key.begin_frame, key.end_frame}.covers(*window))
            detail_analysis_ = completed->analysis;
    }
    audio::update_aligned_waveform_style(detail_style_, window->frames_per_pixel);
    if (!detail_style_.detail_level || detail_task_) return;
    const int direction = playback_rate < 0 ? -1 : 1;
    const auto retained = retained_detail_range();
    const bool pcm_ready = !detail_style_.sample_points ||
        cached_detail_window({binding->source, 32}, *window);
    if (retained && pcm_ready && !retained->needs_refill(*window, direction)) return;
    auto range = audio::padded_aligned_waveform_range(*window, direction);
    if (!range) return;
    range->end_frame = std::min(range->end_frame, binding->output_frame_count);
    if (retained == range && retained->covers(*window) && pcm_ready) return;
    const audio::waveform_key_t requested{binding->source, 32, range->begin_frame, range->end_frame};
    if (request_detail_worker(*asset, requested, detail_style_.sample_points)) detail_key_ = requested;
}

void waveform_analysis_t::service_idle_detail(
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
    const editor_interaction_state_t* compose_interaction_state) {
    const auto selected_channel_waveform_asset = selected_channel_asset(
        document, workspace, compose_scene_cache, compose_interaction_state);
    if (waveform_source_recovered(selected_channel_waveform_asset, channel_waveform_analysis_, sources_))
        channel_waveform_job_asset_.reset();
    if (attempted_channel_asset_ != selected_channel_waveform_asset) attempted_channel_asset_.reset();
    if (selected_channel_waveform_asset != channel_waveform_job_asset_) {
        channel_waveform_job_.cancel();
        channel_waveform_job_asset_.reset();
        channel_waveform_analysis_.reset();
        request_selected_waveform(selected_channel_waveform_asset, channel_waveform_job_,
            channel_waveform_job_asset_, attempted_channel_asset_,
            "Channel waveform analysis could not start.");
    }
    if (const auto completed = channel_waveform_job_.poll();
        completed && channel_waveform_job_asset_ == completed->asset) {
        if (*completed) channel_waveform_analysis_ = completed->analysis;
        report_waveform_completion(*completed, "Selected BMSON channel waveform could not be analyzed.");
    }
}

auto waveform_analysis_t::stem_waveform_analysis(asset_id_t asset) const
    -> std::shared_ptr<const audio::waveform_analysis_result_t> {
    const auto found = stem_analyses_.find(asset);
    return found == stem_analyses_.end() ? nullptr : found->second;
}

// The assets of the selected BMSON chart's sound channels, without the transport's and the
// selected channel's (which have their own analyses), up to the stem limit.
auto waveform_analysis_t::wanted_stem_assets(const song_document_t& document,
                                             const workspace_controller_t& workspace,
                                             std::optional<asset_id_t> selected_channel_asset) const
    -> std::vector<asset_id_t> {
    std::vector<asset_id_t> wanted;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart) return wanted;
    const auto chart = std::ranges::find(document.charts, *workspace.selected_chart, &chart_document_t::id);
    if (chart == document.charts.end() || chart->source_family != format_family_t::bmson) return wanted;
    for (const auto& channel : chart->sound_channels) {
        const auto asset = channel.source_clip.source;
        if (!asset || asset == workspace.transport.audio_asset || asset == selected_channel_asset ||
            std::ranges::find(wanted, asset) != wanted.end()) continue;
        if (wanted.size() >= maximum_stem_waveforms) break;
        wanted.push_back(asset);
    }
    return wanted;
}

void waveform_analysis_t::service_stem_waveforms(
    const song_document_t& document, const workspace_controller_t& workspace,
    compose_scene_cache_t& compose_scene_cache,
    const editor_interaction_state_t* compose_interaction_state) {
    const auto selected = selected_channel_asset(document, workspace, compose_scene_cache,
                                                 compose_interaction_state);
    const auto wanted = wanted_stem_assets(document, workspace, selected);
    const auto is_wanted = [&](asset_id_t asset) { return std::ranges::find(wanted, asset) != wanted.end(); };
    std::erase_if(stem_analyses_, [&](const auto& entry) { return !is_wanted(entry.first); });
    std::erase_if(stem_attempted_, [&](asset_id_t asset) { return !is_wanted(asset); });
    for (const auto asset : wanted) {
        const auto source_back = stem_analyses_.contains(asset) && !stem_analyses_[asset]->waveform &&
            waveform_source_recovered(asset, stem_analyses_[asset], sources_);
        if (source_back) stem_analyses_.erase(asset);
        if (stem_analyses_.contains(asset) || stem_requested_.contains(asset) ||
            stem_attempted_.contains(asset)) continue;
        const auto started = stem_job_.request(asset, 256);
        if (started) {
            stem_requested_.insert(asset);
        } else if (started.status != audio::waveform_job_start_status_t::capacity_exceeded) {
            stem_attempted_.insert(asset);
            if (sink_) sink_->post(notification_severity_t::error, notification_source_t::audio,
                started.message.empty() ? "A BMSON stem waveform could not start." : started.message);
        }
    }
    while (const auto completed = stem_job_.poll()) {
        stem_requested_.erase(completed->asset);
        if (!is_wanted(completed->asset)) continue;
        // A failed analysis is kept so the stem shows a placeholder, and retried once its source
        // registers (the decoder-unavailable case); only a missing result is not retried.
        if (completed->analysis) stem_analyses_[completed->asset] = completed->analysis;
        else stem_attempted_.insert(completed->asset);
        report_waveform_completion(*completed, "A BMSON stem waveform could not be analyzed.");
    }
}

} // namespace wormedit::sys::runtime

#endif
