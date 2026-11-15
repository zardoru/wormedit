#include "sys/audio/sdl_scrub_output.h"

#include "sys/app/thread_priority.h"
#include "sys/audio/sdl_audio_output_config.h"
#include "sys/audio/sdl_audio_stream_lock.h"

#include <SDL3/SDL_error.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <utility>

namespace wormedit::sys {
namespace {

// Blocks decoded ahead of the needle: a second of travel at normal speed, but never fewer than
// two blocks so a fast flick has something to read. Behind it a block stays for a reversal.
constexpr std::uint64_t minimum_ahead_frames = 2 * audio::maximum_read_frames;
constexpr std::uint64_t maximum_ahead_frames = 6 * audio::maximum_read_frames;
constexpr std::uint64_t behind_frames = audio::maximum_read_frames;
// Sixteen blocks, about six seconds of audio (2 MiB).
constexpr std::size_t cache_blocks = 16;

auto cache_limits() -> audio::block_cache_limits_t {
    const auto frames = cache_blocks * audio::maximum_read_frames;
    return {.maximum_bytes = frames * sizeof(float) * audio::output_channel_count,
            .maximum_frames = frames,
            .maximum_bytes_per_asset = frames * sizeof(float) * audio::output_channel_count,
            .maximum_frames_per_asset = frames};
}

} // namespace

sdl_scrub_output_t::sdl_scrub_output_t(audio::decoded_source_registry_t& sources)
    : sources_(sources), cache_(cache_limits()), reader_(cache_) {}

sdl_scrub_output_t::~sdl_scrub_output_t() { close(); }

auto sdl_scrub_output_t::open(std::string* error_message, SDL_AudioDeviceID device,
                              std::uint32_t buffer_frames) -> bool {
    if (stream_ != nullptr) return true;
    const SDL_AudioSpec spec{
        .format = SDL_AUDIO_F32,
        .channels = static_cast<int>(audio::output_channel_count),
        .freq = static_cast<int>(audio::output_sample_rate),
    };
    stream_ = open_audio_output_stream(device, buffer_frames, spec,
                                      &sdl_scrub_output_t::stream_callback, this, error_message);
    if (stream_ == nullptr) {
        if (error_message != nullptr) {
            const auto* message = SDL_GetError();
            *error_message = message != nullptr && message[0] != '\0'
                                 ? std::string(message) : "could not open the scrub audio output";
        }
        return false;
    }
    static_cast<void>(SDL_SetAudioStreamGain(stream_, static_cast<float>(volume_) / 100.0F));
    // The device only runs while a scrub is in progress.
    static_cast<void>(pause_audio_stream_device(stream_));
    try {
        worker_ = std::jthread([this](std::stop_token stop) { refill_loop(stop); });
    } catch (...) {
        if (error_message) *error_message = "could not start the scrub refill worker";
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
        return false;
    }
    return true;
}

void sdl_scrub_output_t::close() noexcept {
    if (stream_ == nullptr) return;
    active_.store(false, std::memory_order_release);
    static_cast<void>(pause_audio_stream_device(stream_));
    {
        const std::scoped_lock lock(worker_mutex_);
        worker_.request_stop();
    }
    worker_ready_.notify_all();
    if (worker_.joinable()) worker_.join();
    SDL_DestroyAudioStream(stream_);
    stream_ = nullptr;
}

auto sdl_scrub_output_t::set_volume(std::uint32_t percent) -> bool {
    if (percent > 100) return false;
    if (stream_ && !SDL_SetAudioStreamGain(stream_, static_cast<float>(percent) / 100.0F)) return false;
    volume_ = percent;
    return true;
}

void sdl_scrub_output_t::set_segments(std::vector<audio::scrub_segment_t> segments) {
    if (segments == segments_) return;
    {
        // The worker reads `segments_` under this mutex, the callback under the stream lock.
        const std::scoped_lock lock(worker_mutex_);
        segments_ = segments;
    }
    audio_stream_lock_t stream_lock{stream_};
    reader_.set_segments(std::move(segments));
}

auto sdl_scrub_output_t::begin(std::uint64_t timeline_frame) -> bool {
    if (stream_ == nullptr) return false;
    {
        audio_stream_lock_t stream_lock{stream_};
        if (!stream_lock) return false;
        reader_.reset(static_cast<double>(timeline_frame));
        needle_frame_.store(static_cast<double>(timeline_frame), std::memory_order_release);
        needle_speed_.store(0.0, std::memory_order_release);
        silent_.store(true, std::memory_order_release);
    }
    target_speed_.store(0.0, std::memory_order_release);
    hold_.store(false, std::memory_order_release);
    ending_.store(false, std::memory_order_release);
    active_.store(true, std::memory_order_release);
    worker_ready_.notify_all();
    return resume_audio_stream_device(stream_);
}

void sdl_scrub_output_t::set_motion(double speed, bool hold) noexcept {
    target_speed_.store(speed, std::memory_order_release);
    hold_.store(hold, std::memory_order_release);
}

void sdl_scrub_output_t::end() noexcept {
    hold_.store(true, std::memory_order_release);
    ending_.store(true, std::memory_order_release);
}

void sdl_scrub_output_t::service() noexcept {
    if (!active_.load(std::memory_order_acquire) || !ending_.load(std::memory_order_acquire)) return;
    if (!silent_.load(std::memory_order_acquire)) return;
    active_.store(false, std::memory_order_release);
    ending_.store(false, std::memory_order_release);
    if (stream_ != nullptr) {
        static_cast<void>(pause_audio_stream_device(stream_));
        static_cast<void>(SDL_ClearAudioStream(stream_));
    }
}

void SDLCALL sdl_scrub_output_t::stream_callback(void* userdata, SDL_AudioStream* stream,
                                                 int additional_amount, int) {
    auto* output = static_cast<sdl_scrub_output_t*>(userdata);
    if (output == nullptr || stream == nullptr || additional_amount <= 0) return;
    const audio_stream_callback_scope_t callback_lock;
    output->render_requested(stream, additional_amount);
}

void sdl_scrub_output_t::render_requested(SDL_AudioStream* stream, int additional_amount) noexcept {
    constexpr auto bytes_per_frame = sizeof(float) * audio::output_channel_count;
    auto remaining = static_cast<std::size_t>(additional_amount);
    while (remaining != 0) {
        const auto frames = std::min(callback_frames, (remaining + bytes_per_frame - 1) / bytes_per_frame);
        const auto samples = frames * audio::output_channel_count;
        const auto bytes = samples * sizeof(float);
        static_cast<void>(reader_.render(target_speed_.load(std::memory_order_acquire),
                                         hold_.load(std::memory_order_acquire),
                                         std::span{scratch_.data(), samples}));
        needle_frame_.store(reader_.position_frames(), std::memory_order_release);
        needle_speed_.store(reader_.speed(), std::memory_order_release);
        silent_.store(reader_.is_silent(), std::memory_order_release);
        if (bytes > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
            !SDL_PutAudioStreamData(stream, scratch_.data(), static_cast<int>(bytes)))
            return;
        remaining = remaining > bytes ? remaining - bytes : 0;
    }
}

void sdl_scrub_output_t::refill_loop(std::stop_token stop) {
    apply_current_thread_priority(SDL_THREAD_PRIORITY_HIGH);
    std::vector<audio::audio_block_key_t> wanted;
    // Blocks the decoder could not produce; not retried until the next scrub.
    std::vector<audio::audio_block_key_t> failed;
    std::unique_lock lock(worker_mutex_);
    while (!stop.stop_requested()) {
        const auto idle = !active_.load(std::memory_order_acquire);
        worker_ready_.wait_for(lock, std::chrono::milliseconds(idle ? 50 : 4),
                               [&] { return stop.stop_requested(); });
        if (stop.stop_requested()) return;
        if (!active_.load(std::memory_order_acquire)) {
            failed.clear();
            continue;
        }
        const auto position = needle_frame_.load(std::memory_order_acquire);
        const auto speed = needle_speed_.load(std::memory_order_acquire);
        const auto target = target_speed_.load(std::memory_order_acquire);
        // Plan for where the needle is heading as well as where it is going now.
        const auto heading = std::abs(target) > std::abs(speed) ? target : speed;
        const auto ahead = std::clamp(static_cast<std::uint64_t>(std::abs(heading) * audio::output_sample_rate / 2),
                                      minimum_ahead_frames, maximum_ahead_frames);
        audio::scrub_reader_t::plan_blocks(segments_, position, ahead, behind_frames, heading, wanted);
        // Decoding can take a while: work on a copy of the next missing key without the lock.
        auto missing = std::ranges::find_if(wanted, [&](const auto& key) {
            return !cache_.has_block(key) && std::ranges::find(failed, key) == failed.end();
        });
        if (missing == wanted.end()) continue;
        const auto key = *missing;
        lock.unlock();
        auto decoded = false;
        try {
            decoded = sources_.decode_block_into(cache_, key);
        } catch (...) {
        }
        lock.lock();
        if (!decoded) failed.push_back(key);
    }
}

} // namespace wormedit::sys
