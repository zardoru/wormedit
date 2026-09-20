#include <wormedit/visual/video_decoder.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <cerrno>
#include <limits>
#include <memory>
#include <utility>

namespace wormedit::visual {
namespace {

struct format_deleter_t {
    void operator()(AVFormatContext* context) const noexcept {
        avformat_close_input(&context);
    }
};
struct codec_deleter_t {
    void operator()(AVCodecContext* context) const noexcept {
        avcodec_free_context(&context);
    }
};
struct frame_deleter_t {
    void operator()(AVFrame* frame) const noexcept {
        av_frame_free(&frame);
    }
};
struct packet_deleter_t {
    void operator()(AVPacket* packet) const noexcept {
        av_packet_free(&packet);
    }
};
struct scale_deleter_t {
    void operator()(SwsContext* context) const noexcept {
        sws_freeContext(context);
    }
};

auto interrupt_ffmpeg(void* opaque) -> int {
    const auto* stop = static_cast<const std::stop_token*>(opaque);
    return stop && stop->stop_requested() ? 1 : 0;
}

using format_ptr_t = std::unique_ptr<AVFormatContext, format_deleter_t>;
using codec_ptr_t = std::unique_ptr<AVCodecContext, codec_deleter_t>;
using frame_ptr_t = std::unique_ptr<AVFrame, frame_deleter_t>;
using packet_ptr_t = std::unique_ptr<AVPacket, packet_deleter_t>;
using scale_ptr_t = std::unique_ptr<SwsContext, scale_deleter_t>;

auto ffmpeg_message(int error) -> std::string {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, message, sizeof(message)) < 0) {
        return "unknown FFmpeg error";
    }
    return message;
}

auto failure(video_frame_status_t status, std::string message) -> video_frame_decode_result_t {
    return {.status = status, .frame = std::nullopt, .message = std::move(message)};
}

auto checked_rgba_bytes(int width, int height, video_frame_decode_limits_t limits,
                        std::size_t& byte_count) -> video_frame_status_t {
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) > limits.maximum_width ||
        static_cast<std::uint64_t>(height) > limits.maximum_height) {
        return video_frame_status_t::dimension_limit_exceeded;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > limits.maximum_pixels) {
        return video_frame_status_t::pixel_limit_exceeded;
    }
    if (pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        return video_frame_status_t::byte_limit_exceeded;
    }
    byte_count = static_cast<std::size_t>(pixels) * 4U;
    return byte_count <= limits.maximum_rgba_bytes ? video_frame_status_t::ready
                                                   : video_frame_status_t::byte_limit_exceeded;
}

auto receive_frames(AVCodecContext* decoder, AVFrame* scratch, AVFrame* selected,
                    std::int64_t target_timestamp, bool& passed_target, std::stop_token stop)
    -> int {
    while (true) {
        if (stop.stop_requested()) {
            return AVERROR_EXIT;
        }
        const auto received = avcodec_receive_frame(decoder, scratch);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
            return 0;
        }
        if (received < 0) {
            return received;
        }
        const auto timestamp = scratch->best_effort_timestamp;
        if (timestamp == AV_NOPTS_VALUE) {
            av_frame_unref(scratch);
            return AVERROR_INVALIDDATA;
        }
        if (timestamp > target_timestamp) {
            passed_target = true;
            av_frame_unref(scratch);
            return 0;
        }
        av_frame_unref(selected);
        const auto referenced = av_frame_ref(selected, scratch);
        av_frame_unref(scratch);
        if (referenced < 0) {
            return referenced;
        }
    }
}

} // namespace

