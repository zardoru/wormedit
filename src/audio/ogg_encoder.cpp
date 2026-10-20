#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/audio/decoder.h>
#include <vorbis/vorbisenc.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace wormedit::audio {
namespace {
class vorbis_encoder_t {
  public:
    vorbis_encoder_t() { vorbis_info_init(&info_); vorbis_comment_init(&comment_); }
    ~vorbis_encoder_t() {
        if (stream_ready_) ogg_stream_clear(&stream_);
        if (block_ready_) vorbis_block_clear(&block_);
        if (analysis_ready_) vorbis_dsp_clear(&analysis_);
        vorbis_comment_clear(&comment_);
        vorbis_info_clear(&info_);
    }
    auto start(float quality, std::size_t maximum) -> bool {
        maximum_ = maximum;
        if (vorbis_encode_init_vbr(&info_, 2, output_sample_rate, quality) != 0) return false;
        if (vorbis_analysis_init(&analysis_, &info_) != 0) return false;
        analysis_ready_ = true;
        if (vorbis_block_init(&analysis_, &block_) != 0) return false;
        block_ready_ = true;
        if (ogg_stream_init(&stream_, 0x574F524D) != 0) return false;
        stream_ready_ = true;
        std::array<ogg_packet, 3> headers{};
        if (vorbis_analysis_headerout(&analysis_, &comment_, &headers[0], &headers[1], &headers[2]) != 0)
            return false;
        for (auto& header : headers) if (ogg_stream_packetin(&stream_, &header) != 0) return false;
        return write_pages(true);
    }
    auto feed(std::span<const float> pcm, std::stop_token stop) -> bool {
        const auto frames = static_cast<int>(pcm.size() / 2);
        if (frames != 0) {
            auto** channels = vorbis_analysis_buffer(&analysis_, frames);
            if (!channels) return false;
            for (int frame = 0; frame < frames; ++frame)
                for (std::size_t channel = 0; channel < 2; ++channel)
                    channels[channel][frame] = std::clamp(pcm[static_cast<std::size_t>(frame) * 2 + channel], -1.0F, 1.0F);
        }
        if (vorbis_analysis_wrote(&analysis_, frames) != 0) return false;
        while (vorbis_analysis_blockout(&analysis_, &block_) == 1) {
            if (stop.stop_requested() || vorbis_analysis(&block_, nullptr) != 0 ||
                vorbis_bitrate_addblock(&block_) != 0) return false;
            ogg_packet packet{};
            while (vorbis_bitrate_flushpacket(&analysis_, &packet) == 1) {
                if (ogg_stream_packetin(&stream_, &packet) != 0 || !write_pages(false)) return false;
            }
        }
        return true;
    }
    auto finish() -> std::optional<std::string> {
        if (!write_pages(true)) return {};
        return std::move(bytes_);
    }
  private:
    vorbis_info info_{};
    vorbis_comment comment_{};
    vorbis_dsp_state analysis_{};
    vorbis_block block_{};
    ogg_stream_state stream_{};
    bool analysis_ready_{}, block_ready_{}, stream_ready_{};
    std::size_t maximum_{};
    std::string bytes_;
    auto write_pages(bool flush) -> bool {
        ogg_page page{};
        const auto next = flush ? ogg_stream_flush : ogg_stream_pageout;
        while (next(&stream_, &page) != 0) {
            const auto size = static_cast<std::size_t>(page.header_len + page.body_len);
            if (bytes_.size() > maximum_ || size > maximum_ - bytes_.size()) return false;
            bytes_.append(reinterpret_cast<const char*>(page.header), static_cast<std::size_t>(page.header_len));
            bytes_.append(reinterpret_cast<const char*>(page.body), static_cast<std::size_t>(page.body_len));
        }
        return true;
    }
};
}

auto encode_ogg_vorbis(std::span<const float> pcm, ogg_encode_options_t options, std::stop_token stop)
    -> result_t<std::string, error_t<failure_code_t>> {
    if (pcm.empty() || pcm.size() % 2 != 0 || !std::isfinite(options.quality) ||
        options.quality < -0.1F || options.quality > 1 ||
        !std::ranges::all_of(pcm, [](float value) { return std::isfinite(value); }))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request, "Invalid Vorbis PCM or quality."});
    vorbis_encoder_t encoder;
    if (!encoder.start(options.quality, options.maximum_output_bytes))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, "Vorbis initialization or output limit failed."});
    if (options.progress) options.progress->total.store(pcm.size() / 2);
    for (std::size_t offset = 0; offset < pcm.size(); offset += 8192) {
        if (stop.stop_requested())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Preview encoding cancelled."});
        const auto chunk = pcm.subspan(offset, std::min<std::size_t>(8192, pcm.size() - offset));
        if (!encoder.feed(chunk, stop))
            return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, "Vorbis encoding failed or exceeded its output limit."});
        if (options.progress) options.progress->completed.store((offset + chunk.size()) / 2);
    }
    if (!encoder.feed({}, stop))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, "Vorbis finalization failed."});
    auto encoded = encoder.finish();
    if (!encoded) return std::unexpected(error_t<failure_code_t>{failure_code_t::capacity_exceeded, "Vorbis output limit exceeded."});
    return std::move(*encoded);
}

auto verify_ogg_vorbis(const std::filesystem::path& path, std::uint64_t expected_frames, std::stop_token stop)
    -> result_t<void, error_t<failure_code_t>> {
    decoder_t decoder;
    if (!decoder.open(path))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, "Encoded preview could not be decoded."});
    std::array<float, 8192> pcm{};
    std::uint64_t frames{};
    while (frames <= expected_frames) {
        if (stop.stop_requested())
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "Preview verification cancelled."});
        const auto read = decoder.read_frames(pcm, pcm.size() / 2);
        if (!read.is_success() || !std::ranges::all_of(std::span{pcm}.first(read.frames_read * 2),
            [](float value) { return std::isfinite(value); })) break;
        frames += read.frames_read;
        if (read.frames_read == 0) {
            if (frames == expected_frames) return {};
            break;
        }
    }
    return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed, "Decoded preview length or PCM verification failed."});
}
}
