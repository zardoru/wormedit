#pragma once
#include "sys/sdl_session_results.h"

#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/playback_rate.h>
#include <wormedit/audio/time_stretch.h>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>
#include <span>
#include <string>

namespace wormedit::sys {

enum class sdl_preview_start_status_t {
    started,
    unavailable,
    session_failed,
    device_failed,
};

using sdl_preview_start_result_t =
    sdl_session_start_result_t<sdl_preview_start_status_t, audio::preview_session_start_result_t>;

enum class sdl_preview_service_status_t {
    ready,
    pump_failed,
    render_failed,
    device_failed,
};

struct sdl_preview_service_result_t {
    sdl_preview_service_status_t status{sdl_preview_service_status_t::ready};
    std::optional<audio::preview_session_pump_result_t> pump;
    bool rate_unavailable{};
    std::uint64_t silenced_voices{};
    std::uint64_t silenced_music_voices{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == sdl_preview_service_status_t::ready;
    }
};

// SDL3 output for an owned chart-preview timeline. The stream lock serializes
// rendering with start/seek/Stop. The refill worker decodes and queues loop PCM
// ahead of the device; service() only reads its latest generation result.
class sdl_preview_output_t {
  public:
    explicit sdl_preview_output_t(audio::preview_session_t& session) : session_(session) {}
    ~sdl_preview_output_t();

    sdl_preview_output_t(const sdl_preview_output_t&) = delete;
    auto operator=(const sdl_preview_output_t&) -> sdl_preview_output_t& = delete;

    [[nodiscard]] auto open(std::string* error_message = nullptr) -> bool;
    void close() noexcept;

    [[nodiscard]] auto
    start(const preview_schedule_t& schedule,
          std::span<const audio::preview_object_source_binding_t> objects,
          std::span<const audio::preview_metronome_source_binding_t> metronome = {},
          std::uint64_t seek_frame = 0,
          std::uint64_t minimum_end_frame = 0) -> sdl_preview_start_result_t;
    [[nodiscard]] auto seek(std::uint64_t sample_frame) -> sdl_preview_start_result_t;
    [[nodiscard]] auto set_loop_region(std::optional<audio::preview_loop_region_t> region) -> bool;
    [[nodiscard]] auto service() -> std::optional<sdl_preview_service_result_t>;
    [[nodiscard]] auto stop() noexcept -> audio::transport_stop_result_t;
    // The requested rate is applied at the next SDL callback, without restarting.
    [[nodiscard]] auto set_playback_rate(rational_t rate) noexcept -> bool;
    [[nodiscard]] auto playback_rate() const noexcept -> rational_t;
    [[nodiscard]] auto set_keep_pitch(bool enabled) noexcept -> bool;
    [[nodiscard]] auto keep_pitch() const noexcept -> bool;
    [[nodiscard]] auto playing() const noexcept -> bool;
    // Callback-resolution audible clock; reading never queries or locks SDL.
    [[nodiscard]] auto timeline_frame() const noexcept -> std::uint64_t;
    // Queries SDL, which takes the stream lock that the audio callback holds while rendering;
    // only the callback and one-off transport changes call it. Per-frame checks read
    // published_latency_frames() instead.
    [[nodiscard]] auto source_latency_frames() const noexcept -> std::uint64_t;
    // The latency the callback measured on its last run; lock-free and at most one device
    // period old. It reaches zero once the output has drained.
    [[nodiscard]] auto published_latency_frames() const noexcept -> std::uint64_t {
        return published_latency_frames_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto audio_device_id() const noexcept -> SDL_AudioDeviceID {
        return stream_ ? SDL_GetAudioStreamDevice(stream_) : 0;
    }
    [[nodiscard]] auto available() const noexcept -> bool {
        return stream_ != nullptr;
    }

  private:
    friend struct sdl_preview_output_test_access_t;
    static void SDLCALL stream_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount, int total_amount);
    void render_requested(SDL_AudioStream* stream, int additional_amount) noexcept;
    [[nodiscard]] auto pause_device() noexcept -> bool;
    [[nodiscard]] auto resume_device() noexcept -> bool;
    [[nodiscard]] auto finish_start(sdl_preview_start_result_t result) -> sdl_preview_start_result_t;
    [[nodiscard]] auto service_refill_result(
        std::optional<audio::preview_session_pump_result_t> pumped,
        bool rate_unavailable, std::uint64_t silenced_voices,
        std::uint64_t silenced_music_voices)
        -> std::optional<sdl_preview_service_result_t>;
    void reset_output(std::uint64_t frame) noexcept;
    [[nodiscard]] auto read_output_playhead() const noexcept -> audio::playhead_state_t;
    void publish_output_playhead() noexcept;
    void clear_output_playhead() noexcept;
    [[nodiscard]] auto apply_output_mode(SDL_AudioStream* stream) noexcept -> bool;
    [[nodiscard]] auto render_source(std::size_t frames) noexcept -> std::size_t;
    [[nodiscard]] auto render_block(std::size_t frames) noexcept -> std::size_t;
    void fade_block(std::size_t frames) noexcept;
    void begin_fade(bool preserve, rational_t rate) noexcept;