auto decode_video_frame_rgba(const resolved_visual_asset_t& source,
                             const video_preview_clock_t& clock, video_frame_decode_limits_t limits,
                             std::stop_token stop) -> video_frame_decode_result_t {
    if (stop.stop_requested()) {
        return failure(video_frame_status_t::cancelled, "video frame decode was cancelled");
    }
    if (source.kind != asset_kind_t::video) {
        return failure(video_frame_status_t::unsupported_asset_kind,
                       "only video assets can be decoded as timed frames");
    }
    if (source.resource != clock.resource || clock.media_time_denominator == 0 ||
        limits.maximum_width == 0 || limits.maximum_height == 0 || limits.maximum_pixels == 0 ||
        limits.maximum_rgba_bytes == 0 || limits.maximum_packets == 0) {
        return failure(video_frame_status_t::invalid_request,
                       "video source, media clock, or decode limits are invalid");
    }
    if (clock.media_time_numerator >
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return failure(video_frame_status_t::timestamp_out_of_range,
                       "video media timestamp exceeds FFmpeg's signed range");
    }

    AVFormatContext* opened = avformat_alloc_context();
    if (!opened) {
        return failure(video_frame_status_t::open_failed,
                       "video format context could not be allocated");
    }
    opened->interrupt_callback = {.callback = interrupt_ffmpeg, .opaque = &stop};
    const auto path = source.path.string();
    auto result = avformat_open_input(&opened, path.c_str(), nullptr, nullptr);
    format_ptr_t format{opened};
    if (result < 0) {
        if (stop.stop_requested()) {
            return failure(video_frame_status_t::cancelled, "video input open was cancelled");
        }
        return failure(video_frame_status_t::open_failed,
                       "video input could not be opened: " + ffmpeg_message(result));
    }
    result = avformat_find_stream_info(format.get(), nullptr);
    if (result < 0) {
        if (stop.stop_requested()) {
            return failure(video_frame_status_t::cancelled, "video stream discovery was cancelled");
        }
        return failure(video_frame_status_t::stream_info_failed,
                       "video stream information could not be read: " + ffmpeg_message(result));
    }

    const AVCodec* decoder_type{};
    const auto stream_index =
        av_find_best_stream(format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder_type, 0);
    if (stream_index < 0 || decoder_type == nullptr) {
        return failure(video_frame_status_t::no_video_stream,
                       "asset contains no supported video stream");
    }
    auto* stream = format->streams[stream_index];
    codec_ptr_t decoder{avcodec_alloc_context3(decoder_type)};
    if (!decoder) {
        return failure(video_frame_status_t::decoder_open_failed,
                       "video decoder context could not be allocated");
    }
    result = avcodec_parameters_to_context(decoder.get(), stream->codecpar);
    if (result >= 0) {
        result = avcodec_open2(decoder.get(), decoder_type, nullptr);
    }
    if (result < 0) {
        if (stop.stop_requested()) {
            return failure(video_frame_status_t::cancelled, "video decoder open was cancelled");
        }
        return failure(video_frame_status_t::decoder_open_failed,
                       "video decoder could not be opened: " + ffmpeg_message(result));
    }

    const AVRational media_time_base{1, static_cast<int>(clock.media_time_denominator)};
    auto target_timestamp = av_rescale_q_rnd(static_cast<std::int64_t>(clock.media_time_numerator),
                                             media_time_base, stream->time_base, AV_ROUND_DOWN);
    const auto stream_start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    if ((target_timestamp > 0 &&
         stream_start > std::numeric_limits<std::int64_t>::max() - target_timestamp) ||
        (target_timestamp < 0 &&
         stream_start < std::numeric_limits<std::int64_t>::min() - target_timestamp)) {
        return failure(video_frame_status_t::timestamp_out_of_range,
                       "video media timestamp plus stream origin is out of range");
    }
    target_timestamp += stream_start;
    result =
        avformat_seek_file(format.get(), stream_index, std::numeric_limits<std::int64_t>::min(),
                           target_timestamp, target_timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        if (stop.stop_requested()) {
            return failure(video_frame_status_t::cancelled, "video seek was cancelled");
        }
        return failure(video_frame_status_t::seek_failed,
                       "video stream could not seek to the requested time: " +
                           ffmpeg_message(result));
    }
    avcodec_flush_buffers(decoder.get());

    frame_ptr_t scratch{av_frame_alloc()};
    frame_ptr_t selected{av_frame_alloc()};
    packet_ptr_t packet{av_packet_alloc()};
    if (!scratch || !selected || !packet) {
        return failure(video_frame_status_t::decode_failed,
                       "video packet or frame storage could not be allocated");
    }

    bool passed_target{};
    std::size_t packet_count{};
    while (!passed_target && packet_count < limits.maximum_packets) {
        if (stop.stop_requested()) {
            return failure(video_frame_status_t::cancelled, "video frame decode was cancelled");
        }
        result = av_read_frame(format.get(), packet.get());
        if (result == AVERROR_EOF) {
            result = avcodec_send_packet(decoder.get(), nullptr);
            if (result >= 0 || result == AVERROR_EOF) {
                result = receive_frames(decoder.get(), scratch.get(), selected.get(),
                                        target_timestamp, passed_target, stop);
            }
            break;
        }
        if (result < 0) {
            if (stop.stop_requested()) {
                return failure(video_frame_status_t::cancelled, "video packet read was cancelled");
            }
            return failure(video_frame_status_t::decode_failed,
                           "video packet read failed: " + ffmpeg_message(result));
        }
        ++packet_count;
        if (packet->stream_index == stream_index) {
            result = avcodec_send_packet(decoder.get(), packet.get());
            if (result >= 0) {
                result = receive_frames(decoder.get(), scratch.get(), selected.get(),
                                        target_timestamp, passed_target, stop);
            }
        } else {
            result = 0;
        }
        av_packet_unref(packet.get());
        if (result < 0) {
            if (stop.stop_requested()) {
                return failure(video_frame_status_t::cancelled,
                               "video frame receive was cancelled");
            }
            return failure(video_frame_status_t::decode_failed,
                           "video frame decode failed: " + ffmpeg_message(result));
        }
    }
    if (packet_count == limits.maximum_packets && !passed_target) {
        return failure(video_frame_status_t::decode_failed,
                       "video decode exceeded the configured packet limit");
    }
    if (result < 0 && result != AVERROR_EOF) {
        return failure(video_frame_status_t::decode_failed,
                       "video decoder flush failed: " + ffmpeg_message(result));
    }
    if (selected->best_effort_timestamp == AV_NOPTS_VALUE || selected->width <= 0 ||
        selected->height <= 0) {
        return failure(video_frame_status_t::timestamp_missing,
                       "video decoder produced no timestamped frame at the requested time");
    }

    std::size_t rgba_bytes{};
    const auto geometry = checked_rgba_bytes(selected->width, selected->height, limits, rgba_bytes);
    if (geometry != video_frame_status_t::ready) {
        return failure(geometry, "decoded video frame geometry exceeds the configured limit");
    }
    if ((source.declared_width &&
         *source.declared_width != static_cast<std::uint32_t>(selected->width)) ||
        (source.declared_height &&
         *source.declared_height != static_cast<std::uint32_t>(selected->height))) {
        return failure(video_frame_status_t::declared_geometry_mismatch,
                       "decoded video geometry differs from the document declaration");
    }

    rgba_video_frame_t output{.resource = source.resource,
                              .asset = source.asset,
                              .path = source.path,
                              .width = static_cast<std::uint32_t>(selected->width),
                              .height = static_cast<std::uint32_t>(selected->height),
                              .pixels = std::vector<std::uint8_t>(rgba_bytes),
                              .presentation_timestamp = selected->best_effort_timestamp,
                              .time_base_numerator = stream->time_base.num,
                              .time_base_denominator = stream->time_base.den};
    scale_ptr_t scaler{sws_getContext(selected->width, selected->height,
                                      static_cast<AVPixelFormat>(selected->format), selected->width,
                                      selected->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr,
                                      nullptr, nullptr)};
    if (!scaler) {
        return failure(video_frame_status_t::decode_failed,
                       "video RGBA conversion context could not be created");
    }
    std::uint8_t* destination[] = {output.pixels.data(), nullptr, nullptr, nullptr};
    int strides[] = {selected->width * 4, 0, 0, 0};
    result = sws_scale(scaler.get(), selected->data, selected->linesize, 0, selected->height,
                       destination, strides);
    if (result != selected->height) {
        return failure(video_frame_status_t::decode_failed,
                       "video frame could not be converted completely to RGBA8");
    }
    return {.status = video_frame_status_t::ready, .frame = std::move(output), .message = {}};
}

} // namespace wormedit::visual
