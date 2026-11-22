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
#include <vector>

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

auto checked_rgba_bytes(int width, int height, video_frame_decode_limits_t limits)
    -> std::optional<std::size_t> {
    if (width <= 0 || height <= 0 || static_cast<std::uint64_t>(width) > limits.maximum_width ||
        static_cast<std::uint64_t>(height) > limits.maximum_height) {
        return std::nullopt;
    }
    const auto pixels = static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixels > limits.maximum_pixels) {
        return std::nullopt;
    }
    if (width > INT_MAX / 4 || pixels > std::numeric_limits<std::size_t>::max() / 4U) {
        return std::nullopt;
    }
    const auto byte_count = static_cast<std::size_t>(pixels) * 4U;
    return byte_count <= limits.maximum_rgba_bytes ? std::optional{byte_count} : std::nullopt;
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
    std::optional<std::int64_t> forward_seek_origin;
    // True from opening the file until the first frame is retained: the decoder reads from the
    // stream's beginning, so its first frame is the stream's first frame.
    bool at_stream_origin{};
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

constexpr std::int64_t forward_seek_threshold_seconds = 1;

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
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_data,
                       "video decoder produced no timestamped frame at the requested time"});
    }

    const auto rgba_bytes = checked_rgba_bytes(selected->width, selected->height, limits);
    if (!rgba_bytes) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded,
            "decoded video frame geometry exceeds the configured limit"});
    }
    if ((session.source.declared_width &&
         *session.source.declared_width != static_cast<std::uint32_t>(selected->width)) ||
        (session.source.declared_height &&
         *session.source.declared_height != static_cast<std::uint32_t>(selected->height))) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_geometry,
                       "decoded video geometry differs from the document declaration"});
    }

    rgba_video_frame_t output{
        .resource = session.source.resource,
        .asset = session.source.asset,
        .path = session.source.path,
        .width = static_cast<std::uint32_t>(selected->width),
        .height = static_cast<std::uint32_t>(selected->height),
        .pixels = std::vector<std::uint8_t>(*rgba_bytes),
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
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                       "video RGBA conversion context could not be created"});
    }
    std::uint8_t* destination[] = {output.pixels.data(), nullptr, nullptr, nullptr};
    int strides[] = {selected->width * 4, 0, 0, 0};
    const auto scaled = sws_scale(scaler, selected->data, selected->linesize, 0, selected->height,
                                  destination, strides);
    sws_freeContext(scaler);
    if (scaled != selected->height) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                       "video frame could not be converted completely to RGBA8"});
    }
    return output;
}

void reposition_decoder(video_decoder_session_t::impl_t& session) {
    avcodec_flush_buffers(session.decoder.get());
    av_frame_unref(session.scratch.get());
    av_frame_unref(session.current.get());
    av_frame_unref(session.lookahead.get());
    av_packet_unref(session.packet.get());
    session.positioned = true;
    session.at_stream_origin = false;
    session.input_eof = false;
    session.flush_sent = false;
    session.decoder_eof = false;
}

auto seek_stream(video_decoder_session_t::impl_t& session, std::int64_t target, std::stop_token stop)
    -> std::optional<video_frame_decode_result_t> {
    const auto result = avformat_seek_file(session.format.get(), session.stream_index,
                                           std::numeric_limits<std::int64_t>::min(), target,
                                           target, AVSEEK_FLAG_BACKWARD);
    if (result < 0) {
        if (stop.stop_requested()) {
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "video seek was cancelled"});
        }
        return std::unexpected(error_t<failure_code_t>{failure_code_t::seek_failed,
                       "video stream could not seek to the requested time: " +
                           ffmpeg_message(result)});
    }
    reposition_decoder(session);
    return std::nullopt;
}

