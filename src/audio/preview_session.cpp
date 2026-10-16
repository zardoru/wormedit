#include <wormedit/audio/preview_session.h>

#include <limits>
#include <ranges>

namespace wormedit::audio {
namespace {

class loop_restart_scope_t {
  public:
    loop_restart_scope_t(std::atomic<std::uint64_t>& slot, std::uint64_t generation) : slot_(slot) {
        slot_.store(generation, std::memory_order_release);
    }
    ~loop_restart_scope_t() { slot_.store(0, std::memory_order_release); }
  private:
    std::atomic<std::uint64_t>& slot_;
};

auto checked_window_end(std::uint64_t timeline_frame, std::size_t lookahead_frames)
    -> std::optional<std::uint64_t> {
    if (lookahead_frames > std::numeric_limits<std::uint64_t>::max() - timeline_frame)
        return std::nullopt;
    return timeline_frame + lookahead_frames;
}

auto is_materialize_succeeded(operation_state_t<std::monostate> status) -> bool {
    return status == std::monostate{} ||
           status == failure_code_t::capacity_exceeded;
}

} // namespace

auto preview_session_t::start_session(const preview_schedule_t& schedule,
                                      std::span<const preview_object_source_binding_t> objects,
                                      std::span<const preview_metronome_source_binding_t> metronome,
                                      std::uint64_t seek_frame, preview_pump_options_t options,
                                      std::uint64_t minimum_end_frame)
    -> preview_session_start_outcome_t {
    const std::scoped_lock lock(mutex_);
    const auto start_frame = loop_region_ ? loop_region_->begin_frame : seek_frame;
    auto started = bridge_.start_preview(schedule, objects, metronome, start_frame);
    if (!started) {
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::transport_failed, .detail = {.bridge = started}});
    }
    minimum_end_frame_ = minimum_end_frame;
    return prime_started(started, start_frame, options);
}

auto preview_session_t::seek_session(std::uint64_t sample_frame, preview_pump_options_t options)
    -> preview_session_start_outcome_t {
    const std::scoped_lock lock(mutex_);
    auto started = bridge_.seek_preview(sample_frame);
    if (!started) {
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::transport_failed, .detail = {.bridge = started}});
    }
    return prime_started(started, sample_frame, options);
}

auto preview_session_t::restart_session_loop(std::uint64_t expected_generation)
    -> preview_session_start_outcome_t {
    const std::scoped_lock lock(mutex_);
    if (generation_ != expected_generation || !token_ || !loop_region_ ||
        timeline_frame_ < loop_region_->end_frame || !transport_.is_current(*token_))
        return std::unexpected(preview_session_start_error_t{});
    const loop_restart_scope_t restart_scope{loop_restart_generation_, generation_};
    const auto started = bridge_.seek_preview(loop_region_->begin_frame);
    if (!started)
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::transport_failed, .detail = {.bridge = started}});
    return prime_started(started, loop_region_->begin_frame, {}, true);
}

void preview_session_t::set_output_playhead(std::uint64_t generation,
                                            std::uint64_t frame) noexcept {
    output_playhead_.publish({.active = true, .timeline_frame = frame, .generation = generation});
}

auto preview_session_t::prime_started(preview_bridge_start_outcome_t started,
                                      std::uint64_t timeline_frame, preview_pump_options_t options,
                                      bool loop_restart) -> preview_session_start_outcome_t {
    preview_session_start_detail_t detail{.bridge = started};
    const auto window_end = checked_window_end(timeline_frame, options.lookahead_frames);
    if (!window_end.has_value()) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::materialize_failed,
            .detail = std::move(detail)});
    }
    const auto materialized = bridge_.materialize_preview(*window_end, timeline_frame);
    detail.materialize = materialized;
    const auto& materialized_effect = materialized;
    if (!is_materialize_succeeded(materialized_effect.status)) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return std::unexpected(preview_session_start_error_t{
            .code = failure_code_t::materialize_failed,
            .detail = std::move(detail)});
    }
    prime_pending_ = true;
    prime_options_ = options;
    token_ = started.value();
    timeline_frame_ = timeline_frame;
    start_frame_ = timeline_frame;
    if (!loop_restart) {
        output_latency_frames_.store(0, std::memory_order_release);
        ++generation_;
    }
    publish_playhead();
    return started_preview_session_t{.token = *token_, .detail = std::move(detail)};
}

