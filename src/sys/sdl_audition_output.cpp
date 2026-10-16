#include "sys/thread_priority.h"
#include "sys/sdl_audition_output.h"
#include "sys/sdl_audio_stream_lock.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace wormedit::sys {
namespace {

auto sdl_error(std::string fallback) -> std::string {
    const auto* message = SDL_GetError();
    return message != nullptr && message[0] != '\0' ? std::string(message) : std::move(fallback);
}

} // namespace

sdl_audition_output_t::~sdl_audition_output_t() {
    close();
}

auto sdl_audition_output_t::open(std::string* error_message) -> bool {
    if (stream_ != nullptr) {
        return true;
    }
    const SDL_AudioSpec spec{
        .format = SDL_AUDIO_F32,
        .channels = static_cast<int>(audio::output_channel_count),
        .freq = static_cast<int>(audio::output_sample_rate),
    };
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        &sdl_audition_output_t::stream_callback, this);
    if (stream_ == nullptr) {
        if (error_message != nullptr) {
            *error_message = sdl_error("could not open the default audio output");
        }
        return false;
    }
    try {
        refill_worker_ = std::jthread([this](std::stop_token stop) { refill_loop(stop); });
    } catch (...) {
        if (error_message) *error_message = "could not start the audition refill worker";
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void sdl_audition_output_t::close() noexcept {
    if (stream_ == nullptr) {
        return;
    }
    static_cast<void>(stop());
    {
        // Publish the stop predicate under the waiter's mutex: otherwise the
        // notification can fall between its false predicate check and wait.
        const std::scoped_lock lock(refill_mutex_);
        refill_worker_.request_stop();
    }
    refill_ready_.notify_all();
    if (refill_worker_.joinable()) refill_worker_.join();
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

void sdl_audition_output_t::set_refill_enabled(bool enabled) noexcept {
    {
        const std::scoped_lock lock(refill_mutex_);
        ++refill_generation_;
        refill_enabled_ = enabled;
        refill_result_.reset();
    }
    refill_ready_.notify_all();
}

auto sdl_audition_output_t::take_refill_result()
    -> std::optional<audio::audition_pump_outcome_t> {
    const std::scoped_lock lock(refill_mutex_);
    return std::exchange(refill_result_, std::nullopt);
}

void sdl_audition_output_t::refill_loop(std::stop_token stop) {
    apply_current_thread_priority(SDL_THREAD_PRIORITY_HIGH);
    std::unique_lock lock(refill_mutex_);
    while (!stop.stop_requested()) {
        refill_ready_.wait(lock, [&] { return stop.stop_requested() || refill_enabled_; });
        if (stop.stop_requested()) return;
        const auto generation = refill_generation_;
        lock.unlock();
        audio::audition_pump_outcome_t result =
            std::unexpected(audio::audition_pump_effect_t{});
        try { result = session_.pump(); }
        catch (...) {
            auto failed = result ? std::move(*result) : std::move(result.error());
            failed.status = audio::audition_pump_status_t::decode_failed;
            result = std::unexpected(std::move(failed));
        }
        lock.lock();
        const auto& effect = result ? *result : result.error();
        if (generation == refill_generation_ && refill_enabled_ &&
            (!refill_result_ || static_cast<bool>(*refill_result_))) {
            if (!result || effect.status == audio::audition_pump_status_t::complete)
                refill_enabled_ = false;
            if (effect.status != audio::audition_pump_status_t::stale_generation)
                refill_result_ = std::move(result);
        }
        refill_ready_.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return stop.stop_requested() || generation != refill_generation_ || !refill_enabled_;
        });
    }
}

auto sdl_audition_output_t::pause_device() noexcept -> bool {
    return pause_audio_stream_device(stream_);
}

auto sdl_audition_output_t::resume_device() noexcept -> bool {
    return resume_audio_stream_device(stream_);
}

