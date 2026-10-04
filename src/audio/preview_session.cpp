#include <wormedit/audio/preview_session.h>

#include <limits>
#include <ranges>

namespace wormedit::audio {
namespace {

auto checked_window_end(std::uint64_t timeline_frame, std::size_t lookahead_frames)
    -> std::optional<std::uint64_t> {
    if (lookahead_frames > std::numeric_limits<std::uint64_t>::max() - timeline_frame)
        return std::nullopt;
    return timeline_frame + lookahead_frames;
}

auto materialize_succeeded(preview_bridge_pump_status_t status) -> bool {
    return status == preview_bridge_pump_status_t::ready ||
           status == preview_bridge_pump_status_t::capacity_limited;
}

} // namespace

auto preview_session_t::start(const preview_schedule_t& schedule,
                              std::span<const preview_object_source_binding_t> objects,
                              std::span<const preview_metronome_source_binding_t> metronome,
                              std::uint64_t seek_frame, preview_pump_options_t options,
                              std::uint64_t minimum_end_frame)
    -> preview_session_start_result_t {
    const std::scoped_lock lock(mutex_);
    const auto start_frame = loop_region_ ? loop_region_->begin_frame : seek_frame;
    auto started = bridge_.start(schedule, objects, metronome, start_frame);
    if (!started) {
        auto result = preview_session_start_result_t{};
        result.status = preview_session_start_status_t::bridge_failed;
        result.bridge = started;
        return result;
    }
    minimum_end_frame_ = minimum_end_frame;
    return prime_started(started, start_frame, options);
}

auto preview_session_t::seek(std::uint64_t sample_frame, preview_pump_options_t options)
    -> preview_session_start_result_t {
    const std::scoped_lock lock(mutex_);
    auto started = bridge_.seek(sample_frame);
    if (!started) {
        auto result = preview_session_start_result_t{};
        result.status = preview_session_start_status_t::bridge_failed;
        result.bridge = started;
        return result;
    }
    return prime_started(started, sample_frame, options);
}

auto preview_session_t::prime_started(preview_bridge_start_result_t started,
                                      std::uint64_t timeline_frame, preview_pump_options_t options)
    -> preview_session_start_result_t {
    auto result = preview_session_start_result_t{};
    result.bridge = started;
    const auto window_end = checked_window_end(timeline_frame, options.lookahead_frames);
    if (!window_end.has_value()) {
        static_cast<void>(bridge_.stop());
        clear_state();
        result.status = preview_session_start_status_t::materialize_failed;
        return result;
    }
    const auto materialized = bridge_.materialize_until(*window_end, timeline_frame);
    result.materialize = materialized;
    if (!materialize_succeeded(materialized.status)) {
        static_cast<void>(bridge_.stop());
        clear_state();
        result.status = preview_session_start_status_t::materialize_failed;
        return result;
    }
    const auto prime =
        pump_preview_lookahead(sources_, transport_, *started.token, timeline_frame, options);
    result.prime = prime;
    if (!prime) {
        static_cast<void>(bridge_.stop());
        clear_state();
        result.status = preview_session_start_status_t::prime_failed;
        return result;
    }
    token_ = started.token;
    timeline_frame_ = timeline_frame;
    start_frame_ = timeline_frame;
    output_latency_frames_.store(0, std::memory_order_release);
    ++generation_;
    publish_playhead();
    result.status = preview_session_start_status_t::started;
    result.token = token_;
    return result;
}

auto preview_session_t::pump(preview_pump_options_t options) -> preview_session_pump_result_t {
    std::optional<session_token_t> token;
    std::uint64_t timeline_frame{};
    preview_session_pump_result_t result;
    {
        const std::scoped_lock lock(mutex_);
        if (!token_.has_value())
            return result;
        if (loop_region_ && timeline_frame_ >= loop_region_->end_frame) {
            result.status = preview_session_pump_status_t::loop_boundary;
            return result;
        }
        token = token_;
        timeline_frame = timeline_frame_;
        const auto window_end = checked_window_end(timeline_frame, options.lookahead_frames);
        if (!window_end.has_value()) {
            result.status = preview_session_pump_status_t::invalid_timeline;
            return result;
        }
        result.materialize = bridge_.materialize_until(*window_end, timeline_frame);
        if (!materialize_succeeded(result.materialize->status)) {
            result.status = preview_session_pump_status_t::materialize_failed;
            return result;
        }
    }
    result.decode = pump_preview_lookahead(sources_, transport_, *token, timeline_frame, options);
    result.status = result.decode && static_cast<bool>(*result.decode)
                        ? preview_session_pump_status_t::ready
                        : preview_session_pump_status_t::decode_failed;
    return result;
}

auto preview_session_t::render(std::span<float> interleaved_stereo)
    -> preview_session_render_result_t {
    std::ranges::fill(interleaved_stereo, 0.0F);
    const auto samples_per_frame = static_cast<std::size_t>(output_channel_count);
    if (interleaved_stereo.empty() || interleaved_stereo.size() % samples_per_frame != 0 ||
        interleaved_stereo.size() / samples_per_frame > maximum_read_frames) {
        return {.status = preview_session_render_status_t::invalid_buffer,
                .first_timeline_frame = 0,
                .mix = {.status = transport_mix_status_t::invalid_buffer}};
    }

    const std::scoped_lock lock(mutex_);
    if (!token_.has_value()) {
        return {.status = preview_session_render_status_t::stopped,
                .first_timeline_frame = timeline_frame_,
                .mix = {.status = transport_mix_status_t::stale_generation}};
    }
    const auto first_frame = timeline_frame_;
    transport_mix_result_t mixed;
    if (loop_region_ && first_frame >= loop_region_->end_frame) {
        mixed.status = transport_mix_status_t::mixed;
    } else {
        auto render_samples = interleaved_stereo;
        if (loop_region_) {
            const auto remaining_frames = loop_region_->end_frame - first_frame;
            if (remaining_frames < interleaved_stereo.size() / samples_per_frame) {
                const auto samples = static_cast<std::size_t>(remaining_frames) * samples_per_frame;
                render_samples = interleaved_stereo.first(samples);
            }
        }
        mixed = transport_.mix(*token_, first_frame, render_samples);
    }
    if (mixed.status == transport_mix_status_t::stale_generation) {
        clear_state();
        return {.status = preview_session_render_status_t::stale_generation,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    if (mixed.status != transport_mix_status_t::mixed) {
        return {.status = preview_session_render_status_t::transport_failed,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    if (mixed.frames > std::numeric_limits<std::uint64_t>::max() - timeline_frame_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        std::ranges::fill(interleaved_stereo, 0.0F);
        return {.status = preview_session_render_status_t::transport_failed,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    timeline_frame_ += mixed.frames;
    publish_playhead();
    if (mixed.missing_blocks == 0 && !loop_region_ && bridge_.exhausted() &&
        transport_.scheduled_voice_count() == 0 &&
        timeline_frame_ >= minimum_end_frame_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return {.status = preview_session_render_status_t::complete,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    return {.status = mixed.missing_blocks == 0 ? preview_session_render_status_t::mixed
                                                : preview_session_render_status_t::underrun,
            .first_timeline_frame = first_frame,
            .mix = mixed};
}

auto preview_session_t::stop() noexcept -> transport_stop_result_t {
    const std::scoped_lock lock(mutex_);
    auto stopped = bridge_.stop();
    clear_state();
    return stopped;
}

auto preview_session_t::set_loop_region(std::optional<preview_loop_region_t> region) -> bool {
    if (region && region->begin_frame >= region->end_frame) return false;
    const std::scoped_lock lock(mutex_);
    loop_region_ = region;
    return true;
}

auto preview_session_t::loop_region() const -> std::optional<preview_loop_region_t> {
    const std::scoped_lock lock(mutex_);
    return loop_region_;
}

void preview_session_t::set_metronome_enabled(bool enabled) {
    const std::scoped_lock lock(mutex_);
    bridge_.set_metronome_enabled(enabled, timeline_frame_);
}

auto preview_session_t::metronome_enabled() const -> bool {
    return bridge_.metronome_enabled();
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

auto preview_session_t::active() const -> bool {
    return playhead_.read().active;
}

auto preview_session_t::timeline_frame() const -> std::uint64_t {
    const auto playhead = playhead_.read();
    const auto latency = output_latency_frames_.load(std::memory_order_acquire);
    const auto elapsed = playhead.timeline_frame - playhead.start_frame;
    return playhead.start_frame + (elapsed > latency ? elapsed - latency : 0);
}

void preview_session_t::publish_playhead() noexcept {
    playhead_.publish({.active = token_.has_value(),
                       .timeline_frame = timeline_frame_,
                       .start_frame = start_frame_,
                       .generation = generation_});
}

void preview_session_t::clear_state() noexcept {
    token_.reset();
    timeline_frame_ = 0;
    start_frame_ = 0;
    output_latency_frames_.store(0, std::memory_order_release);
    minimum_end_frame_ = 0;
    loop_region_.reset();
    ++generation_;
    publish_playhead();
}

} // namespace wormedit::audio