// A forward seek pays off only when the demuxer index shows a keyframe between the decoder's
// position and the target. Without an index entry (or with only an earlier keyframe) decoding
// straight through is cheaper than rewinding to the previous keyframe.
auto forward_seek_reaches_new_keyframe(const video_decoder_session_t::impl_t& session,
                                       std::int64_t position, std::int64_t target) -> bool {
    auto* stream = session.format->streams[session.stream_index];
    const auto index = av_index_search_timestamp(stream, target, AVSEEK_FLAG_BACKWARD);
    if (index < 0) return false;
#if LIBAVFORMAT_VERSION_INT >= AV_VERSION_INT(59, 6, 100)
    const auto* entry = avformat_index_get_entry(stream, index);
    return entry != nullptr && entry->timestamp > position;
#else
    return stream->index_entries[index].timestamp > position;
#endif
}

auto seek_to_target(video_decoder_session_t::impl_t& session, std::int64_t target,
                    std::stop_token stop) -> std::optional<video_frame_decode_result_t> {
    session.forward_seek_origin.reset();
    const auto current_timestamp = timestamp_of(session.current.get());
    const auto lookahead_timestamp = timestamp_of(session.lookahead.get());
    const auto decoder_position = lookahead_timestamp != AV_NOPTS_VALUE ? lookahead_timestamp
                                  : current_timestamp != AV_NOPTS_VALUE ? current_timestamp
                                  : session.last_target.value_or(AV_NOPTS_VALUE);
    const auto forward_gap = av_rescale_q(forward_seek_threshold_seconds, AVRational{1, 1},
                                          session.time_base);
    if (!session.positioned && session.at_stream_origin && target - session.stream_start <= forward_gap) {
        // A freshly opened file already reads from its first frame; seeking there could only
        // land later (MPEG program streams do).
        session.positioned = true;
        return std::nullopt;
    }
    const bool far_ahead = session.positioned && decoder_position != AV_NOPTS_VALUE &&
                           target > decoder_position &&
                           static_cast<std::uint64_t>(target) - static_cast<std::uint64_t>(decoder_position) >
                               static_cast<std::uint64_t>(forward_gap) &&
                           forward_seek_reaches_new_keyframe(session, decoder_position, target);
    const bool seek_required = !session.positioned || far_ahead ||
                               (session.last_target && target < *session.last_target) ||
                               (current_timestamp != AV_NOPTS_VALUE && target < current_timestamp);
    if (!seek_required) {
        return std::nullopt;
    }
    if (far_ahead) session.forward_seek_origin = decoder_position;
    return seek_stream(session, target, stop);
}

auto retain_decoded_frame(video_decoder_session_t::impl_t& session, std::int64_t target,
                          video_frame_decode_limits_t limits)
    -> std::optional<video_frame_decode_result_t> {
    auto timestamp = timestamp_of(session.scratch.get());
    if (timestamp == AV_NOPTS_VALUE) {
        // Some streams end with a flushed frame that carries no timestamp; it cannot be placed
        // on the clock, so it is skipped rather than failing the whole video.
        av_frame_unref(session.scratch.get());
        return std::nullopt;
    }
    if (session.at_stream_origin && timestamp > session.stream_start) {
        // B-frame reordering (AVI, ASF) can time the first decoded frame after the stream start;
        // a player shows it from the start instead of nothing.
        session.scratch->best_effort_timestamp = session.stream_start;
        timestamp = session.stream_start;
    }
    session.at_stream_origin = false;
    auto* destination = timestamp <= target ? session.current.get() : session.lookahead.get();
    const auto copied = copy_frame(destination, session.scratch.get());
    av_frame_unref(session.scratch.get());
    if (copied < 0) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
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
                return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
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
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "video packet read was cancelled"});
        }
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
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
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                       "video packet submission failed: " + ffmpeg_message(result)});
    }
    return std::nullopt;
}

