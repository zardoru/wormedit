#include "ogg_encoder_internal.h"
#include <wormedit/audio/ogg_encoder.h>
#include <wormedit/audio/decoder.h>
#include <vorbis/vorbisenc.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace wormedit::audio {
namespace {
using detail::vorbis_encoder_t;
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
