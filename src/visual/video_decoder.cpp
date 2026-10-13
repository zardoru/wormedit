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
#include <climits>
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

using format_ptr_t = std::unique_ptr<AVFormatContext, format_deleter_t>;
using codec_ptr_t = std::unique_ptr<AVCodecContext, codec_deleter_t>;
using frame_ptr_t = std::unique_ptr<AVFrame, frame_deleter_t>;
using packet_ptr_t = std::unique_ptr<AVPacket, packet_deleter_t>;

auto ffmpeg_message(int error) -> std::string {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(error, message, sizeof(message)) < 0) {
        return "unknown FFmpeg error";
    }
    return message;
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
    if (width > INT_MAX / 4 || pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        return video_frame_status_t::byte_limit_exceeded;
    }
    byte_count = static_cast<std::size_t>(pixels) * 4U;
    return byte_count <= limits.maximum_rgba_bytes ? video_frame_status_t::ready
                                                   : video_frame_status_t::byte_limit_exceeded;
}

auto clock_timestamp(const video_preview_clock_t& clock, AVRational time_base,
                     std::int64_t stream_start) -> std::optional<std::int64_t> {
    if (clock.media_time_denominator == 0 || clock.media_time_denominator > INT_MAX ||
        clock.media_time_numerator >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        time_base.num <= 0 || time_base.den <= 0) {
        return std::nullopt;
    }
    const AVRational media_time_base{1, static_cast<int>(clock.media_time_denominator)};
    auto timestamp = av_rescale_q_rnd(static_cast<std::int64_t>(clock.media_time_numerator),
                                      media_time_base, time_base, AV_ROUND_DOWN);
    if ((timestamp > 0 && stream_start > std::numeric_limits<std::int64_t>::max() - timestamp) ||
        (timestamp < 0 && stream_start < std::numeric_limits<std::int64_t>::min() - timestamp)) {
        return std::nullopt;
    }
    return timestamp + stream_start;
}

} // namespace

struct video_decoder_session_t::impl_t {
    resolved_visual_asset_t source;
    format_ptr_t format;
    codec_ptr_t decoder;
    frame_ptr_t scratch{av_frame_alloc()};
    frame_ptr_t current{av_frame_alloc()};
    frame_ptr_t lookahead{av_frame_alloc()};
    packet_ptr_t packet{av_packet_alloc()};
    int stream_index{-1};
    AVRational time_base{1, 1};
    std::int64_t stream_start{};
    std::optional<std::int64_t> last_target;
    bool positioned{};
    bool input_eof{};
    bool flush_sent{};
    bool decoder_eof{};
    std::stop_token active_stop;
};