    void refill_loop(std::stop_token stop);
    [[nodiscard]] auto refill_loop_output(std::uint64_t generation,
        audio::preview_session_pump_result_t result) -> audio::preview_session_pump_result_t;
    [[nodiscard]] auto fill_loop_queue(std::uint64_t generation) -> bool;
    [[nodiscard]] auto loop_queue_target_bytes() const noexcept -> std::uint64_t;
    [[nodiscard]] auto set_loop_output_region(std::optional<audio::preview_loop_region_t> region) -> bool;
    void set_refill_enabled(bool enabled) noexcept;
    [[nodiscard]] auto take_refill_result() -> std::optional<audio::preview_session_pump_result_t>;

    static constexpr std::size_t callback_frames = 4'096;
    enum class callback_failure_t {
        none,
        render,
        device,
    };

    audio::preview_session_t& session_;
    audio::playhead_snapshot_t output_playhead_;
    std::atomic<bool> output_playhead_cleared_{true};
    std::uint64_t output_generation_{};
    std::uint64_t output_transport_generation_{};
    std::mutex refill_mutex_;
    std::condition_variable refill_ready_;
    std::jthread refill_worker_;
    std::atomic<std::uint64_t> refill_generation_{};
    std::atomic<bool> refill_requested_{};
    bool refill_enabled_{};
    std::optional<audio::preview_session_pump_result_t> refill_result_;
    SDL_AudioStream* stream_{};
    std::atomic<callback_failure_t> callback_failure_{callback_failure_t::none};
    std::atomic<std::uint64_t> silenced_voices_{};
    std::atomic<std::uint64_t> silenced_music_voices_{};
    std::atomic<bool> rate_unavailable_{};
    std::atomic<std::size_t> requested_rate_index_{8};
    std::atomic<std::size_t> active_rate_index_{8};
    std::atomic<bool> requested_keep_pitch_{};
    std::atomic<bool> active_keep_pitch_{};
    std::atomic<bool> output_playing_{};
    std::atomic<std::uint64_t> rendered_source_frame_{};
    std::atomic<std::uint64_t> buffered_source_frames_{};
    std::atomic<std::uint64_t> published_latency_frames_{};
    std::uint64_t source_end_frame_{};
    const preview_schedule_t* retained_schedule_{};
    std::span<const audio::preview_object_source_binding_t> retained_objects_;
    std::span<const audio::preview_metronome_source_binding_t> retained_metronome_;
    std::atomic<std::uint64_t> output_start_frame_{};
    bool source_finished_{};
    std::atomic<bool> output_drained_{};
    std::atomic<bool> loop_enabled_{};
    std::atomic<std::uint64_t> loop_begin_frame_{};
    std::atomic<std::uint64_t> loop_end_frame_{};
    std::size_t fade_remaining_{};
    std::size_t fade_total_{};
    bool have_output_{};
    bool fading_out_{};
    bool switch_ready_{};
    audio::time_stretch_t stretcher_;
    std::array<float, callback_frames * audio::output_channel_count> source_scratch_{};
    std::array<float, callback_frames * audio::output_channel_count> scratch_{};
};

} // namespace wormedit::sys
