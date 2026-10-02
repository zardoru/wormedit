#include <wormedit/audio/slice_render.h>

#include <wormedit/core/atomic_file_storage.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <system_error>

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
    if (!info) {
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

auto read_range(decoder_t& decoder, const audio_slice_range_t& range, std::vector<float>& samples)
    -> bool {
    const auto frames = static_cast<std::size_t>(range.end_frame - range.begin_frame);
    samples.assign(frames * output_channel_count, 0.0F);
    std::size_t done = 0;
    while (done < frames) {
        const auto request = std::min(maximum_read_frames, frames - done);
        const auto result = decoder.read_at(
            range.begin_frame + done,
            std::span{samples}.subspan(done * output_channel_count, request * output_channel_count),
            request);
        if (result.status != read_status_t::success || result.frames_read == 0) return false;
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

auto fail(slice_render_result_t& result, slice_render_status_t status, std::string error,
          const std::vector<std::filesystem::path>& published) -> slice_render_result_t {
    remove_all(published);
    result.status = status;
    result.files.clear();
    result.error = std::move(error);
    return result;
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

auto render_audio_slices(decoder_t& decoder, std::span<const audio_slice_range_t> ranges,
                         const slice_render_options_t& options) -> slice_render_result_t {
    slice_render_result_t result;
    if (!validate(decoder, ranges, options, result.error)) return result;
    std::error_code error;
    std::filesystem::create_directories(options.directory, error);
    if (error) {
        result.status = slice_render_status_t::write_error;
        result.error = "cannot create the output directory: " + error.message();
        return result;
    }
    std::vector<std::filesystem::path> published;
    std::vector<float> samples;
    for (std::size_t index = 0; index < ranges.size(); ++index) {
        if (options.stop.stop_requested())
            return fail(result, slice_render_status_t::cancelled, "cancelled", published);
        if (!read_range(decoder, ranges[index], samples))
            return fail(result, slice_render_status_t::decode_error,
                        "decoding failed inside slice " + std::to_string(index + 1), published);
        const auto bytes = encode_wav_pcm16(samples, output_sample_rate);
        const auto path = options.directory / slice_file_name(options.base_name, index, ranges.size());
        const auto saved = save_file_atomic(path, bytes, bytes.size());
        if (!saved)
            return fail(result, slice_render_status_t::write_error, saved.error, published);
        published.push_back(path);
        if (options.progress) options.progress(index + 1, ranges.size());
    }
    result.status = slice_render_status_t::completed;
    result.files = std::move(published);
    return result;
}

} // namespace wormedit::audio