namespace {

auto interrupt_ffmpeg(void* opaque) -> int {
    const auto* session = static_cast<const video_decoder_session_t::impl_t*>(opaque);
    return session && session->active_stop.stop_requested() ? 1 : 0;
}

auto timestamp_of(const AVFrame* frame) -> std::int64_t {
    return frame ? frame->best_effort_timestamp : AV_NOPTS_VALUE;
}

auto copy_frame(AVFrame* destination, const AVFrame* source) -> int {
    av_frame_unref(destination);
    return av_frame_ref(destination, source);
}

auto render_frame(video_decoder_session_t::impl_t& session, video_frame_decode_limits_t limits)
    -> video_frame_decode_result_t {
    const auto* selected = session.current.get();
    if (timestamp_of(selected) == AV_NOPTS_VALUE || selected->width <= 0 || selected->height <= 0) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::timestamp_missing,
                       "video decoder produced no timestamped frame at the requested time"});
    }

    std::size_t rgba_bytes{};
    const auto geometry = checked_rgba_bytes(selected->width, selected->height, limits, rgba_bytes);
    if (geometry != video_frame_status_t::ready) {
        return std::unexpected(error_t<video_frame_status_t>{geometry, "decoded video frame geometry exceeds the configured limit"});
    }
    if ((session.source.declared_width &&
         *session.source.declared_width != static_cast<std::uint32_t>(selected->width)) ||
        (session.source.declared_height &&
         *session.source.declared_height != static_cast<std::uint32_t>(selected->height))) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::declared_geometry_mismatch,
                       "decoded video geometry differs from the document declaration"});
    }

    rgba_video_frame_t output{
        .resource = session.source.resource,
        .asset = session.source.asset,
        .path = session.source.path,
        .width = static_cast<std::uint32_t>(selected->width),
        .height = static_cast<std::uint32_t>(selected->height),
        .pixels = std::vector<std::uint8_t>(rgba_bytes),
        .presentation_timestamp = selected->best_effort_timestamp,
        .next_presentation_timestamp =
            timestamp_of(session.lookahead.get()) == AV_NOPTS_VALUE
                ? std::nullopt
                : std::optional<std::int64_t>{session.lookahead->best_effort_timestamp},
        .stream_start_timestamp = session.stream_start,
        .time_base_numerator = session.time_base.num,
        .time_base_denominator = session.time_base.den,
    };
    auto* scaler =
        sws_getContext(selected->width, selected->height,
                       static_cast<AVPixelFormat>(selected->format), selected->width,
                       selected->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!scaler) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                       "video RGBA conversion context could not be created"});
    }
    std::uint8_t* destination[] = {output.pixels.data(), nullptr, nullptr, nullptr};
    int strides[] = {selected->width * 4, 0, 0, 0};
    const auto scaled = sws_scale(scaler, selected->data, selected->linesize, 0, selected->height,
                                  destination, strides);
    sws_freeContext(scaler);
    if (scaled != selected->height) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                       "video frame could not be converted completely to RGBA8"});
    }
    return output;
}

auto seek_to_target(video_decoder_session_t::impl_t& session, std::int64_t target,
                    std::stop_token stop) -> std::optional<video_frame_decode_result_t> {
    const auto current_timestamp = timestamp_of(session.current.get());
    const bool seek_required = !session.positioned ||
                               (session.last_target && target < *session.last_target) ||
                               (current_timestamp != AV_NOPTS_VALUE && target < current_timestamp);
    if (!seek_required) {
        return std::nullopt;
    }
    const auto result = avformat_seek_file(session.format.get(), session.stream_index,
                                           std::numeric_limits<std::int64_t>::min(), target,
                                           target, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video seek was cancelled"});
        }
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::seek_failed,
                       "video stream could not seek to the requested time: " +
                           ffmpeg_message(result)});
    }
    avcodec_flush_buffers(session.decoder.get());
    av_frame_unref(session.scratch.get());
    av_frame_unref(session.current.get());
    av_frame_unref(session.lookahead.get());
    av_packet_unref(session.packet.get());
    session.positioned = true;
    session.input_eof = false;
    session.flush_sent = false;
    session.decoder_eof = false;
    return std::nullopt;
}

auto retain_decoded_frame(video_decoder_session_t::impl_t& session, std::int64_t target,
                          video_frame_decode_limits_t limits)
    -> std::optional<video_frame_decode_result_t> {
    const auto timestamp = timestamp_of(session.scratch.get());
    if (timestamp == AV_NOPTS_VALUE) {
        av_frame_unref(session.scratch.get());
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::timestamp_missing,
                       "video decoder produced a frame without a timestamp"});
    }
    auto* destination = timestamp <= target ? session.current.get() : session.lookahead.get();
    const auto copied = copy_frame(destination, session.scratch.get());
    av_frame_unref(session.scratch.get());
    if (copied < 0) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                       "decoded video frame could not be retained: " + ffmpeg_message(copied)});
    }
    if (timestamp > target) {
        return render_frame(session, limits);
    }
    return std::nullopt;
}

