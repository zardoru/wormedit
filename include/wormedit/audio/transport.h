#pragma once

#include <wormedit/audio/block_cache.h>
#include <wormedit/audio/resident_samples.h>
#include <wormedit/core/result.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <vector>
#include <unordered_set>

namespace wormedit::audio {

enum class voice_kind_t : std::uint8_t {
    music,
    bgm,
    playable_note,
    click_preview,
    layer_assist,
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
    // Stable BGM lane identity; zero is outside the lane mix.
    std::uint64_t mix_group{};

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

enum class active_voice_snapshot_status_t {
    ready,
    stale_generation,
    capacity_exceeded,
};

using active_voice_snapshot_outcome_t =
    result_t<std::vector<transport_voice_t>, active_voice_snapshot_status_t>;

struct transport_mix_result_t {
    [[nodiscard]] static auto from(transport_mix_result_t&& report)
        -> result_t<transport_mix_result_t, transport_mix_result_t>;
    transport_mix_status_t status{transport_mix_status_t::stale_generation};
    std::size_t frames{};
    std::size_t voices_mixed{};
    std::size_t missing_blocks{};
    std::size_t dropped_voices{};
    std::size_t dropped_music_voices{};
    std::size_t clipped_samples{};
    auto operator==(const transport_mix_result_t&) const -> bool = default;
};
using transport_mix_outcome_t = result_t<transport_mix_result_t, transport_mix_result_t>;

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

    // Disabling retires pending clicks and silences the next callback; other voices survive.
    void set_clicks_enabled(bool enabled);
    // Control thread; active muted voices keep advancing and can resume mid-tail.
    void set_muted_mix_groups(std::span<const std::uint64_t> groups);
    // Control thread: retire one preview family without restarting the transport.
    void clear_voices(voice_kind_t kind);
    [[nodiscard]] auto set_click_preview_volume(std::uint8_t volume_percent) noexcept -> bool;
    [[nodiscard]] auto click_preview_volume() const noexcept -> std::uint8_t {
        return click_volume_percent_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto start() -> std::optional<session_token_t>;
    [[nodiscard]] auto stop() noexcept -> transport_stop_result_t;
    [[nodiscard]] auto is_current(session_token_t token) const noexcept -> bool;

    [[nodiscard]] auto decode_block(decoder_t& decoder, session_token_t token,
                                    audio_block_key_t key) -> block_decode_result_t;
    [[nodiscard]] auto decode_source_block(decoder_t& decoder, session_token_t token,
                                         audio_block_key_t key) -> block_decode_outcome_t;
    // Control/worker threads only. The mixer reads the published snapshot under
    // the transport lock; the replaced snapshot is released after the lock drops,
    // never on the audio callback.
    void set_resident_snapshot(resident_snapshot_ptr_t snapshot);
    // A block served from resident samples needs no decoder: returns a cache-hit
    // style result for it, nullopt when the block is not resident.
    [[nodiscard]] auto resident_block_hit(session_token_t token, const audio_block_key_t& key)
        -> std::optional<block_decode_result_t>;
    [[nodiscard]] auto schedule_voice(session_token_t token, transport_voice_t voice)
        -> voice_schedule_status_t;
    // Validates the complete batch before publishing any voice. This keeps a
    // layered chart event atomic when the bounded slot pool is exhausted.
    [[nodiscard]] auto schedule_voices(session_token_t token,
                                       std::span<const transport_voice_t> voices)
        -> voice_schedule_status_t;
    [[nodiscard]] auto mix_frames(session_token_t token, std::uint64_t first_timeline_frame,
                                 std::span<float> interleaved_stereo) -> transport_mix_outcome_t;

    // Control-thread snapshot for bounded decoder lookahead. Realtime mixing
    // never calls this allocating API.
    [[nodiscard]] auto snapshot_voices(session_token_t token, std::size_t maximum_voices = 256)
        -> active_voice_snapshot_outcome_t;

    [[nodiscard]] auto block_cache_limits() const noexcept -> block_cache_limits_t;
    [[nodiscard]] auto scheduled_voice_count() const -> std::size_t;

  private:
    struct mix_operation_t;
    struct snapshot_operation_t;
    struct scheduled_voice_entry_t {
        transport_voice_t voice;
        bool active{};
    };

    decoded_block_cache_t& block_cache_;
    resident_snapshot_ptr_t resident_;
    decode_session_t decode_session_;
    std::size_t maximum_scheduled_voices_{};
    mutable std::mutex mutex_;
    // Storage is reserved once and bounded by maximum_scheduled_voices_. The
    // mix callback only marks completed slots inactive; schedule_voice() on a
    // control thread reuses their storage without growing the vector.
    std::vector<scheduled_voice_entry_t> scheduled_voices_;
    std::unordered_set<std::uint64_t> muted_mix_groups_;
    std::size_t active_scheduled_voices_{};
    std::atomic<bool> clicks_enabled_{true};
    std::atomic<std::uint8_t> click_volume_percent_{100};
    std::optional<std::uint64_t> next_timeline_frame_;
};

} // namespace wormedit::audio