auto preview_session_t::pump_session(preview_pump_options_t options)
    -> preview_session_pump_effect_t {
    std::optional<session_token_t> token;
    std::uint64_t timeline_frame{};
    preview_session_pump_effect_t result;
    {
        const std::scoped_lock lock(mutex_);
        if (!token_.has_value())
            return result;
        if (loop_region_ && timeline_frame_ >= loop_region_->end_frame) {
            result.status = preview_session_pump_phase_t::loop_boundary;
            return result;
        }
        token = token_;
        timeline_frame = timeline_frame_;
        if (prime_pending_)
            options = prime_options_;
        const auto window_end = checked_window_end(timeline_frame, options.lookahead_frames);
        if (!window_end.has_value()) {
            result.status = failure_code_t::invalid_request;
            return result;
        }
        result.materialize = bridge_.materialize_preview(*window_end, timeline_frame);
        const auto& materialized = *result.materialize;
        const auto& materialized_effect = materialized;
        if (!is_materialize_succeeded(materialized_effect.status)) {
            result.status = failure_code_t::materialize_failed;
            return result;
        }
    }
    result.decode = pump_preview_blocks(sources_, transport_, *token, timeline_frame, options);
    {
        const std::scoped_lock lock(mutex_);
        if (token_ != token)
            return preview_session_pump_effect_t{};
        if (result.has_ready_decode())
            prime_pending_ = false;
    }
    if (!result.has_ready_decode()) {
        result.status = failure_code_t::decode_failed;
        return result;
    }
    result.status = preview_session_pump_phase_t::ready;
    return result;
}

auto preview_session_t::mix_render_frames(std::uint64_t first_frame,
                                           std::span<float> interleaved_stereo)
    -> transport_mix_result_t {
    if (loop_region_ && first_frame >= loop_region_->end_frame)
        return {.status = std::monostate{}};
    auto render_samples = interleaved_stereo;
    if (loop_region_) {
        const auto remaining_frames = loop_region_->end_frame - first_frame;
        const auto samples_per_frame = static_cast<std::size_t>(output_channel_count);
        if (remaining_frames < interleaved_stereo.size() / samples_per_frame) {
            const auto samples = static_cast<std::size_t>(remaining_frames) * samples_per_frame;
            render_samples = interleaved_stereo.first(samples);
        }
    }
    return transport_.mix_frames(*token_, first_frame, render_samples);
}

auto preview_session_t::render_frames(std::span<float> interleaved_stereo,
                                      std::optional<std::uint64_t> expected_generation)
    -> session_render_effect_t {
    std::ranges::fill(interleaved_stereo, 0.0F);
    const auto samples_per_frame = static_cast<std::size_t>(output_channel_count);
    if (interleaved_stereo.empty() || interleaved_stereo.size() % samples_per_frame != 0 ||
        interleaved_stereo.size() / samples_per_frame > maximum_read_frames) {
        return session_render_effect_t::from(
            failure_code_t::invalid_request, 0,
            {.status = failure_code_t::invalid_request});
    }

    const std::scoped_lock lock(mutex_);
    if (expected_generation && *expected_generation != generation_)
        return session_render_effect_t::from(
            failure_code_t::stale_generation);
    if (!token_.has_value()) {
        return session_render_effect_t::from(
            session_render_phase_t::stopped, timeline_frame_,
            {.status = failure_code_t::stale_generation});
    }
    const auto first_frame = timeline_frame_;
    if (!transport_.is_current(*token_)) {
        clear_state();
        return session_render_effect_t::from(
            failure_code_t::stale_generation, first_frame);
    }
    if (prime_pending_) {
        return session_render_effect_t::from(session_render_phase_t::priming,
                                                     first_frame);
    }
    const auto mixed = mix_render_frames(first_frame, interleaved_stereo);
    if (mixed.status.has_failure(failure_code_t::stale_generation)) {
        clear_state();
        return session_render_effect_t::from(
            failure_code_t::stale_generation, first_frame, mixed);
    }
    if (mixed.status.has_failure()) {
        return session_render_effect_t::from(
            failure_code_t::transport_failed, first_frame, mixed);
    }
    if (mixed.frames > std::numeric_limits<std::uint64_t>::max() - timeline_frame_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        std::ranges::fill(interleaved_stereo, 0.0F);
        return session_render_effect_t::from(
            failure_code_t::transport_failed, first_frame, mixed);
    }
    timeline_frame_ += mixed.frames;
    publish_playhead();
    if (mixed.missing_blocks == 0 && !loop_region_ && bridge_.is_exhausted() &&
        transport_.scheduled_voice_count() == 0 && timeline_frame_ >= minimum_end_frame_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return session_render_effect_t::from(session_render_phase_t::complete,
                                                     first_frame, mixed);
    }
    return session_render_effect_t::from(mixed.missing_blocks == 0
                                                     ? session_render_state_t{session_render_phase_t::mixed}
                                                     : session_render_state_t{failure_code_t::underrun},
                                                 first_frame, mixed);
}

