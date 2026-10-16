#include <wormedit/audio/audition_session.h>

#include <algorithm>
#include <limits>
#include <ranges>

namespace wormedit::audio {

auto audition_session_t::start_session(std::span<const audition_clip_t> clips)
    -> audition_session_start_outcome_t {
    const std::scoped_lock lock(mutex_);
    const auto transport = bridge_.start_clips(clips);
    if (!transport) {
        return std::unexpected(audition_session_start_error_t{
            .code = failure_code_t::transport_failed, .transport = transport});
    }
    const auto prime = prime_resident_audition_blocks(transport_, transport.value().token, clips);
    prime_pending_ = !prime.has_value();

    token_ = transport.value().token;
    clips_.assign(clips.begin(), clips.end());
    timeline_frame_ = 0;
    duration_frames_ = std::ranges::max(clips_, {}, &audition_clip_t::frame_count).frame_count;
    ++generation_;
    publish_playhead();
    return started_audition_session_t{.token = *token_, .transport = transport, .prime = prime};
}

auto audition_session_t::snapshot_pump_state() const -> pump_state_t {
    const std::scoped_lock lock(mutex_);
    return {token_, clips_, timeline_frame_};
}

auto audition_session_t::complete_pump(session_token_t token, audition_pump_outcome_t result)
    -> audition_pump_outcome_t {
    const std::scoped_lock lock(mutex_);
    if (token_ != token)
        return std::unexpected(
            audition_pump_effect_t{.status = failure_code_t::stale_generation});
    if (result) prime_pending_ = false;
    return result;
}

auto audition_session_t::pump(audition_pump_options_t options) -> audition_pump_outcome_t {
    const auto state = snapshot_pump_state();
    if (!state.token)
        return std::unexpected(
            audition_pump_effect_t{.status = failure_code_t::stale_generation});
    auto result = pump_audition_blocks(sources_, transport_, *state.token, state.clips,
                                          state.timeline_frame, options);
    return complete_pump(*state.token, std::move(result));
}

auto audition_session_t::render_frames(std::span<float> interleaved_stereo)
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
    if (prime_pending_)
        return session_render_effect_t::from(session_render_phase_t::priming,
                                                      first_frame);
    const auto mixed = transport_.mix_frames(*token_, first_frame, interleaved_stereo);
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
    if (timeline_frame_ >= duration_frames_) {
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

auto audition_session_t::stop() noexcept -> transport_stop_result_t {
    const std::scoped_lock lock(mutex_);
    auto stopped = bridge_.stop();
    clear_state();
    return stopped;
}

auto audition_session_t::playhead() const noexcept -> playhead_state_t {
    const auto state = playhead_.read();
    if (state.active && !transport_.is_current({state.transport_generation})) {
        return {.generation = state.generation};
    }
    return state;
}

auto audition_session_t::is_active() const -> bool {
    return playhead().active;
}

auto audition_session_t::timeline_frame() const -> std::uint64_t {
    return playhead().timeline_frame;
}

void audition_session_t::publish_playhead() noexcept {
    playhead_.publish({.active = token_.has_value(),
                       .timeline_frame = timeline_frame_,
                       .start_frame = 0,
                       .generation = generation_,
                       .transport_generation = token_ ? token_->generation : 0});
}

void audition_session_t::clear_state() noexcept {
    token_.reset();
    prime_pending_ = false;
    clips_.clear();
    timeline_frame_ = 0;
    duration_frames_ = 0;
    ++generation_;
    publish_playhead();
}

} // namespace wormedit::audio
