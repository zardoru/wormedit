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

} // namespace

auto pump_audition_lookahead(decoded_source_registry_t& sources, transport_t& transport,
                             session_token_t token, std::span<const audition_clip_t> clips,
                             std::uint64_t timeline_frame, audition_pump_options_t options)
    -> audition_pump_result_t {
    const auto current = transport.is_current(token);
    if (!current) {
        return {.status = audition_pump_status_t::stale_generation,
                .requested_blocks = 0,
                .decoded_blocks = 0,
                .cached_blocks = 0,
                .failure = std::nullopt};
    }
    if (clips.empty() || clips.size() > maximum_audition_pump_clips ||
        options.lookahead_frames == 0 || options.lookahead_frames > maximum_read_frames ||
        options.maximum_blocks == 0 || options.maximum_blocks > maximum_audition_pump_blocks) {
        return {.status = audition_pump_status_t::invalid_request,
                .requested_blocks = 0,
                .decoded_blocks = 0,
                .cached_blocks = 0,
                .failure = std::nullopt};
    }
    const auto window_end = checked_end(timeline_frame, options.lookahead_frames);
    if (!window_end.has_value()) {
        return {.status = audition_pump_status_t::invalid_request,
                .requested_blocks = 0,
                .decoded_blocks = 0,
                .cached_blocks = 0,
                .failure = std::nullopt};
    }

    std::vector<audio_block_key_t> keys;
    keys.reserve(options.maximum_blocks);
    bool active{};
    for (const auto& clip : clips) {
        if (!valid_source(clip.source) || clip.frame_count == 0 || !std::isfinite(clip.gain) ||
            clip.gain < 0.0F || !checked_end(clip.source_start_frame, clip.frame_count)) {
            return {.status = audition_pump_status_t::invalid_request,
                    .requested_blocks = 0,
                    .decoded_blocks = 0,
                    .cached_blocks = 0,
                    .failure = std::nullopt};
        }
        if (timeline_frame >= clip.frame_count) {
            continue;
        }
        active = true;
        const auto clip_window_end = std::min<std::uint64_t>(*window_end, clip.frame_count);
        const auto source_begin = clip.source_start_frame + timeline_frame;
        const auto source_end = clip.source_start_frame + clip_window_end;
        const auto first_block = source_begin / maximum_read_frames;
        const auto last_block = (source_end - 1) / maximum_read_frames;
        for (auto block = first_block;; ++block) {
            const audio_block_key_t key{clip.source, block};
            if (std::ranges::find(keys, key) == keys.end()) {
                if (keys.size() >= options.maximum_blocks) {
                    return {.status = audition_pump_status_t::capacity_exceeded,
                            .requested_blocks = keys.size() + 1,
                            .decoded_blocks = 0,
                            .cached_blocks = 0,
                            .failure = std::nullopt};
                }
                keys.push_back(key);
            }
            if (block == last_block) {
                break;
            }
        }
    }
    if (!active) {
        return {.status = audition_pump_status_t::complete,
                .requested_blocks = 0,
                .decoded_blocks = 0,
                .cached_blocks = 0,
                .failure = std::nullopt};
    }

    audition_pump_result_t result{
        .status = audition_pump_status_t::ready,
        .requested_blocks = keys.size(),
        .decoded_blocks = 0,
        .cached_blocks = 0,
        .failure = std::nullopt,
    };
    for (const auto& key : keys) {
        const auto decoded = sources.decode_block(transport, token, key);
        if (decoded.status == block_decode_status_t::decoded) {
            ++result.decoded_blocks;
        } else if (decoded.status == block_decode_status_t::cache_hit) {
            ++result.cached_blocks;
        } else {
            result.status = decode_failure_status(decoded.status);
            result.failure = decoded;
            return result;
        }
    }
    return result;
}

} // namespace wormedit::audio