auto preview_session_t::stop(std::optional<std::uint64_t> expected_generation) noexcept
    -> transport_stop_result_t {
    const std::scoped_lock lock(mutex_);
    if (expected_generation && *expected_generation != generation_) return {};
    auto stopped = bridge_.stop();
    clear_state();
    return stopped;
}

auto preview_session_t::set_loop_region(std::optional<preview_loop_region_t> region) -> bool {
    if (region && region->begin_frame >= region->end_frame) return false;
    const std::scoped_lock lock(mutex_);
    loop_region_ = region;
    loop_region_set_.store(region.has_value(), std::memory_order_release);
    return true;
}

auto preview_session_t::loop_region() const -> std::optional<preview_loop_region_t> {
    const std::scoped_lock lock(mutex_);
    return loop_region_;
}

auto preview_session_t::set_layer_tick_frames(std::span<const std::uint64_t> frames) -> bool {
    const std::scoped_lock lock(mutex_);
    return bridge_.set_layer_tick_frames(frames, timeline_frame_);
}

void preview_session_t::set_muted_mix_groups(std::span<const std::uint64_t> groups) {
    transport_.set_muted_mix_groups(groups);
}

void preview_session_t::set_metronome_enabled(bool enabled) {
    const std::scoped_lock lock(mutex_);
    bridge_.set_metronome_enabled(enabled, timeline_frame_);
}

auto preview_session_t::is_metronome_enabled() const -> bool {
    return bridge_.is_metronome_enabled();
}

auto preview_session_t::set_metronome_volume(std::uint8_t volume_percent) noexcept -> bool {
    return bridge_.set_metronome_volume(volume_percent);
}

auto preview_session_t::set_metronome_count_in(std::uint8_t measures) noexcept -> bool {
    if (measures > 4) return false;
    metronome_count_in_measures_.store(measures, std::memory_order_release);
    return true;
}

auto preview_session_t::set_assist_grid(bool active,
    std::optional<preview_assist_click_grid_t> grid) -> bool {
    const std::scoped_lock lock(mutex_);
    return bridge_.set_assist_grid(active, grid, timeline_frame_);
}

auto preview_session_t::playhead() const noexcept -> playhead_state_t {
    for (int attempt = 0; attempt < playhead_snapshot_t::maximum_read_attempts; ++attempt) {
        const auto before = loop_restart_generation_.load(std::memory_order_acquire);
        const auto state = playhead_.read();
        if (!state.active || transport_.is_current({state.transport_generation}) || before == state.generation ||
            loop_restart_generation_.load(std::memory_order_acquire) == state.generation) return state;
        // The internal handoff may have finished since the first snapshot. A
        // changed token needs a fresh validation; an unchanged stale token does not.
        const auto latest = playhead_.read();
        if (latest.generation == state.generation && latest.transport_generation == state.transport_generation)
            return {.generation = state.generation};
    }
    return {.generation = playhead_.read().generation};
}

auto preview_session_t::is_active() const -> bool {
    return playhead().active;
}

auto preview_session_t::timeline_frame() const -> std::uint64_t {
    const auto playhead = this->playhead();
    const auto output = output_playhead_.read();
    if (playhead.active && output.active && output.generation == playhead.generation)
        return output.timeline_frame;
    const auto latency = output_latency_frames_.load(std::memory_order_acquire);
    const auto elapsed = playhead.timeline_frame - playhead.start_frame;
    return playhead.start_frame + (elapsed > latency ? elapsed - latency : 0);
}

void preview_session_t::publish_playhead() noexcept {
    playhead_.publish({.active = token_.has_value(),
                       .timeline_frame = timeline_frame_,
                       .start_frame = start_frame_,
                       .generation = generation_,
                       .transport_generation = token_ ? token_->generation : 0});
}

void preview_session_t::clear_state() noexcept {
    token_.reset();
    prime_pending_ = false;
    timeline_frame_ = 0;
    start_frame_ = 0;
    output_latency_frames_.store(0, std::memory_order_release);
    minimum_end_frame_ = 0;
    loop_region_.reset();
    loop_region_set_.store(false, std::memory_order_release);
    ++generation_;
    publish_playhead();
}

} // namespace wormedit::audio
