#include <wormedit/audio/audition_pump.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace wormedit::audio {
namespace {

auto valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto checked_end(std::uint64_t begin, std::uint64_t count) -> std::optional<std::uint64_t> {
    if (count > std::numeric_limits<std::uint64_t>::max() - begin) {
        return std::nullopt;
    }
    return begin + count;
}

auto decode_failure_status(block_decode_status_t status) -> audition_pump_status_t {
    switch (status) {
    case block_decode_status_t::stale_generation:
        return audition_pump_status_t::stale_generation;
    case block_decode_status_t::invalid_key:
    case block_decode_status_t::decoder_unavailable:
        return audition_pump_status_t::source_unavailable;
    case block_decode_status_t::cache_hit:
    case block_decode_status_t::decoded:
        break;
    case block_decode_status_t::beyond_end:
    case block_decode_status_t::seek_failed:
    case block_decode_status_t::decode_failed:
    case block_decode_status_t::cache_capacity_exceeded:
    case block_decode_status_t::session_queue_full:
        return audition_pump_status_t::decode_failed;
    }
    return audition_pump_status_t::decode_failed;
}

auto empty_result(audition_pump_status_t status) -> audition_pump_result_t {
    return {.status = status,
            .requested_blocks = 0,
            .decoded_blocks = 0,
            .cached_blocks = 0,
            .failure = std::nullopt};
}

auto valid_options(std::span<const audition_clip_t> clips,
                   audition_pump_options_t options) -> bool {
    return !clips.empty() && clips.size() <= maximum_audition_pump_clips &&
           options.lookahead_frames != 0 && options.lookahead_frames <= maximum_read_frames &&
           options.maximum_blocks != 0 && options.maximum_blocks <= maximum_audition_pump_blocks;
}

auto valid_clip(const audition_clip_t& clip) -> bool {
    return valid_source(clip.source) && clip.frame_count != 0 && std::isfinite(clip.gain) &&
           clip.gain >= 0.0F && checked_end(clip.source_start_frame, clip.frame_count).has_value();
}

struct audition_key_plan_t {
    audition_pump_status_t status{audition_pump_status_t::ready};
    std::vector<audio_block_key_t> keys;
    bool active{};
};

auto append_clip_keys(const audition_clip_t& clip, std::uint64_t timeline_frame,
                      std::uint64_t window_end, std::size_t maximum_blocks,
                      audition_key_plan_t& plan) -> bool {
    if (timeline_frame >= clip.frame_count) {
        return true;
    }
    plan.active = true;
    const auto clip_window_end = std::min<std::uint64_t>(window_end, clip.frame_count);
    const auto source_begin = clip.source_start_frame + timeline_frame;
    const auto source_end = clip.source_start_frame + clip_window_end;
    const auto first_block = source_begin / maximum_read_frames;
    const auto last_block = (source_end - 1) / maximum_read_frames;
    for (auto block = first_block;; ++block) {
        const audio_block_key_t key{clip.source, block};
        if (std::ranges::find(plan.keys, key) == plan.keys.end()) {
            if (plan.keys.size() >= maximum_blocks) {
                plan.status = audition_pump_status_t::capacity_exceeded;
                return false;
            }
            plan.keys.push_back(key);
        }
        if (block == last_block) {
            return true;
        }
    }
}

auto plan_audition_keys(std::span<const audition_clip_t> clips, std::uint64_t timeline_frame,
                        std::uint64_t window_end, std::size_t maximum_blocks)
    -> audition_key_plan_t {
    audition_key_plan_t plan;
    plan.keys.reserve(maximum_blocks);
    for (const auto& clip : clips) {
        if (!valid_clip(clip)) {
            plan.status = audition_pump_status_t::invalid_request;
            return plan;
        }
        if (!append_clip_keys(clip, timeline_frame, window_end, maximum_blocks, plan)) {
            return plan;
        }
    }
    return plan;
}

auto decode_audition_plan(decoded_source_registry_t& sources, transport_t& transport,
                          session_token_t token, const audition_key_plan_t& plan)
    -> audition_pump_result_t {
    audition_pump_result_t result{.status = audition_pump_status_t::ready,
                                  .requested_blocks = plan.keys.size(),
                                  .decoded_blocks = 0,
                                  .cached_blocks = 0,
                                  .failure = std::nullopt};
    for (const auto& key : plan.keys) {
        const auto decoded = sources.decode_block(transport, token, key);
        if (decoded.status == block_decode_status_t::decoded) {
            ++result.decoded_blocks;
            continue;
        }
        if (decoded.status == block_decode_status_t::cache_hit) {
            ++result.cached_blocks;
            continue;
        }
        result.status = decode_failure_status(decoded.status);
        result.failure = decoded;
        return result;
    }
    return result;
}

} // namespace

auto pump_audition_lookahead(decoded_source_registry_t& sources, transport_t& transport,
                             session_token_t token, std::span<const audition_clip_t> clips,
                             std::uint64_t timeline_frame, audition_pump_options_t options)
    -> audition_pump_result_t {
    if (!transport.is_current(token)) {
        return empty_result(audition_pump_status_t::stale_generation);
    }
    if (!valid_options(clips, options)) {
        return empty_result(audition_pump_status_t::invalid_request);
    }
    const auto window_end = checked_end(timeline_frame, options.lookahead_frames);
    if (!window_end.has_value()) {
        return empty_result(audition_pump_status_t::invalid_request);
    }
    const auto plan = plan_audition_keys(clips, timeline_frame, *window_end,
                                         options.maximum_blocks);
    if (plan.status != audition_pump_status_t::ready) {
        auto result = empty_result(plan.status);
        if (plan.status == audition_pump_status_t::capacity_exceeded) {
            result.requested_blocks = options.maximum_blocks + 1U;
        }
        return result;
    }
    if (!plan.active) {
        return empty_result(audition_pump_status_t::complete);
    }
    return decode_audition_plan(sources, transport, token, plan);
}

} // namespace wormedit::audio
