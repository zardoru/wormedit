#include <wormedit/audio/decoder.h>

#include <miniaudio.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace wormedit::audio {
namespace {

auto decoder_init_file(const std::filesystem::path& path, const ma_decoder_config* config,
                       ma_decoder* decoder) -> ma_result {
#if defined(_WIN32)
    const auto native_path = path.wstring();
    return ma_decoder_init_file_w(native_path.c_str(), config, decoder);
#else
    const auto native_path = path.string();
    return ma_decoder_init_file(native_path.c_str(), config, decoder);
#endif
}

// miniaudio reports a length of 0 for Ogg Vorbis opened through its VFS path (every wide-character
// open on Windows), because stb_vorbis then runs in push mode. Count the frames by decoding to the
// end, then rewind; real zero-length sources count as zero either way.
auto count_frames_by_decoding(ma_decoder& decoder) -> std::optional<ma_uint64> {
    ma_format format{};
    ma_uint32 channels{};
    if (ma_decoder_get_data_format(&decoder, &format, &channels, nullptr, nullptr, 0) !=
            MA_SUCCESS ||
        channels == 0) {
        return std::nullopt;
    }
    const auto frame_bytes = ma_get_bytes_per_frame(format, channels);
    if (frame_bytes == 0 || ma_decoder_seek_to_pcm_frame(&decoder, 0) != MA_SUCCESS) {
        return std::nullopt;
    }
    constexpr ma_uint64 chunk_frames = 4096;
    std::vector<std::byte> scratch(static_cast<std::size_t>(chunk_frames * frame_bytes));
    ma_uint64 total = 0;
    while (true) {
        ma_uint64 frames_read = 0;
        const auto result =
            ma_decoder_read_pcm_frames(&decoder, scratch.data(), chunk_frames, &frames_read);
        total += frames_read;
        if (result == MA_AT_END || (result == MA_SUCCESS && frames_read == 0)) {
            break;
        }
        if (result != MA_SUCCESS) {
            return std::nullopt;
        }
    }
    if (ma_decoder_seek_to_pcm_frame(&decoder, 0) != MA_SUCCESS) {
        return std::nullopt;
    }
    return total;
}

auto decoder_length_in_frames(ma_decoder& decoder, ma_uint64& frames) -> ma_result {
    const auto result = ma_decoder_get_length_in_pcm_frames(&decoder, &frames);
    if (result == MA_SUCCESS && frames == 0) {
        if (const auto counted = count_frames_by_decoding(decoder); counted.has_value()) {
            frames = *counted;
        }
    }
    return result;
}

void set_error(std::string* destination, std::string message) {
    if (destination != nullptr) {
        *destination = std::move(message);
    }
}

// Seeking compressed audio can cost time proportional to the stream position,
// so a short forward gap is cheaper to decode through than to seek over.
constexpr std::uint64_t maximum_skip_frames = 4U * maximum_read_frames;

// Without a seek table, seeking an MP3 decodes from the start of the file, which takes
// seconds late in an album-length track. Building the table scans the whole file once at
// open, so only long sources pay for it. Backends other than MP3 ignore the setting.
constexpr ma_uint32 mp3_seek_point_count = 4096;

auto seek_point_count_for(std::uint64_t source_frames, std::uint32_t source_rate) -> ma_uint32 {
    return source_frames >= static_cast<std::uint64_t>(source_rate) * (long_source_frames / output_sample_rate)
               ? mp3_seek_point_count
               : 0;
}

auto skip_decoder_frames(ma_decoder& decoder, std::uint64_t count) -> bool {
    std::vector<float> discard(maximum_read_frames * output_channel_count);
    while (count != 0) {
        const auto chunk = std::min<std::uint64_t>(count, maximum_read_frames);
        ma_uint64 frames_read{};
        const auto result = ma_decoder_read_pcm_frames(&decoder, discard.data(), chunk, &frames_read);
        if (frames_read != chunk || (result != MA_SUCCESS && result != MA_AT_END))
            return false;
        count -= chunk;
    }
    return true;
}

auto position_decoder(ma_decoder& decoder, std::uint64_t frame) -> bool {
    ma_uint64 cursor{};
    if (ma_decoder_get_cursor_in_pcm_frames(&decoder, &cursor) == MA_SUCCESS) {
        if (cursor == frame)
            return true;
        if (cursor < frame && frame - cursor <= maximum_skip_frames &&
            skip_decoder_frames(decoder, frame - cursor))
            return true;
    }
    return ma_decoder_seek_to_pcm_frame(&decoder, frame) == MA_SUCCESS;
}

} // namespace

struct decoder_t::impl_t {
    ma_decoder decoder{};
    stream_info_t stream_info{};
    bool initialized{};
    std::vector<float> memory_pcm;
    std::uint64_t memory_cursor{};
    mutable std::mutex mutex;

    ~impl_t() {
        if (initialized) {
            ma_decoder_uninit(&decoder);
        }
    }
};

