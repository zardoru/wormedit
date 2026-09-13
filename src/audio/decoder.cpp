#include <wormedit/audio/decoder.h>

#include <miniaudio.h>

#include <limits>
#include <string>
#include <utility>

namespace wormedit::audio {
namespace {

auto decoder_init_file(const std::filesystem::path& path,
                      const ma_decoder_config* config,
                      ma_decoder* decoder) -> ma_result {
#if defined(_WIN32)
    const auto native_path = path.wstring();
    return ma_decoder_init_file_w(native_path.c_str(), config, decoder);
#else
    const auto native_path = path.string();
    return ma_decoder_init_file(native_path.c_str(), config, decoder);
#endif
}

void set_error(std::string* destination, std::string message) {
    if (destination != nullptr) {
        *destination = std::move(message);
    }
}

} // namespace

struct decoder_t::impl_t {
    ma_decoder decoder{};
    stream_info_t stream_info{};
    bool initialized{};

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

auto decoder_t::open(const std::filesystem::path& path,
                     std::string* error_message) -> bool {
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
        set_error(error_message,
                  "could not open audio source (miniaudio result " +
                      std::to_string(static_cast<int>(result)) + ")");
        return false;
    }

    stream_info_t stream_info{};
    ma_uint32 source_channels{};
    ma_uint32 source_rate{};
    ma_uint64 source_frames{};
    result = ma_decoder_get_data_format(&source_decoder, nullptr,
                                        &source_channels, &source_rate, nullptr, 0);
    if (result == MA_SUCCESS) {
        result = ma_decoder_get_length_in_pcm_frames(&source_decoder, &source_frames);
    }
    if (result == MA_SUCCESS) {
        stream_info.source_sample_rate = source_rate;
        stream_info.source_channel_count = source_channels;
        stream_info.source_frame_count = source_frames;
        stream_info.seekable = ma_decoder_seek_to_pcm_frame(&source_decoder, 0) == MA_SUCCESS;
    }
    ma_decoder_uninit(&source_decoder);
    if (result != MA_SUCCESS || source_rate == 0 || source_channels == 0) {
        set_error(error_message,
                  "could not inspect audio source (miniaudio result " +
                      std::to_string(static_cast<int>(result)) + ")");
        return false;
    }

    auto candidate = std::make_unique<impl_t>();
    const auto config = ma_decoder_config_init(ma_format_f32, output_channel_count,
                                                output_sample_rate);
    result = decoder_init_file(path, &config, &candidate->decoder);
    if (result != MA_SUCCESS) {
        set_error(error_message,
                  "could not initialize normalized audio decoder (miniaudio result " +
                      std::to_string(static_cast<int>(result)) + ")");
        return false;
    }
    candidate->initialized = true;

    ma_uint64 output_frames{};
    result = ma_decoder_get_length_in_pcm_frames(&candidate->decoder, &output_frames);
    if (result != MA_SUCCESS) {
        set_error(error_message,
                  "could not determine normalized audio length (miniaudio result " +
                      std::to_string(static_cast<int>(result)) + ")");
        return false;
    }
    stream_info.output_frame_count = output_frames;
    candidate->stream_info = stream_info;
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
    return impl_->stream_info;
}

auto decoder_t::seek(std::uint64_t output_frame) noexcept -> bool {
    if (!impl_ || !impl_->stream_info.seekable ||
        output_frame > impl_->stream_info.output_frame_count) {
        return false;
    }
    return ma_decoder_seek_to_pcm_frame(&impl_->decoder, output_frame) == MA_SUCCESS;
}

auto decoder_t::read(std::span<float> interleaved_stereo,
                     std::size_t requested_frames) noexcept -> read_result_t {
    if (!impl_) {
        return {read_status_t::not_open, 0};
    }
    if (requested_frames > maximum_read_frames) {
        return {read_status_t::request_too_large, 0};
    }
    if (requested_frames > std::numeric_limits<std::size_t>::max() /
                               output_channel_count ||
        interleaved_stereo.size() < requested_frames * output_channel_count) {
        return {read_status_t::insufficient_output_space, 0};
    }
    if (requested_frames == 0) {
        return {read_status_t::success, 0};
    }

    ma_uint64 frames_read{};
    const auto result = ma_decoder_read_pcm_frames(
        &impl_->decoder, interleaved_stereo.data(), requested_frames, &frames_read);
    if (frames_read > requested_frames) {
        return {read_status_t::decode_error, 0};
    }
    if (result == MA_SUCCESS) {
        return {read_status_t::success, static_cast<std::size_t>(frames_read)};
    }
    if (result == MA_AT_END) {
        return {read_status_t::end_of_stream, static_cast<std::size_t>(frames_read)};
    }
    return {read_status_t::decode_error, static_cast<std::size_t>(frames_read)};
}

} // namespace wormedit::audio
