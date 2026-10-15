#include <wormedit/audio/transport.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace wormedit::audio {
namespace {

auto is_valid_source(const audio_source_identity_t& source) -> bool {
    return !source.asset_identity.empty() && !source.source_identity.empty();
}

auto checked_end(std::uint64_t start, std::uint64_t count) -> std::optional<std::uint64_t> {
    if (count > std::numeric_limits<std::uint64_t>::max() - start) {
        return std::nullopt;
    }
    return start + count;
}

auto is_valid_voice(const transport_voice_t& voice) -> bool {
    return voice.id != 0 &&
           static_cast<std::size_t>(voice.kind) < static_cast<std::size_t>(voice_kind_t::count) &&
           is_valid_source(voice.source) && voice.frame_count != 0 && std::isfinite(voice.gain) &&
           voice.gain >= 0.0F &&
           checked_end(voice.timeline_start_frame, voice.frame_count).has_value() &&
           checked_end(voice.source_start_frame, voice.frame_count).has_value();
}

struct transport_mix_context_t {
    decoded_block_cache_t& block_cache;
    const resident_snapshot_t* resident;
    std::uint64_t first_timeline_frame;
    std::uint64_t callback_end;
    std::span<float> interleaved_stereo;
    transport_mix_result_t& result;
    float click_preview_gain{1.0F};
};

struct transport_block_window_t {
    std::size_t offset_in_block;
    std::size_t output_offset;
    std::size_t requested;
    float gain;
};

auto mix_pcm_samples(std::span<float> output, std::span<const float> pcm,
                     const transport_block_window_t& window) -> std::size_t {
    const auto frame_count = pcm.size() / output_channel_count;
    if (window.offset_in_block >= frame_count) {
        return 0;
    }
    const auto available = std::min(window.requested, frame_count - window.offset_in_block);
    for (std::size_t frame = 0; frame < available; ++frame) {
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            const auto input_index =
                (window.offset_in_block + frame) * output_channel_count + channel;
            const auto output_index =
                (window.output_offset + frame) * output_channel_count + channel;
            output[output_index] += pcm[input_index] * window.gain;
        }
    }
    return available;
}

auto mix_block_samples(std::span<float> output, const decoded_audio_block_t& block,
                       const transport_block_window_t& window) -> std::size_t {
    const auto pcm = std::span<const float>{block.interleaved_stereo}.first(
        std::min(block.interleaved_stereo.size(), block.frame_count * output_channel_count));
    return mix_pcm_samples(output, pcm, window);
}

// Resident samples win over the block cache; a miss in both drops the voice.
auto mix_source_block(transport_mix_context_t& context, const transport_voice_t& voice,
                      std::uint64_t block_index, const transport_block_window_t& window)
    -> std::size_t {
    const audio_block_key_t key{.source = voice.source, .block_index = block_index};
    if (context.resident != nullptr) {
        const auto pcm = context.resident->block(key);
        if (!pcm.empty()) {
            return mix_pcm_samples(context.interleaved_stereo, pcm, window);
        }
    }
    std::size_t available{};
    const auto found = context.block_cache.with_block(key, [&](const decoded_audio_block_t& block) {
        available = mix_block_samples(context.interleaved_stereo, block, window);
    });
    return found ? available : 0;
}

