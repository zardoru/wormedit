#include <wormedit/audio/preview_pump.h>

#include <algorithm>
#include <limits>
#include <ranges>
#include <vector>

namespace wormedit::audio {
namespace {

auto checked_end(std::uint64_t begin, std::uint64_t count) -> std::optional<std::uint64_t> {
    if (count > std::numeric_limits<std::uint64_t>::max() - begin)
        return std::nullopt;
    return begin + count;
}

auto failure_status(block_decode_status_t status) -> preview_pump_status_t {
    switch (status) {
    case block_decode_status_t::stale_generation:
        return preview_pump_status_t::stale_generation;
    case block_decode_status_t::invalid_key:
    case block_decode_status_t::decoder_unavailable:
        return preview_pump_status_t::source_unavailable;
    case block_decode_status_t::cache_hit:
    case block_decode_status_t::decoded:
        break;
    case block_decode_status_t::beyond_end:
    case block_decode_status_t::seek_failed:
    case block_decode_status_t::decode_failed:
    case block_decode_status_t::cache_capacity_exceeded:
    case block_decode_status_t::session_queue_full:
        return preview_pump_status_t::decode_failed;
    }
    return preview_pump_status_t::decode_failed;
}

auto empty_result(preview_pump_status_t status) -> preview_pump_result_t {
    auto result = preview_pump_result_t{};
    result.status = status;
    return result;
}

struct preview_key_plan_t {
    preview_pump_status_t status{preview_pump_status_t::ready};
    std::vector<audio_block_key_t> keys;
    std::size_t overlapping_voices{};
};

auto append_voice_keys(const transport_voice_t& voice, std::uint64_t timeline_frame,
                       std::uint64_t window_end, std::size_t maximum_blocks,
                       preview_key_plan_t& plan) -> bool {
    const auto voice_end = checked_end(voice.timeline_start_frame, voice.frame_count);
    if (!voice_end.has_value()) {
        plan.status = preview_pump_status_t::invalid_request;
        return false;
    }
    const auto overlap_begin = std::max(timeline_frame, voice.timeline_start_frame);
    const auto overlap_end = std::min(window_end, *voice_end);
    if (overlap_begin >= overlap_end) {
        return true;
    }
    ++plan.overlapping_voices;
    const auto source_begin = voice.source_start_frame + (overlap_begin - voice.timeline_start_frame);
    const auto source_end = voice.source_start_frame + (overlap_end - voice.timeline_start_frame);
    const auto first_block = source_begin / maximum_read_frames;
    const auto last_block = (source_end - 1) / maximum_read_frames;
    for (auto block = first_block;; ++block) {
        const audio_block_key_t key{voice.source, block};
        if (std::ranges::find(plan.keys, key) == plan.keys.end()) {
            if (plan.keys.size() >= maximum_blocks) {
                plan.status = preview_pump_status_t::capacity_exceeded;
                return false;
            }
            plan.keys.push_back(key);
        }
        if (block == last_block) {
            return true;
        }
    }
}

auto plan_preview_keys(std::span<const transport_voice_t> voices, std::uint64_t timeline_frame,
                       std::uint64_t window_end, std::size_t maximum_blocks)
    -> preview_key_plan_t {
    preview_key_plan_t plan;
    plan.keys.reserve(maximum_blocks);
    for (const auto& voice : voices) {
        if (!append_voice_keys(voice, timeline_frame, window_end, maximum_blocks, plan)) {
            return plan;
        }
    }
    return plan;
}

auto decode_preview_plan(decoded_source_registry_t& sources, transport_t& transport,
                         session_token_t token, const preview_key_plan_t& plan,
                         preview_pump_result_t result) -> preview_pump_result_t {
    result.requested_blocks = plan.keys.size();
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
        result.status = failure_status(decoded.status);
        result.failure = decoded;
        return result;
    }
    return result;
}

} // namespace

auto pump_preview_lookahead(decoded_source_registry_t& sources, transport_t& transport,
                            session_token_t token, std::uint64_t timeline_frame,
                            preview_pump_options_t options) -> preview_pump_result_t {
    if (options.lookahead_frames == 0 || options.lookahead_frames > maximum_read_frames ||
        options.maximum_voices == 0 || options.maximum_voices > maximum_preview_pump_voices ||
        options.maximum_blocks == 0 || options.maximum_blocks > maximum_preview_pump_blocks) {
        return empty_result(preview_pump_status_t::invalid_request);
    }
    const auto window_end = checked_end(timeline_frame, options.lookahead_frames);
    if (!window_end.has_value())
        return empty_result(preview_pump_status_t::invalid_request);

    auto snapshot = transport.snapshot_active_voices(token, options.maximum_voices);
    if (snapshot.status == active_voice_snapshot_status_t::stale_generation)
        return empty_result(preview_pump_status_t::stale_generation);
    if (snapshot.status == active_voice_snapshot_status_t::capacity_exceeded) {
        auto result = empty_result(preview_pump_status_t::capacity_exceeded);
        result.active_voices = options.maximum_voices + 1;
        return result;
    }

    const auto plan = plan_preview_keys(snapshot.voices, timeline_frame, *window_end,
                                        options.maximum_blocks);
    if (plan.status != preview_pump_status_t::ready) {
        auto result = empty_result(plan.status);
        result.active_voices = snapshot.voices.size();
        result.overlapping_voices = plan.overlapping_voices;
        if (plan.status == preview_pump_status_t::capacity_exceeded) {
            result.requested_blocks = options.maximum_blocks + 1U;
        }
        return result;
    }
    auto result = empty_result(preview_pump_status_t::ready);
    result.active_voices = snapshot.voices.size();
    result.overlapping_voices = plan.overlapping_voices;
    return decode_preview_plan(sources, transport, token, plan, std::move(result));
}

} // namespace wormedit::audio