auto decode_after_seek(video_decoder_session_t::impl_t& session, std::int64_t target,
                       video_frame_decode_limits_t limits, std::stop_token stop)
    -> video_frame_decode_result_t {
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
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "video frame decode was cancelled"});
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
            return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                           "video frame decode failed: " + ffmpeg_message(result)});
        }

        if (auto fed = feed_video_decoder(session, limits, stop, packet_count)) {
            return std::move(*fed);
        }
    }
    return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                   "video decode exceeded the configured packet limit"});
}

auto open_failure(const std::stop_token& stop, failure_code_t code, const char* cancelled,
                  std::string message) -> error_t<failure_code_t> {
    if (stop.stop_requested()) return {failure_code_t::cancelled, cancelled};
    return {code, std::move(message)};
}

auto open_decoder(video_decoder_session_t::impl_t& session, const AVCodec* decoder_type,
                  std::stop_token stop) -> std::optional<error_t<failure_code_t>> {
    session.decoder.reset(avcodec_alloc_context3(decoder_type));
    if (!session.decoder) {
        return error_t<failure_code_t>{failure_code_t::decode_failed,
                                       "video decoder context could not be allocated"};
    }
    auto result = avcodec_parameters_to_context(session.decoder.get(),
                                                session.format->streams[session.stream_index]->codecpar);
    if (result >= 0) result = avcodec_open2(session.decoder.get(), decoder_type, nullptr);
    if (result < 0) {
        return open_failure(stop, failure_code_t::decode_failed, "video decoder open was cancelled",
                            "video decoder could not be opened: " + ffmpeg_message(result));
    }
    return std::nullopt;
}

// Opens the file and its best video stream, reading from the first packet.
auto open_media(video_decoder_session_t::impl_t& session, std::stop_token stop)
    -> std::optional<error_t<failure_code_t>> {
    session.decoder.reset();
    session.format.reset();
    AVFormatContext* opened = avformat_alloc_context();
    if (!opened) {
        return error_t<failure_code_t>{failure_code_t::io_failed,
                                       "video format context could not be allocated"};
    }
    opened->interrupt_callback = {.callback = interrupt_ffmpeg, .opaque = &session};
    const auto path = session.source.path.string();
    auto result = avformat_open_input(&opened, path.c_str(), nullptr, nullptr);
    session.format.reset(opened);
    if (result < 0) {
        return open_failure(stop, failure_code_t::io_failed, "video input open was cancelled",
                            "video input could not be opened: " + ffmpeg_message(result));
    }
    result = avformat_find_stream_info(session.format.get(), nullptr);
    if (result < 0) {
        return open_failure(stop, failure_code_t::decode_failed,
                            "video stream discovery was cancelled",
                            "video stream information could not be read: " + ffmpeg_message(result));
    }
    const AVCodec* decoder_type{};
    session.stream_index =
        av_find_best_stream(session.format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &decoder_type, 0);
    if (session.stream_index < 0 || decoder_type == nullptr) {
        return error_t<failure_code_t>{failure_code_t::decode_failed,
                                       "asset contains no supported video stream"};
    }
    const auto* stream = session.format->streams[session.stream_index];
    session.time_base = stream->time_base;
    session.stream_start = stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time;
    session.at_stream_origin = true;
    return open_decoder(session, decoder_type, stop);
}

// Seeking to the stream start is not a reliable rewind (MPEG program streams land later, byte
// seeks break ASF and AVI timing), so the last resort reopens the file at its first packet.
auto reopen_at_stream_origin(video_decoder_session_t::impl_t& session, std::stop_token stop)
    -> std::optional<video_frame_decode_result_t> {
    if (auto failed = open_media(session, stop)) return std::unexpected(std::move(*failed));
    reposition_decoder(session);
    session.at_stream_origin = true;
    return std::nullopt;
}

// The demuxer positioned the decoder past the target: no decoded frame is at or before it.
auto is_landed_after_target(const video_decoder_session_t::impl_t& session,
                            const video_frame_decode_result_t& decoded) -> bool {
    return !decoded && decoded.error().code == failure_code_t::invalid_data &&
           timestamp_of(session.current.get()) == AV_NOPTS_VALUE;
}

