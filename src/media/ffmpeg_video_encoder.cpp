#include <wormedit/media/video_encoder.h>
#include <wormedit/core/atomic_file_storage.h>

#include "../core/checked_math.h"
#include "../core/text_util.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <exception>
#include <string_view>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#if defined(WORMEDIT_HAS_VIDEO_ENCODER) && WORMEDIT_HAS_VIDEO_ENCODER
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#endif

namespace wormedit::media {

auto video_encoder_options_t::input_audio_frames() const -> result_t<std::uint64_t, failure_code_t> {
    if (clock.denominator != 1 || (clock.numerator != 30 && clock.numerator != 60))
        return std::unexpected(failure_code_t::invalid_request);
    const auto frames = checked_math::checked_multiply(frame_count, audio::output_sample_rate / clock.numerator);
    if (!frames) return std::unexpected(failure_code_t::arithmetic_overflow);
    return *frames;
}
auto video_encoder_options_t::scene_time_at(std::uint64_t index) const -> result_t<storyboard_time_t, failure_code_t> {
    if (clock.denominator != 1 || (clock.numerator != 30 && clock.numerator != 60))
        return std::unexpected(failure_code_t::invalid_request);
    const auto offset = checked_math::checked_multiply(index, audio::output_sample_rate / clock.numerator);
    std::uint64_t frame{};
    if (!offset || !checked_math::checked_add(begin_audio_frame, *offset, frame))
        return std::unexpected(failure_code_t::arithmetic_overflow);
    return storyboard_time_t::from_frame(frame, audio::output_sample_rate);
}
auto video_encoder_options_t::is_valid() const -> bool {
    constexpr std::array resolutions{std::pair{640U, 480U}, std::pair{1280U, 720U},
        std::pair{1920U, 1080U}, std::pair{3840U, 2160U}};
    if (destination.empty() || destination.filename().empty() || frame_count == 0 || threads == 0 || threads > 64 ||
        maximum_output_bytes == 0 || maximum_output_bytes > std::numeric_limits<std::size_t>::max() ||
        std::ranges::find(resolutions, std::pair{width, height}) == resolutions.end()) return false;
    if (profile != video_profile_t::mp4_h264_aac && profile != video_profile_t::webm_vp9_opus) return false;
    if (conditional_layer != storyboard_conditional_layer_t::pass && conditional_layer != storyboard_conditional_layer_t::fail &&
        conditional_layer != storyboard_conditional_layer_t::neither) return false;
    if (!input_audio_frames() || !scene_time_at(frame_count)) return false;
    const auto output_frames = checked_math::checked_multiply(frame_count, 48'000 / clock.numerator);
    return output_frames && *output_frames <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
}
auto video_encoder_options_t::colour_matrix() const -> video_colour_matrix_t {
    return width == 640 && height == 480 ? video_colour_matrix_t::bt601 : video_colour_matrix_t::bt709;
}
auto video_encoder_options_t::estimated_video_bit_rate() const -> std::uint64_t {
    return static_cast<std::uint64_t>(width) * height * 4;
}
auto video_encoder_options_t::h264_level() const -> int {
    if (height == 2160) return clock.numerator == 60 ? 52 : 51;
    if (height == 1080) return clock.numerator == 60 ? 42 : 40;
    if (height == 720) return clock.numerator == 60 ? 32 : 31;
    return clock.numerator == 60 ? 31 : 30;
}

#if defined(WORMEDIT_HAS_VIDEO_ENCODER) && WORMEDIT_HAS_VIDEO_ENCODER
namespace {
using encode_status_t = result_t<void, error_t<failure_code_t>>;
constexpr int converted_capacity = static_cast<int>(audio::maximum_read_frames * 2);

auto ffmpeg_message(int code) -> std::string {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(code, message.data(), message.size());
    return message.data();
}
struct dictionary_t {
    AVDictionary* value{};
    ~dictionary_t() { av_dict_free(&value); }
    auto put(const char* name, const char* text) -> encode_status_t {
        const auto result = av_dict_set(&value, name, text, 0);
        if (result < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(result)});
        return {};
    }
};
struct colour_tags_t {
    AVColorSpace space;
    AVColorPrimaries primaries;
    AVColorTransferCharacteristic transfer;
    int coefficients;
};
auto colour_tags(video_colour_matrix_t matrix) -> colour_tags_t {
    if (matrix == video_colour_matrix_t::bt601)
        return {AVCOL_SPC_SMPTE170M, AVCOL_PRI_SMPTE170M, AVCOL_TRC_SMPTE170M, SWS_CS_ITU601};
    return {AVCOL_SPC_BT709, AVCOL_PRI_BT709, AVCOL_TRC_BT709, SWS_CS_ITU709};
}
auto selected_video_codec(const video_encoder_options_t& options) -> const AVCodec* {
    if (options.profile == video_profile_t::webm_vp9_opus) return avcodec_find_encoder_by_name("libvpx-vp9");
    if (const auto* codec = avcodec_find_encoder_by_name("libopenh264")) return codec;
#if defined(WORMEDIT_VIDEO_ENCODER_ALLOW_GPL) && WORMEDIT_VIDEO_ENCODER_ALLOW_GPL
    if (options.allow_gpl) return avcodec_find_encoder_by_name("libx264");
#endif
    return nullptr;
}
auto configure_video_options(dictionary_t& dictionary, std::string_view codec, std::uint32_t width) -> encode_status_t {
    if (codec == "libopenh264") return dictionary.put("allow_skip_frames", "0");
    if (codec == "libx264") {
        if (auto result = dictionary.put("preset", "fast"); !result) return result;
        return dictionary.put("tune", "zerolatency");
    }
    const std::array<std::pair<const char*, const char*>, 6> values{{
        {"row-mt", "1"}, {"tile-columns", width >= 3840 ? "2" : "0"},
        {"lag-in-frames", "0"}, {"auto-alt-ref", "0"}, {"cpu-used", "4"}, {"deadline", "good"}}};
    for (const auto& [key, value] : values)
        if (auto result = dictionary.put(key, value); !result) return result;
    return {};
}

auto opaque_frame(const storyboard_raster_frame_t& frame, const video_encoder_options_t& options) -> bool {
    if (frame.width != options.width || frame.height != options.height ||
        frame.pixels.size() != static_cast<std::size_t>(options.width) * options.height * 4) return false;
    for (std::size_t i = 3; i < frame.pixels.size(); i += 4)
        if (frame.pixels[i] != 255) return false;
    return true;
}

class mux_encoder_t {
    struct channel_t {
        AVCodecContext* codec{};
        AVStream* stream{};
        AVFrame* frame{};
        channel_t() = default;
        channel_t(const channel_t&) = delete;
        auto operator=(const channel_t&) -> channel_t& = delete;
        ~channel_t() { av_frame_free(&frame); avcodec_free_context(&codec); }
    };
    const video_encoder_options_t& options_;
    const video_encoder_sources_t& sources_;
    std::stop_token stop_;
    const std::function<void(video_encoder_progress_t)>& progress_;
    AVFormatContext* format_{};
    AVIOContext* io_{};
    prepared_atomic_file_t::stream_t* output_{};
    std::string io_failure_;
    channel_t video_, audio_;
    AVPacket* packet_{};
    SwsContext* scaler_{};
    SwrContext* resampler_{};
    AVAudioFifo* fifo_{};
    AVFrame* converted_{};
    std::uint64_t video_frames_{}, input_frames_{}, audio_frames_{};

    static auto write_packet(void* opaque, const std::uint8_t* data, int size) noexcept -> int {
        try { return write_output(*static_cast<mux_encoder_t*>(opaque), data, size); }
        catch (...) { return AVERROR(ENOMEM); }
    }
    static auto write_output(mux_encoder_t& owner, const std::uint8_t* data, int size) -> int {
        if (owner.stop_.stop_requested()) return AVERROR_EXIT;
        if (size < 0) return AVERROR(EINVAL);
        const auto result = owner.output_->write(std::as_bytes(std::span{data, static_cast<std::size_t>(size)}));
        if (!result) { owner.io_failure_ = result.error(); return AVERROR(EIO); }
        return size;
    }
    static auto seek_packet(void* opaque, std::int64_t offset, int whence) noexcept -> std::int64_t {
        try { return seek_output(*static_cast<mux_encoder_t*>(opaque), offset, whence); }
        catch (...) { return AVERROR(ENOMEM); }
    }
    static auto seek_output(mux_encoder_t& owner, std::int64_t offset, int whence) -> std::int64_t {
        if (owner.stop_.stop_requested()) return AVERROR_EXIT;
        if (whence == AVSEEK_SIZE) return static_cast<std::int64_t>(owner.output_->size());
        prepared_atomic_file_t::seek_origin_t origin;
        switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET: origin = prepared_atomic_file_t::seek_origin_t::begin; break;
        case SEEK_CUR: origin = prepared_atomic_file_t::seek_origin_t::current; break;
        case SEEK_END: origin = prepared_atomic_file_t::seek_origin_t::end; break;
        default: return AVERROR(EINVAL);
        }
        const auto position = owner.output_->seek(offset, origin);
        if (!position) { owner.io_failure_ = position.error(); return AVERROR(EIO); }
        return *position;
    }
    auto open_io(prepared_atomic_file_t::stream_t& output) -> encode_status_t {
        output_ = &output;
        auto* buffer = static_cast<std::uint8_t*>(av_malloc(64 * 1024));
        if (!buffer) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate mux buffer"}});
        io_ = avio_alloc_context(buffer, 64 * 1024, 1, this, nullptr, write_packet, seek_packet);
        if (!io_) { av_free(buffer); return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate mux IO"}}); }
        io_->seekable = AVIO_SEEKABLE_NORMAL;
        format_->pb = io_;
        format_->flags |= AVFMT_FLAG_CUSTOM_IO;
        format_->max_interleave_delta = AV_TIME_BASE;
        format_->avoid_negative_ts = AVFMT_AVOID_NEG_TS_DISABLED;
        return {};
    }
    auto open_channel(channel_t& channel, const AVCodec* codec, dictionary_t& dictionary) -> encode_status_t {
        const auto status = avcodec_open2(channel.codec, codec, &dictionary.value);
        if (status < 0) return std::unexpected(error_t{failure_code_t::unavailable,
            std::string{codec->name} + " rejected selected configuration: " + ffmpeg_message(status)});
        if (dictionary.value) return std::unexpected(error_t{failure_code_t::unavailable,
            "Codec does not support required option: " + std::string{av_dict_get(dictionary.value, "", nullptr, AV_DICT_IGNORE_SUFFIX)->key}});
        channel.stream = avformat_new_stream(format_, nullptr);
        if (!channel.stream) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate mux stream"}});
        channel.stream->time_base = channel.codec->time_base;
        if (channel.codec->codec_type == AVMEDIA_TYPE_VIDEO) channel.stream->avg_frame_rate = channel.codec->framerate;
        const auto copied = avcodec_parameters_from_context(channel.stream->codecpar, channel.codec);
        if (copied < 0) return std::unexpected(error_t{failure_code_t::task_failed, ffmpeg_message(copied)});
        channel.frame = av_frame_alloc();
        if (!channel.frame) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate codec frame"}});
        return {};
    }
    auto open_video() -> encode_status_t {
        const auto* codec = selected_video_codec(options_);
        if (!codec) return std::unexpected(error_t{failure_code_t::unavailable,
            std::string{"Selected video profile encoder unavailable (OpenH264 preferred; GPL encoders require build and request opt-in)."}});
        video_.codec = avcodec_alloc_context3(codec);
        if (!video_.codec) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate video encoder"}});
        auto& context = *video_.codec;
        context.width = static_cast<int>(options_.width);
        context.height = static_cast<int>(options_.height);
        context.time_base = {1, static_cast<int>(options_.clock.numerator)};
        context.framerate = {static_cast<int>(options_.clock.numerator), 1};
        context.pix_fmt = AV_PIX_FMT_YUV420P;
        context.sample_aspect_ratio = {1, 1};
        context.thread_count = static_cast<int>(options_.threads);
        context.bit_rate = static_cast<std::int64_t>(options_.estimated_video_bit_rate());
        context.gop_size = static_cast<int>(options_.clock.numerator * 2);
        context.max_b_frames = 0;
        const auto tags = colour_tags(options_.colour_matrix());
        context.colorspace = tags.space;
        context.color_primaries = tags.primaries;
        context.color_trc = tags.transfer;
        context.color_range = AVCOL_RANGE_MPEG;
        if (options_.profile == video_profile_t::mp4_h264_aac) context.level = options_.h264_level();
        if (format_->oformat->flags & AVFMT_GLOBALHEADER) context.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        dictionary_t dictionary;
        if (auto result = configure_video_options(dictionary, codec->name, options_.width); !result) return result;
        if (auto result = open_channel(video_, codec, dictionary); !result) return result;
        return open_video_conversion();
    }
    auto open_video_conversion() -> encode_status_t {
        auto& frame = *video_.frame;
        frame.format = AV_PIX_FMT_YUV420P;
        frame.width = video_.codec->width;
        frame.height = video_.codec->height;
        frame.color_range = AVCOL_RANGE_MPEG;
        const auto tags = colour_tags(options_.colour_matrix());
        frame.colorspace = tags.space;
        frame.color_primaries = tags.primaries;
        frame.color_trc = tags.transfer;
        const auto allocated = av_frame_get_buffer(&frame, 32);
        if (allocated < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(allocated)});
        scaler_ = sws_getContext(frame.width, frame.height, AV_PIX_FMT_RGBA, frame.width, frame.height,
            AV_PIX_FMT_YUV420P, SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!scaler_) return std::unexpected(error_t{failure_code_t::unavailable, std::string{"RGBA/YUV420P conversion unavailable"}});
        const auto* coefficients = sws_getCoefficients(tags.coefficients);
        const auto colour = sws_setColorspaceDetails(scaler_, coefficients, 1, coefficients, 0, 0, 1 << 16, 1 << 16);
        if (colour < 0) return std::unexpected(error_t{failure_code_t::unavailable, ffmpeg_message(colour)});
        return {};
    }
    auto open_audio() -> encode_status_t {
        if (!sources_.audio) return {};
        const auto* name = options_.profile == video_profile_t::mp4_h264_aac ? "aac" : "libopus";
        const auto* codec = avcodec_find_encoder_by_name(name);
        if (!codec) return std::unexpected(error_t{failure_code_t::unavailable, std::string{name} + " audio encoder unavailable"});
        audio_.codec = avcodec_alloc_context3(codec);
        if (!audio_.codec) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate audio encoder"}});
        auto& context = *audio_.codec;
        context.sample_fmt = options_.profile == video_profile_t::mp4_h264_aac ? AV_SAMPLE_FMT_FLTP : AV_SAMPLE_FMT_FLT;
        context.sample_rate = options_.profile == video_profile_t::mp4_h264_aac ? static_cast<int>(audio::output_sample_rate) : 48'000;
        context.time_base = {1, context.sample_rate};
        context.bit_rate = static_cast<std::int64_t>(video_encoder_options_t::audio_target_bit_rate);
        context.thread_count = 1;
        av_channel_layout_default(&context.ch_layout, 2);
        if (format_->oformat->flags & AVFMT_GLOBALHEADER) context.flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        dictionary_t dictionary;
        if (auto opened = open_channel(audio_, codec, dictionary); !opened) return opened;
        if (context.frame_size <= 0 || context.frame_size > converted_capacity ||
            !(codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME))
            return std::unexpected(error_t{failure_code_t::unavailable, std::string{"Audio encoder cannot preserve bounded final PCM frames"}});
        return open_audio_conversion();
    }
    auto open_audio_conversion() -> encode_status_t {
        auto& context = *audio_.codec;
        audio_.frame->format = context.sample_fmt;
        audio_.frame->sample_rate = context.sample_rate;
        audio_.frame->nb_samples = context.frame_size;
        auto status = av_channel_layout_copy(&audio_.frame->ch_layout, &context.ch_layout);
        if (status >= 0) status = av_frame_get_buffer(audio_.frame, 0);
        if (status < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(status)});
        AVChannelLayout input{};
        av_channel_layout_default(&input, 2);
        status = swr_alloc_set_opts2(&resampler_, &context.ch_layout, context.sample_fmt, context.sample_rate,
            &input, AV_SAMPLE_FMT_FLT, static_cast<int>(audio::output_sample_rate), 0, nullptr);
        av_channel_layout_uninit(&input);
        if (status >= 0) status = swr_init(resampler_);
        if (status < 0) return std::unexpected(error_t{failure_code_t::unavailable, ffmpeg_message(status)});
        fifo_ = av_audio_fifo_alloc(context.sample_fmt, 2, converted_capacity * 2);
        converted_ = av_frame_alloc();
        if (!fifo_ || !converted_) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate bounded PCM conversion"}});
        converted_->format = context.sample_fmt;
        converted_->sample_rate = context.sample_rate;
        converted_->nb_samples = converted_capacity;
        status = av_channel_layout_copy(&converted_->ch_layout, &context.ch_layout);
        if (status >= 0) status = av_frame_get_buffer(converted_, 0);
        if (status < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(status)});
        return {};
    }
    auto receive_packets(channel_t& channel) -> encode_status_t {
        for (;;) {
            if (stop_.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video export cancelled"}});
            const auto status = avcodec_receive_packet(channel.codec, packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return {};
            if (status < 0) return std::unexpected(error_t{failure_code_t::task_failed, ffmpeg_message(status)});
            if (packet_->duration <= 0 && &channel == &video_) packet_->duration = 1;
            av_packet_rescale_ts(packet_, channel.codec->time_base, channel.stream->time_base);
            packet_->stream_index = channel.stream->index;
            const auto written = av_interleaved_write_frame(format_, packet_);
            av_packet_unref(packet_);
            if (written < 0) return std::unexpected(error_t{failure_code_t::io_failed,
                io_failure_.empty() ? ffmpeg_message(written) : io_failure_});
        }
    }
    auto send_frame(channel_t& channel, AVFrame* frame) -> encode_status_t {
        const auto status = avcodec_send_frame(channel.codec, frame);
        if (status < 0) return std::unexpected(error_t{failure_code_t::task_failed, ffmpeg_message(status)});
        return receive_packets(channel);
    }
    auto write_video() -> encode_status_t {
        if (stop_.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video export cancelled"}});
        const auto time = options_.scene_time_at(video_frames_);
        if (!time) return std::unexpected(error_t{time.error(), std::string{"Scene timestamp overflow"}});
        const auto rgba = sources_.frame(video_frames_, *time, options_.conditional_layer, stop_);
        if (!rgba) return std::unexpected(rgba.error());
        if (!opaque_frame(*rgba, options_)) return std::unexpected(error_t{failure_code_t::invalid_geometry,
            std::string{"Frame source must supply the requested opaque RGBA resolution"}});
        const auto writable = av_frame_make_writable(video_.frame);
        if (writable < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(writable)});
        const std::array<const std::uint8_t*, 4> data{rgba->pixels.data(), nullptr, nullptr, nullptr};
        const std::array<int, 4> strides{static_cast<int>(rgba->width * 4), 0, 0, 0};
        if (sws_scale(scaler_, data.data(), strides.data(), 0, static_cast<int>(rgba->height),
                video_.frame->data, video_.frame->linesize) != static_cast<int>(rgba->height))
            return std::unexpected(error_t{failure_code_t::render_failed, std::string{"RGBA conversion did not produce a complete frame"}});
        video_.frame->pts = static_cast<std::int64_t>(video_frames_);
        video_.frame->duration = 1;
        if (auto result = send_frame(video_, video_.frame); !result) return result;
        ++video_frames_;
        if (progress_) progress_({.completed_frames = std::min(video_frames_, options_.frame_count - 1), .total_frames = options_.frame_count});
        return {};
    }
    auto send_audio(int count) -> encode_status_t {
        const auto writable = av_frame_make_writable(audio_.frame);
        if (writable < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(writable)});
        std::array<void*, 2> data{audio_.frame->data[0], audio_.frame->data[1]};
        if (av_audio_fifo_read(fifo_, data.data(), count) != count)
            return std::unexpected(error_t{failure_code_t::render_failed, std::string{"PCM queue was incomplete"}});
        audio_.frame->nb_samples = count;
        audio_.frame->pts = static_cast<std::int64_t>(audio_frames_);
        if (auto result = send_frame(audio_, audio_.frame); !result) return result;
        audio_frames_ += static_cast<std::uint64_t>(count);
        return {};
    }
    auto queue_converted(int count) -> encode_status_t {
        if (count > av_audio_fifo_space(fifo_)) return std::unexpected(error_t{
            failure_code_t::capacity_exceeded, std::string{"PCM conversion exceeded its fixed queue"}});
        std::array<void*, 2> data{converted_->data[0], converted_->data[1]};
        if (av_audio_fifo_write(fifo_, data.data(), count) != count)
            return std::unexpected(error_t{failure_code_t::render_failed, std::string{"Could not queue converted PCM"}});
        while (av_audio_fifo_size(fifo_) >= audio_.codec->frame_size)
            if (auto result = send_audio(audio_.codec->frame_size); !result) return result;
        return {};
    }
    auto write_pcm(std::uint64_t first, std::span<const float> samples) -> encode_status_t {
        if (stop_.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video export cancelled"}});
        const auto total = *options_.input_audio_frames();
        if (first != options_.begin_audio_frame + input_frames_ || samples.empty() || samples.size() % 2 != 0 ||
            samples.size() / 2 > audio::maximum_read_frames || samples.size() / 2 > total - input_frames_ ||
            !std::ranges::all_of(samples, [](float value) { return std::isfinite(value) && value >= -1 && value <= 1; }))
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"PCM source violated its bounded contiguous preview interval"}});
        const auto end = input_frames_ + samples.size() / 2;
        const auto per_frame = audio::output_sample_rate / options_.clock.numerator;
        while (video_frames_ < options_.frame_count && video_frames_ * per_frame < end)
            if (auto result = write_video(); !result) return result;
        const auto writable = av_frame_make_writable(converted_);
        if (writable < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(writable)});
        const std::uint8_t* input[]{reinterpret_cast<const std::uint8_t*>(samples.data())};
        const auto count = swr_convert(resampler_, converted_->data, converted_capacity,
            input, static_cast<int>(samples.size() / 2));
        if (count < 0) return std::unexpected(error_t{failure_code_t::render_failed, ffmpeg_message(count)});
        input_frames_ = end;
        return queue_converted(count);
    }
    auto finish_audio() -> encode_status_t {
        for (;;) {
            const auto writable = av_frame_make_writable(converted_);
            if (writable < 0) return std::unexpected(error_t{failure_code_t::out_of_memory, ffmpeg_message(writable)});
            const auto count = swr_convert(resampler_, converted_->data, converted_capacity, nullptr, 0);
            if (count < 0) return std::unexpected(error_t{failure_code_t::render_failed, ffmpeg_message(count)});
            if (count == 0) break;
            if (auto result = queue_converted(count); !result) return result;
        }
        const auto remaining = av_audio_fifo_size(fifo_);
        if (remaining && !stop_.stop_requested())
            if (auto result = send_audio(remaining); !result) return result;
        const auto expected = options_.frame_count * (static_cast<std::uint64_t>(audio_.codec->sample_rate) / options_.clock.numerator);
        if (audio_frames_ != expected) return std::unexpected(error_t{failure_code_t::render_failed,
            std::string{"Audio resampler did not preserve the exact selected duration"}});
        return send_frame(audio_, nullptr);
    }
  public:
    mux_encoder_t(const video_encoder_options_t& options, const video_encoder_sources_t& sources,
        std::stop_token stop, const std::function<void(video_encoder_progress_t)>& progress)
        : options_(options), sources_(sources), stop_(stop), progress_(progress) {}
    mux_encoder_t(const mux_encoder_t&) = delete;
    auto operator=(const mux_encoder_t&) -> mux_encoder_t& = delete;
    ~mux_encoder_t() {
        av_packet_free(&packet_);
        av_frame_free(&converted_);
        av_audio_fifo_free(fifo_);
        swr_free(&resampler_);
        sws_freeContext(scaler_);
        if (format_) format_->pb = nullptr;
        if (io_) { av_freep(&io_->buffer); avio_context_free(&io_); }
        avformat_free_context(format_);
    }
    auto run(prepared_atomic_file_t::stream_t& output) -> result_t<video_encoder_report_t, error_t<failure_code_t>> {
        const auto* muxer = options_.profile == video_profile_t::mp4_h264_aac ? "mp4" : "webm";
        const auto status = avformat_alloc_output_context2(&format_, nullptr, muxer, nullptr);
        if (status < 0) return std::unexpected(error_t{failure_code_t::unavailable, ffmpeg_message(status)});
        packet_ = av_packet_alloc();
        if (!packet_) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate mux packet"}});
        if (auto result = open_io(output); !result) return std::unexpected(result.error());
        if (auto result = open_video(); !result) return std::unexpected(result.error());
        if (auto result = open_audio(); !result) return std::unexpected(result.error());
        const auto header = avformat_write_header(format_, nullptr);
        if (header < 0) return std::unexpected(error_t{failure_code_t::io_failed, ffmpeg_message(header)});
        if (auto result = produce(); !result) return std::unexpected(result.error());
        const auto trailer = av_write_trailer(format_);
        avio_flush(io_);
        if (trailer < 0 || io_->error < 0) return std::unexpected(error_t{failure_code_t::io_failed,
            io_failure_.empty() ? ffmpeg_message(trailer < 0 ? trailer : io_->error) : io_failure_});
        return report();
    }
    auto produce() -> encode_status_t {
        if (sources_.audio) {
            std::optional<error_t<failure_code_t>> sink_failure;
            const auto sink = [this, &sink_failure](std::uint64_t first, std::span<const float> samples) -> encode_status_t {
                if (sink_failure) return std::unexpected(*sink_failure);
                auto result = write_pcm(first, samples);
                if (!result) sink_failure = result.error();
                return result;
            };
            if (auto result = sources_.audio(sink, stop_); !result) return result;
            if (sink_failure) return std::unexpected(*sink_failure);
            if (input_frames_ != *options_.input_audio_frames()) return std::unexpected(error_t{
                failure_code_t::render_failed, std::string{"PCM source ended before selected video duration"}});
        }
        while (video_frames_ < options_.frame_count)
            if (auto result = write_video(); !result) return result;
        if (auto result = send_frame(video_, nullptr); !result) return result;
        if (sources_.audio) return finish_audio();
        return {};
    }
    auto report() const -> video_encoder_report_t {
        return {.video_frames = video_frames_, .audio_frames = audio_frames_,
            .audio_sample_rate = audio_.codec ? static_cast<std::uint32_t>(audio_.codec->sample_rate) : 0,
            .video_encoder = video_.codec->codec->name,
            .audio_encoder = audio_.codec ? audio_.codec->codec->name : "",
            .ffmpeg_version = av_version_info(), .ffmpeg_configuration = avcodec_configuration(),
            .colour_matrix = options_.colour_matrix(),
            .h264_level = options_.profile == video_profile_t::mp4_h264_aac ? options_.h264_level() : 0};
    }
};