auto mix_voice(transport_mix_context_t& context, const transport_voice_t& voice) -> bool {
    const auto voice_end = voice.timeline_start_frame + voice.frame_count;
    const auto overlap_start = std::max(context.first_timeline_frame, voice.timeline_start_frame);
    const auto overlap_end = std::min(context.callback_end, voice_end);
    if (overlap_start >= overlap_end) {
        return false;
    }
    ++context.result.voices_mixed;
    auto timeline_frame = overlap_start;
    while (timeline_frame < overlap_end) {
        const auto source_frame =
            voice.source_start_frame + (timeline_frame - voice.timeline_start_frame);
        const auto block_index = source_frame / maximum_read_frames;
        const auto offset_in_block = static_cast<std::size_t>(source_frame % maximum_read_frames);
        const transport_block_window_t window{
            .offset_in_block = offset_in_block,
            .output_offset =
                static_cast<std::size_t>(timeline_frame - context.first_timeline_frame),
            .requested = static_cast<std::size_t>(std::min<std::uint64_t>(
                overlap_end - timeline_frame, maximum_read_frames - offset_in_block)),
            .gain = voice.gain *
                ((voice.kind == voice_kind_t::click_preview || voice.kind == voice_kind_t::layer_assist)
                    ? context.click_preview_gain : 1.0F),
        };
        const auto available = mix_source_block(context, voice, block_index, window);
        if (available != window.requested) {
            ++context.result.missing_blocks;
            ++context.result.dropped_voices;
            if (voice.kind == voice_kind_t::music)
                ++context.result.dropped_music_voices;
            return true;
        }
        timeline_frame += window.requested;
    }
    return false;
}

auto clip_mix_samples(transport_mix_context_t& context) -> void {
    for (auto& sample : context.interleaved_stereo) {
        const auto clipped = std::clamp(sample, -1.0F, 1.0F);
        if (clipped != sample) {
            ++context.result.clipped_samples;
            sample = clipped;
        }
    }
}

} // namespace

struct transport_t::mix_operation_t {
    transport_t& owner;
    std::uint64_t first_timeline_frame;
    std::uint64_t callback_end;
    std::span<float> interleaved_stereo;
    transport_mix_result_t& result;

    void operator()() {
        const std::scoped_lock lock(owner.mutex_);
        if (owner.next_timeline_frame_.has_value() &&
            first_timeline_frame < *owner.next_timeline_frame_) {
            result.status = transport_mix_status_t::non_monotonic_callback;
            return;
        }
        result.status = transport_mix_status_t::mixed;
        transport_mix_context_t context{owner.block_cache_,   owner.resident_.get(),
                                        first_timeline_frame, callback_end,
                                        interleaved_stereo,   result};
        context.click_preview_gain =
            static_cast<float>(owner.click_volume_percent_.load(std::memory_order_acquire)) /
            100.0F;
        const auto clicks_enabled = owner.clicks_enabled_.load(std::memory_order_acquire);
        for (auto& entry : owner.scheduled_voices_) {
            if (entry.active && !owner.muted_mix_groups_.contains(entry.voice.mix_group) &&
                (entry.voice.kind != voice_kind_t::click_preview || clicks_enabled)) {
                if (mix_voice(context, entry.voice)) {
                    entry.active = false;
                    --owner.active_scheduled_voices_;
                }
            }
        }
        clip_mix_samples(context);
        for (auto& entry : owner.scheduled_voices_) {
            if (entry.active &&
                entry.voice.timeline_start_frame + entry.voice.frame_count <= callback_end) {
                entry.active = false;
                --owner.active_scheduled_voices_;
            }
        }
        owner.next_timeline_frame_ = callback_end;
    }
};

struct transport_t::snapshot_operation_t {
    transport_t& owner;
    std::size_t maximum_voices;
    active_voice_snapshot_outcome_t& result;

    void operator()() {
        const std::scoped_lock lock(owner.mutex_);
        if (owner.active_scheduled_voices_ > maximum_voices) {
            result = std::unexpected(active_voice_snapshot_status_t::capacity_exceeded);
            return;
        }
        auto& voices = result.emplace();
        voices.reserve(owner.active_scheduled_voices_);
        for (const auto& entry : owner.scheduled_voices_) {
            if (entry.active)
                voices.push_back(entry.voice);
        }
    }
};

auto transport_mix_result_t::from(transport_mix_result_t&& report) -> transport_mix_outcome_t {
    if (report.status != transport_mix_status_t::mixed)
        return std::unexpected(std::move(report));
    return std::move(report);
}

transport_t::transport_t(decoded_block_cache_t& block_cache, std::size_t maximum_scheduled_voices)
    : block_cache_(block_cache),
      decode_session_(std::max<std::size_t>(maximum_scheduled_voices, 1)),
      maximum_scheduled_voices_(maximum_scheduled_voices) {
    scheduled_voices_.reserve(maximum_scheduled_voices_);
}

