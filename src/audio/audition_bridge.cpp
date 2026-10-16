#include <wormedit/audio/audition_bridge.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace wormedit::audio {
namespace {

auto is_valid_clip(const audition_clip_t& clip) -> bool {
    return !clip.source.asset_identity.empty() && !clip.source.source_identity.empty() &&
           clip.frame_count != 0 &&
           clip.frame_count <=
               std::numeric_limits<std::uint64_t>::max() - clip.source_start_frame &&
           std::isfinite(clip.gain) && clip.gain >= 0.0F;
}

} // namespace

auto audition_transport_bridge_t::start_clips(std::span<const audition_clip_t> clips)
    -> audition_start_outcome_t {
    if (clips.empty()) {
        return std::unexpected(failure_code_t::invalid_request);
    }
    if (clips.size() > maximum_layers_) {
        return std::unexpected(failure_code_t::capacity_exceeded);
    }
    if (!std::ranges::all_of(clips, is_valid_clip)) {
        return std::unexpected(failure_code_t::invalid_data);
    }

    const auto token = transport_.start();
    if (!token.has_value()) {
        token_.reset();
        return std::unexpected(failure_code_t::transport_failed);
    }
    token_ = token;
    std::vector<transport_voice_t> voices;
    voices.reserve(clips.size());
    for (std::size_t index = 0; index < clips.size(); ++index) {
        const auto& clip = clips[index];
        voices.push_back({.id = static_cast<std::uint64_t>(index + 1),
                          .kind = voice_kind_t::click_preview,
                          .source = clip.source,
                          .source_start_frame = clip.source_start_frame,
                          .frame_count = clip.frame_count,
                          .gain = clip.gain});
    }
    const auto status = transport_.schedule_voices(*token, voices);
    if (status.has_failure()) {
        static_cast<void>(transport_.stop());
        token_.reset();
        return std::unexpected(status.has_failure(failure_code_t::capacity_exceeded)
                                   ? failure_code_t::capacity_exceeded
                                   : failure_code_t::invalid_state);
    }
    return started_audition_t{.token = *token, .scheduled_layers = clips.size()};
}

auto audition_transport_bridge_t::stop() noexcept -> transport_stop_result_t {
    auto result = transport_stop_result_t{};
    if (token_.has_value() && transport_.is_current(*token_)) {
        result = transport_.stop();
    }
    token_.reset();
    return result;
}

} // namespace wormedit::audio
