#include <wormedit/audio/audition_session.h>

#include <algorithm>
#include <limits>
#include <ranges>

namespace wormedit::audio {

auto audition_session_t::start(std::span<const audition_clip_t> clips)
    -> audition_session_start_result_t {
    const std::scoped_lock lock(mutex_);
    const auto transport = bridge_.start(clips);
    if (!transport) {
        return {.status = audition_session_start_status_t::transport_failed,
                .token = std::nullopt,
                .transport = transport,
                .prime = std::nullopt};
    }
    const auto prime = pump_audition_lookahead(sources_, transport_, *transport.token, clips, 0);
    if (!prime) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return {.status = audition_session_start_status_t::prime_failed,
                .token = std::nullopt,
                .transport = transport,
                .prime = prime};
    }

    token_ = transport.token;
    clips_.assign(clips.begin(), clips.end());
    timeline_frame_ = 0;
    duration_frames_ = std::ranges::max(clips_, {}, &audition_clip_t::frame_count).frame_count;
    return {.status = audition_session_start_status_t::started,
            .token = token_,
            .transport = transport,
            .prime = prime};
}

auto audition_session_t::pump(audition_pump_options_t options) -> audition_pump_result_t {
    std::optional<session_token_t> token;
    std::vector<audition_clip_t> clips;
    std::uint64_t timeline_frame{};
    {
        const std::scoped_lock lock(mutex_);
        token = token_;
        clips = clips_;
        timeline_frame = timeline_frame_;
    }
    if (!token.has_value()) {
        return {.status = audition_pump_status_t::stale_generation,
                .requested_blocks = 0,
                .decoded_blocks = 0,
                .cached_blocks = 0,
                .failure = std::nullopt};
    }
    return pump_audition_lookahead(sources_, transport_, *token, clips, timeline_frame, options);
}

auto audition_session_t::render(std::span<float> interleaved_stereo)
    -> audition_session_render_result_t {
    std::ranges::fill(interleaved_stereo, 0.0F);
    const auto samples_per_frame = static_cast<std::size_t>(output_channel_count);
    if (interleaved_stereo.empty() || interleaved_stereo.size() % samples_per_frame != 0 ||
        interleaved_stereo.size() / samples_per_frame > maximum_read_frames) {
        return {.status = audition_session_render_status_t::invalid_buffer,
                .first_timeline_frame = 0,
                .mix = {.status = transport_mix_status_t::invalid_buffer}};
    }

    const std::scoped_lock lock(mutex_);
    if (!token_.has_value()) {
        return {.status = audition_session_render_status_t::stopped,
                .first_timeline_frame = timeline_frame_,
                .mix = {.status = transport_mix_status_t::stale_generation}};
    }
    const auto first_frame = timeline_frame_;
    auto mixed = transport_.mix(*token_, first_frame, interleaved_stereo);
    if (mixed.status == transport_mix_status_t::stale_generation) {
        clear_state();
        return {.status = audition_session_render_status_t::stale_generation,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    if (mixed.status != transport_mix_status_t::mixed) {
        return {.status = audition_session_render_status_t::transport_failed,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    if (mixed.frames > std::numeric_limits<std::uint64_t>::max() - timeline_frame_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        std::ranges::fill(interleaved_stereo, 0.0F);
        return {.status = audition_session_render_status_t::transport_failed,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    timeline_frame_ += mixed.frames;
    if (timeline_frame_ >= duration_frames_) {
        static_cast<void>(bridge_.stop());
        clear_state();
        return {.status = audition_session_render_status_t::complete,
                .first_timeline_frame = first_frame,
                .mix = mixed};
    }
    return {.status = mixed.missing_blocks == 0 ? audition_session_render_status_t::mixed
                                                : audition_session_render_status_t::underrun,
            .first_timeline_frame = first_frame,
            .mix = mixed};
}

auto audition_session_t::stop() noexcept -> transport_stop_result_t {
    const std::scoped_lock lock(mutex_);
    auto stopped = bridge_.stop();
    clear_state();
    return stopped;
}

auto audition_session_t::active() const -> bool {
    const std::scoped_lock lock(mutex_);
    return token_.has_value();
}

auto audition_session_t::timeline_frame() const -> std::uint64_t {
    const std::scoped_lock lock(mutex_);
    return timeline_frame_;
}

void audition_session_t::clear_state() noexcept {
    token_.reset();
    clips_.clear();
    timeline_frame_ = 0;
    duration_frames_ = 0;
}

} // namespace wormedit::audio