class encoded_file_probe_t {
    const video_encoder_options_t& options_;
    const video_encoder_report_t& report_;
    std::stop_token stop_;
    AVFormatContext* input_{};
    AVCodecContext* decoder_{};
    AVPacket* packet_{};
    AVFrame* frame_{};
    int video_index_{-1}, audio_index_{-1};
    std::uint64_t frames_{};
    double first_video_{}, last_video_{};
    std::optional<double> first_audio_, last_audio_end_;

    static auto interrupt(void* opaque) -> int {
        return static_cast<encoded_file_probe_t*>(opaque)->stop_.stop_requested() ? 1 : 0;
    }
    auto check_video_metadata() -> encode_status_t {
        const auto* parameters = input_->streams[video_index_]->codecpar;
        const auto expected_codec = options_.profile == video_profile_t::mp4_h264_aac ? AV_CODEC_ID_H264 : AV_CODEC_ID_VP9;
        const auto tags = colour_tags(options_.colour_matrix());
        if (parameters->codec_id != expected_codec || parameters->width != static_cast<int>(options_.width) ||
            parameters->height != static_cast<int>(options_.height) || parameters->format != AV_PIX_FMT_YUV420P ||
            parameters->color_range != AVCOL_RANGE_MPEG || parameters->color_space != tags.space ||
            parameters->color_primaries != tags.primaries || parameters->color_trc != tags.transfer)
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed file has wrong video dimensions, format or colour tags"}});
        if (expected_codec == AV_CODEC_ID_H264 && parameters->level != options_.h264_level())
            return std::unexpected(error_t{failure_code_t::unavailable,
                "H.264 encoder did not signal requested level " + std::to_string(options_.h264_level()) +
                " (actual " + std::to_string(parameters->level) + "); this configuration is unavailable"});
        return {};
    }
    auto check_audio_metadata() -> encode_status_t {
        if (report_.audio_sample_rate == 0) {
            if (audio_index_ >= 0) return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Unexpected audio stream"}});
            return {};
        }
        if (audio_index_ < 0) return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed file has no selected audio stream"}});
        const auto* stream = input_->streams[audio_index_];
        const auto* parameters = stream->codecpar;
        const auto codec = options_.profile == video_profile_t::mp4_h264_aac ? AV_CODEC_ID_AAC : AV_CODEC_ID_OPUS;
        if (parameters->codec_id != codec || parameters->sample_rate != static_cast<int>(report_.audio_sample_rate) ||
            parameters->ch_layout.nb_channels != 2)
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed file has wrong audio codec, rate or channels"}});
        if (stream->start_time != AV_NOPTS_VALUE) first_audio_ = stream->start_time * av_q2d(stream->time_base);
        return {};
    }
    auto open(const std::filesystem::path& path) -> encode_status_t {
        input_ = avformat_alloc_context();
        if (!input_) return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Could not allocate completed-file probe"}});
        input_->interrupt_callback = {.callback = interrupt, .opaque = this};
        input_->probesize = 1024 * 1024;
        input_->max_analyze_duration = AV_TIME_BASE;
        const auto filename = text_util::path_utf8(path);
        auto status = avformat_open_input(&input_, filename.c_str(), nullptr, nullptr);
        if (status >= 0) status = avformat_find_stream_info(input_, nullptr);
        if (status < 0) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(status)});
        video_index_ = av_find_best_stream(input_, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        audio_index_ = av_find_best_stream(input_, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (video_index_ < 0) return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed file has no video stream"}});
        if (auto result = check_video_metadata(); !result) return result;
        if (auto result = check_audio_metadata(); !result) return result;
        return open_decoder();
    }
    auto open_decoder() -> encode_status_t {
        const auto* codec = avcodec_find_decoder(input_->streams[video_index_]->codecpar->codec_id);
        if (!codec) return std::unexpected(error_t{failure_code_t::unavailable, std::string{"Completed-file video validation decoder unavailable"}});
        decoder_ = avcodec_alloc_context3(codec);
        packet_ = av_packet_alloc();
        frame_ = av_frame_alloc();
        if (!decoder_ || !packet_ || !frame_) return std::unexpected(error_t{failure_code_t::out_of_memory,
            std::string{"Could not allocate completed-file validation buffers"}});
        auto status = avcodec_parameters_to_context(decoder_, input_->streams[video_index_]->codecpar);
        decoder_->thread_count = static_cast<int>(options_.threads);
        if (status >= 0) status = avcodec_open2(decoder_, codec, nullptr);
        if (status < 0) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(status)});
        return {};
    }
    auto check_decoded_frame() const -> encode_status_t {
        if (frame_->width != static_cast<int>(options_.width) || frame_->height != static_cast<int>(options_.height) ||
            frame_->format != AV_PIX_FMT_YUV420P || frame_->best_effort_timestamp == AV_NOPTS_VALUE ||
            frame_->color_range != AVCOL_RANGE_MPEG || frame_->colorspace != colour_tags(options_.colour_matrix()).space ||
            frame_->color_primaries != colour_tags(options_.colour_matrix()).primaries ||
            frame_->color_trc != colour_tags(options_.colour_matrix()).transfer)
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Decoded output frame has invalid dimensions, format or timestamp"}});
        return {};
    }
    auto drain() -> encode_status_t {
        for (;;) {
            if (stop_.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video validation cancelled"}});
            const auto status = avcodec_receive_frame(decoder_, frame_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return {};
            if (status < 0) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(status)});
            if (auto result = check_decoded_frame(); !result) return result;
            const auto time = frame_->best_effort_timestamp * av_q2d(input_->streams[video_index_]->time_base);
            if (frames_ == 0) first_video_ = time;
            if (frames_ != 0 && time <= last_video_) return std::unexpected(error_t{
                failure_code_t::invalid_data, std::string{"Completed video has non-monotonic frame timestamps"}});
            last_video_ = time;
            av_frame_unref(frame_);
            if (++frames_ > options_.frame_count) return std::unexpected(error_t{
                failure_code_t::invalid_data, std::string{"Completed video contains excess frames"}});
        }
    }
    auto check_timing() const -> encode_status_t {
        const auto tolerance = 1.0 / options_.clock.numerator + 1e-6;
        const auto duration = static_cast<double>(options_.frame_count) / options_.clock.numerator;
        const auto expected_last = static_cast<double>(options_.frame_count - 1) / options_.clock.numerator;
        if (options_.profile == video_profile_t::mp4_h264_aac && decoder_->level != options_.h264_level())
            return std::unexpected(error_t{failure_code_t::unavailable, std::string{"Decoded H.264 SPS did not preserve requested level"}});
        if (frames_ != options_.frame_count || std::abs(first_video_) > tolerance ||
            std::abs(last_video_ - expected_last) > tolerance || input_->duration == AV_NOPTS_VALUE ||
            std::abs(static_cast<double>(input_->duration) / AV_TIME_BASE - duration) > tolerance)
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed video frame count or duration differs from request"}});
        if (report_.audio_sample_rate != 0 && (!first_audio_ || std::abs(*first_audio_ - first_video_) > tolerance))
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed audio/video starts differ by more than one frame"}});
        if (report_.audio_sample_rate != 0 && (!last_audio_end_ || std::abs(*last_audio_end_ - duration) > tolerance))
            return std::unexpected(error_t{failure_code_t::invalid_data, std::string{"Completed audio duration differs by more than one frame"}});
        return {};
    }
    void observe_audio_packet() {
        const auto* stream = input_->streams[audio_index_];
        if (packet_->pts == AV_NOPTS_VALUE) return;
        const auto time = packet_->pts * av_q2d(stream->time_base);
        if (!first_audio_) first_audio_ = time;
        if (packet_->duration > 0) {
            const auto end = time + packet_->duration * av_q2d(stream->time_base);
            last_audio_end_ = std::max(last_audio_end_.value_or(end), end);
        }
    }
  public:
    encoded_file_probe_t(const video_encoder_options_t& options, const video_encoder_report_t& report, std::stop_token stop)
        : options_(options), report_(report), stop_(stop) {}
    ~encoded_file_probe_t() {
        av_frame_free(&frame_);
        av_packet_free(&packet_);
        avcodec_free_context(&decoder_);
        avformat_close_input(&input_);
    }
    auto validate(const std::filesystem::path& path) -> encode_status_t {
        if (auto result = open(path); !result) return result;
        int status{};
        while ((status = av_read_frame(input_, packet_)) >= 0) {
            if (stop_.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video validation cancelled"}});
            if (packet_->stream_index == video_index_) {
                const auto sent = avcodec_send_packet(decoder_, packet_);
                if (sent < 0) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(sent)});
                if (auto result = drain(); !result) return result;
            } else if (packet_->stream_index == audio_index_) observe_audio_packet();
            av_packet_unref(packet_);
        }
        if (status != AVERROR_EOF) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(status)});
        const auto sent = avcodec_send_packet(decoder_, nullptr);
        if (sent < 0) return std::unexpected(error_t{failure_code_t::decode_failed, ffmpeg_message(sent)});
        if (auto result = drain(); !result) return result;
        return check_timing();
    }
};

