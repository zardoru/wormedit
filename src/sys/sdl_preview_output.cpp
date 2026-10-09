#include "sys/frame_timing.h"
#include "sys/thread_priority.h"
#include "sys/sdl_preview_output.h"
#include "sys/sdl_audio_stream_lock.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <cmath>
#include <chrono>
#include <limits>
#include <unordered_map>
#include <utility>

namespace wormedit::sys {
namespace {

auto sdl_error(std::string fallback) -> std::string {
    const auto* message = SDL_GetError();
    return message != nullptr && message[0] != '\0' ? std::string(message) : std::move(fallback);
}

auto schedule_end(const preview_schedule_t& schedule,
                  std::span<const audio::preview_object_source_binding_t> objects,
                  std::span<const audio::preview_metronome_source_binding_t> metronome,
                  std::uint64_t minimum) -> std::uint64_t {
    std::unordered_map<std::uint64_t, std::uint64_t> durations;
    for (const auto& object : objects)
        for (const auto& voice : object.voices)
            durations[object.object.value] = std::max(durations[object.object.value], voice.frame_count);
    auto end = minimum;
    const auto extend = [&](std::uint64_t frame, std::uint64_t duration) {
        if (duration <= std::numeric_limits<std::uint64_t>::max() - frame)
            end = std::max(end, frame + duration);
    };
    for (const auto& event : schedule.playable) extend(event.sample_frame, durations[event.object.value]);
    for (const auto& event : schedule.bgm) extend(event.sample_frame, durations[event.object.value]);
    for (const auto& event : schedule.metronome)
        for (const auto& binding : metronome)
            if (event.accent == binding.accent) extend(event.sample_frame, binding.voice.frame_count);
    if (schedule.music) extend(schedule.music->sample_frame, schedule.music->frame_count);
    return end;
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
    try {
        refill_worker_ = std::jthread([this](std::stop_token stop) { refill_loop(stop); });
    } catch (...) {
        if (error_message) *error_message = "could not start the chart-preview refill worker";
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void sdl_preview_output_t::close() noexcept {
    if (stream_ == nullptr)
        return;
    static_cast<void>(stop());
    {
        // Serialize the stop predicate with wait: notification must not slip
        // between the worker's predicate check and entering its wait.
        const std::scoped_lock lock(refill_mutex_);
        refill_worker_.request_stop();
    }
    refill_ready_.notify_all();
    if (refill_worker_.joinable()) refill_worker_.join();
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

void sdl_preview_output_t::set_refill_enabled(bool enabled) noexcept {
    {
        const std::scoped_lock lock(refill_mutex_);
        ++refill_generation_;
        refill_enabled_ = enabled;
        refill_result_.reset();
    }
    refill_ready_.notify_all();
}

auto sdl_preview_output_t::take_refill_result()
    -> std::optional<audio::preview_session_pump_result_t> {
    const std::scoped_lock lock(refill_mutex_);
    return std::exchange(refill_result_, std::nullopt);
}

void sdl_preview_output_t::refill_loop(std::stop_token stop) {
    apply_current_thread_priority(SDL_THREAD_PRIORITY_HIGH);
    std::unique_lock lock(refill_mutex_);
    while (!stop.stop_requested()) {
        refill_ready_.wait(lock, [&] { return stop.stop_requested() || refill_enabled_; });
        if (stop.stop_requested()) return;
        const auto generation = refill_generation_;
        lock.unlock();
        audio::preview_session_pump_result_t result;
        try {
            result = session_.pump();
        } catch (...) {
            result.status = audio::preview_session_pump_status_t::decode_failed;
        }
        lock.lock();
        if (generation == refill_generation_ && refill_enabled_ &&
            (!refill_result_ || static_cast<bool>(*refill_result_))) {
            if (result.status == audio::preview_session_pump_status_t::not_started)
                refill_enabled_ = false;
            if (result.status == audio::preview_session_pump_status_t::loop_boundary)
                refill_enabled_ = false;
            refill_result_ = std::move(result);
        }
        refill_ready_.wait_for(lock, std::chrono::milliseconds(10), [&] {
            return stop.stop_requested() || generation != refill_generation_ || !refill_enabled_;
        });
    }
}

auto sdl_preview_output_t::pause_device() noexcept -> bool {
    return pause_audio_stream_device(stream_);
}

auto sdl_preview_output_t::resume_device() noexcept -> bool {
    return resume_audio_stream_device(stream_);
}

auto sdl_preview_output_t::finish_start(sdl_preview_start_result_t result) -> sdl_preview_start_result_t {
    if (result.status == sdl_preview_start_status_t::session_failed && !playing()) {
        set_refill_enabled(false);
        return result;
    }
    if (result.status == sdl_preview_start_status_t::device_failed || !resume_device()) {
        if (result.status != sdl_preview_start_status_t::device_failed) {
            result.status = sdl_preview_start_status_t::device_failed;
            result.message = sdl_error("could not reset and resume the chart-preview audio stream");
        }
        static_cast<void>(stop());
        return result;
    }
    set_refill_enabled(playing() && session_.active());
    return result;
}

auto sdl_preview_output_t::start(
    const preview_schedule_t& schedule,
    std::span<const audio::preview_object_source_binding_t> objects,
    std::span<const audio::preview_metronome_source_binding_t> metronome, std::uint64_t seek_frame,
    std::uint64_t minimum_end_frame) -> sdl_preview_start_result_t {
    if (stream_ == nullptr) {
        return {.status = sdl_preview_start_status_t::unavailable,
                .session = std::nullopt,
                .message = "default chart-preview audio output is unavailable"};
    }
    if (!pause_device()) {
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt,
                .message = sdl_error("could not reset and resume the chart-preview audio stream")};
    }
    audio_stream_lock_t stream_lock{stream_};
    if (!stream_lock) {
        const auto message = sdl_error("could not lock the chart-preview audio stream");
        if (playing()) static_cast<void>(resume_device());
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt, .message = message};
    }
    set_refill_enabled(false);
    silenced_voices_.store(0, std::memory_order_release);
    silenced_music_voices_.store(0, std::memory_order_release);
    const auto loop = session_.loop_region();
    const auto start_frame = loop ? loop->begin_frame : seek_frame;
    auto started = session_.start(schedule, objects, metronome, start_frame, {}, minimum_end_frame);
    if (started) {
        retained_schedule_ = &schedule;
        retained_objects_ = objects;
        retained_metronome_ = metronome;
        source_end_frame_ = schedule_end(schedule, objects, metronome, minimum_end_frame);
        reset_output(start_frame);
    }
    auto result = sdl_preview_start_result_t{.session = started};
    const bool preserved = !started && session_.active();
    const auto cleared = preserved || SDL_ClearAudioStream(stream_);
    if (!preserved) callback_failure_.store(callback_failure_t::none, std::memory_order_release);
    output_playing_.store(static_cast<bool>(started) || preserved, std::memory_order_release);
    if (!started) {
        result.status = sdl_preview_start_status_t::session_failed;
        result.message = "chart-preview session could not be started and primed";
    } else if (!cleared) {
        result.status = sdl_preview_start_status_t::device_failed;
        result.message = sdl_error("could not reset and resume the chart-preview audio stream");
    } else result.status = sdl_preview_start_status_t::started;
    publish_output_playhead();
    output_playhead_cleared_.store(!playing(), std::memory_order_release);
    stream_lock.unlock();
    return finish_start(std::move(result));
}

auto sdl_preview_output_t::seek(std::uint64_t sample_frame) -> sdl_preview_start_result_t {
    loop_restart_pending_ = false;
    loop_disable_pending_.store(false, std::memory_order_release);
    if (stream_ == nullptr) {
        return {.status = sdl_preview_start_status_t::unavailable,
                .session = std::nullopt,
                .message = "default chart-preview audio output is unavailable"};
    }
    if (!pause_device()) {
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt,
                .message = sdl_error("could not reset and resume the chart-preview audio stream")};
    }
    audio_stream_lock_t stream_lock{stream_};
    if (!stream_lock) {
        const auto message = sdl_error("could not lock the chart-preview audio stream");
        if (playing()) static_cast<void>(resume_device());
        return {.status = sdl_preview_start_status_t::device_failed,
                .session = std::nullopt, .message = message};
    }
    set_refill_enabled(false);
    silenced_voices_.store(0, std::memory_order_release);
    silenced_music_voices_.store(0, std::memory_order_release);
    const bool preserved_tail = source_finished_ && playing() && !session_.active();
    auto started = !session_.active() && playing() && retained_schedule_
        ? session_.start(*retained_schedule_, retained_objects_, retained_metronome_, sample_frame, {}, source_end_frame_)
        : session_.seek(sample_frame);
    auto result = sdl_preview_start_result_t{.session = started};
    if (!started) {
        result.status = sdl_preview_start_status_t::session_failed;
        result.message = "chart-preview seek could not be materialized and primed";
        if (!session_.active() && !preserved_tail) {
            output_playing_.store(false, std::memory_order_release);
            static_cast<void>(SDL_ClearAudioStream(stream_));
        }
    } else {
        reset_output(sample_frame);
        callback_failure_.store(callback_failure_t::none, std::memory_order_release);
        output_playing_.store(true, std::memory_order_release);
        result.status = SDL_ClearAudioStream(stream_) ? sdl_preview_start_status_t::started
                                                    : sdl_preview_start_status_t::device_failed;
        if (result.status == sdl_preview_start_status_t::device_failed)
            result.message = sdl_error("could not reset and resume the chart-preview audio stream");
    }
    publish_output_playhead();
    output_playhead_cleared_.store(!playing(), std::memory_order_release);
    stream_lock.unlock();
    return finish_start(std::move(result));
}

auto sdl_preview_output_t::set_loop_region(
    std::optional<audio::preview_loop_region_t> region) -> bool {
    const auto was_looping = loop_enabled_.load(std::memory_order_acquire);
    const auto old_end = loop_end_frame_.load(std::memory_order_relaxed);
    const bool reached_boundary = !region && was_looping &&
        rendered_source_frame_.load(std::memory_order_acquire) >= old_end;
    const bool drain_boundary = !region && (loop_restart_pending_ || reached_boundary);
    if (drain_boundary) loop_disable_pending_.store(true, std::memory_order_release);
    if (!session_.set_loop_region(region)) {
        loop_disable_pending_.store(false, std::memory_order_release);
        return false;
    }
    if (region) loop_end_frame_.store(region->end_frame, std::memory_order_relaxed);
    if (!region) {
        if (drain_boundary) {
            loop_enabled_.store(false, std::memory_order_release);
            set_refill_enabled(false);
        } else {
            loop_enabled_.store(false, std::memory_order_release);
            set_refill_enabled(playing() && session_.active());
        }
        loop_restart_pending_ = false;
    } else {
        loop_enabled_.store(true, std::memory_order_release);
    }
    return true;
}

auto sdl_preview_output_t::service() -> std::optional<sdl_preview_service_result_t> {
    const auto rate_unavailable = rate_unavailable_.exchange(false, std::memory_order_acq_rel);
    const auto silenced_voices = silenced_voices_.exchange(0, std::memory_order_acq_rel);
    const auto silenced_music_voices =
        silenced_music_voices_.exchange(0, std::memory_order_acq_rel);
    const auto callback_failure =
        callback_failure_.exchange(callback_failure_t::none, std::memory_order_acq_rel);
    if (callback_failure != callback_failure_t::none) {
        return sdl_preview_service_result_t{
            .status = callback_failure == callback_failure_t::device
                          ? sdl_preview_service_status_t::device_failed
                          : sdl_preview_service_status_t::render_failed,
            .pump = std::nullopt,
            .rate_unavailable = rate_unavailable,
            .silenced_voices = silenced_voices,
            .silenced_music_voices = silenced_music_voices,
        };
    }
    if (loop_disable_pending_.load(std::memory_order_acquire)) {
        if (published_latency_frames() != 0) return std::nullopt;
        loop_disable_pending_.store(false, std::memory_order_release);
        const auto resumed = seek(loop_end_frame_.load(std::memory_order_relaxed));
        return sdl_preview_service_result_t{
            .status = resumed ? sdl_preview_service_status_t::ready
                              : sdl_preview_service_status_t::pump_failed,
            .rate_unavailable = rate_unavailable,
            .silenced_voices = silenced_voices,
            .silenced_music_voices = silenced_music_voices,
        };
    }
    if (loop_restart_pending_) {
        const auto restart = service_loop_restart();
        if (restart == loop_restart_status_t::waiting_for_drain) return std::nullopt;
        return sdl_preview_service_result_t{
            .status = restart == loop_restart_status_t::restarted
                          ? sdl_preview_service_status_t::ready
                          : sdl_preview_service_status_t::pump_failed,
            .pump = std::nullopt,
            .rate_unavailable = rate_unavailable,
            .silenced_voices = silenced_voices,
            .silenced_music_voices = silenced_music_voices,
        };
    }
    // Callback publication owns ordinary latency updates; frame service must
    // not wait for its stream lock while active audio is being rendered.
    if (stream_ != nullptr && output_drained_.load(std::memory_order_acquire) && SDL_GetAudioStreamQueued(stream_) == 0)
    {
        output_playing_.store(false, std::memory_order_release);
        if (session_.active()) static_cast<void>(session_.stop());
    }
    frame_phase_scope_t pump(frame_phase_t::audio_pump);
    return service_refill_result(take_refill_result(), rate_unavailable,
                                 silenced_voices, silenced_music_voices);
}

auto sdl_preview_output_t::service_refill_result(
    std::optional<audio::preview_session_pump_result_t> pumped,
    bool rate_unavailable, std::uint64_t silenced_voices,
    std::uint64_t silenced_music_voices)
    -> std::optional<sdl_preview_service_result_t> {
    if (pumped && pumped->status == audio::preview_session_pump_status_t::not_started)
        pumped.reset();
    if (!pumped) {
        if (rate_unavailable || silenced_voices || silenced_music_voices)
            return sdl_preview_service_result_t{.rate_unavailable = rate_unavailable,
                .silenced_voices = silenced_voices, .silenced_music_voices = silenced_music_voices};
        return std::nullopt;
    }
    if (pumped->decode && pumped->decode->status == audio::preview_pump_status_t::stale_generation)
        return std::nullopt;
    if (pumped->status == audio::preview_session_pump_status_t::loop_boundary)
        return service_loop_boundary(std::move(*pumped), rate_unavailable,
                                     silenced_voices, silenced_music_voices);
    return sdl_preview_service_result_t{
        .status = *pumped ? sdl_preview_service_status_t::ready
                         : sdl_preview_service_status_t::pump_failed,
        .pump = std::move(pumped),
        .rate_unavailable = rate_unavailable,
        .silenced_voices = silenced_voices,
        .silenced_music_voices = silenced_music_voices,
    };
}

auto sdl_preview_output_t::service_loop_boundary(
    audio::preview_session_pump_result_t pumped, bool rate_unavailable,
    std::uint64_t silenced_voices, std::uint64_t silenced_music_voices)
    -> std::optional<sdl_preview_service_result_t> {
    loop_restart_pending_ = true;
    const auto restart = service_loop_restart();
    if (restart == loop_restart_status_t::waiting_for_drain) return std::nullopt;
    if (restart != loop_restart_status_t::restarted)
        pumped.status = audio::preview_session_pump_status_t::materialize_failed;
    else
        pumped.status = audio::preview_session_pump_status_t::ready;
    return sdl_preview_service_result_t{
        .status = pumped ? sdl_preview_service_status_t::ready
                         : sdl_preview_service_status_t::pump_failed,
        .pump = std::move(pumped),
        .rate_unavailable = rate_unavailable,
        .silenced_voices = silenced_voices,
        .silenced_music_voices = silenced_music_voices,
    };
}

auto sdl_preview_output_t::service_loop_restart() -> loop_restart_status_t {
    if (!loop_restart_pending_) return loop_restart_status_t::none;
    if (published_latency_frames() != 0) return loop_restart_status_t::waiting_for_drain;
    loop_restart_pending_ = false;
    if (!playing()) return loop_restart_status_t::failed;
    const auto loop = session_.loop_region();
    if (!loop) return loop_restart_status_t::failed;
    return static_cast<bool>(seek(loop->begin_frame)) ? loop_restart_status_t::restarted
                                                       : loop_restart_status_t::failed;
}

auto sdl_preview_output_t::stop() noexcept -> audio::transport_stop_result_t {
    loop_enabled_.store(false, std::memory_order_release);
    loop_restart_pending_ = false;
    loop_disable_pending_.store(false, std::memory_order_release);
    set_refill_enabled(false);
    output_playing_.store(false, std::memory_order_release);
    output_playhead_cleared_.store(true, std::memory_order_release);
    buffered_source_frames_.store(0, std::memory_order_release);
    published_latency_frames_.store(0, std::memory_order_release);
    rendered_source_frame_.store(0, std::memory_order_release);
    output_start_frame_.store(0, std::memory_order_release);
    if (stream_ == nullptr) {
        callback_failure_.store(callback_failure_t::none, std::memory_order_release);
        silenced_voices_.store(0, std::memory_order_release);
        silenced_music_voices_.store(0, std::memory_order_release);
        return session_.stop();
    }
    static_cast<void>(pause_device());
    audio_stream_lock_t stream_lock{stream_};
    if (!stream_lock) {
        auto stopped = session_.stop();
        static_cast<void>(SDL_ClearAudioStream(stream_));
        callback_failure_.store(callback_failure_t::none, std::memory_order_release);
        silenced_voices_.store(0, std::memory_order_release);
        silenced_music_voices_.store(0, std::memory_order_release);
        return stopped;
    }
    auto stopped = session_.stop();
    static_cast<void>(SDL_ClearAudioStream(stream_));
    callback_failure_.store(callback_failure_t::none, std::memory_order_release);
    silenced_voices_.store(0, std::memory_order_release);
    silenced_music_voices_.store(0, std::memory_order_release);
    buffered_source_frames_.store(0, std::memory_order_release);
    rendered_source_frame_.store(0, std::memory_order_release);
    output_start_frame_.store(0, std::memory_order_release);
    clear_output_playhead();
    stream_lock.unlock();
    return stopped;
}

auto sdl_preview_output_t::set_playback_rate(rational_t rate) noexcept -> bool {
    const auto index = audio::playback_rate_index(rate);
    if (!index) return false;
    requested_rate_index_.store(*index, std::memory_order_release);
    return true;
}

auto sdl_preview_output_t::playback_rate() const noexcept -> rational_t {
    return audio::playback_rates[active_rate_index_.load(std::memory_order_acquire)];
}

auto sdl_preview_output_t::set_keep_pitch(bool enabled) noexcept -> bool {
    requested_keep_pitch_.store(enabled, std::memory_order_release);
    return true;
}

auto sdl_preview_output_t::keep_pitch() const noexcept -> bool {
    return active_keep_pitch_.load(std::memory_order_acquire);
}

auto sdl_preview_output_t::playing() const noexcept -> bool {
    return output_playing_.load(std::memory_order_acquire);
}

auto sdl_preview_output_t::timeline_frame() const noexcept -> std::uint64_t {
    return read_output_playhead().timeline_frame;
}

auto sdl_preview_output_t::read_output_playhead() const noexcept -> audio::playhead_state_t {
    auto state = output_playhead_.read();
    if (output_playhead_cleared_.load(std::memory_order_acquire))
        return {.generation = state.generation};
    const auto current = session_.playhead();
    const bool completed_tail = state.source_finished && current.generation > state.generation &&
                               current.generation - state.generation == 1;
    const bool replaced = current.active
        ? current.transport_generation != state.transport_generation
        : !completed_tail;
    if (replaced) return {.generation = state.generation};
    if (!playing()) state.active = false;
    return state;
}

void sdl_preview_output_t::publish_output_playhead() noexcept {
    const auto rendered = rendered_source_frame_.load(std::memory_order_acquire);
    const auto latency = source_latency_frames();
    const auto start = output_start_frame_.load(std::memory_order_acquire);
    const auto audible = rendered > latency ? std::max(start, rendered - latency) : start;
    published_latency_frames_.store(latency, std::memory_order_release);
    session_.set_output_latency_frames(latency);
    output_playhead_.publish({.active = playing(), .timeline_frame = audible,
        .start_frame = start, .generation = output_generation_,
        .transport_generation = output_transport_generation_, .source_finished = source_finished_});
}

void sdl_preview_output_t::clear_output_playhead() noexcept {
    output_generation_ = session_.playhead().generation;
    output_transport_generation_ = 0;
    output_playhead_.publish({.generation = output_generation_});
}

auto sdl_preview_output_t::source_latency_frames() const noexcept -> std::uint64_t {
    if (stream_ == nullptr) return 0;
    const auto queued_bytes = SDL_GetAudioStreamQueued(stream_);
    const auto queued = queued_bytes > 0 ? static_cast<std::uint64_t>(queued_bytes) /
                           (sizeof(float) * audio::output_channel_count) : 0;
    // SDL input is source-clock data in resampling mode, but output-clock data
    // after WSOLA. Only the latter needs the rational rate conversion.
    const auto source_queue = keep_pitch() ? audio::source_frames_for(queued, playback_rate()).value_or(0) : queued;
    return source_queue + buffered_source_frames_.load(std::memory_order_acquire);
}

void sdl_preview_output_t::reset_output(std::uint64_t frame) noexcept {
    const auto current = session_.playhead();
    output_generation_ = current.generation;
    output_transport_generation_ = current.transport_generation;
    output_playing_.store(false, std::memory_order_release);
    source_finished_ = false;
    have_output_ = fading_out_ = switch_ready_ = false;
    output_drained_.store(false, std::memory_order_release);
    rendered_source_frame_.store(frame, std::memory_order_release);
    output_start_frame_.store(frame, std::memory_order_release);
    buffered_source_frames_.store(0, std::memory_order_release);
    static_cast<void>(stretcher_.reset(audio::playback_rates[requested_rate_index_.load()]));
    begin_fade(requested_keep_pitch_.load(), audio::playback_rates[requested_rate_index_.load()]);
}

auto sdl_preview_output_t::apply_output_mode(SDL_AudioStream* stream) noexcept -> bool {
    // These SDL operations acquire only the recursive stream mutex, not the
    // device mutex. Mode changes remain serialized inside the audio callback.
    assert(audio_stream_lock_depth != 0);
    if (source_finished_) return false;
    const auto requested = requested_rate_index_.load(std::memory_order_acquire);
    const auto preserve = requested_keep_pitch_.load(std::memory_order_acquire);
    const auto rate = audio::playback_rates[requested];
    const bool changed = requested != active_rate_index_.load(std::memory_order_acquire) || preserve != keep_pitch();
    if (fading_out_ || (!changed && !switch_ready_)) return false;
    if (have_output_ && !switch_ready_) {
        fading_out_ = true;
        begin_fade(keep_pitch(), playback_rate());
        return false;
    }
    switch_ready_ = false;
    if (!changed) {
        begin_fade(preserve, rate);
        return false;
    }
    const auto ratio = preserve ? 1.0F : static_cast<float>(rate.numerator) / static_cast<float>(rate.denominator);
    if (!SDL_SetAudioStreamFrequencyRatio(stream, ratio)) {
        rate_unavailable_.store(true, std::memory_order_release);
        requested_rate_index_.store(active_rate_index_.load(), std::memory_order_release);
        requested_keep_pitch_.store(keep_pitch(), std::memory_order_release);
        begin_fade(keep_pitch(), playback_rate());
        return false;
    }
    // The old stage ended at zero before its queued conversion is discarded.
    static_cast<void>(SDL_ClearAudioStream(stream));
    active_rate_index_.store(requested, std::memory_order_release);
    active_keep_pitch_.store(preserve, std::memory_order_release);
    static_cast<void>(stretcher_.reset(rate));
    buffered_source_frames_.store(0, std::memory_order_release);
    begin_fade(preserve, rate);
    return true;
}

void SDLCALL sdl_preview_output_t::stream_callback(void* userdata, SDL_AudioStream* stream,
                                                   int additional_amount, int total_amount) {
    auto* output = static_cast<sdl_preview_output_t*>(userdata);
    if (output == nullptr || stream == nullptr) return;
    const audio_stream_callback_scope_t callback_lock;
    const auto old_ratio = output->keep_pitch() ? 1.0L :
        static_cast<long double>(output->playback_rate().numerator) / output->playback_rate().denominator;
    if (output->apply_output_mode(stream)) {
        const auto new_ratio = output->keep_pitch() ? 1.0L :
            static_cast<long double>(output->playback_rate().numerator) / output->playback_rate().denominator;
        // SDL computed this callback's byte budget before the ratio changed.
        // Refill the whole request on the new input clock, including a window
        // of headroom for the freshly reset converter's lookahead.
        const auto bytes = std::ceil(static_cast<long double>(total_amount) * new_ratio / old_ratio) +
            output->fade_total_ * sizeof(float) * audio::output_channel_count;
        additional_amount = static_cast<int>(std::min<long double>(bytes, std::numeric_limits<int>::max()));
    }
    if (additional_amount > 0 && output->playing()) output->render_requested(stream, additional_amount);
    output->publish_output_playhead();
}

auto sdl_preview_output_t::render_source(std::size_t frames) noexcept -> std::size_t {
    if (loop_disable_pending_.load(std::memory_order_acquire)) return 0;
    const auto cursor = rendered_source_frame_.load(std::memory_order_acquire);
    const auto loop_enabled = loop_enabled_.load(std::memory_order_acquire);
    const auto loop_end = loop_end_frame_.load(std::memory_order_relaxed);
    const auto source_end = loop_enabled ? loop_end : source_end_frame_;
    if (loop_enabled && cursor >= loop_end) return 0;
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(frames,
        source_end > cursor ? source_end - cursor : 0));
    if (count == 0 && !loop_enabled) {
        source_finished_ = true;
        stretcher_.finish();
        return 0;
    }
    if (count == 0) return 0;
    const auto rendered = session_.render(std::span{source_scratch_.data(), count * 2});
    if (rendered.status == audio::preview_session_render_status_t::priming) return 0;
    if (rendered.status == audio::preview_session_render_status_t::underrun) {
        silenced_voices_.fetch_add(rendered.mix.dropped_voices, std::memory_order_relaxed);
        silenced_music_voices_.fetch_add(rendered.mix.dropped_music_voices, std::memory_order_relaxed);
    }
    if (rendered.status == audio::preview_session_render_status_t::transport_failed ||
        rendered.status == audio::preview_session_render_status_t::stale_generation ||
        rendered.status == audio::preview_session_render_status_t::stopped) {
        callback_failure_.store(callback_failure_t::render, std::memory_order_release);
        output_playing_.store(false, std::memory_order_release);
        output_playhead_cleared_.store(true, std::memory_order_release);
        static_cast<void>(session_.stop());
        return 0;
    }
    rendered_source_frame_.store(cursor + count, std::memory_order_release);
    if (rendered.status == audio::preview_session_render_status_t::complete ||
        (!loop_enabled && cursor + count == source_end_frame_))
        source_finished_ = true;
    return count;
}

auto sdl_preview_output_t::render_block(std::size_t frames) noexcept -> std::size_t {
    if (!keep_pitch() || playback_rate() == rational_t{1, 1}) {
        const auto count = source_finished_ ? 0 : render_source(frames);
        std::copy_n(source_scratch_.data(), count * 2, scratch_.data());
        output_drained_.store(source_finished_, std::memory_order_release);
        return count;
    }
    std::size_t written{};
    while (written < frames) {
        const auto need = std::min(callback_frames, stretcher_.input_frames_needed(frames - written));
        const auto supplied = !source_finished_ && need != 0 ? render_source(need) : 0;
        if (loop_enabled_.load(std::memory_order_acquire) &&
            rendered_source_frame_.load(std::memory_order_acquire) >=
                loop_end_frame_.load(std::memory_order_relaxed))
            stretcher_.finish();
        const auto result = stretcher_.process(std::span{source_scratch_.data(), supplied * 2},
            std::span{scratch_.data() + written * 2, (frames - written) * 2});
        if (source_finished_) stretcher_.finish();
        written += result.produced_frames;
        if (result.produced_frames == 0 && supplied == 0) break;
    }
    buffered_source_frames_.store(stretcher_.buffered_source_frames(), std::memory_order_release);
    output_drained_.store(!loop_enabled_.load(std::memory_order_acquire) && stretcher_.complete(),
                          std::memory_order_release);
    return written;
}

void sdl_preview_output_t::begin_fade(bool preserve, rational_t rate) noexcept {
    // The interpolation window is always 20ms on the audible output clock.
    // Raw resampling input advances more slowly at reduced playback rates.
    fade_total_ = preserve ? stretcher_.fade_frames() : std::max<std::uint64_t>(1,
        audio::source_frames_for(stretcher_.fade_frames(), rate).value_or(1));
    fade_remaining_ = fade_total_;
}

void sdl_preview_output_t::fade_block(std::size_t frames) noexcept {
    for (std::size_t frame = 0; frame < frames; ++frame) {
        if (fading_out_) {
            const auto weight = static_cast<float>(fade_remaining_ - 1) / static_cast<float>(fade_total_);
            scratch_[frame * 2] *= weight;
            scratch_[frame * 2 + 1] *= weight;
            if (--fade_remaining_ == 0) { fading_out_ = false; switch_ready_ = true; }
        } else if (switch_ready_) {
            scratch_[frame * 2] = scratch_[frame * 2 + 1] = 0.0F;
        } else {
            const auto weight = fade_remaining_ == 0 ? 1.0F :
                1.0F - static_cast<float>(fade_remaining_) / static_cast<float>(fade_total_);
            for (unsigned channel = 0; channel < 2; ++channel)
                scratch_[frame * 2 + channel] *= weight;
            if (fade_remaining_ != 0) --fade_remaining_;
        }
    }
}

void sdl_preview_output_t::render_requested(SDL_AudioStream* stream,
                                            int additional_amount) noexcept {
    constexpr auto bytes_per_frame = sizeof(float) * audio::output_channel_count;
    auto remaining = static_cast<std::size_t>(additional_amount);
    while (remaining != 0) {
        const auto requested_frames =
            std::min(callback_frames, (remaining + bytes_per_frame - 1) / bytes_per_frame);
        const auto render_frames = fading_out_ ? std::min(requested_frames, fade_remaining_) : requested_frames;
        auto rendered_frames = switch_ready_ ? requested_frames : render_block(render_frames);
        if (switch_ready_)
            std::fill_n(scratch_.data(), requested_frames * 2, 0.0F);
        if (fading_out_ && rendered_frames == render_frames) {
            std::fill(scratch_.begin() + rendered_frames * 2, scratch_.begin() + requested_frames * 2, 0.0F);
            rendered_frames = requested_frames;
        }
        if (!playing()) return;
        if (rendered_frames == 0) break;
        fade_block(rendered_frames);
        have_output_ = true;
        const auto rendered_bytes = rendered_frames * bytes_per_frame;
        if (rendered_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !SDL_PutAudioStreamData(stream, scratch_.data(), static_cast<int>(rendered_bytes))) {
            callback_failure_.store(callback_failure_t::device, std::memory_order_release);
            output_playing_.store(false, std::memory_order_release);
            output_playhead_cleared_.store(true, std::memory_order_release);
            static_cast<void>(session_.stop());
            return;
        }
        remaining = remaining > rendered_bytes ? remaining - rendered_bytes : 0;
        // A finite loop iteration also ends the SDL converter input. Without
        // flushing, reduced-rate resampling retains lookahead and never drains.
        const bool loop_boundary = loop_enabled_.load(std::memory_order_acquire) &&
            rendered_source_frame_.load(std::memory_order_acquire) >=
                loop_end_frame_.load(std::memory_order_relaxed);
        if (output_drained_.load(std::memory_order_acquire) || loop_boundary) {
            static_cast<void>(SDL_FlushAudioStream(stream));
            break;
        }
    }
}

} // namespace wormedit::sys
