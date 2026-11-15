#pragma once
#include <wormedit/audio/block_cache.h>
#include <wormedit/audio/scrub_reader.h>
#include <wormedit/audio/source_registry.h>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace wormedit::sys {

// Turntable-style scrub playback on its own SDL stream: the audio follows a signed speed that
// the caller updates each frame, reverses when it goes negative and falls silent when the record
// is held. It decodes through a private block cache, so it never touches the chart preview's
// transport. A refill worker keeps the blocks around the needle decoded; the callback only
// reads blocks that are already cached.
class sdl_scrub_output_t {
  public:
    explicit sdl_scrub_output_t(audio::decoded_source_registry_t& sources);
    ~sdl_scrub_output_t();

    sdl_scrub_output_t(const sdl_scrub_output_t&) = delete;
    auto operator=(const sdl_scrub_output_t&) -> sdl_scrub_output_t& = delete;

    [[nodiscard]] auto open(std::string* error_message = nullptr,
        SDL_AudioDeviceID device = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, std::uint32_t buffer_frames = 0) -> bool;
    void close() noexcept;
    [[nodiscard]] auto available() const noexcept -> bool { return stream_ != nullptr; }
    [[nodiscard]] auto set_volume(std::uint32_t percent) -> bool;

    // The music laid on the playback timeline. Replacing it with an equal list is free.
    void set_segments(std::vector<audio::scrub_segment_t> segments);
    // Drops the needle at a timeline frame and starts the device.
    [[nodiscard]] auto begin(std::uint64_t timeline_frame) -> bool;
    // Speed in audio seconds per real second (negative reads backwards); `hold` stops the record.
    void set_motion(double speed, bool hold) noexcept;
    // Fades out and stops the device once silent; service() finishes the stop.
    void end() noexcept;
    // Per frame: pauses the device after an end() once the sound has faded out.
    void service() noexcept;
    [[nodiscard]] auto active() const noexcept -> bool { return active_.load(std::memory_order_acquire); }

  private:
    static void SDLCALL stream_callback(void* userdata, SDL_AudioStream* stream,
                                        int additional_amount, int total_amount);
    void render_requested(SDL_AudioStream* stream, int additional_amount) noexcept;
    void refill_loop(std::stop_token stop);

    static constexpr std::size_t callback_frames = 2'048;
    audio::decoded_source_registry_t& sources_;
    audio::decoded_block_cache_t cache_;
    audio::scrub_reader_t reader_;
    SDL_AudioStream* stream_{};
    std::uint32_t volume_{100};

    // Written by the control thread, read by the callback.
    std::atomic<double> target_speed_{};
    std::atomic<bool> hold_{true};
    std::atomic<bool> active_{};
    std::atomic<bool> ending_{};
    // Published by the callback for the refill worker and service().
    std::atomic<double> needle_frame_{};
    std::atomic<double> needle_speed_{};
    std::atomic<bool> silent_{true};

    std::mutex worker_mutex_;
    std::condition_variable worker_ready_;
    std::jthread worker_;
    std::vector<audio::scrub_segment_t> segments_;
    std::array<float, callback_frames * audio::output_channel_count> scratch_{};
};

} // namespace wormedit::sys
