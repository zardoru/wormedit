#pragma once
#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_hints.h>
#include <optional>
#include <cstdint>
#include <string>

namespace wormedit::sys {
// Both output adapters open on the UI thread. Restore the caller's hint after this open.
inline auto open_audio_output_stream(SDL_AudioDeviceID device, std::uint32_t buffer_frames,
    const SDL_AudioSpec& spec, SDL_AudioStreamCallback callback, void* userdata,
    std::string* message) -> SDL_AudioStream* {
    const auto* hint = SDL_GetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
    const auto previous = hint ? std::optional<std::string>{hint} : std::nullopt;
    if (buffer_frames) SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, std::to_string(buffer_frames).c_str());
    auto* stream = SDL_OpenAudioDeviceStream(device, &spec, callback, userdata);
    if (!stream && device != SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK) {
        if (message) *message = "Selected audio device could not open; using System default.";
        stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, callback, userdata);
    }
    if (previous) SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, previous->c_str());
    else SDL_ResetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES);
    return stream;
}
}