void transport_t::set_muted_mix_groups(std::span<const std::uint64_t> groups) {
    std::unordered_set<std::uint64_t> next{groups.begin(), groups.end()};
    next.erase(0);
    {
        const std::scoped_lock lock(mutex_);
        muted_mix_groups_.swap(next);
    }
}

void transport_t::set_clicks_enabled(bool enabled) {
    clicks_enabled_.store(enabled, std::memory_order_release);
    if (enabled) return;
    clear_voices(voice_kind_t::click_preview);
}

void transport_t::clear_voices(voice_kind_t kind) {
    const std::scoped_lock lock(mutex_);
    for (auto& entry : scheduled_voices_) {
        if (entry.active && entry.voice.kind == kind) {
            entry.active = false;
            --active_scheduled_voices_;
        }
    }
}

auto transport_t::set_click_preview_volume(std::uint8_t volume_percent) noexcept -> bool {
    if (volume_percent > 100) return false;
    click_volume_percent_.store(volume_percent, std::memory_order_release);
    return true;
}

auto transport_t::start() -> std::optional<session_token_t> {
    const auto token = decode_session_.start();
    if (!token.has_value()) {
        return std::nullopt;
    }
    const std::scoped_lock lock(mutex_);
    scheduled_voices_.clear();
    active_scheduled_voices_ = 0;
    next_timeline_frame_.reset();
    return token;
}

auto transport_t::stop() noexcept -> transport_stop_result_t {
    // Session cancellation serializes with mix/schedule callbacks first.
    decode_session_.stop();
    const std::scoped_lock lock(mutex_);
    transport_stop_result_t result{.scheduled_voices = active_scheduled_voices_};
    for (const auto& entry : scheduled_voices_) {
        if (entry.active) {
            ++result.voices_by_kind[static_cast<std::size_t>(entry.voice.kind)];
        }
    }
    scheduled_voices_.clear();
    active_scheduled_voices_ = 0;
    next_timeline_frame_.reset();
    return result;
}

auto transport_t::is_current(session_token_t token) const noexcept -> bool {
    return decode_session_.is_current(token);
}

auto transport_t::decode_source_block(decoder_t& decoder, session_token_t token, audio_block_key_t key)
    -> block_decode_outcome_t {
    auto result =
        decode_cached_block(decoder, block_cache_, decode_session_, token, std::move(key));
    // Publication is only a generation handoff. The mixer addresses immutable
    // scheduled ranges and can now find the block directly in the shared LRU.
    static_cast<void>(decode_session_.take_ready(token));
    return result;
}

void transport_t::set_resident_snapshot(resident_snapshot_ptr_t snapshot) {
    {
        const std::scoped_lock lock(mutex_);
        resident_.swap(snapshot);
    }
    // `snapshot` now holds the previous value and is released here, off the lock.
}

auto transport_t::resident_block_hit(session_token_t token, const audio_block_key_t& key)
    -> std::optional<block_decode_result_t> {
    resident_snapshot_ptr_t snapshot;
    {
        const std::scoped_lock lock(mutex_);
        snapshot = resident_;
    }
    if (!snapshot) {
        return std::nullopt;
    }
    const auto pcm = snapshot->block(key);
    if (pcm.empty()) {
        return std::nullopt;
    }
    if (!is_current(token)) {
        return block_decode_result_t{.status = block_decode_status_t::stale_generation};
    }
    return block_decode_result_t{.status = block_decode_status_t::cache_hit,
                                 .frames = pcm.size() / output_channel_count,
                                 .decoder_status = read_status_t::success};
}

auto transport_t::schedule_voice(session_token_t token, transport_voice_t voice)
    -> voice_schedule_status_t {
    return schedule_voices(token, std::span<const transport_voice_t>{&voice, 1});
}