auto feed_video_decoder(video_decoder_session_t::impl_t& session,
                        video_frame_decode_limits_t limits, std::stop_token stop,
                        std::size_t& packet_count)
    -> std::optional<video_frame_decode_result_t> {
    if (session.input_eof) {
        if (!session.flush_sent) {
            const auto result = avcodec_send_packet(session.decoder.get(), nullptr);
            if (result < 0 && result != AVERROR_EOF) {
                return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                               "video decoder flush failed: " + ffmpeg_message(result)});
            }
            session.flush_sent = true;
            if (result == AVERROR_EOF) {
                session.decoder_eof = true;
                return render_frame(session, limits);
            }
            return std::nullopt;
        }
        session.decoder_eof = true;
        return render_frame(session, limits);
    }

    auto result = av_read_frame(session.format.get(), session.packet.get());
    if (result == AVERROR_EOF) {
        session.input_eof = true;
        return std::nullopt;
    }
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video packet read was cancelled"});
        }
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                       "video packet read failed: " + ffmpeg_message(result)});
    }
    ++packet_count;
    if (session.packet->stream_index == session.stream_index) {
        result = avcodec_send_packet(session.decoder.get(), session.packet.get());
    } else {
        result = 0;
    }
    av_packet_unref(session.packet.get());
    if (result < 0) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                       "video packet submission failed: " + ffmpeg_message(result)});
    }
    return std::nullopt;
}

} // namespace

video_decoder_session_t::video_decoder_session_t(std::unique_ptr<impl_t> impl)
    : impl_(std::move(impl)) {}

video_decoder_session_t::video_decoder_session_t(video_decoder_session_t&&) noexcept = default;
auto video_decoder_session_t::operator=(video_decoder_session_t&&) noexcept
    -> video_decoder_session_t& = default;
video_decoder_session_t::~video_decoder_session_t() = default;

auto video_decoder_session_t::source() const noexcept -> const resolved_visual_asset_t& {
    return impl_->source;
}

auto open_video_decoder_session(const resolved_visual_asset_t& source, std::stop_token stop)
    -> video_decoder_session_open_result_t {
    if (stop.stop_requested()) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video decoder open was cancelled"});
    }
    if (source.kind != asset_kind_t::video) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::unsupported_asset_kind,
                            "only video assets can be decoded as timed frames"});
    }

    auto impl = std::make_unique<video_decoder_session_t::impl_t>();
    impl->source = source;
    impl->active_stop = stop;
    AVFormatContext* opened = avformat_alloc_context();
    if (!opened) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::open_failed,
                            "video format context could not be allocated"});
    }
    opened->interrupt_callback = {.callback = interrupt_ffmpeg, .opaque = impl.get()};
    const auto path = source.path.string();
    auto result = avformat_open_input(&opened, path.c_str(), nullptr, nullptr);
    impl->format.reset(opened);
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video input open was cancelled"});
        }
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::open_failed,
                            "video input could not be opened: " + ffmpeg_message(result)});
    }
    result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled,
                                "video stream discovery was cancelled"});
        }
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::stream_info_failed,
                            "video stream information could not be read: " +
                                ffmpeg_message(result)});
    }

    const AVCodec* decoder_type{};
    impl->stream_index =
        av_find_best_stream(impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder_type, 0);
    if (impl->stream_index < 0 || decoder_type == nullptr) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::no_video_stream,
                            "asset contains no supported video stream"});
    }
    auto* stream = impl->format->streams[impl->stream_index];
    impl->time_base = stream->time_base;
    impl->stream_start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    impl->decoder.reset(avcodec_alloc_context3(decoder_type));
    if (!impl->decoder) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decoder_open_failed,
                            "video decoder context could not be allocated"});
    }
    result = avcodec_parameters_to_context(impl->decoder.get(), stream->codecpar);
    if (result >= 0) {
        result = avcodec_open2(impl->decoder.get(), decoder_type, nullptr);
    }
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled,
                                "video decoder open was cancelled"});
        }
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decoder_open_failed,
                            "video decoder could not be opened: " + ffmpeg_message(result)});
    }
    if (!impl->scratch || !impl->current || !impl->lookahead || !impl->packet) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                            "video packet or frame storage could not be allocated"});
    }
    return std::unique_ptr<video_decoder_session_t>{new video_decoder_session_t{std::move(impl)}};
}

