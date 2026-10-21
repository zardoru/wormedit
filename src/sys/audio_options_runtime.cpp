#include "sys/audio_options_runtime.h"
#include "sys/options_runtime.h"
#include "sys/chart_preview_state.h"
#include <SDL3/SDL_audio.h>
#include <algorithm>

namespace wormedit::sys {
namespace {
auto selected_audio_device(std::string_view wanted, std::string& message) -> SDL_AudioDeviceID {
    if (wanted.empty()) return SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    int count{};
    auto* devices = SDL_GetAudioPlaybackDevices(&count);
    SDL_AudioDeviceID selected = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    for (int index = 0; devices && index < count; ++index) {
        const auto* name = SDL_GetAudioDeviceName(devices[index]);
        if (name && wanted == name) { selected = devices[index]; break; }
    }
    SDL_free(devices);
    if (selected == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK)
        message = "Selected audio device is unavailable; using System default.";
    return selected;
}

auto reopen_audio_outputs(const audio_preferences_t& preferences, sdl_audition_output_t& audition,
                         sdl_preview_output_t& preview) -> std::string {
    std::string warning;
    const auto device = selected_audio_device(preferences.output_device, warning);
    audition.close();
    preview.close();
    std::string error;
    const auto audition_open = audition.open(&error, device, preferences.buffer_frames);
    if (!error.empty()) warning += (warning.empty() ? "" : " ") + error;
    error.clear();
    const auto preview_open = preview.open(&error, device, preferences.buffer_frames);
    if (!error.empty()) warning += (warning.empty() ? "" : " ") + error;
    if (!audition_open || !preview_open) warning += " Audio output is unavailable.";
    return warning;
}
}

void audio_options_runtime_t::refresh_devices(options_runtime_t& options) {
    options.audio_devices.clear();
    int count{};
    auto* devices = SDL_GetAudioPlaybackDevices(&count);
    for (int index = 0; devices && index < count; ++index)
        if (const auto* name = SDL_GetAudioDeviceName(devices[index]))
            options.audio_devices.emplace_back(name);
    SDL_free(devices);
    configured_output_.reset(); // Retry an unavailable preferred device after explicit refresh.
}

auto audio_options_runtime_t::service(const audio_preferences_t& preferences, options_runtime_t& options,
    sdl_audition_output_t& audition, sdl_preview_output_t& preview, metronome_t& metronome,
    runtime::chart_preview_t& chart_preview, bool audition_active) -> std::string {
    if (!preferences.is_valid()) return "Invalid audio preferences; previous output retained.";
    if (!last_preferences_ || last_preferences_->metronome_volume != preferences.metronome_volume)
        static_cast<void>(metronome.set_volume(static_cast<std::uint8_t>(preferences.metronome_volume)));
    if (!last_preferences_ || last_preferences_->accent_volume != preferences.accent_volume)
        static_cast<void>(metronome.set_accent_volume(static_cast<std::uint8_t>(preferences.accent_volume)));
    if (!last_preferences_ || last_preferences_->count_in_measures != preferences.count_in_measures)
        static_cast<void>(metronome.set_count_in(static_cast<std::uint8_t>(preferences.count_in_measures)));
    if (!last_preferences_ || last_preferences_->audition_volume != preferences.audition_volume)
        static_cast<void>(audition.set_volume(preferences.audition_volume));
    chart_preview.display_latency_ms = preferences.preview_latency_ms;
    last_preferences_ = preferences;
    options.audio_output_pending = !configured_output_ || configured_output_->first != preferences.output_device ||
        configured_output_->second != preferences.buffer_frames;
    if (!options.audio_output_pending || preview.playing() || chart_preview.paused || audition_active) return {};
    auto message = reopen_audio_outputs(preferences, audition, preview);
    configured_output_.emplace(preferences.output_device, preferences.buffer_frames);
    options.audio_output_pending = false;
    metronome.settings.available = preview.available() && !metronome.source_bindings.empty();
    return message;
}
}
