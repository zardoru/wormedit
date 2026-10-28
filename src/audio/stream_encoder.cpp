#include <wormedit/audio/stream_encoder.h>
#include "ogg_encoder_internal.h"
#include "../core/text_util.h"
#include <cstdio>
#include <fstream>
#include <limits>
#include <utility>

#if defined(WORMEDIT_HAS_FFMPEG)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
}
#endif

namespace wormedit::audio {
namespace {
constexpr std::size_t maximum_encoded_bytes = 128U * 1024U * 1024U;
#if defined(WORMEDIT_HAS_FFMPEG)
class mp3_encoder_t {
  public:
    ~mp3_encoder_t() {
        av_packet_free(&packet_);
        av_frame_free(&frame_);
        avcodec_free_context(&codec_);
        if (format_) {
            if (format_->pb) avio_closep(&format_->pb);
            avformat_free_context(format_);
        }
    }
    auto open(const std::filesystem::path& path, std::uint32_t rate, std::uint32_t bitrate) -> bool {
        const auto filename = text_util::path_utf8(path);
        const auto* encoder = avcodec_find_encoder_by_name("libmp3lame");
        if (!encoder || avformat_alloc_output_context2(&format_, nullptr, "mp3", filename.c_str()) < 0) return false;
        codec_ = avcodec_alloc_context3(encoder);
        if (!codec_) return false;
        codec_->sample_rate = static_cast<int>(rate);
        codec_->sample_fmt = AV_SAMPLE_FMT_FLTP;
        codec_->bit_rate = static_cast<std::int64_t>(bitrate) * 1000;
        codec_->time_base = {1, static_cast<int>(rate)};
        av_channel_layout_default(&codec_->ch_layout, 2);
        if (format_->oformat->flags & AVFMT_GLOBALHEADER) codec_->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(codec_, encoder, nullptr) < 0) return false;
        stream_ = avformat_new_stream(format_, nullptr);
        if (!stream_ || avcodec_parameters_from_context(stream_->codecpar, codec_) < 0) return false;
        stream_->time_base = codec_->time_base;
        if (avio_open(&format_->pb, filename.c_str(), AVIO_FLAG_WRITE) < 0 || avformat_write_header(format_, nullptr) < 0) return false;
        frame_ = av_frame_alloc();
        packet_ = av_packet_alloc();
        if (!frame_ || !packet_ || codec_->frame_size <= 0) return false;
        frame_->format = codec_->sample_fmt;
        frame_->sample_rate = codec_->sample_rate;
        frame_->nb_samples = codec_->frame_size;
        return av_channel_layout_copy(&frame_->ch_layout, &codec_->ch_layout) >= 0 && av_frame_get_buffer(frame_, 0) >= 0;
    }
    auto write(std::span<const float> pcm, std::stop_token stop) -> bool {
        for (std::size_t at = 0; at < pcm.size(); at += 2) {
            if (stop.stop_requested()) return false;
            if (pending_ == 0 && av_frame_make_writable(frame_) < 0) return false;
            for (std::size_t channel = 0; channel < 2; ++channel)
                reinterpret_cast<float*>(frame_->data[channel])[pending_] = std::clamp(pcm[at + channel], -1.0F, 1.0F);
            if (++pending_ == codec_->frame_size && !send_frame()) return false;
        }
        return true;
    }
    auto finish() -> bool {
        if (pending_ && !send_frame()) return false;
        if (avcodec_send_frame(codec_, nullptr) < 0 || !receive_packets()) return false;
        return av_write_trailer(format_) >= 0 && avio_closep(&format_->pb) >= 0;
    }
  private:
    AVFormatContext* format_{};
    AVCodecContext* codec_{};
    AVStream* stream_{};
    AVFrame* frame_{};
    AVPacket* packet_{};
    int pending_{};
    std::int64_t next_pts_{};
    std::size_t bytes_{};
    auto receive_packets() -> bool {
        for (;;) {
            const auto status = avcodec_receive_packet(codec_, packet_);
            if (status == AVERROR(EAGAIN) || status == AVERROR_EOF) return true;
            if (status < 0 || packet_->size < 0) return false;
            bytes_ += static_cast<std::size_t>(packet_->size);
            if (bytes_ > maximum_encoded_bytes) return false;
            av_packet_rescale_ts(packet_, codec_->time_base, stream_->time_base);
            packet_->stream_index = stream_->index;
            const auto written = av_interleaved_write_frame(format_, packet_);
            av_packet_unref(packet_);
            if (written < 0) return false;
        }
    }
    auto send_frame() -> bool {
        frame_->nb_samples = pending_;
        frame_->pts = next_pts_;
        next_pts_ += pending_;
        pending_ = 0;
        return avcodec_send_frame(codec_, frame_) >= 0 && receive_packets();
    }
};
#endif
}

