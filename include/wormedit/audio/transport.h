#pragma once

#include <wormedit/audio/block_cache.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace wormedit::audio {

enum class voice_kind_t : std::uint8_t {
    music,
    bgm,
    playable_note,
    click_preview,
    count,
};

struct transport_voice_t {
    std::uint64_t id{};
    voice_kind_t kind{voice_kind_t::playable_note};
    audio_source_identity_t source;
    std::uint64_t timeline_start_frame{};
    std::uint64_t source_start_frame{};
    std::uint64_t frame_count{};
    float gain{1.0F};

    auto operator==(const transport_voice_t&) const -> bool = default;
};

enum class voice_schedule_status_t {
    scheduled,
    stale_generation,
    invalid_voice,
    duplicate_id,
    capacity_exceeded,
};

enum class transport_mix_status_t {
    mixed,
    stale_generation,
    invalid_buffer,
    timeline_overflow,
    non_monotonic_callback,
};

struct transport_mix_result_t {
    transport_mix_status_t status{transport_mix_status_t::stale_generation};
    std::size_t frames{};
    std::size_t voices_mixed{};
    std::size_t missing_blocks{};
    std::size_t clipped_samples{};
};

struct transport_stop_result_t {
    std::size_t scheduled_voices{};
    std::array<std::size_t, static_cast<std::size_t>(voice_kind_t::count)> voices_by_kind{};
};

// A headless, sample-frame transport boundary. Schedules are immutable source
// ranges over one normalized clock; callbacks mix only decoded LRU blocks.
// Completed voices retire after monotonically advancing callbacks. Seeking or
// looping starts a new session and rebuilds its schedule. stop() invalidates
// worker tokens before clearing every voice, so once it returns the next
// callback is guaranteed to emit digital silence.
class transport_t {
public:
    explicit transport_t(decoded_block_cache_t& block_cache,
                         std::size_t maximum_scheduled_voices = 256);

    [[nodiscard]] auto start() -> std::optional<session_token_t>;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;

    [[nodiscard]] auto decode_block(decoder_t& decoder, session_token_t token,
                                    audio_block_key_t key) -> block_decode_result_t;
    [[nodiscard]] auto schedule_voice(session_token_t token, transport_voice_t voice)
        -> voice_schedule_status_t;
    [[nodiscard]] auto mix(session_token_t token, std::uint64_t first_timeline_frame,
                           std::span<float> interleaved_stereo) -> transport_mix_result_t;

    [[nodiscard]] auto scheduled_voice_count() const -> std::size_t;

private:
    decoded_block_cache_t& block_cache_;
    decode_session_t decode_session_;
    std::size_t maximum_scheduled_voices_{};
    mutable std::mutex mutex_;
    std::vector<transport_voice_t> scheduled_voices_;
    std::optional<std::uint64_t> next_timeline_frame_;
};

} // namespace wormedit::audio
