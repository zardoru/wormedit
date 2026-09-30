#pragma once

#include <SDL3/SDL_audio.h>
#include <cassert>

namespace wormedit::sys {

// Shared across both adapters: a device call while *any* stream lock is held
// on this thread can invert SDL's device -> stream callback lock order.
inline thread_local unsigned audio_stream_lock_depth{};

class audio_stream_lock_t {
  public:
    explicit audio_stream_lock_t(SDL_AudioStream* stream) noexcept
        : stream_(stream), locked_(stream && SDL_LockAudioStream(stream)) {
        if (locked_) ++audio_stream_lock_depth;
    }
    ~audio_stream_lock_t() { unlock(); }
    audio_stream_lock_t(const audio_stream_lock_t&) = delete;
    auto operator=(const audio_stream_lock_t&) -> audio_stream_lock_t& = delete;
    [[nodiscard]] explicit operator bool() const noexcept { return locked_; }
    void unlock() noexcept {
        if (!locked_) return;
        static_cast<void>(SDL_UnlockAudioStream(stream_));
        assert(audio_stream_lock_depth != 0);
        --audio_stream_lock_depth;
        locked_ = false;
    }
  private:
    SDL_AudioStream* stream_{};
    bool locked_{};
};

// SDL already holds the stream lock when it invokes the adapter callback.
class audio_stream_callback_scope_t {
  public:
    audio_stream_callback_scope_t() noexcept { ++audio_stream_lock_depth; }
    ~audio_stream_callback_scope_t() { --audio_stream_lock_depth; }
    audio_stream_callback_scope_t(const audio_stream_callback_scope_t&) = delete;
    auto operator=(const audio_stream_callback_scope_t&) -> audio_stream_callback_scope_t& = delete;
};

[[nodiscard]] inline auto pause_audio_stream_device(SDL_AudioStream* stream) noexcept -> bool {
    assert(audio_stream_lock_depth == 0 && "pause device before locking an audio stream");
    return SDL_PauseAudioStreamDevice(stream);
}
[[nodiscard]] inline auto resume_audio_stream_device(SDL_AudioStream* stream) noexcept -> bool {
    assert(audio_stream_lock_depth == 0 && "unlock audio streams before resuming a device");
    return SDL_ResumeAudioStreamDevice(stream);
}

} // namespace wormedit::sys
