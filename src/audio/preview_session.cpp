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
    auto started = bridge_.start(schedule, objects, metronome, seek_frame);
    if (!started) {
        auto result = preview_session_start_result_t{};
        result.status = preview_session_start_status_t::bridge_failed;
        result.bridge = started;
        return result;
    }
    minimum_end_frame_ = minimum_end_frame;
    return prime_started(started, seek_frame, options);
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
    const auto materialized = bridge_.materialize_until(*window_end);
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
        token = token_;
        timeline_frame = timeline_frame_;
        const auto window_end = checked_window_end(timeline_frame, options.lookahead_frames);
        if (!window_end.has_value()) {
            result.status = preview_session_pump_status_t::invalid_timeline;
            return result;
        }
        result.materialize = bridge_.materialize_until(*window_end);
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
    const auto mixed = transport_.mix(*token_, first_frame, interleaved_stereo);
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
    if (mixed.missing_blocks == 0 && bridge_.exhausted() &&
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

auto preview_session_t::active() const -> bool {
    const std::scoped_lock lock(mutex_);
    return token_.has_value();
}

auto preview_session_t::timeline_frame() const -> std::uint64_t {
    const std::scoped_lock lock(mutex_);
    return timeline_frame_;
}

void preview_session_t::clear_state() noexcept {
    token_.reset();
    timeline_frame_ = 0;
    minimum_end_frame_ = 0;
}

} // namespace wormedit::audio
