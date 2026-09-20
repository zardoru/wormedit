#include <wormedit/audio/audition_bridge.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit::audio {
namespace {

auto valid_clip(const audition_clip_t& clip) -> bool {
    return !clip.source.asset_identity.empty() && !clip.source.source_identity.empty() &&
           clip.frame_count != 0 &&
           clip.frame_count <=
               std::numeric_limits<std::uint64_t>::max() - clip.source_start_frame &&
           std::isfinite(clip.gain) && clip.gain >= 0.0F;
}

} // namespace

auto audition_transport_bridge_t::start(std::span<const audition_clip_t> clips)
    -> audition_start_result_t {
    if (clips.empty()) {
        return {.status = audition_start_status_t::empty, .token = std::nullopt};
    }
    if (clips.size() > maximum_layers_) {
        return {.status = audition_start_status_t::too_many_layers, .token = std::nullopt};
    }
    if (!std::ranges::all_of(clips, valid_clip)) {
        return {.status = audition_start_status_t::invalid_clip, .token = std::nullopt};
    }

    const auto token = transport_.start();
    if (!token.has_value()) {
        token_.reset();
        return {.status = audition_start_status_t::transport_start_failed,
                .token = std::nullopt};
    }
    token_ = token;
    std::size_t scheduled{};
    for (const auto& clip : clips) {
        const auto status = transport_.schedule_voice(
            *token,
            {.id = static_cast<std::uint64_t>(scheduled + 1),
             .kind = voice_kind_t::click_preview,
             .source = clip.source,
             .source_start_frame = clip.source_start_frame,
             .frame_count = clip.frame_count,
             .gain = clip.gain});
        if (status != voice_schedule_status_t::scheduled) {
            static_cast<void>(transport_.stop());
            token_.reset();
            return {
                .status = status == voice_schedule_status_t::capacity_exceeded
                              ? audition_start_status_t::transport_capacity_exceeded
                              : audition_start_status_t::transport_rejected_clip,
                .token = std::nullopt,
                .scheduled_layers = scheduled,
            };
        }
        ++scheduled;
    }
    return {.status = audition_start_status_t::started,
            .token = token,
            .scheduled_layers = scheduled};
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
