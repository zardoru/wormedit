#pragma once

#include <wormedit/audio/decoder.h>
#include <wormedit/core/audio_slicer.h>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <span>
#include <stop_token>
#include <string>
#include <vector>

namespace wormedit::audio {

// 16-bit PCM RIFF/WAVE bytes for interleaved stereo float samples (clipped to
// [-1, 1], rounded to nearest). The frame count is samples.size() / 2.
[[nodiscard]] auto encode_wav_pcm16(std::span<const float> interleaved_stereo,
                                    std::uint32_t sample_rate) -> std::string;

// IEEE float32 RIFF/WAVE with a fact chunk; preserves finite samples beyond
// [-1,1]. Invalid stereo/rate/size/nonfinite input returns empty bytes.
[[nodiscard]] auto encode_wav_float32(std::span<const float> interleaved_stereo,
    std::uint32_t sample_rate) -> std::string;

enum class slice_wav_encoding_t { pcm16, float32 };

struct slice_render_options_t {
    // Created when missing. Files are named <base_name>_001.wav, _002.wav, ...
    std::filesystem::path directory;
    std::string base_name;
    std::stop_token stop;
    // Called after each published file with (done, total).
    std::function<void(std::size_t, std::size_t)> progress;
    // A single slice longer than this is rejected up front (default 10 minutes).
    std::uint64_t maximum_slice_frames{10ULL * 60ULL * output_sample_rate};
    std::size_t maximum_slices{10'000};
    slice_wav_encoding_t encoding{slice_wav_encoding_t::pcm16};
};

enum class slice_render_status_t {
    completed,
    cancelled,
    invalid_request,
    decode_error,
    write_error,
};

struct slice_render_result_t {
    slice_render_status_t status{slice_render_status_t::invalid_request};
    // Every published file in order; empty unless status is completed.
    std::vector<std::filesystem::path> files;
    std::string error;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == slice_render_status_t::completed;
    }
};

// Cuts the decoder's 44.1 kHz stereo output at `ranges` (output frames, as
// returned by compute_audio_slices with sample_rate output_sample_rate) and
// publishes one WAV per range. The source is never modified. Ranges must be
// ascending, non-overlapping and inside the stream. A failed or cancelled run
// removes every file it already published.
[[nodiscard]] auto render_audio_slices(decoder_t& decoder,
                                       std::span<const audio_slice_range_t> ranges,
                                       const slice_render_options_t& options)
    -> slice_render_result_t;

} // namespace wormedit::audio
