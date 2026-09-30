#pragma once

#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/playback_rate.h>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <cstdint>
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

struct sdl_preview_start_result_t {
    sdl_preview_start_status_t status{sdl_preview_start_status_t::unavailable};
    std::optional<audio::preview_session_start_result_t> session;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == sdl_preview_start_status_t::started;
    }
};

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
// callback rendering with start/seek/Stop, while decoder/materialization work
// remains on the control thread through service().
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
    [[nodiscard]] auto service() -> std::optional<sdl_preview_service_result_t>;
    [[nodiscard]] auto stop() noexcept -> audio::transport_stop_result_t;
    // The requested rate is applied at the next SDL callback, without restarting.
    [[nodiscard]] auto set_playback_rate(rational_t rate) noexcept -> bool;
    [[nodiscard]] auto playback_rate() const noexcept -> rational_t;
    [[nodiscard]] auto source_latency_frames() const noexcept -> std::uint64_t;
    [[nodiscard]] auto available() const noexcept -> bool {
        return stream_ != nullptr;
    }

  private:
    static void SDLCALL stream_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount, int total_amount);
    void render_requested(SDL_AudioStream* stream, int additional_amount) noexcept;
    [[nodiscard]] auto activate_device() -> bool;

    static constexpr std::size_t callback_frames = 4'096;
    enum class callback_failure_t {
        none,
        render,
        device,
    };

    audio::preview_session_t& session_;
    SDL_AudioStream* stream_{};
    std::atomic<callback_failure_t> callback_failure_{callback_failure_t::none};
    std::atomic<std::uint64_t> silenced_voices_{};
    std::atomic<std::uint64_t> silenced_music_voices_{};
    std::atomic<bool> rate_unavailable_{};
    std::atomic<std::size_t> requested_rate_index_{8};
    std::atomic<std::size_t> active_rate_index_{8};
    std::array<float, callback_frames * audio::output_channel_count> scratch_{};
};

} // namespace wormedit::sys
