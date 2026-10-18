#include <wormedit/audio/slice_render.h>

#include <wormedit/core/atomic_file_storage.h>

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <numeric>
#include <cmath>
#include <cstring>
#include <system_error>
#include <utility>

namespace wormedit::audio {
namespace {

void append_u16(std::string& out, std::uint16_t value) {
    out.push_back(static_cast<char>(value & 0xFFU));
    out.push_back(static_cast<char>(value >> 8));
}

void append_u32(std::string& out, std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8)
        out.push_back(static_cast<char>((value >> shift) & 0xFFU));
}

auto slice_file_name(const std::string& base, std::size_t index, std::size_t total) -> std::string {
    const auto width = std::max<std::size_t>(3, std::to_string(total).size());
    auto number = std::to_string(index + 1);
    number.insert(0, width - number.size(), '0');
    return base + "_" + number + ".wav";
}

auto validate(const decoder_t& decoder, std::span<const audio_slice_range_t> ranges,
              const slice_render_options_t& options, std::string& error) -> bool {
    const auto info = decoder.info();
    if (options.encoding != slice_wav_encoding_t::pcm16 &&
        options.encoding != slice_wav_encoding_t::float32) {
        error = "unknown WAV encoding";
    } else if (!info) {
        error = "decoder is not open";
    } else if (ranges.empty() || ranges.size() > options.maximum_slices) {
        error = "slice count is zero or over the limit";
    } else if (options.directory.empty() || options.base_name.empty() ||
               options.base_name.find_first_of("/\\") != std::string::npos) {
        error = "output directory and plain base name are required";
    } else {
        std::uint64_t cursor = 0;
        for (const auto& range : ranges) {
            if (range.begin_frame < cursor || range.end_frame <= range.begin_frame ||
                range.end_frame > info->output_frame_count ||
                range.end_frame - range.begin_frame > options.maximum_slice_frames) {
                error = "slice ranges must be ascending, inside the stream and within the size limit";
                return false;
            }
            cursor = range.end_frame;
        }
        return true;
    }
    return false;
}

auto prime_float_range(decoder_t& decoder, std::uint64_t begin) -> bool {
    const auto info = decoder.info();
    if (!info || info->source_sample_rate == output_sample_rate || begin == 0) return true;
    constexpr std::uint64_t warmup = 4096;
    const auto period = output_sample_rate / std::gcd(info->source_sample_rate, output_sample_rate);
    const auto start = begin > warmup ? (begin - warmup) / period * period : 0;
    std::array<float, maximum_read_frames * output_channel_count> discard{};
    auto cursor = start;
    while (cursor < begin) {
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(maximum_read_frames, begin - cursor));
        const auto read = decoder.read_frames_at(cursor, discard, count);
        if (read.status != read_phase_t::success || read.frames_read != count) return false;
        cursor += count;
    }
    return true;
}

auto read_range(decoder_t& decoder, const audio_slice_range_t& range, std::vector<float>& samples)
    -> bool {
    const auto frames = static_cast<std::size_t>(range.end_frame - range.begin_frame);
    samples.assign(frames * output_channel_count, 0.0F);
    std::size_t done = 0;
    while (done < frames) {
        const auto request = std::min(maximum_read_frames, frames - done);
        const auto result = decoder.read_frames_at(
            range.begin_frame + done,
            std::span{samples}.subspan(done * output_channel_count, request * output_channel_count),
            request);
        if (result.status != read_phase_t::success || result.frames_read == 0) return false;
        done += result.frames_read;
    }
    return true;
}

void remove_all(const std::vector<std::filesystem::path>& files) {
    for (const auto& file : files) {
        std::error_code ignored;
        std::filesystem::remove(file, ignored);
    }
}

} // namespace