auto transport_t::schedule_voices(session_token_t token, std::span<const transport_voice_t> voices)
    -> voice_schedule_status_t {
    if (voices.empty() || !std::ranges::all_of(voices, is_valid_voice)) {
        return voice_schedule_status_t::invalid_voice;
    }
    for (std::size_t index = 0; index < voices.size(); ++index) {
        if (std::ranges::any_of(voices.subspan(index + 1), [&](const auto& candidate) {
                return candidate.id == voices[index].id;
            })) {
            return voice_schedule_status_t::duplicate_id;
        }
    }
    auto status = voice_schedule_status_t::stale_generation;
    const auto current = decode_session_.with_current(token, [&] {
        const std::scoped_lock lock(mutex_);
        if (std::ranges::any_of(voices, [&](const auto& voice) {
                return std::ranges::any_of(scheduled_voices_, [&](const auto& entry) {
                    return entry.active && entry.voice.id == voice.id;
                });
            })) {
            status = voice_schedule_status_t::duplicate_id;
        } else if (voices.size() > maximum_scheduled_voices_ - active_scheduled_voices_) {
            status = voice_schedule_status_t::capacity_exceeded;
        } else {
            for (const auto& voice : voices) {
                const auto reusable = std::ranges::find_if(
                    scheduled_voices_, [](const auto& entry) { return !entry.active; });
                if (reusable != scheduled_voices_.end()) {
                    reusable->voice = voice;
                    reusable->active = true;
                } else {
                    scheduled_voices_.push_back({.voice = voice, .active = true});
                }
            }
            active_scheduled_voices_ += voices.size();
            status = voice_schedule_status_t::scheduled;
        }
    });
    return current ? status : voice_schedule_status_t::stale_generation;
}

auto transport_t::mix_frames(session_token_t token, std::uint64_t first_timeline_frame,
                             std::span<float> interleaved_stereo) -> transport_mix_outcome_t {
    std::ranges::fill(interleaved_stereo, 0.0F);
    if (interleaved_stereo.size() % output_channel_count != 0) {
        return std::unexpected(
            transport_mix_result_t{.status = transport_mix_status_t::invalid_buffer});
    }
    const auto frame_count = interleaved_stereo.size() / output_channel_count;
    if (frame_count > std::numeric_limits<std::uint64_t>::max() - first_timeline_frame) {
        return std::unexpected(
            transport_mix_result_t{.status = transport_mix_status_t::timeline_overflow});
    }
    transport_mix_result_t result{
        .status = transport_mix_status_t::stale_generation,
        .frames = frame_count,
    };
    const auto callback_end = first_timeline_frame + frame_count;
    mix_operation_t operation{.owner = *this,
                              .first_timeline_frame = first_timeline_frame,
                              .callback_end = callback_end,
                              .interleaved_stereo = interleaved_stereo,
                              .result = result};
    const auto current = decode_session_.with_current(token, operation);
    if (!current) {
        std::ranges::fill(interleaved_stereo, 0.0F);
        result.status = transport_mix_status_t::stale_generation;
        result.voices_mixed = 0;
        result.missing_blocks = 0;
        result.dropped_voices = 0;
        result.dropped_music_voices = 0;
        result.clipped_samples = 0;
    }
    return transport_mix_result_t::from(std::move(result));
}

auto transport_t::scheduled_voice_count() const -> std::size_t {
    const std::scoped_lock lock(mutex_);
    return active_scheduled_voices_;
}

auto transport_t::block_cache_limits() const noexcept -> block_cache_limits_t {
    return block_cache_.limits();
}

auto transport_t::snapshot_voices(session_token_t token, std::size_t maximum_voices)
    -> active_voice_snapshot_outcome_t {
    active_voice_snapshot_outcome_t result{
        std::unexpected(active_voice_snapshot_status_t::stale_generation)};
    snapshot_operation_t operation{
        .owner = *this, .maximum_voices = maximum_voices, .result = result};
    if (!decode_session_.with_current(token, operation))
        result = std::unexpected(active_voice_snapshot_status_t::stale_generation);
    return result;
}

} // namespace wormedit::audio