auto video_decoder_session_t::decode(const video_preview_clock_t& clock,
                                     video_frame_decode_limits_t limits, std::stop_token stop)
    -> video_frame_decode_result_t {
    auto& session = *impl_;
    session.active_stop = stop;
    if (stop.stop_requested()) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video frame decode was cancelled"});
    }
    if (session.source.resource != clock.resource || limits.maximum_width == 0 ||
        limits.maximum_height == 0 || limits.maximum_pixels == 0 ||
        limits.maximum_rgba_bytes == 0 || limits.maximum_packets == 0) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::invalid_request,
                       "video source, media clock, or decode limits are invalid"});
    }
    const auto requested = clock_timestamp(clock, session.time_base, session.stream_start);
    if (!requested) {
        return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::timestamp_out_of_range,
                       "video media timestamp is outside FFmpeg's signed range"});
    }
    const auto target = *requested;

    if (auto seek_error = seek_to_target(session, target, stop)) {
        return std::move(*seek_error);
    }
    session.last_target = target;

    if (timestamp_of(session.lookahead.get()) != AV_NOPTS_VALUE &&
        timestamp_of(session.lookahead.get()) <= target) {
        av_frame_unref(session.current.get());
        av_frame_move_ref(session.current.get(), session.lookahead.get());
    }
    if (timestamp_of(session.current.get()) != AV_NOPTS_VALUE &&
        ((timestamp_of(session.lookahead.get()) != AV_NOPTS_VALUE &&
          target < timestamp_of(session.lookahead.get())) ||
         session.decoder_eof)) {
        return render_frame(session, limits);
    }

    std::size_t packet_count{};
    while (packet_count < limits.maximum_packets) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::cancelled, "video frame decode was cancelled"});
        }
        auto result = avcodec_receive_frame(session.decoder.get(), session.scratch.get());
        if (result >= 0) {
            if (auto frame = retain_decoded_frame(session, target, limits)) {
                return std::move(*frame);
            }
            continue;
        }
        if (result == AVERROR_EOF) {
            session.decoder_eof = true;
            return render_frame(session, limits);
        }
        if (result != AVERROR(EAGAIN)) {
            return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                           "video frame decode failed: " + ffmpeg_message(result)});
        }

        if (auto fed = feed_video_decoder(session, limits, stop, packet_count)) {
            return std::move(*fed);
        }
    }
    return std::unexpected(error_t<video_frame_status_t>{video_frame_status_t::decode_failed,
                   "video decode exceeded the configured packet limit"});
}

auto is_video_frame_covering_clock(const rgba_video_frame_t& frame,
                              const video_preview_clock_t& clock) noexcept -> bool {
    if (frame.resource != clock.resource || frame.time_base_numerator <= 0 ||
        frame.time_base_denominator <= 0) {
        return false;
    }
    const auto target =
        clock_timestamp(clock, AVRational{frame.time_base_numerator, frame.time_base_denominator},
                        frame.stream_start_timestamp);
    return target && *target >= frame.presentation_timestamp &&
           (!frame.next_presentation_timestamp || *target < *frame.next_presentation_timestamp);
}

auto decode_video_frame_rgba(const resolved_visual_asset_t& source,
                             const video_preview_clock_t& clock, video_frame_decode_limits_t limits,
                             std::stop_token stop) -> video_frame_decode_result_t {
    auto opened = open_video_decoder_session(source, stop);
    if (!opened) {
        return std::unexpected(std::move(opened.error()));
    }
    return (*opened)->decode(clock, limits, stop);
}

} // namespace wormedit::visual
