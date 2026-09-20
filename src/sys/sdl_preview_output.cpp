#include "sys/sdl_preview_output.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <limits>

namespace wormedit::sys {
namespace {

auto sdl_error(std::string fallback) -> std::string {
    const auto* message = SDL_GetError();
    return message != nullptr && message[0] != '\0' ? std::string(message) : std::move(fallback);
}

} // namespace

sdl_preview_output_t::~sdl_preview_output_t() {
    close();
}

auto sdl_preview_output_t::open(std::string* error_message) -> bool {
    if (stream_ != nullptr)
        return true;
    const SDL_AudioSpec spec{
        .format = SDL_AUDIO_F32,
        .channels = static_cast<int>(audio::output_channel_count),
        .freq = static_cast<int>(audio::output_sample_rate),
    };
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &sdl_preview_output_t::stream_callback, this);
    if (stream_ == nullptr) {
        if (error_message != nullptr)
            *error_message = sdl_error("could not open the default chart-preview audio output");
        return false;
    }
    return true;
}

void sdl_preview_output_t::close() noexcept {
    if (stream_ == nullptr)
        return;
    static_cast<void>(stop());
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

auto sdl_preview_output_t::activate_device() -> bool {
    callback_failure_.store(callback_failure_t::none, std::memory_order_release);
    const auto paused = SDL_PauseAudioStreamDevice(stream_);
    const auto cleared = SDL_ClearAudioStream(stream_);
    return paused && cleared && SDL_ResumeAudioStreamDevice(stream_);
}

auto sdl_preview_output_t::start(
    const preview_schedule_t& schedule,
    std::span<const audio::preview_object_source_binding_t> objects,
    std::span<const audio::preview_metronome_source_binding_t> metronome, std::uint64_t seek_frame)
    -> sdl_preview_start_result_t {
    if (stream_ == nullptr) {
        return {.status = sdl_preview_start_status_t::unavailable,
                .session = std::nullopt,
                .message = "default chart-preview audio output is unavailable"};
    }
    if (!SDL_LockAudioStream(stream_)) {
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt,
                .message = sdl_error("could not lock the chart-preview audio stream")};
    }
    auto started = session_.start(schedule, objects, metronome, seek_frame);
    auto result = sdl_preview_start_result_t{};
    result.session = started;
    if (!started) {
        result.status = sdl_preview_start_status_t::session_failed;
        result.message = "chart-preview session could not be started and primed";
    } else if (!activate_device()) {
        result.status = sdl_preview_start_status_t::device_failed;
        result.message = sdl_error("could not reset and resume the chart-preview audio stream");
        static_cast<void>(session_.stop());
        static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
        static_cast<void>(SDL_ClearAudioStream(stream_));
    } else {
        result.status = sdl_preview_start_status_t::started;
    }
    static_cast<void>(SDL_UnlockAudioStream(stream_));
    return result;
}

auto sdl_preview_output_t::seek(std::uint64_t sample_frame) -> sdl_preview_start_result_t {
    if (stream_ == nullptr) {
        return {.status = sdl_preview_start_status_t::unavailable,
                .session = std::nullopt,
                .message = "default chart-preview audio output is unavailable"};
    }
    if (!SDL_LockAudioStream(stream_)) {
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt,
                .message = sdl_error("could not lock the chart-preview audio stream")};
    }
    auto started = session_.seek(sample_frame);
    auto result = sdl_preview_start_result_t{};
    result.session = started;
    if (!started) {
        result.status = sdl_preview_start_status_t::session_failed;
        result.message = "chart-preview seek could not be materialized and primed";
    } else if (!activate_device()) {
        result.status = sdl_preview_start_status_t::device_failed;
        result.message = sdl_error("could not reset and resume the chart-preview audio stream");
        static_cast<void>(session_.stop());
        static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
        static_cast<void>(SDL_ClearAudioStream(stream_));
    } else {
        result.status = sdl_preview_start_status_t::started;
    }
    static_cast<void>(SDL_UnlockAudioStream(stream_));
    return result;
}

auto sdl_preview_output_t::service() -> std::optional<sdl_preview_service_result_t> {
    const auto callback_failure =
        callback_failure_.exchange(callback_failure_t::none, std::memory_order_acq_rel);
    if (callback_failure != callback_failure_t::none) {
        return sdl_preview_service_result_t{
            .status = callback_failure == callback_failure_t::device
                          ? sdl_preview_service_status_t::device_failed
                          : sdl_preview_service_status_t::render_failed,
            .pump = std::nullopt,
        };
    }
    if (stream_ == nullptr || !session_.active())
        return std::nullopt;
    auto pumped = session_.pump();
    if (pumped.decode.has_value() &&
        pumped.decode->status == audio::preview_pump_status_t::stale_generation) {
        return std::nullopt;
    }
    return sdl_preview_service_result_t{
        .status = pumped ? sdl_preview_service_status_t::ready
                         : sdl_preview_service_status_t::pump_failed,
        .pump = std::move(pumped),
    };
}

auto sdl_preview_output_t::stop() noexcept -> audio::transport_stop_result_t {
    if (stream_ == nullptr) {
        callback_failure_.store(callback_failure_t::none, std::memory_order_release);
        return session_.stop();
    }
    if (!SDL_LockAudioStream(stream_)) {
        static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
        auto stopped = session_.stop();
        static_cast<void>(SDL_ClearAudioStream(stream_));
        callback_failure_.store(callback_failure_t::none, std::memory_order_release);
        return stopped;
    }
    static_cast<void>(SDL_PauseAudioStreamDevice(stream_));
    auto stopped = session_.stop();
    static_cast<void>(SDL_ClearAudioStream(stream_));
    callback_failure_.store(callback_failure_t::none, std::memory_order_release);
    static_cast<void>(SDL_UnlockAudioStream(stream_));
    return stopped;
}

void SDLCALL sdl_preview_output_t::stream_callback(void* userdata, SDL_AudioStream* stream,
                                                   int additional_amount, int) {
    auto* output = static_cast<sdl_preview_output_t*>(userdata);
    if (output != nullptr && stream != nullptr && additional_amount > 0)
        output->render_requested(stream, additional_amount);
}

void sdl_preview_output_t::render_requested(SDL_AudioStream* stream,
                                            int additional_amount) noexcept {
    constexpr auto bytes_per_frame = sizeof(float) * audio::output_channel_count;
    auto remaining = static_cast<std::size_t>(additional_amount);
    while (remaining != 0) {
        const auto requested_frames =
            std::min(callback_frames, (remaining + bytes_per_frame - 1) / bytes_per_frame);
        const auto samples = requested_frames * audio::output_channel_count;
        const auto bytes = samples * sizeof(float);
        const auto rendered = session_.render(std::span{scratch_.data(), samples});
        if (rendered.status == audio::preview_session_render_status_t::underrun ||
            rendered.status == audio::preview_session_render_status_t::transport_failed) {
            callback_failure_.store(callback_failure_t::render, std::memory_order_release);
            static_cast<void>(session_.stop());
            return;
        }
        if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !SDL_PutAudioStreamData(stream, scratch_.data(), static_cast<int>(bytes))) {
            callback_failure_.store(callback_failure_t::device, std::memory_order_release);
            static_cast<void>(session_.stop());
            return;
        }
        remaining = remaining > bytes ? remaining - bytes : 0;
    }
}

} // namespace wormedit::sys