auto sdl_audition_output_t::start(std::span<const audio::audition_clip_t> clips)
    -> sdl_audition_start_result_t {
    if (stream_ == nullptr) {
        return {.error =
                    error_t{failure_code_t::unavailable, "default audio output is unavailable"},
                .session = std::nullopt};
    }
    if (!pause_device()) {
        return {.error = error_t{failure_code_t::device_failed,
                                 sdl_error("could not reset and resume the audio output stream")},
                .session = std::nullopt};
    }
    audio_stream_lock_t stream_lock{stream_};
    if (!stream_lock) {
        const auto message = sdl_error("could not lock the audio output stream");
        if (session_.is_active())
            static_cast<void>(resume_device());
        return {.error = error_t{failure_code_t::device_failed, message}, .session = std::nullopt};
    }
    set_refill_enabled(false);
    auto started = session_.start_session(clips);
    std::optional<error_t<failure_code_t>> error{error_t{failure_code_t::session_failed, {}}};
    const bool preserved = !started && session_.is_active();
    if (!preserved)
        callback_failure_.store(std::nullopt, std::memory_order_release);
    const auto cleared = preserved || SDL_ClearAudioStream(stream_);
    if (started) {
        if (cleared)
            error.reset();
        else
            error = error_t{failure_code_t::device_failed,
                            sdl_error("could not reset and resume the audio output stream")};
    } else if (started.error().code == failure_code_t::prime_failed)
        error->message = "audio preview lookahead could not be decoded";
    else
        error->message = "audio preview transport rejected the prepared clips";
    stream_lock.unlock();
    if ((!error || preserved) && !resume_device()) {
        error = error_t{failure_code_t::device_failed,
                        sdl_error("could not reset and resume the audio output stream")};
    }
    if (error && error->code == failure_code_t::device_failed)
        static_cast<void>(stop());
    else
        set_refill_enabled(session_.is_active());
    return {.error = std::move(error), .session = std::move(started)};
}

auto sdl_audition_output_t::service() -> std::optional<sdl_audition_service_outcome_t> {
    const auto callback_failure =
        callback_failure_.exchange(std::nullopt, std::memory_order_acq_rel);
    if (callback_failure) {
        return std::unexpected(sdl_audition_service_result_t{
            .status = *callback_failure,
            .pump = std::nullopt,
        });
    }
    auto pumped = take_refill_result();
    if (!pumped)
        return std::nullopt;
    sdl_audition_service_result_t result{
        .pump = std::move(pumped),
    };
    if (!*result.pump) {
        result.status = failure_code_t::pump_failed;
        return std::unexpected(std::move(result));
    }
    return result;
}

auto sdl_audition_output_t::stop() noexcept -> audio::transport_stop_result_t {
    set_refill_enabled(false);
    if (stream_ == nullptr) {
        callback_failure_.store(std::nullopt, std::memory_order_release);
        return session_.stop();
    }
    static_cast<void>(pause_device());
    audio_stream_lock_t stream_lock{stream_};
    if (!stream_lock) {
        auto stopped = session_.stop();
        static_cast<void>(SDL_ClearAudioStream(stream_));
        callback_failure_.store(std::nullopt, std::memory_order_release);
        return stopped;
    }
    auto stopped = session_.stop();
    static_cast<void>(SDL_ClearAudioStream(stream_));
    callback_failure_.store(std::nullopt, std::memory_order_release);
    stream_lock.unlock();
    return stopped;
}

void SDLCALL sdl_audition_output_t::stream_callback(void* userdata, SDL_AudioStream* stream,
                                                    int additional_amount, int) {
    auto* output = static_cast<sdl_audition_output_t*>(userdata);
    if (output == nullptr || stream == nullptr || additional_amount <= 0) {
        return;
    }
    const audio_stream_callback_scope_t callback_lock;
    output->render_requested(stream, additional_amount);
}

void sdl_audition_output_t::render_requested(SDL_AudioStream* stream,
                                             int additional_amount) noexcept {
    constexpr auto bytes_per_frame = sizeof(float) * audio::output_channel_count;
    auto remaining = static_cast<std::size_t>(additional_amount);
    while (remaining != 0) {
        const auto requested_frames =
            std::min(callback_frames, (remaining + bytes_per_frame - 1) / bytes_per_frame);
        const auto samples = requested_frames * audio::output_channel_count;
        const auto bytes = samples * sizeof(float);
        const auto rendered_outcome = session_.render_frames(std::span{scratch_.data(), samples});
        const auto& rendered = rendered_outcome ? *rendered_outcome : rendered_outcome.error();
        if (rendered.status.has_failure(failure_code_t::underrun) ||
            rendered.status.has_failure(failure_code_t::transport_failed)) {
            callback_failure_.store(failure_code_t::render_failed, std::memory_order_release);
            static_cast<void>(session_.stop());
            return;
        }
        if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !SDL_PutAudioStreamData(stream, scratch_.data(), static_cast<int>(bytes))) {
            callback_failure_.store(failure_code_t::device_failed, std::memory_order_release);
            static_cast<void>(session_.stop());
            return;
        }
        remaining = remaining > bytes ? remaining - bytes : 0;
    }
}

} // namespace wormedit::sys