// FFmpeg lands at or before the target in indexed containers, but MPEG program streams (and
// other sparsely indexed files) can land after it. Each retry starts earlier: where the decoder
// was before a forward seek, one and four seconds back, and finally the file's first frame.
auto decode_from_earlier_position(video_decoder_session_t::impl_t& session, std::int64_t target,
                                  video_frame_decode_limits_t limits, std::stop_token stop)
    -> video_frame_decode_result_t {
    std::vector<std::int64_t> positions;
    if (session.forward_seek_origin) positions.push_back(*session.forward_seek_origin);
    session.forward_seek_origin.reset();
    for (const std::int64_t seconds : {1, 4}) {
        const auto position = target - av_rescale_q(seconds, AVRational{1, 1}, session.time_base);
        if (position > session.stream_start) positions.push_back(position);
    }
    for (const auto position : positions) {
        if (auto seek_error = seek_stream(session, position, stop)) return std::move(*seek_error);
        auto decoded = decode_after_seek(session, target, limits, stop);
        if (!is_landed_after_target(session, decoded)) return decoded;
    }
    if (auto reopen_error = reopen_at_stream_origin(session, stop)) return std::move(*reopen_error);
    return decode_after_seek(session, target, limits, stop);
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
        return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "video decoder open was cancelled"});
    }
    if (source.kind != asset_kind_t::video) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::unsupported,
                            "only video assets can be decoded as timed frames"});
    }

    auto impl = std::make_unique<video_decoder_session_t::impl_t>();
    impl->source = source;
    impl->active_stop = stop;
    if (auto failed = open_media(*impl, stop)) return std::unexpected(std::move(*failed));
    if (!impl->scratch || !impl->current || !impl->lookahead || !impl->packet) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
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
        return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "video frame decode was cancelled"});
    }
    if (session.source.resource != clock.resource || limits.maximum_width == 0 ||
        limits.maximum_height == 0 || limits.maximum_pixels == 0 ||
        limits.maximum_rgba_bytes == 0 || limits.maximum_packets == 0) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
                       "video source, media clock, or decode limits are invalid"});
    }
    const auto requested = clock_timestamp(clock, session.time_base, session.stream_start);
    if (!requested) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
                       "video media timestamp is outside FFmpeg's signed range"});
    }
    const auto target = *requested;

    if (auto seek_error = seek_to_target(session, target, stop)) {
        return std::move(*seek_error);
    }
    auto decoded = decode_after_seek(session, target, limits, stop);
    if (!is_landed_after_target(session, decoded)) return decoded;
    return decode_from_earlier_position(session, target, limits, stop);
}

namespace {

// The clock as a timestamp in the frame's stream, when the frame belongs to the clock's resource.
auto frame_clock_timestamp(const rgba_video_frame_t& frame,
                           const video_preview_clock_t& clock) noexcept -> std::optional<std::int64_t> {
    if (frame.resource != clock.resource || frame.time_base_numerator <= 0 ||
        frame.time_base_denominator <= 0) {
        return std::nullopt;
    }
    return clock_timestamp(clock, AVRational{frame.time_base_numerator, frame.time_base_denominator},
                           frame.stream_start_timestamp);
}

} // namespace

auto is_video_frame_covering_clock(const rgba_video_frame_t& frame,
                              const video_preview_clock_t& clock) noexcept -> bool {
    const auto target = frame_clock_timestamp(frame, clock);
    return target && *target >= frame.presentation_timestamp &&
           (!frame.next_presentation_timestamp || *target < *frame.next_presentation_timestamp);
}

auto is_video_frame_started_by_clock(const rgba_video_frame_t& frame,
                                     const video_preview_clock_t& clock) noexcept -> bool {
    const auto target = frame_clock_timestamp(frame, clock);
    return target && *target >= frame.presentation_timestamp;
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
