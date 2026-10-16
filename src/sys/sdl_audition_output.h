#pragma once
#include "sys/sdl_session_results.h"

#include <wormedit/audio/audition_session.h>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <optional>
#include <span>
#include <string>

namespace wormedit::sys {

using sdl_audition_start_result_t =
    sdl_session_start_result_t<audio::audition_session_start_outcome_t>;

using sdl_audition_service_result_t = sdl_session_service_result_t<audio::audition_pump_effect_t>;
// Temporary: removed by T-633.
using sdl_audition_service_outcome_t =
    sdl_session_service_outcome_t<audio::audition_pump_effect_t>;

// App-private SDL3 playback adapter. SDL's stream lock serializes callback
// rendering with replacement and hard Stop. Decoder work runs on one refill
// worker; service() only consumes its generation-checked result.
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
    [[nodiscard]] auto service() -> std::optional<sdl_audition_service_outcome_t>;
    [[nodiscard]] auto stop() noexcept -> audio::transport_stop_result_t;
    [[nodiscard]] auto available() const noexcept -> bool {
        return stream_ != nullptr;
    }

  private:
    friend struct sdl_audition_output_test_access_t;
    void refill_loop(std::stop_token stop);
    void set_refill_enabled(bool enabled) noexcept;
    [[nodiscard]] auto take_refill_result() -> std::optional<audio::audition_pump_effect_t>;
    static void SDLCALL stream_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount, int total_amount);
    void render_requested(SDL_AudioStream* stream, int additional_amount) noexcept;
    [[nodiscard]] auto pause_device() noexcept -> bool;
    [[nodiscard]] auto resume_device() noexcept -> bool;

    static constexpr std::size_t callback_frames = 4'096;
    audio::audition_session_t& session_;
    std::mutex refill_mutex_;
    std::condition_variable refill_ready_;
    std::jthread refill_worker_;
    std::uint64_t refill_generation_{};
    bool refill_enabled_{};
    std::optional<audio::audition_pump_effect_t> refill_result_;
    SDL_AudioStream* stream_{};
    std::atomic<sdl_callback_failure_t> callback_failure_{std::nullopt};
    std::array<float, callback_frames * audio::output_channel_count> scratch_{};
};

} // namespace wormedit::sys