decoder_t::decoder_t() = default;
decoder_t::~decoder_t() = default;
decoder_t::decoder_t(decoder_t&&) noexcept = default;
auto decoder_t::operator=(decoder_t&&) noexcept -> decoder_t& = default;

auto decoder_t::open(const std::filesystem::path& path, std::string* error_message,
                     decoder_index_t index) -> bool {
    close();
    if (error_message != nullptr) {
        error_message->clear();
    }
    if (path.empty()) {
        set_error(error_message, "audio path is empty");
        return false;
    }

    ma_decoder source_decoder{};
    auto result = decoder_init_file(path, nullptr, &source_decoder);
    if (result != MA_SUCCESS) {
        set_error(error_message, "could not open audio source (miniaudio result " +
                                     std::to_string(static_cast<int>(result)) + ")");
        return false;
    }

    stream_info_t stream_info{};
    ma_uint32 source_channels{};
    ma_uint32 source_rate{};
    ma_uint64 source_frames{};
    result = ma_decoder_get_data_format(&source_decoder, nullptr, &source_channels, &source_rate,
                                        nullptr, 0);
    if (result == MA_SUCCESS) {
        result = decoder_length_in_frames(source_decoder, source_frames);
    }
    if (result == MA_SUCCESS) {
        stream_info.source_sample_rate = source_rate;
        stream_info.source_channel_count = source_channels;
        stream_info.source_frame_count = source_frames;
        stream_info.seekable = ma_decoder_seek_to_pcm_frame(&source_decoder, 0) == MA_SUCCESS;
    }
    ma_decoder_uninit(&source_decoder);
    if (result != MA_SUCCESS || source_rate == 0 || source_channels == 0) {
        set_error(error_message, "could not inspect audio source (miniaudio result " +
                                     std::to_string(static_cast<int>(result)) + ")");
        return false;
    }

    auto candidate = std::make_unique<impl_t>();
    auto config = ma_decoder_config_init(ma_format_f32, output_channel_count, output_sample_rate);
    if (index == decoder_index_t::build)
        config.seekPointCount = seek_point_count_for(source_frames, source_rate);
    result = decoder_init_file(path, &config, &candidate->decoder);
    if (result != MA_SUCCESS) {
        set_error(error_message,
                  "could not initialize normalized audio decoder (miniaudio result " +
                      std::to_string(static_cast<int>(result)) + ")");
        return false;
    }
    candidate->initialized = true;

    ma_uint64 output_frames{};
    result = decoder_length_in_frames(candidate->decoder, output_frames);
    if (result != MA_SUCCESS) {
        set_error(error_message, "could not determine normalized audio length (miniaudio result " +
                                     std::to_string(static_cast<int>(result)) + ")");
        return false;
    }
    stream_info.output_frame_count = output_frames;
    candidate->stream_info = stream_info;
    impl_ = std::move(candidate);
    return true;
}

auto decoder_t::open_pcm(std::span<const float> interleaved_stereo, std::string* error_message)
    -> bool {
    close();
    if (error_message != nullptr)
        error_message->clear();
    if (interleaved_stereo.empty() || interleaved_stereo.size() % output_channel_count != 0) {
        set_error(error_message, "generated PCM must contain complete nonempty stereo frames");
        return false;
    }
    auto candidate = std::make_unique<impl_t>();
    candidate->memory_pcm.assign(interleaved_stereo.begin(), interleaved_stereo.end());
    const auto frames = candidate->memory_pcm.size() / output_channel_count;
    candidate->stream_info = {.source_sample_rate = output_sample_rate,
                              .source_channel_count = output_channel_count,
                              .source_frame_count = frames,
                              .output_sample_rate = output_sample_rate,
                              .output_channel_count = output_channel_count,
                              .output_frame_count = frames,
                              .seekable = true};
    impl_ = std::move(candidate);
    return true;
}

void decoder_t::close() noexcept {
    impl_.reset();
}

auto decoder_t::is_open() const noexcept -> bool {
    return impl_ != nullptr;
}

auto decoder_t::info() const noexcept -> std::optional<stream_info_t> {
    if (!impl_) {
        return std::nullopt;
    }
    const std::scoped_lock lock(impl_->mutex);
    return impl_->stream_info;
}

auto decoder_t::seek(std::uint64_t output_frame) noexcept -> bool {
    if (!impl_) {
        return false;
    }
    const std::scoped_lock lock(impl_->mutex);
    if (!impl_->memory_pcm.empty()) {
        if (output_frame > impl_->stream_info.output_frame_count)
            return false;
        impl_->memory_cursor = output_frame;
        return true;
    }
    if (!impl_->stream_info.seekable || output_frame > impl_->stream_info.output_frame_count) {
        return false;
    }
    return ma_decoder_seek_to_pcm_frame(&impl_->decoder, output_frame) == MA_SUCCESS;
}

auto read_result_t::from(operation_state_t<read_phase_t> status, std::size_t frames_read) noexcept -> read_result_t {
    return {.status = status, .frames_read = frames_read};
}

