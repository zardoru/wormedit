#include <wormedit/audio/transport.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace wormedit::audio {
namespace {

auto valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto checked_end(std::uint64_t start, std::uint64_t count)
    -> std::optional<std::uint64_t> {
    if (count > std::numeric_limits<std::uint64_t>::max() - start) {
        return std::nullopt;
    }
    return start + count;
}

auto valid_voice(const transport_voice_t& voice) -> bool {
    return voice.id != 0 &&
           static_cast<std::size_t>(voice.kind) <
               static_cast<std::size_t>(voice_kind_t::count) &&
           valid_source(voice.source) && voice.frame_count != 0 &&
           std::isfinite(voice.gain) && voice.gain >= 0.0F &&
           checked_end(voice.timeline_start_frame, voice.frame_count).has_value() &&
           checked_end(voice.source_start_frame, voice.frame_count).has_value();
}

} // namespace

transport_t::transport_t(decoded_block_cache_t& block_cache,
                         std::size_t maximum_scheduled_voices)
    : block_cache_(block_cache),
      decode_session_(std::max<std::size_t>(maximum_scheduled_voices, 1)),
      maximum_scheduled_voices_(maximum_scheduled_voices) {}

auto transport_t::start() -> std::optional<session_token_t> {
    const auto token = decode_session_.start();
    if (!token.has_value()) {
        return std::nullopt;
    }
    const std::scoped_lock lock(mutex_);
    scheduled_voices_.clear();
    next_timeline_frame_.reset();
    return token;
}

auto transport_t::stop() noexcept -> transport_stop_result_t {
    // Session cancellation serializes with mix/schedule callbacks first.
    decode_session_.stop();
    const std::scoped_lock lock(mutex_);
    transport_stop_result_t result{.scheduled_voices = scheduled_voices_.size()};
    for (const auto& voice : scheduled_voices_) {
        ++result.voices_by_kind[static_cast<std::size_t>(voice.kind)];
    }
    scheduled_voices_.clear();
    next_timeline_frame_.reset();
    return result;
}

auto transport_t::decode_block(decoder_t& decoder, session_token_t token,
                               audio_block_key_t key) -> block_decode_result_t {
    auto result = decode_and_cache_block(decoder, block_cache_, decode_session_, token,
                                         std::move(key));
    // Publication is only a generation handoff. The mixer addresses immutable
    // scheduled ranges and can now find the block directly in the shared LRU.
    static_cast<void>(decode_session_.take_ready(token));
    return result;
}

auto transport_t::schedule_voice(session_token_t token, transport_voice_t voice)
    -> voice_schedule_status_t {
    if (!valid_voice(voice)) {
        return voice_schedule_status_t::invalid_voice;
    }
    auto status = voice_schedule_status_t::stale_generation;
    const auto current = decode_session_.with_current(token, [&] {
        const std::scoped_lock lock(mutex_);
        if (std::ranges::find(scheduled_voices_, voice.id,
                              &transport_voice_t::id) != scheduled_voices_.end()) {
            status = voice_schedule_status_t::duplicate_id;
        } else if (scheduled_voices_.size() >= maximum_scheduled_voices_) {
            status = voice_schedule_status_t::capacity_exceeded;
        } else {
            scheduled_voices_.push_back(std::move(voice));
            status = voice_schedule_status_t::scheduled;
        }
    });
    return current ? status : voice_schedule_status_t::stale_generation;
}

auto transport_t::mix(session_token_t token, std::uint64_t first_timeline_frame,
                      std::span<float> interleaved_stereo) -> transport_mix_result_t {
    std::ranges::fill(interleaved_stereo, 0.0F);
    if (interleaved_stereo.size() % output_channel_count != 0) {
        return {.status = transport_mix_status_t::invalid_buffer};
    }
    const auto frame_count = interleaved_stereo.size() / output_channel_count;
    if (frame_count > std::numeric_limits<std::uint64_t>::max() -
                          first_timeline_frame) {
        return {.status = transport_mix_status_t::timeline_overflow};
    }
    transport_mix_result_t result{
        .status = transport_mix_status_t::stale_generation,
        .frames = frame_count,
    };
    const auto callback_end = first_timeline_frame + frame_count;
    const auto current = decode_session_.with_current(token, [&] {
        const std::scoped_lock lock(mutex_);
        if (next_timeline_frame_.has_value() &&
            first_timeline_frame < *next_timeline_frame_) {
            result.status = transport_mix_status_t::non_monotonic_callback;
            return;
        }
        result.status = transport_mix_status_t::mixed;
        for (const auto& voice : scheduled_voices_) {
            const auto voice_end = voice.timeline_start_frame + voice.frame_count;
            const auto overlap_start = std::max(first_timeline_frame,
                                                voice.timeline_start_frame);
            const auto overlap_end = std::min(callback_end, voice_end);
            if (overlap_start >= overlap_end) {
                continue;
            }
            ++result.voices_mixed;
            auto timeline_frame = overlap_start;
            while (timeline_frame < overlap_end) {
                const auto source_frame = voice.source_start_frame +
                                          (timeline_frame - voice.timeline_start_frame);
                const auto block_index = source_frame / maximum_read_frames;
                const auto offset_in_block = static_cast<std::size_t>(
                    source_frame % maximum_read_frames);
                const auto requested = static_cast<std::size_t>(std::min<std::uint64_t>(
                    overlap_end - timeline_frame,
                    maximum_read_frames - offset_in_block));
                const auto block = block_cache_.find({
                    .source = voice.source,
                    .block_index = block_index,
                });
                if (!block.has_value() || offset_in_block >= block->frame_count) {
                    ++result.missing_blocks;
                    timeline_frame += requested;
                    continue;
                }
                const auto available = std::min(requested,
                                                block->frame_count - offset_in_block);
                const auto output_offset = static_cast<std::size_t>(
                    timeline_frame - first_timeline_frame);
                for (std::size_t frame = 0; frame < available; ++frame) {
                    for (std::size_t channel = 0; channel < output_channel_count;
                         ++channel) {
                        const auto input_index =
                            (offset_in_block + frame) * output_channel_count + channel;
                        const auto output_index =
                            (output_offset + frame) * output_channel_count + channel;
                        interleaved_stereo[output_index] +=
                            block->interleaved_stereo[input_index] * voice.gain;
                    }
                }
                if (available != requested) {
                    ++result.missing_blocks;
                }
                timeline_frame += requested;
            }
        }
        for (auto& sample : interleaved_stereo) {
            const auto clipped = std::clamp(sample, -1.0F, 1.0F);
            if (clipped != sample) {
                ++result.clipped_samples;
                sample = clipped;
            }
        }
        std::erase_if(scheduled_voices_, [callback_end](const auto& voice) {
            return voice.timeline_start_frame + voice.frame_count <= callback_end;
        });
        next_timeline_frame_ = callback_end;
    });
    if (!current) {
        std::ranges::fill(interleaved_stereo, 0.0F);
        result.status = transport_mix_status_t::stale_generation;
        result.voices_mixed = 0;
        result.missing_blocks = 0;
        result.clipped_samples = 0;
    }
    return result;
}

auto transport_t::scheduled_voice_count() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return scheduled_voices_.size();
}

} // namespace wormedit::audio