auto encode_wav_pcm16(std::span<const float> interleaved_stereo, std::uint32_t sample_rate)
    -> std::string {
    const auto data_bytes = static_cast<std::uint32_t>(interleaved_stereo.size() * 2U);
    std::string out;
    out.reserve(44U + data_bytes);
    out += "RIFF";
    append_u32(out, 36U + data_bytes);
    out += "WAVEfmt ";
    append_u32(out, 16);
    append_u16(out, 1);
    append_u16(out, 2);
    append_u32(out, sample_rate);
    append_u32(out, sample_rate * 4U);
    append_u16(out, 4);
    append_u16(out, 16);
    out += "data";
    append_u32(out, data_bytes);
    for (const auto sample : interleaved_stereo) {
        const auto clipped = std::clamp(std::isfinite(sample) ? sample : 0.0F, -1.0F, 1.0F);
        append_u16(out, static_cast<std::uint16_t>(
                            static_cast<std::int16_t>(std::lrint(clipped * 32767.0F))));
    }
    return out;
}

auto encode_wav_float32(std::span<const float> interleaved_stereo, std::uint32_t sample_rate)
    -> std::string {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
    constexpr auto maximum_samples = (std::numeric_limits<std::uint32_t>::max() - 48U) / 4U;
    if (interleaved_stereo.empty() || interleaved_stereo.size() % 2 != 0 ||
        interleaved_stereo.size() > maximum_samples || sample_rate == 0 ||
        sample_rate > std::numeric_limits<std::uint32_t>::max() / 8U ||
        std::ranges::any_of(interleaved_stereo, [](float sample) { return !std::isfinite(sample); })) return {};
    const auto data_bytes = static_cast<std::uint32_t>(interleaved_stereo.size() * 4U);
    std::string out;
    out.reserve(std::size_t{56} + data_bytes);
    out += "RIFF";
    append_u32(out, 48U + data_bytes);
    out += "WAVEfmt ";
    append_u32(out, 16);
    append_u16(out, 3);
    append_u16(out, 2);
    append_u32(out, sample_rate);
    append_u32(out, sample_rate * 8U);
    append_u16(out, 8);
    append_u16(out, 32);
    out += "fact";
    append_u32(out, 4);
    append_u32(out, static_cast<std::uint32_t>(interleaved_stereo.size() / 2));
    out += "data";
    append_u32(out, data_bytes);
    for (const auto sample : interleaved_stereo) append_u32(out, std::bit_cast<std::uint32_t>(sample));
    return out;
}

auto rendered_audio_slices_t::from(decoder_t& decoder, std::span<const audio_slice_range_t> ranges,
                                    const slice_render_options_t& options)
        -> result_t<rendered_audio_slices_t, error_t<failure_code_t>> {
    std::string message;
    if (!validate(decoder, ranges, options, message))
        return std::unexpected(error_t<failure_code_t>{failure_code_t::invalid_request,
                                                             std::move(message)});
    std::error_code error;
    std::filesystem::create_directories(options.directory, error);
    if (error) {
        return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed,
            "cannot create the output directory: " + error.message()});
    }
    std::vector<std::filesystem::path> published;
    std::vector<float> samples;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (options.stop.stop_requested()) {
            remove_all(published);
            return std::unexpected(error_t<failure_code_t>{failure_code_t::cancelled, "cancelled"});
        }
        if ((options.encoding == slice_wav_encoding_t::float32 &&
             !prime_float_range(decoder, ranges[index].begin_frame)) ||
            !read_range(decoder, ranges[index], samples)) {
            remove_all(published);
            return std::unexpected(error_t<failure_code_t>{failure_code_t::decode_failed,
                "decoding failed inside slice " + std::to_string(index + 1)});
        }
        const auto bytes = options.encoding == slice_wav_encoding_t::float32
            ? encode_wav_float32(samples, output_sample_rate) : encode_wav_pcm16(samples, output_sample_rate);
        if (bytes.empty()) {
            remove_all(published);
            return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed,
                "samples cannot be encoded in the requested WAV format"});
        }
        const auto path = options.directory / slice_file_name(options.base_name, index, ranges.size());
        auto saved = create_file_atomic(path, bytes, bytes.size());
        if (!saved) {
            remove_all(published);
            return std::unexpected(error_t<failure_code_t>{failure_code_t::io_failed,
                                                                 std::move(saved.error())});
        }
        published.push_back(path);
        if (options.progress) options.progress(index + 1, ranges.size());
    }
    return rendered_audio_slices_t{.files = std::move(published)};
}

} // namespace wormedit::audio
