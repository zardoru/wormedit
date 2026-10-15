#include "sys/waveform_analysis_runtime.h"

#if defined(WORMEDIT_HAS_AUDIO_DECODER)

#include "sys/compose_input.h"
#include "sys/stem_waveform_window.h"

#include <algorithm>
#include <unordered_set>
#include <span>
#include <limits>

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
                                         audio::decoded_block_cache_t* preview_blocks,
                                         notification_sink_t sink, std::size_t stem_budget_bytes)
    : sources_(sources), preview_blocks_(preview_blocks), sink_(std::move(sink)),
      waveform_job_(sources_, coarse_blocks_, waveform_cache_, scheduler_),
      channel_waveform_job_(sources_, coarse_blocks_, waveform_cache_, scheduler_),
      detail_job_(sources_, decoded_blocks_, waveform_cache_, scheduler_),
      stem_budget_bytes_(stem_budget_bytes) {
    for (std::size_t slot = 0; slot < stem_slot_count; ++slot)
        stem_slots_.push_back(std::make_unique<stem_slot_t>(sources_, coarse_blocks_, waveform_cache_, scheduler_));
}

waveform_analysis_t::~waveform_analysis_t() {
    shutdown();
}

void waveform_analysis_t::shutdown() noexcept {
    if (shutdown_started_) return;
    shutdown_started_ = true;
    cancel_detail_worker();
    waveform_job_.cancel();
    channel_waveform_job_.cancel();
    detail_job_.cancel();
    for (auto& slot : stem_slots_) slot->job.cancel();
    scheduler_.shutdown();
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
        if (decoded_blocks_.has_block(block_key)) continue;
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
        return task_result_t::success(task_value_t::from(
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
    for (auto& slot : stem_slots_) {
        slot->job.cancel();
        slot->asset.reset();
    }
    stem_analyses_.clear();
    stem_cache_stats_ = {};
    stem_visible_.clear();
    stem_budget_rejected_.clear();
    stem_oversized_.clear();
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
}

void waveform_analysis_t::service_compose_stem_waveforms(
    const song_document_t& document, const workspace_controller_t& workspace,
    compose_scene_cache_t& compose_scene_cache,
    const editor_interaction_state_t* compose_interaction_state,
    std::span<const sound_channel_t* const> visible_stems) {
    service_stem_waveforms(document, workspace, compose_scene_cache,
                           compose_interaction_state, visible_stems);
}

void waveform_analysis_t::request_selected_waveform(std::optional<asset_id_t> asset,
    audio::waveform_job_t& job, std::optional<asset_id_t>& accepted,
    std::optional<asset_id_t>& attempted, const char* failure_message) {
    if (attempted != asset) attempted.reset();
    if (!asset || attempted == asset) return;
    const auto started = job.begin(*asset, 256);
    if (started) {
        accepted = asset;
        return;
    }
    if (started.error().code == audio::waveform_job_start_status_t::capacity_exceeded) return;
    attempted = asset;
    sink_.post(notification_severity_t::error, notification_source_t::audio,
               started.error().message.empty() ? failure_message : started.error().message);
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
    if (sink_.is_muted())
        return;
    if (const auto notification = waveform_completion_notification(completed, failure_message))
        sink_.post(*notification);
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
            audio::aligned_waveform_detail_range_t{key.begin_frame, key.end_frame}.is_covering_window(*window))
            detail_analysis_ = completed->analysis;
    }
    audio::update_aligned_waveform_style(detail_style_, window->frames_per_pixel);
    if (!detail_style_.detail_level || detail_task_) return;
    const int direction = playback_rate < 0 ? -1 : 1;
    const auto retained = retained_detail_range();
    const bool pcm_ready = !detail_style_.sample_points ||
        cached_detail_window({binding->source, 32}, *window);
    if (retained && pcm_ready && !retained->is_refill_needed(*window, direction)) return;
    auto range = audio::padded_aligned_waveform_range(*window, direction);
    if (!range) return;
    range->end_frame = std::min(range->end_frame, binding->output_frame_count);
    if (retained == range && retained->is_covering_window(*window) && pcm_ready) return;
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
        !audio::build_cached_waveform_samples_view(decoded_blocks_, key.source,
            key.begin_frame, key.end_frame, audio::maximum_aligned_waveform_rows);
    if (detail_job_.is_busy() || (detail_analysis_ && !pcm_missing)) return;
    const auto started = detail_job_.begin(*asset, 32,
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
        const auto chart = document.find_chart(*workspace.selected_chart);
        if (chart != nullptr) {
            const auto selection = selected_bmson_waveform_source(
                compose_scene_cache, *chart, document.revision,
                compose_interaction_state->selected_objects.front().id);
            if (selection && stem_lane_for_channel(*chart, selection->channel))
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
    return found == stem_analyses_.end() ? nullptr : found->second.analysis;
}

namespace {
auto charge_waveform_capacity(std::size_t bytes, std::size_t count, std::size_t stride = 1)
    -> std::size_t {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    return count > (maximum - bytes) / stride ? maximum : bytes + count * stride;
}
auto stem_result_capacity(const audio::waveform_analysis_result_t& result) -> std::size_t {
    auto bytes = sizeof(result);
    if (!result.waveform) return bytes;
    const auto& waveform = *result.waveform;
    bytes = charge_waveform_capacity(bytes, waveform.bins.capacity(), sizeof(audio::waveform_bin_t));
    bytes = charge_waveform_capacity(bytes, waveform.coarser_levels.capacity(), sizeof(audio::waveform_level_t));
    for (const auto& level : waveform.coarser_levels)
        bytes = charge_waveform_capacity(bytes, level.bins.capacity(), sizeof(audio::waveform_bin_t));
    bytes = charge_waveform_capacity(bytes, waveform.key.source.asset_identity.capacity());
    bytes = charge_waveform_capacity(bytes, waveform.key.source.source_identity.capacity());
    return charge_waveform_capacity(bytes, 2); // String terminators, conservatively including SSO.
}
} // namespace

auto waveform_analysis_t::stem_waveform_cache_stats() const -> stem_waveform_cache_stats_t {
    auto stats = stem_cache_stats_;
    stats.entries = stem_analyses_.size();
    stats.backing_cached_bytes = waveform_cache_.stats().cached_bytes;
    return stats;
}
void waveform_analysis_t::erase_stem_analysis(stem_analysis_map_t::iterator entry, bool eviction) {
    stem_cache_stats_.resident_bytes -= entry->second.bytes;
    stem_analyses_.erase(entry);
    if (eviction) ++stem_cache_stats_.evictions;
}
auto waveform_analysis_t::evict_offscreen_stem_analysis() -> bool {
    auto oldest = stem_analyses_.end();
    for (auto entry = stem_analyses_.begin(); entry != stem_analyses_.end(); ++entry) {
        if (stem_visible_.contains(entry->first)) continue;
        if (oldest == stem_analyses_.end() || entry->second.last_viewport < oldest->second.last_viewport)
            oldest = entry;
    }
    if (oldest == stem_analyses_.end()) return false;
    erase_stem_analysis(oldest, true);
    return true;
}
void waveform_analysis_t::retain_stem_analysis(asset_id_t asset,
    std::shared_ptr<const audio::waveform_analysis_result_t> analysis) {
    // Charge result plus conservative shared-control/map-node bookkeeping and key.
    const auto bytes = charge_waveform_capacity(stem_result_capacity(*analysis),
        sizeof(stem_analysis_entry_t) + sizeof(asset_id_t) + 8U * sizeof(void*));
    if (bytes > stem_budget_bytes_) {
        const auto binding = sources_.binding(asset);
        stem_oversized_[asset] = binding ? binding->source : audio::audio_source_identity_t{};
        ++stem_cache_stats_.rejected_results;
        return;
    }
    {
        while (stem_cache_stats_.resident_bytes > stem_budget_bytes_ - bytes)
            if (!evict_offscreen_stem_analysis()) break;
        if (stem_cache_stats_.resident_bytes <= stem_budget_bytes_ - bytes) {
            const auto inserted = stem_analyses_.emplace(asset,
                stem_analysis_entry_t{std::move(analysis), bytes, stem_viewport_sequence_}).second;
            if (inserted) stem_cache_stats_.resident_bytes += bytes;
            return;
        }
    }
    stem_budget_rejected_.insert(asset);
    ++stem_cache_stats_.rejected_results;
}

// The assets of the stems near the cursor in the selected BMSON chart, without the transport's
// and the selected channel's (which have their own analyses).
auto waveform_analysis_t::wanted_stem_assets(const song_document_t& document,
                                             const workspace_controller_t& workspace,
                                             std::optional<asset_id_t> selected_channel_asset,
                                             const editor_interaction_state_t* interaction,
                                             std::span<const sound_channel_t* const> visible_stems) const
    -> std::vector<asset_id_t> {
    std::vector<asset_id_t> wanted;
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart || !interaction) return wanted;
    const auto chart = document.find_chart(*workspace.selected_chart);
    if (chart == nullptr || chart->source_family != format_family_t::bmson) return wanted;
    std::unordered_set<std::uint64_t> seen;
    for (const auto* channel : visible_stems) {
        if (!channel || !stem_lane_for_channel(*chart, channel->id)) continue;
        const auto asset = channel->source_clip.source;
        if (!asset || asset == workspace.transport.audio_asset || asset == selected_channel_asset ||
            !seen.insert(asset.value).second) continue;
        wanted.push_back(asset);
    }
    return wanted;
}

// Collects finished stem analyses and frees the slots of stems that are no longer wanted.
void waveform_analysis_t::collect_stem_waveforms(const std::vector<asset_id_t>& wanted) {
    const std::unordered_set<std::uint64_t> wanted_ids = [&] {
        std::unordered_set<std::uint64_t> ids;
        ids.reserve(wanted.size());
        for (const auto asset : wanted) ids.insert(asset.value);
        return ids;
    }();
    for (auto& slot : stem_slots_) {
        if (!slot->asset) continue;
        if (!wanted_ids.contains(slot->asset->value)) {
            slot->job.cancel();
            slot->asset.reset();
            continue;
        }
        const auto completed = slot->job.poll();
        if (!completed) continue;
        slot->asset.reset();
        // A failed analysis is kept so the stem shows a placeholder, and retried once its
        // source registers (the decoder-unavailable case); only a missing result is not.
        if (completed->analysis) retain_stem_analysis(completed->asset, completed->analysis);
        else stem_attempted_.insert(completed->asset);
        report_waveform_completion(*completed, "A BMSON stem waveform could not be analyzed.");
    }
}

// Whether a wanted stem still needs an analysis, after letting a failed one retry.
auto waveform_analysis_t::stem_needs_analysis(asset_id_t asset) -> bool {
    const auto found = stem_analyses_.find(asset);
    if (found != stem_analyses_.end() &&
        waveform_source_recovered(asset, found->second.analysis, sources_))
        erase_stem_analysis(found, false);
    const auto oversized = stem_oversized_.find(asset);
    if (oversized != stem_oversized_.end()) {
        const auto binding = sources_.binding(asset);
        if (!binding || binding->source == oversized->second) return false;
        stem_oversized_.erase(oversized);
    }
    // A failed start is retried once the source has a binding (it registers in the background).
    if (stem_attempted_.contains(asset) && sources_.binding(asset)) stem_attempted_.erase(asset);
    const auto running = std::ranges::any_of(stem_slots_, [&](const auto& slot) { return slot->asset == asset; });
    return !stem_analyses_.contains(asset) && !stem_attempted_.contains(asset) &&
           !stem_budget_rejected_.contains(asset) && !running;
}

void waveform_analysis_t::start_stem_waveforms(const std::vector<asset_id_t>& wanted) {
    for (const auto asset : wanted) {
        if (!stem_needs_analysis(asset)) continue;
        const auto free_slot = std::ranges::find_if(stem_slots_, [](const auto& slot) { return !slot->asset; });
        if (free_slot == stem_slots_.end()) return;
        const auto started = (*free_slot)->job.begin(asset, 256);
        if (started) {
            (*free_slot)->asset = asset;
        } else if (started.error().code != audio::waveform_job_start_status_t::capacity_exceeded) {
            stem_attempted_.insert(asset);
            sink_.post(notification_severity_t::error, notification_source_t::audio,
                       started.error().message.empty() ? "A BMSON stem waveform could not start."
                                               : started.error().message);
        }
    }
}

namespace {
void append_packed_waveform_assets(const song_document_t& document,
    const workspace_controller_t& workspace, compose_scene_cache_t& cache,
    const editor_interaction_state_t* interaction, const audio::decoded_source_registry_t& sources,
    std::vector<asset_id_t>& wanted) {
    if (workspace.active_tab != workspace_tab_t::compose || !workspace.selected_chart || !interaction ||
        interaction->chart != *workspace.selected_chart || !cache.timing.marker_timing_index) return;
    const auto chart = document.find_chart(*workspace.selected_chart);
    if (chart == nullptr || chart->source_family != format_family_t::bmson ||
        cache.scene.chart != chart->id || cache.scene.revision != document.revision) return;
    prepare_packed_waveform_index(cache.stems.packed_waveform_index, document, *chart,
                                 sources.bindings(), *cache.timing.marker_timing_index);
    const auto window = packed_window_seconds(*cache.timing.marker_timing_index,
        interaction->viewport_scroll_pixels, interaction->viewport_height_pixels,
        interaction->pixels_per_standard_measure);
    if (!window) return;
    const auto pieces = visible_packed_waveform_pieces(cache.stems.packed_waveform_index,
                                                     window->first, window->second);
    std::unordered_set<std::uint64_t> seen;
    for (const auto asset : wanted) seen.insert(asset.value);
    for (const auto* piece : pieces) {
        const auto asset = piece->clip.source;
        if (asset != workspace.transport.audio_asset && seen.insert(asset.value).second)
            wanted.push_back(asset);
    }
}
}

void waveform_analysis_t::service_stem_waveforms(
    const song_document_t& document, const workspace_controller_t& workspace,
    compose_scene_cache_t& compose_scene_cache,
    const editor_interaction_state_t* compose_interaction_state,
    std::span<const sound_channel_t* const> visible_stems) {
    const auto selected = selected_channel_asset(document, workspace, compose_scene_cache,
                                                 compose_interaction_state);
    auto wanted = wanted_stem_assets(document, workspace, selected, compose_interaction_state,
                                     visible_stems);
    append_packed_waveform_assets(document, workspace, compose_scene_cache,
                                  compose_interaction_state, sources_, wanted);
    std::unordered_set<std::uint64_t> wanted_ids;
    wanted_ids.reserve(wanted.size());
    for (const auto asset : wanted) wanted_ids.insert(asset.value);
    const std::set<asset_id_t> visible(wanted.begin(), wanted.end());
    if (visible != stem_visible_) stem_budget_rejected_.clear();
    stem_visible_ = visible;
    ++stem_viewport_sequence_;
    for (auto& [asset, entry] : stem_analyses_)
        if (stem_visible_.contains(asset)) entry.last_viewport = stem_viewport_sequence_;
    std::erase_if(stem_attempted_, [&](asset_id_t asset) { return !wanted_ids.contains(asset.value); });
    collect_stem_waveforms(wanted);
    start_stem_waveforms(wanted);
}

} // namespace wormedit::sys::runtime

#endif