void notify_published(const std::function<void(video_encoder_progress_t)>& progress, std::uint64_t total) noexcept {
    // Publication is the commit point. A telemetry exception cannot turn an
    // already-published valid file into a failed/cancelled export result.
    if (!progress) return;
    try { progress({.completed_frames = total, .total_frames = total}); }
    catch (...) {}
}

auto encode_file(const video_encoder_options_t& options, const video_encoder_sources_t& sources,
    std::stop_token stop, const std::function<void(video_encoder_progress_t)>& progress, bool publish)
    -> result_t<video_encoder_report_t, error_t<failure_code_t>> {
    if (!options.is_valid() || !sources.frame) return std::unexpected(error_t{failure_code_t::invalid_request,
        std::string{"Invalid video export options or missing frame source"}});
    if (stop.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video export cancelled"}});
    std::optional<error_t<failure_code_t>> failure;
    std::optional<video_encoder_report_t> report;
    const auto producer = [&](prepared_atomic_file_t::stream_t& stream) -> atomic_file_save_outcome_t {
        mux_encoder_t encoder{options, sources, stop, progress};
        auto result = encoder.run(stream);
        if (!result) { failure = result.error(); return std::unexpected(result.error().message); }
        report = std::move(*result);
        return {};
    };
    const auto validator = [&](const std::filesystem::path& path) -> atomic_file_save_outcome_t {
        encoded_file_probe_t probe{options, *report, stop};
        if (auto result = probe.validate(path); !result) {
            failure = result.error();
            return std::unexpected(result.error().message);
        }
        return {};
    };
    auto prepared = prepared_atomic_file_t::from_stream(options.destination,
        static_cast<std::size_t>(options.maximum_output_bytes), producer, validator, stop);
    if (!prepared) {
        if (stop.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video export cancelled"}});
        return std::unexpected(failure.value_or(error_t{failure_code_t::io_failed, prepared.error()}));
    }
    if (publish) {
        if (auto result = prepared->publish(options.replace_existing); !result) return std::unexpected(error_t{
            stop.stop_requested() ? failure_code_t::cancelled : failure_code_t::publication_failed, result.error()});
        notify_published(progress, options.frame_count);
    }
    return std::move(*report);
}
} // namespace
#endif

auto video_encoder_t::is_available() noexcept -> bool {
#if defined(WORMEDIT_HAS_VIDEO_ENCODER) && WORMEDIT_HAS_VIDEO_ENCODER
    return true;
#else
    return false;
#endif
}
auto video_encoder_t::encode(const video_encoder_options_t& options, const video_encoder_sources_t& sources,
    std::stop_token stop, std::function<void(video_encoder_progress_t)> progress)
    -> result_t<video_encoder_report_t, error_t<failure_code_t>> {
#if defined(WORMEDIT_HAS_VIDEO_ENCODER) && WORMEDIT_HAS_VIDEO_ENCODER
    try { return encode_file(options, sources, stop, progress, true); }
    catch (const std::bad_alloc&) { return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Video export allocation failed"}}); }
    catch (const std::exception& error) { return std::unexpected(error_t{failure_code_t::task_threw, std::string{error.what()}}); }
    catch (...) { return std::unexpected(error_t{failure_code_t::task_threw, std::string{"Video export callback threw an unknown exception"}}); }
#else
    (void)options; (void)sources; (void)stop; (void)progress;
    return std::unexpected(error_t{failure_code_t::unavailable, std::string{"Video export unavailable: this build has no video encoder"}});
#endif
}
auto video_encoder_t::probe(video_encoder_options_t options, bool with_audio, std::stop_token stop)
    -> result_t<video_encoder_report_t, error_t<failure_code_t>> {
#if defined(WORMEDIT_HAS_VIDEO_ENCODER) && WORMEDIT_HAS_VIDEO_ENCODER
    options.frame_count = 1;
    options.begin_audio_frame = 0;
    if (!options.is_valid()) return std::unexpected(error_t{failure_code_t::invalid_request, std::string{"Invalid video probe options"}});
    try {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(options.width) * options.height * 4, 0);
        for (std::size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
        video_encoder_sources_t sources{.frame = [&](std::uint64_t, storyboard_time_t,
            storyboard_conditional_layer_t, std::stop_token) -> result_t<storyboard_raster_frame_t, error_t<failure_code_t>> {
            return storyboard_raster_frame_t{.width = options.width, .height = options.height, .pixels = pixels};
        }};
        if (with_audio) sources.audio = [&](const audio::offline_mix_sink_t& sink, std::stop_token token) -> encode_status_t {
            const auto count = *options.input_audio_frames();
            std::vector<float> silence(static_cast<std::size_t>(count) * audio::output_channel_count);
            if (token.stop_requested()) return std::unexpected(error_t{failure_code_t::cancelled, std::string{"Video probe cancelled"}});
            return sink(0, silence);
        };
        return encode_file(options, sources, stop, {}, false);
    } catch (const std::bad_alloc&) {
        return std::unexpected(error_t{failure_code_t::out_of_memory, std::string{"Video probe allocation failed"}});
    } catch (const std::exception& error) {
        return std::unexpected(error_t{failure_code_t::task_threw, std::string{error.what()}});
    } catch (...) {
        return std::unexpected(error_t{failure_code_t::task_threw, std::string{"Video probe callback threw an unknown exception"}});
    }
#else
    (void)options; (void)with_audio; (void)stop;
    return std::unexpected(error_t{failure_code_t::unavailable, std::string{"Video export unavailable: this build has no video encoder"}});
#endif
}

} // namespace wormedit::media