struct stream_encoder_t::impl_t {
    export_audio_codec_t codec{};
    std::filesystem::path path;
    detail::vorbis_encoder_t vorbis;
#if defined(WORMEDIT_HAS_FFMPEG)
    mp3_encoder_t mp3;
#endif
    bool opened{}, finished{};
};
stream_encoder_t::stream_encoder_t() : impl_(std::make_unique<impl_t>()) {}
stream_encoder_t::~stream_encoder_t() = default;
auto stream_encoder_t::can_encode(export_audio_codec_t codec) -> bool {
    if (codec == export_audio_codec_t::ogg) return true;
#if defined(WORMEDIT_HAS_FFMPEG)
    return codec == export_audio_codec_t::mp3 && avcodec_find_encoder_by_name("libmp3lame");
#else
    return false;
#endif
}
auto stream_encoder_t::open(const std::filesystem::path& path, export_audio_settings_t settings,
    std::uint32_t rate) -> bool {
    if (impl_->opened || !settings.is_valid() || !rate || rate > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) return false;
    impl_->codec = settings.codec;
    impl_->path = path;
    if (settings.codec == export_audio_codec_t::ogg)
        impl_->opened = impl_->vorbis.start(settings.ogg_quality, maximum_encoded_bytes, rate);
#if defined(WORMEDIT_HAS_FFMPEG)
    else if (settings.codec == export_audio_codec_t::mp3)
        impl_->opened = impl_->mp3.open(path, rate, settings.bitrate_for(0));
#endif
    return impl_->opened;
}
auto stream_encoder_t::write(std::span<const float> pcm, std::stop_token stop) -> bool {
    if (!impl_->opened || impl_->finished || pcm.size() % 2 ||
        !std::ranges::all_of(pcm, [](float value) { return std::isfinite(value); })) return false;
    if (impl_->codec == export_audio_codec_t::ogg) return impl_->vorbis.feed(pcm, stop);
#if defined(WORMEDIT_HAS_FFMPEG)
    return impl_->mp3.write(pcm, stop);
#else
    return false;
#endif
}
auto stream_encoder_t::finish() -> bool {
    if (!impl_->opened || std::exchange(impl_->finished, true)) return false;
    if (impl_->codec == export_audio_codec_t::ogg) {
        if (!impl_->vorbis.feed({}, {})) return false;
        const auto bytes = impl_->vorbis.finish();
        if (!bytes) return false;
        std::ofstream file{impl_->path, std::ios::binary | std::ios::trunc};
        file.write(bytes->data(), static_cast<std::streamsize>(bytes->size()));
        file.close();
        return static_cast<bool>(file);
    }
#if defined(WORMEDIT_HAS_FFMPEG)
    return impl_->mp3.finish();
#else
    return false;
#endif
}
auto stream_encoder_t::source_bitrate(const std::filesystem::path& path) -> std::uint32_t {
#if defined(WORMEDIT_HAS_FFMPEG)
    auto* input = avformat_alloc_context();
    if (!input) return 0;
    input->probesize = 1024 * 1024;
    input->max_analyze_duration = AV_TIME_BASE;
    const auto filename = text_util::path_utf8(path);
    if (avformat_open_input(&input, filename.c_str(), nullptr, nullptr) < 0) {
        avformat_close_input(&input);
        return 0;
    }
    std::int64_t bitrate{};
    if (avformat_find_stream_info(input, nullptr) >= 0)
        for (unsigned index = 0; index < input->nb_streams; ++index)
            if (input->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
                bitrate = input->streams[index]->codecpar->bit_rate;
                break;
            }
    avformat_close_input(&input);
    return bitrate > 0 && bitrate <= std::numeric_limits<std::uint32_t>::max() ? static_cast<std::uint32_t>(bitrate) : 0;
#else
    (void)path;
    return 0;
#endif
}
}
