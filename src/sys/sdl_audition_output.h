#pragma once

#include <wormedit/audio/audition_session.h>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <optional>
#include <span>
#include <string>

namespace wormedit::sys {

enum class sdl_audition_start_status_t {
    started,
    unavailable,
    session_failed,
    device_failed,
};

struct sdl_audition_start_result_t {
    sdl_audition_start_status_t status{sdl_audition_start_status_t::unavailable};
    std::optional<audio::audition_session_start_result_t> session;
    std::string message;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == sdl_audition_start_status_t::started;
    }
};

enum class sdl_audition_service_status_t {
    ready,
    pump_failed,
    render_failed,
    device_failed,
};

struct sdl_audition_service_result_t {
    sdl_audition_service_status_t status{sdl_audition_service_status_t::ready};
    std::optional<audio::audition_pump_result_t> pump;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == sdl_audition_service_status_t::ready;
    }
};

// App-private SDL3 playback adapter. SDL's stream lock serializes callback
// rendering with replacement and hard Stop; decoder refill remains on the UI
// thread through service() and never runs inside the device callback.
class sdl_audition_output_t {
  public:
    explicit sdl_audition_output_t(audio::audition_session_t& session) : session_(session) {}
    ~sdl_audition_output_t();

    sdl_audition_output_t(const sdl_audition_output_t&) = delete;
    auto operator=(const sdl_audition_output_t&) -> sdl_audition_output_t& = delete;

    [[nodiscard]] auto open(std::string* error_message = nullptr) -> bool;
    void close() noexcept;

    [[nodiscard]] auto start(std::span<const audio::audition_clip_t> clips)
        -> sdl_audition_start_result_t;
    [[nodiscard]] auto service() -> std::optional<sdl_audition_service_result_t>;
    [[nodiscard]] auto stop() noexcept -> audio::transport_stop_result_t;
    [[nodiscard]] auto available() const noexcept -> bool {
        return stream_ != nullptr;
    }

  private:
    static void SDLCALL stream_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount, int total_amount);
    void render_requested(SDL_AudioStream* stream, int additional_amount) noexcept;
    [[nodiscard]] auto pause_device() noexcept -> bool;
    [[nodiscard]] auto resume_device() noexcept -> bool;

    static constexpr std::size_t callback_frames = 4'096;
    enum class callback_failure_t {
        none,
        render,
        device,
    };

    audio::audition_session_t& session_;
    SDL_AudioStream* stream_{};
    std::atomic<callback_failure_t> callback_failure_{callback_failure_t::none};
    std::array<float, callback_frames * audio::output_channel_count> scratch_{};
};

} // namespace wormedit::sys