auto decoder_t::read_frames(std::span<float> interleaved_stereo,
                            std::size_t requested_frames) noexcept -> read_result_t {
    if (!impl_) {
        return read_result_t::from(wormedit::failure_code_t::not_started, 0);
    }
    if (requested_frames > maximum_read_frames) {
        return read_result_t::from(wormedit::failure_code_t::capacity_exceeded, 0);
    }
    if (requested_frames > std::numeric_limits<std::size_t>::max() / output_channel_count ||
        interleaved_stereo.size() < requested_frames * output_channel_count) {
        return read_result_t::from(wormedit::failure_code_t::invalid_request, 0);
    }
    if (requested_frames == 0) {
        return read_result_t::from(read_phase_t::success, 0);
    }

    const std::scoped_lock lock(impl_->mutex);
    if (!impl_->memory_pcm.empty()) {
        const auto remaining = impl_->stream_info.output_frame_count - impl_->memory_cursor;
        const auto frames =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, requested_frames));
        const auto begin = static_cast<std::size_t>(impl_->memory_cursor) * output_channel_count;
        std::copy_n(impl_->memory_pcm.begin() + static_cast<std::ptrdiff_t>(begin),
                    frames * output_channel_count, interleaved_stereo.begin());
        impl_->memory_cursor += frames;
        return read_result_t::from(frames == requested_frames ? read_phase_t::success
                                                              : read_phase_t::end_of_stream,
                                   frames);
    }
    ma_uint64 frames_read{};
    const auto result = ma_decoder_read_pcm_frames(&impl_->decoder, interleaved_stereo.data(),
                                                   requested_frames, &frames_read);
    if (frames_read > requested_frames) {
        return read_result_t::from(wormedit::failure_code_t::decode_failed, 0);
    }
    if (result == MA_SUCCESS) {
        return read_result_t::from(read_phase_t::success, static_cast<std::size_t>(frames_read));
    }
    if (result == MA_AT_END) {
        return read_result_t::from(read_phase_t::end_of_stream,
                                   static_cast<std::size_t>(frames_read));
    }
    return read_result_t::from(wormedit::failure_code_t::decode_failed, static_cast<std::size_t>(frames_read));
}

auto decoder_t::read_frames_at(std::uint64_t output_frame, std::span<float> interleaved_stereo,
                               std::size_t requested_frames) noexcept -> read_result_t {
    if (!impl_) {
        return read_result_t::from(wormedit::failure_code_t::not_started, 0);
    }
    if (requested_frames > maximum_read_frames) {
        return read_result_t::from(wormedit::failure_code_t::capacity_exceeded, 0);
    }
    if (requested_frames > std::numeric_limits<std::size_t>::max() / output_channel_count ||
        interleaved_stereo.size() < requested_frames * output_channel_count) {
        return read_result_t::from(wormedit::failure_code_t::invalid_request, 0);
    }

    const std::scoped_lock lock(impl_->mutex);
    if (!impl_->memory_pcm.empty()) {
        if (output_frame > impl_->stream_info.output_frame_count)
            return read_result_t::from(wormedit::failure_code_t::seek_failed, 0);
        const auto remaining = impl_->stream_info.output_frame_count - output_frame;
        const auto frames =
            static_cast<std::size_t>(std::min<std::uint64_t>(remaining, requested_frames));
        const auto begin = static_cast<std::size_t>(output_frame) * output_channel_count;
        std::copy_n(impl_->memory_pcm.begin() + static_cast<std::ptrdiff_t>(begin),
                    frames * output_channel_count, interleaved_stereo.begin());
        return read_result_t::from(frames == requested_frames ? read_phase_t::success
                                                              : read_phase_t::end_of_stream,
                                   frames);
    }
    if (!impl_->stream_info.seekable || output_frame > impl_->stream_info.output_frame_count ||
        !position_decoder(impl_->decoder, output_frame)) {
        return read_result_t::from(wormedit::failure_code_t::seek_failed, 0);
    }
    if (requested_frames == 0) {
        return read_result_t::from(read_phase_t::success, 0);
    }
    ma_uint64 frames_read{};
    const auto result = ma_decoder_read_pcm_frames(&impl_->decoder, interleaved_stereo.data(),
                                                   requested_frames, &frames_read);
    if (frames_read > requested_frames) {
        return read_result_t::from(wormedit::failure_code_t::decode_failed, 0);
    }
    if (result == MA_SUCCESS) {
        return read_result_t::from(read_phase_t::success, static_cast<std::size_t>(frames_read));
    }
    if (result == MA_AT_END) {
        return read_result_t::from(read_phase_t::end_of_stream,
                                   static_cast<std::size_t>(frames_read));
    }
    return read_result_t::from(wormedit::failure_code_t::decode_failed, static_cast<std::size_t>(frames_read));
}

auto decoder_t::read_at(std::uint64_t output_frame, std::span<float> interleaved_stereo,
                        std::size_t requested_frames) noexcept -> read_result_t {
    return read_frames_at(output_frame, interleaved_stereo, requested_frames);
}

} // namespace wormedit::audio
